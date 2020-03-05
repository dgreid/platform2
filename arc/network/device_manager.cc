// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/device_manager.h"

#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include <utility>
#include <vector>

#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

namespace arc_networkd {
namespace {
constexpr std::array<const char*, 2> kEthernetInterfacePrefixes{{"eth", "usb"}};
constexpr std::array<const char*, 2> kWifiInterfacePrefixes{{"wlan", "mlan"}};

bool IsEthernetInterface(const std::string& ifname) {
  for (const auto& prefix : kEthernetInterfacePrefixes) {
    if (base::StartsWith(ifname, prefix,
                         base::CompareCase::INSENSITIVE_ASCII)) {
      return true;
    }
  }
  return false;
}

bool IsWifiInterface(const std::string& ifname) {
  for (const auto& prefix : kWifiInterfacePrefixes) {
    if (base::StartsWith(ifname, prefix,
                         base::CompareCase::INSENSITIVE_ASCII)) {
      return true;
    }
  }
  return false;
}

}  // namespace

DeviceManager::DeviceManager(ShillClient* shill_client,
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

  shill_client_->RegisterDevicesChangedHandler(
      base::Bind(&DeviceManager::OnDevicesChanged, weak_factory_.GetWeakPtr()));
  shill_client_->ScanDevices(
      base::Bind(&DeviceManager::OnDevicesChanged, weak_factory_.GetWeakPtr()));
}

bool DeviceManager::IsMulticastInterface(const std::string& ifname) const {
  if (ifname.empty()) {
    return false;
  }

  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    // If IPv4 fails, try to open a socket using IPv6.
    fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd < 0) {
      LOG(ERROR) << "Unable to create socket";
      return false;
    }
  }

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ);
  if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
    PLOG(ERROR) << "SIOCGIFFLAGS failed for " << ifname;
    close(fd);
    return false;
  }

  close(fd);
  return (ifr.ifr_flags & IFF_MULTICAST);
}

void DeviceManager::RegisterDeviceAddedHandler(GuestMessage::GuestType guest,
                                               const DeviceHandler& handler) {
  add_handlers_[guest] = handler;
}

void DeviceManager::RegisterDeviceRemovedHandler(GuestMessage::GuestType guest,
                                                 const DeviceHandler& handler) {
  rm_handlers_[guest] = handler;
}

void DeviceManager::UnregisterAllGuestHandlers(GuestMessage::GuestType guest) {
  add_handlers_.erase(guest);
  rm_handlers_.erase(guest);
}

void DeviceManager::ProcessDevices(const DeviceHandler& handler) {
  for (const auto& d : devices_) {
    handler.Run(d.second.get());
  }
}

void DeviceManager::OnGuestStart(GuestMessage::GuestType guest) {
  for (auto& d : devices_) {
    d.second->OnGuestStart(guest);
  }
}

void DeviceManager::OnGuestStop(GuestMessage::GuestType guest) {
  for (auto& d : devices_) {
    d.second->OnGuestStop(guest);
  }
}

bool DeviceManager::Add(const std::string& name) {
  return AddWithContext(name, nullptr);
}

bool DeviceManager::AddWithContext(const std::string& name,
                                   std::unique_ptr<Device::Context> ctx) {
  if (name.empty() || Exists(name))
    return false;

  auto dev = MakeDevice(name);
  if (!dev)
    return false;

  if (ctx)
    dev->set_context(std::move(ctx));

  LOG(INFO) << "Adding device " << *dev;
  auto* device = dev.get();
  devices_.emplace(name, std::move(dev));

  for (auto& h : add_handlers_) {
    h.second.Run(device);
  }

  return true;
}

bool DeviceManager::Remove(const std::string& name) {
  auto it = devices_.find(name);
  if (it == devices_.end())
    return false;

  Device* device = it->second.get();
  if (device->options().is_sticky)
    return false;

  LOG(INFO) << "Removing device " << name;

  StopForwarding(*device, device->ifname());

  for (auto& h : rm_handlers_) {
    h.second.Run(device);
  }

  devices_.erase(it);
  return true;
}

Device* DeviceManager::FindByHostInterface(const std::string& ifname) const {
  // As long as the device list is small, this linear search is fine.
  for (auto& d : devices_) {
    if (d.second->config().host_ifname() == ifname)
      return d.second.get();
  }
  return nullptr;
}

Device* DeviceManager::FindByGuestInterface(const std::string& ifname) const {
  // As long as the device list is small, this linear search is fine.
  for (auto& d : devices_) {
    if (d.second->config().guest_ifname() == ifname)
      return d.second.get();
  }
  return nullptr;
}

bool DeviceManager::Exists(const std::string& name) const {
  return devices_.find(name) != devices_.end();
}

std::unique_ptr<Device> DeviceManager::MakeDevice(
    const std::string& ifname) const {
  DCHECK(!ifname.empty());

  Device::Options opts{
      .fwd_multicast = IsMulticastInterface(ifname),
      // TODO(crbug/726815) Also enable |ipv6_enabled| for cellular networks
      // once IPv6 is enabled on cellular networks in shill.
      .ipv6_enabled = IsEthernetInterface(ifname) || IsWifiInterface(ifname),
      .use_default_interface = false,
      .is_android = false,
      .is_sticky = false,
  };
  std::string host_ifname = base::StringPrintf("arc_%s", ifname.c_str());
  auto ipv4_subnet =
      addr_mgr_->AllocateIPv4Subnet(AddressManager::Guest::ARC_NET);
  if (!ipv4_subnet) {
    LOG(ERROR) << "Subnet already in use or unavailable. Cannot make device: "
               << ifname;
    return nullptr;
  }
  auto host_ipv4_addr = ipv4_subnet->AllocateAtOffset(0);
  if (!host_ipv4_addr) {
    LOG(ERROR)
        << "Bridge address already in use or unavailable. Cannot make device: "
        << ifname;
    return nullptr;
  }
  auto guest_ipv4_addr = ipv4_subnet->AllocateAtOffset(1);
  if (!guest_ipv4_addr) {
    LOG(ERROR)
        << "ARC address already in use or unavailable. Cannot make device: "
        << ifname;
    return nullptr;
  }

  auto config = std::make_unique<Device::Config>(
      host_ifname, ifname, addr_mgr_->GenerateMacAddress(),
      std::move(ipv4_subnet), std::move(host_ipv4_addr),
      std::move(guest_ipv4_addr));

  return std::make_unique<Device>(ifname, std::move(config), opts,
                                  GuestMessage::ARC);
}

void DeviceManager::StartForwarding(const Device& device,
                                    const std::string& ifname) {
  forwarder_->StartForwarding(
      ifname, device.config().host_ifname(), device.config().guest_ipv4_addr(),
      device.options().ipv6_enabled, device.options().fwd_multicast);
}

void DeviceManager::StopForwarding(const Device& device,
                                   const std::string& ifname) {
  forwarder_->StopForwarding(ifname, device.config().host_ifname(),
                             device.options().ipv6_enabled,
                             device.options().fwd_multicast);
}

void DeviceManager::OnDevicesChanged(const std::set<std::string>& added,
                                     const std::set<std::string>& removed) {
  for (const std::string& name : removed)
    Remove(name);

  for (const std::string& name : added)
    Add(name);
}

}  // namespace arc_networkd
