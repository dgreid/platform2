// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/cellular_controller_trogdor.h"

#include <algorithm>
#include <libqrtr.h>
#include <utility>
#include <base/bind.h>
#include <base/strings/string_number_conversions.h>

#include "power_manager/common/prefs.h"
#include <base/system/sys_info.h>

#define TROGDOR_MODEM_NODE_ID (0x0)
#define TROGDOR_WDS_SERVICE_ID (0x1)

namespace power_manager {
namespace policy {

CellularControllerTrogdor::CellularControllerTrogdor() : buffer_(4096) {}

CellularControllerTrogdor::~CellularControllerTrogdor() {
  watcher_ = nullptr;
  if (socket_.is_valid()) {
    StopServiceLookup(TROGDOR_WDS_SERVICE_ID, 1, 0);
    watcher_ = nullptr;
    socket_.reset();
  }
}

void CellularControllerTrogdor::Init(Delegate* delegate,
                                     PrefsInterface* prefs) {
  DCHECK(delegate);
  DCHECK(prefs);

  delegate_ = delegate;

  prefs->GetBool(kSetCellularTransmitPowerForTabletModePref,
                 &set_transmit_power_for_tablet_mode_);
  prefs->GetBool(kSetCellularTransmitPowerForProximityPref,
                 &set_transmit_power_for_proximity_);
  LOG(INFO)
      << "In CellularController::Init set_transmit_power_for_proximity_ = "
      << set_transmit_power_for_proximity_
      << " set_transmit_power_for_tablet_mode_ = "
      << set_transmit_power_for_tablet_mode_;
  if (set_transmit_power_for_proximity_ || set_transmit_power_for_tablet_mode_)
    CHECK(InitQrtrSocket());
}

void CellularControllerTrogdor::ProximitySensorDetected(UserProximity value) {
  if (set_transmit_power_for_proximity_) {
    if (set_transmit_power_for_tablet_mode_) {
      LOG(INFO) << "Cellular power will be handled by proximity sensor and "
                   "tablet mode";
    } else {
      LOG(INFO) << "Cellular power will be handled by proximity sensor";
    }
    HandleProximityChange(value);
  }
}

void CellularControllerTrogdor::HandleTabletModeChange(TabletMode mode) {
  if (!set_transmit_power_for_tablet_mode_)
    return;

  if (tablet_mode_ == mode)
    return;

  tablet_mode_ = mode;
  UpdateTransmitPower();
}

void CellularControllerTrogdor::HandleProximityChange(UserProximity proximity) {
  if (!set_transmit_power_for_proximity_)
    return;

  if (proximity_ == proximity)
    return;

  proximity_ = proximity;
  UpdateTransmitPower();
}

void CellularControllerTrogdor::HandleModemStateChange(ModemState state) {
  if (state_ == state)
    return;

  state_ = state;
  UpdateTransmitPower();
}

// Add support for 3 SAR power levels in trogdor
// compared to 2 on other boards (see DetermineTransmitPower)
RadioTransmitPower CellularControllerTrogdor::DetermineTransmitPower() const {
  RadioTransmitPower proximity_power = RadioTransmitPower::HIGH;
  RadioTransmitPower tablet_mode_power = RadioTransmitPower::HIGH;

  if (set_transmit_power_for_proximity_) {
    switch (proximity_) {
      case UserProximity::UNKNOWN:
        break;
      case UserProximity::NEAR:
        proximity_power = RadioTransmitPower::LOW;
        break;
      case UserProximity::FAR:
        proximity_power = RadioTransmitPower::HIGH;
        break;
    }
  }

  if (set_transmit_power_for_tablet_mode_) {
    switch (tablet_mode_) {
      case TabletMode::UNSUPPORTED:
        break;
      case TabletMode::ON:
        tablet_mode_power = RadioTransmitPower::LOW;
        break;
      case TabletMode::OFF:
        tablet_mode_power = RadioTransmitPower::HIGH;
        break;
    }
  }

  if (proximity_power == RadioTransmitPower::LOW &&
      tablet_mode_power == RadioTransmitPower::LOW) {
    return RadioTransmitPower::LOW;
  }
  if (proximity_power == RadioTransmitPower::LOW &&
      tablet_mode_power == RadioTransmitPower::HIGH) {
    return RadioTransmitPower::MEDIUM;
  }
  return RadioTransmitPower::HIGH;
}

void CellularControllerTrogdor::UpdateTransmitPower() {
  if (state_ == ModemState::ONLINE) {
    RadioTransmitPower wanted_power = DetermineTransmitPower();
    delegate_->SetCellularTransmitPower(wanted_power);
  }
}

void CellularControllerTrogdor::OnFileCanReadWithoutBlocking() {
  OnDataAvailable(this);
}

int CellularControllerTrogdor::Recv(void* buf, size_t size, void* metadata) {
  uint32_t node, port;
  int ret = qrtr_recvfrom(socket_.get(), buf, size, &node, &port);
  VLOG(2) << "Receiving packet from node: " << node << " port: " << port;
  if (metadata) {
    PacketMetadata* data = reinterpret_cast<PacketMetadata*>(metadata);
    data->node = node;
    data->port = port;
  }
  return ret;
}

void CellularControllerTrogdor::ProcessQrtrPacket(uint32_t node,
                                                  uint32_t port,
                                                  int size) {
  sockaddr_qrtr qrtr_sock;
  qrtr_sock.sq_family = AF_QIPCRTR;
  qrtr_sock.sq_node = node;
  qrtr_sock.sq_port = port;

  qrtr_packet pkt;
  int ret = qrtr_decode(&pkt, buffer_.data(), size, &qrtr_sock);
  if (ret < 0) {
    LOG(ERROR) << "qrtr_decode failed";
    return;
  }

  switch (pkt.type) {
    case QRTR_TYPE_NEW_SERVER:
      VLOG(1) << "Received NEW_SERVER QRTR packet node = " << pkt.node
              << " port = " << pkt.port << " service = " << pkt.service;
      if (pkt.node == TROGDOR_MODEM_NODE_ID &&
          pkt.service == TROGDOR_WDS_SERVICE_ID) {
        HandleModemStateChange(ModemState::ONLINE);
      }
      break;
    case QRTR_TYPE_DEL_SERVER:
      VLOG(1) << "Received DEL_SERVER QRTR packet node = " << pkt.node
              << " port = " << pkt.port << " service = " << pkt.service;
      if (pkt.node == TROGDOR_MODEM_NODE_ID &&
          pkt.service == TROGDOR_WDS_SERVICE_ID) {
        HandleModemStateChange(ModemState::OFFLINE);
      }
      break;
    default:
      VLOG(1) << "Received QRTR packet but did not recognize packet type "
              << pkt.type << ".";
  }
}

int CellularControllerTrogdor::Send(const void* data,
                                    size_t size,
                                    const void* metadata) {
  uint32_t node = 0, port = 0;
  if (metadata) {
    const PacketMetadata* data =
        reinterpret_cast<const PacketMetadata*>(metadata);
    node = data->node;
    port = data->port;
  }
  VLOG(2) << "Sending packet to node: " << node << " port: " << port;
  return qrtr_sendto(socket_.get(), node, port, data, size);
}

bool CellularControllerTrogdor::StartServiceLookup(uint32_t service,
                                                   uint16_t version_major,
                                                   uint16_t version_minor) {
  return qrtr_new_lookup(socket_.get(), service, version_major,
                         version_minor) >= 0;
}

bool CellularControllerTrogdor::StopServiceLookup(uint32_t service,
                                                  uint16_t version_major,
                                                  uint16_t version_minor) {
  return qrtr_remove_lookup(socket_.get(), service, version_major,
                            version_minor) >= 0;
}

inline void CellularControllerTrogdor::OnDataAvailable(
    CellularControllerTrogdor* cc) {
  void* metadata = nullptr;
  CellularControllerTrogdor::PacketMetadata data = {0, 0};
  metadata = reinterpret_cast<void*>(&data);

  int bytes_received = cc->Recv(buffer_.data(), buffer_.size(), metadata);
  if (bytes_received < 0) {
    LOG(ERROR) << "Socket recv failed";
    return;
  }
  VLOG(1) << "ModemQrtr recevied raw data (" << bytes_received
          << " bytes): " << base::HexEncode(buffer_.data(), bytes_received);
  ProcessQrtrPacket(data.node, data.port, bytes_received);
}

bool CellularControllerTrogdor::InitQrtrSocket() {
  uint8_t kQrtrPort = 0;
  socket_.reset(qrtr_open(kQrtrPort));
  if (!socket_.is_valid()) {
    LOG(ERROR) << "Failed to open QRTR socket with port " << kQrtrPort;
    return false;
  }

  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      socket_.get(),
      base::BindRepeating(
          &CellularControllerTrogdor::OnFileCanReadWithoutBlocking,
          base::Unretained(this)));

  if (!watcher_) {
    LOG(ERROR) << "Failed to set up WatchFileDescriptor";
    socket_.reset();
    return false;
  }

  return StartServiceLookup(TROGDOR_WDS_SERVICE_ID, 1, 0);
}

}  // namespace policy
}  // namespace power_manager
