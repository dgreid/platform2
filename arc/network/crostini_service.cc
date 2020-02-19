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

#include "arc/network/device.h"

namespace arc_networkd {
namespace {
constexpr int32_t kInvalidID = 0;

std::string MakeKey(uint64_t vm_id, bool is_termina) {
  return base::StringPrintf("%s:%s", is_termina ? "t" : "p",
                            base::NumberToString(vm_id).c_str());
}

}  // namespace

CrostiniService::CrostiniService(ShillClient* shill_client,
                                 AddressManager* addr_mgr,
                                 Datapath* datapath,
                                 TrafficForwarder* forwarder)
    : shill_client_(shill_client),
      addr_mgr_(addr_mgr),
      datapath_(datapath),
      forwarder_(forwarder) {
  DCHECK(shill_client_);
  DCHECK(addr_mgr_);
  DCHECK(datapath_);
  DCHECK(forwarder_);

  shill_client_->RegisterDefaultInterfaceChangedHandler(base::Bind(
      &CrostiniService::OnDefaultInterfaceChanged, weak_factory_.GetWeakPtr()));
}

bool CrostiniService::Start(uint64_t vm_id, bool is_termina, int subnet_index) {
  if (vm_id == kInvalidID) {
    LOG(ERROR) << "Invalid VM id";
    return false;
  }

  const auto key = MakeKey(vm_id, is_termina);
  if (taps_.find(key) != taps_.end()) {
    LOG(WARNING) << "Already started for {id: " << vm_id << "}";
    return false;
  }

  auto tap = AddTAP(is_termina, subnet_index);
  if (!tap) {
    LOG(ERROR) << "Cannot start for {id: " << vm_id << "}";
    return false;
  }

  LOG(INFO) << "Crostini network service started for {id: " << vm_id << "}";
  StartForwarding(shill_client_->default_interface(),
                  tap->config().host_ifname(), tap->config().guest_ipv4_addr());
  taps_.emplace(key, std::move(tap));
  return true;
}

void CrostiniService::Stop(uint64_t vm_id, bool is_termina) {
  const auto key = MakeKey(vm_id, is_termina);
  const auto it = taps_.find(key);
  if (it == taps_.end()) {
    LOG(WARNING) << "Unknown {id: " << vm_id << "}";
    return;
  }

  const auto* dev = it->second.get();
  const auto& ifname = dev->config().host_ifname();
  StopForwarding(shill_client_->default_interface(), ifname);
  datapath_->RemoveInterface(ifname);
  taps_.erase(key);

  LOG(INFO) << "Crostini network service stopped for {id: " << vm_id << "}";
}

const Device* const CrostiniService::TAP(uint64_t vm_id,
                                         bool is_termina) const {
  const auto it = taps_.find(MakeKey(vm_id, is_termina));
  if (it == taps_.end()) {
    return nullptr;
  }
  return it->second.get();
}

std::unique_ptr<Device> CrostiniService::AddTAP(bool is_termina,
                                                int subnet_index) {
  auto ipv4_subnet = addr_mgr_->AllocateIPv4Subnet(
      is_termina ? AddressManager::Guest::VM_TERMINA
                 : AddressManager::Guest::VM_PLUGIN_EXT,
      subnet_index);
  if (!ipv4_subnet) {
    LOG(ERROR) << "Subnet already in use or unavailable.";
    return nullptr;
  }
  auto host_ipv4_addr = ipv4_subnet->AllocateAtOffset(0);
  if (!host_ipv4_addr) {
    LOG(ERROR) << "Host address already in use or unavailable.";
    return nullptr;
  }
  auto guest_ipv4_addr = ipv4_subnet->AllocateAtOffset(1);
  if (!guest_ipv4_addr) {
    LOG(ERROR) << "VM address already in use or unavailable.";
    return nullptr;
  }
  std::unique_ptr<Subnet> lxd_subnet;
  if (is_termina) {
    lxd_subnet =
        addr_mgr_->AllocateIPv4Subnet(AddressManager::Guest::CONTAINER);
    if (!lxd_subnet) {
      LOG(ERROR) << "lxd subnet already in use or unavailable.";
      return nullptr;
    }
  }

  const auto mac_addr = addr_mgr_->GenerateMacAddress();
  const std::string tap =
      datapath_->AddTAP("" /* auto-generate name */, &mac_addr,
                        host_ipv4_addr.get(), vm_tools::kCrosVmUser);
  if (tap.empty()) {
    LOG(ERROR) << "Failed to create TAP device.";
    return nullptr;
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

  return std::make_unique<Device>(tap, std::move(config), opts,
                                  GuestMessage::TERMINA_VM);
}

void CrostiniService::OnDefaultInterfaceChanged(
    const std::string& new_ifname, const std::string& prev_ifname) {
  for (const auto& t : taps_) {
    const auto& config = t.second->config();
    StopForwarding(prev_ifname, config.host_ifname());
    StartForwarding(new_ifname, config.host_ifname(), config.guest_ipv4_addr());
  }
}

void CrostiniService::StartForwarding(const std::string& phys_ifname,
                                      const std::string& virt_ifname,
                                      uint32_t ipv4_addr) {
  if (!phys_ifname.empty())
    forwarder_->StartForwarding(phys_ifname, virt_ifname, ipv4_addr,
                                true /*ipv6*/, true /*multicast*/);
}

void CrostiniService::StopForwarding(const std::string& phys_ifname,
                                     const std::string& virt_ifname) {
  if (!phys_ifname.empty())
    forwarder_->StopForwarding(phys_ifname, virt_ifname, true /*ipv6*/,
                               true /*multicast*/);
}

}  // namespace arc_networkd
