// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/crostini_service.h"

#include <memory>
#include <utility>

#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/constants/vm_tools.h>
#include <session_manager/dbus-proxies.h>

#include "arc/network/adb_proxy.h"
#include "arc/network/device.h"

namespace arc_networkd {
namespace {
constexpr int32_t kInvalidCID = 0;

bool IsAdbSideloadingEnabled(const scoped_refptr<dbus::Bus>& bus) {
  static bool checked = false;
  static bool result = false;

  if (checked)
    return result;

  auto session_manager_proxy =
      new org::chromium::SessionManagerInterfaceProxy(bus);

  brillo::ErrorPtr error;
  session_manager_proxy->QueryAdbSideload(&result, &error);

  if (error) {
    LOG(ERROR) << "Error calling D-Bus proxy call to interface "
               << "'" << session_manager_proxy->GetObjectPath().value()
               << "': " << error->GetMessage();
    return false;
  }

  checked = true;
  return result;
}

}  // namespace

CrostiniService::CrostiniService(DeviceManagerBase* dev_mgr, Datapath* datapath)
    : dev_mgr_(dev_mgr), datapath_(datapath) {
  DCHECK(dev_mgr_);
  DCHECK(datapath_);

  dev_mgr_->RegisterDefaultInterfaceChangedHandler(
      GuestMessage::TERMINA_VM,
      base::Bind(&CrostiniService::OnDefaultInterfaceChanged,
                 base::Unretained(this)));
}

bool CrostiniService::Start(uint32_t cid) {
  if (cid == kInvalidCID) {
    LOG(ERROR) << "Invalid VM cid " << cid;
    return false;
  }
  if (taps_.find(cid) != taps_.end()) {
    LOG(WARNING) << "Already started for {cid: " << cid << "}";
    return false;
  }

  if (!AddTAP(cid))
    return false;

  LOG(INFO) << "Crostini network service started for {cid: " << cid << "}";

  const auto* dev = taps_[cid].get();
  dev_mgr_->StartForwarding(*dev);
  StartAdbPortForwarding(dev->ifname());

  return true;
}

void CrostiniService::Stop(uint32_t cid) {
  const auto it = taps_.find(cid);
  if (it == taps_.end()) {
    LOG(WARNING) << "Unknown {cid: " << cid << "}";
    return;
  }

  const auto* dev = it->second.get();
  dev_mgr_->StopForwarding(*dev);
  datapath_->RemoveInterface(dev->config().host_ifname());
  StopAdbPortForwarding(dev->ifname());
  taps_.erase(it);

  LOG(INFO) << "Crostini network service stopped for {cid: " << cid << "}";
}

const Device* const CrostiniService::TAP(uint32_t cid) const {
  const auto it = taps_.find(cid);
  if (it == taps_.end()) {
    return nullptr;
  }
  return it->second.get();
}

bool CrostiniService::AddTAP(uint32_t cid) {
  auto* const addr_mgr = dev_mgr_->addr_mgr();
  auto ipv4_subnet =
      addr_mgr->AllocateIPv4Subnet(AddressManager::Guest::VM_TERMINA);
  if (!ipv4_subnet) {
    LOG(ERROR)
        << "Subnet already in use or unavailable. Cannot make device for cid "
        << cid;
    return false;
  }
  auto host_ipv4_addr = ipv4_subnet->AllocateAtOffset(0);
  if (!host_ipv4_addr) {
    LOG(ERROR) << "Host address already in use or unavailable. Cannot make "
                  "device for cid "
               << cid;
    return false;
  }
  auto guest_ipv4_addr = ipv4_subnet->AllocateAtOffset(1);
  if (!guest_ipv4_addr) {
    LOG(ERROR) << "VM address already in use or unavailable. Cannot make "
                  "device for cid "
               << cid;
    return false;
  }
  auto lxd_subnet =
      addr_mgr->AllocateIPv4Subnet(AddressManager::Guest::CONTAINER);
  if (!lxd_subnet) {
    LOG(ERROR) << "lxd subnet already in use or unavailable."
               << " Cannot make device for cid " << cid;
    return false;
  }

  const auto mac_addr = addr_mgr->GenerateMacAddress();
  const std::string tap =
      datapath_->AddTAP("" /* auto-generate name */, &mac_addr,
                        host_ipv4_addr.get(), vm_tools::kCrosVmUser);
  if (tap.empty()) {
    LOG(ERROR) << "Failed to create TAP device for cid " << cid;
    return false;
  }

  auto config = std::make_unique<Device::Config>(
      tap, "", mac_addr, std::move(ipv4_subnet), std::move(host_ipv4_addr),
      std::move(guest_ipv4_addr), std::move(lxd_subnet));

  Device::Options opts{
      .fwd_multicast = true,
      .ipv6_enabled = true,
      .find_ipv6_routes_legacy = false,
      .use_default_interface = true,
      .is_android = false,
      .is_sticky = true,
  };

  taps_.emplace(cid, std::make_unique<Device>(tap, std::move(config), opts,
                                              GuestMessage::TERMINA_VM));
  return true;
}

void CrostiniService::OnDefaultInterfaceChanged(const std::string& ifname) {
  for (const auto& t : taps_)
    dev_mgr_->StopForwarding(*t.second.get());

  if (ifname.empty())
    return;

  for (const auto& t : taps_)
    dev_mgr_->StartForwarding(*t.second.get());
}

bool CrostiniService::SetupFirewallClient() {
  if (bus_)
    return true;

  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;

  bus_ = new dbus::Bus(options);
  if (!bus_->Connect()) {
    LOG(ERROR) << "Failed to connect to system bus";
    return false;
  }

  permission_broker_proxy_.reset(
      new org::chromium::PermissionBrokerProxy(bus_));

  return true;
}

void CrostiniService::StartAdbPortForwarding(const std::string& ifname) {
  DCHECK(lifeline_fds_.find(ifname) == lifeline_fds_.end());
  if (!SetupFirewallClient() || !IsAdbSideloadingEnabled(bus_))
    return;

  // Setup lifeline pipe.
  int lifeline_fds[2];
  if (pipe(lifeline_fds) != 0) {
    PLOG(ERROR) << "Failed to create lifeline pipe";
    return;
  }
  base::ScopedFD lifeline_read_fd(lifeline_fds[0]);
  base::ScopedFD lifeline_write_fd(lifeline_fds[1]);

  bool allowed = false;
  brillo::ErrorPtr error;
  permission_broker_proxy_->RequestAdbPortForward(ifname, lifeline_fds[0],
                                                  &allowed, &error);
  if (error) {
    LOG(ERROR) << "Error calling D-Bus proxy call to interface "
               << "'" << permission_broker_proxy_->GetObjectPath().value()
               << "': " << error->GetMessage();
    return;
  }
  if (!allowed) {
    LOG(ERROR) << "ADB port forwarding on " << ifname << " not allowed";
    return;
  }

  permission_broker_proxy_->RequestTcpPortAccess(
      kAdbProxyTcpListenPort, ifname, lifeline_fds[0], &allowed, &error);
  if (error) {
    LOG(ERROR) << "Error calling D-Bus proxy call to interface "
               << "'" << permission_broker_proxy_->GetObjectPath().value()
               << "': " << error->GetMessage();
    return;
  }
  if (!allowed) {
    LOG(ERROR) << "ADB port access on " << ifname << " not allowed";
    return;
  }

  if (datapath_->runner().sysctl_w(
          "net.ipv4.conf." + ifname + ".route_localnet", "1") != 0) {
    LOG(ERROR) << "Failed to set up route localnet for " << ifname;
    return;
  }

  lifeline_fds_.emplace(ifname, std::move(lifeline_write_fd));
}

void CrostiniService::StopAdbPortForwarding(const std::string& ifname) {
  if (!SetupFirewallClient() || !IsAdbSideloadingEnabled(bus_))
    return;

  const auto& lifeline_fd = lifeline_fds_.find(ifname);
  if (lifeline_fd == lifeline_fds_.end()) {
    LOG(WARNING) << "Stopping ADB port forwarding on a deleted lifeline fd on "
                 << ifname;
    return;
  }
  lifeline_fds_.erase(lifeline_fd);
}

}  // namespace arc_networkd
