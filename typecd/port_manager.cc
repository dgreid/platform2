// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/port_manager.h"

#include <base/logging.h>
#include <re2/re2.h>

namespace typecd {

PortManager::PortManager() : mode_entry_supported_(true) {}

void PortManager::OnPortAddedOrRemoved(const base::FilePath& path,
                                       int port_num,
                                       bool added) {
  auto it = ports_.find(port_num);
  if (added) {
    if (it != ports_.end()) {
      LOG(WARNING) << "Attempting to add an already added port.";
      return;
    }

    ports_.emplace(port_num, std::make_unique<Port>(path, port_num));
  } else {
    if (it == ports_.end()) {
      LOG(WARNING) << "Attempting to remove a non-existent port.";
      return;
    }

    ports_.erase(it);
  }
}

void PortManager::OnPartnerAddedOrRemoved(const base::FilePath& path,
                                          int port_num,
                                          bool added) {
  auto it = ports_.find(port_num);
  if (it == ports_.end()) {
    LOG(WARNING) << "Partner add/remove attempted for non-existent port "
                 << port_num;
    return;
  }

  auto port = it->second.get();
  if (added) {
    port->AddPartner(path);
    RunModeEntry(port_num);
  } else {
    port->RemovePartner();
    port->SetCurrentMode(TYPEC_MODE_NONE);
  }
}

void PortManager::OnPartnerAltModeAddedOrRemoved(const base::FilePath& path,
                                                 int port_num,
                                                 bool added) {
  auto it = ports_.find(port_num);
  if (it == ports_.end()) {
    LOG(WARNING)
        << "Partner alt mode add/remove attempted for non-existent port "
        << port_num;
    return;
  }

  auto port = it->second.get();
  port->AddRemovePartnerAltMode(path, added);
  if (added)
    RunModeEntry(port_num);
}

void PortManager::OnCableAddedOrRemoved(const base::FilePath& path,
                                        int port_num,
                                        bool added) {
  auto it = ports_.find(port_num);
  if (it == ports_.end()) {
    LOG(WARNING) << "Cable add/remove attempted for non-existent port "
                 << port_num;
    return;
  }

  auto port = it->second.get();
  if (added) {
    port->AddCable(path);
  } else {
    port->RemoveCable();
  }
}

void PortManager::OnCablePlugAdded(const base::FilePath& path, int port_num) {
  auto it = ports_.find(port_num);
  if (it == ports_.end()) {
    LOG(WARNING) << "Cable plug (SOP') add attempted for non-existent port "
                 << port_num;
    return;
  }

  auto port = it->second.get();
  port->AddCablePlug(path);
  RunModeEntry(port_num);
}

void PortManager::OnCableAltModeAdded(const base::FilePath& path,
                                      int port_num) {
  auto it = ports_.find(port_num);
  if (it == ports_.end()) {
    LOG(WARNING) << "Cable alt mode add attempted for non-existent port "
                 << port_num;
    return;
  }

  auto port = it->second.get();
  port->AddCableAltMode(path);
  RunModeEntry(port_num);
}

void PortManager::OnPartnerChanged(int port_num) {
  auto it = ports_.find(port_num);
  if (it == ports_.end()) {
    LOG(WARNING) << "Partner change detected for non-existent port "
                 << port_num;
    return;
  }

  auto port = it->second.get();
  port->PartnerChanged();
  RunModeEntry(port_num);
}

void PortManager::RunModeEntry(int port_num) {
  if (!ec_util_) {
    LOG(ERROR) << "No EC Util implementation registered, mode entry aborted.";
    return;
  }

  if (!GetModeEntrySupported())
    return;

  auto it = ports_.find(port_num);
  if (it == ports_.end()) {
    LOG(WARNING) << "Mode entry attempted for non-existent port " << port_num;
    return;
  }

  auto port = it->second.get();

  if (port->GetDataRole() != "host") {
    LOG(WARNING) << "Can't switch modes because data role is not DFP on port "
                 << port_num;
    return;
  }

  if (!port->IsPartnerDiscoveryComplete()) {
    LOG(INFO) << "Can't enter mode; partner discovery not complete for port "
              << port_num;
    return;
  }

  if (!port->IsCableDiscoveryComplete()) {
    LOG(INFO) << "Can't enter mode; cable discovery not complete for port "
              << port_num;
    return;
  }

  if (port->GetCurrentMode() != TYPEC_MODE_NONE) {
    LOG(INFO) << "Mode entry already executed for port " << port_num
              << ", mode: " << port->GetCurrentMode();
    return;
  }

  // If the host supports USB4 and we can enter USB4 in this partner, do so.
  if (port->CanEnterUSB4()) {
    if (ec_util_->EnterMode(port_num, TYPEC_MODE_USB4)) {
      port->SetCurrentMode(TYPEC_MODE_USB4);
      LOG(INFO) << "Entered USB4 mode on port " << port_num;
    } else {
      LOG(ERROR) << "Attempt to call Enter USB4 failed for port " << port_num;
    }

    return;
  }

  if (port->CanEnterTBTCompatibilityMode()) {
    if (ec_util_->EnterMode(port_num, TYPEC_MODE_TBT)) {
      port->SetCurrentMode(TYPEC_MODE_TBT);
      LOG(INFO) << "Entered TBT compat mode on port " << port_num;
    } else {
      LOG(ERROR) << "Attempt to call Enter TBT failed for port " << port_num;
    }

    return;
  }

  if (port->CanEnterDPAltMode()) {
    if (ec_util_->EnterMode(port_num, TYPEC_MODE_DP)) {
      port->SetCurrentMode(TYPEC_MODE_DP);
      LOG(INFO) << "Entered DP mode on port " << port_num;
    } else {
      LOG(ERROR) << "Attempt to call Enter DP failed for port " << port_num;
    }

    return;
  }
}

}  // namespace typecd
