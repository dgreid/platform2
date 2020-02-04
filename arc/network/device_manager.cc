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

// HACK: See b/148416701.
bool IgnoreInterface(const std::string& ifname) {
  return base::StartsWith(ifname, "peer", base::CompareCase::INSENSITIVE_ASCII);
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

  shill_client_->RegisterDefaultInterfaceChangedHandler(base::Bind(
      &DeviceManager::OnDefaultInterfaceChanged, weak_factory_.GetWeakPtr()));
  shill_client_->RegisterDevicesChangedHandler(
      base::Bind(&DeviceManager::OnDevicesChanged, weak_factory_.GetWeakPtr()));
  shill_client_->ScanDevices(
      base::Bind(&DeviceManager::OnDevicesChanged, weak_factory_.GetWeakPtr()));
}

DeviceManager::~DeviceManager() {
  shill_client_->UnregisterDevicesChangedHandler();
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

void DeviceManager::RegisterDefaultInterfaceChangedHandler(
    GuestMessage::GuestType guest,
    const ShillClient::DefaultInterfaceChangeHandler& handler) {
  default_iface_handlers_[guest] = handler;
}

void DeviceManager::UnregisterAllGuestHandlers(GuestMessage::GuestType guest) {
  add_handlers_.erase(guest);
  rm_handlers_.erase(guest);
  default_iface_handlers_.erase(guest);
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
  if (name.empty() || Exists(name) || IgnoreInterface(name))
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

  LOG(INFO) << "Removing device " << name;

  StopForwarding(*it->second);

  for (auto& h : rm_handlers_) {
    h.second.Run(it->second.get());
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

const std::string& DeviceManager::DefaultInterface() const {
  return default_ifname_;
}

std::unique_ptr<Device> DeviceManager::MakeDevice(
    const std::string& name) const {
  DCHECK(!name.empty());

  Device::Options opts{
      .fwd_multicast = false,
      .ipv6_enabled = false,
      .find_ipv6_routes_legacy = forwarder_->ForwardsLegacyIPv6(),
      .use_default_interface = false,
      .is_android = false,
      .is_sticky = false,
  };
  std::string host_ifname, guest_ifname;
  AddressManager::Guest guest = AddressManager::Guest::ARC;
  std::unique_ptr<Subnet> lxd_subnet;

  if (name == kAndroidLegacyDevice || name == kAndroidVmDevice) {
    host_ifname = "arcbr0";
    guest_ifname = "arc0";

    if (name == kAndroidVmDevice) {
      guest = AddressManager::Guest::VM_ARC;
      // (b/145644889) There are a couple things driving this device name:
      // 1. Until ArcNetworkService is running in ARCVM, arcbr0 cannot be resued
      // since the IPv4 addresses are different.
      // 2. Because of crbug/1008686, arcbr0 cannot be destroyed and recreated.
      // 3. Because shill only treats arcbr0 and devices prefixed with arc_ as
      // special, arc_br1 has to be used instead of arcbr1.
      // TODO(garrick): When either b/123431422 or the above crbug is fixed,
      // this can be removed.
      host_ifname = "arc_br1";
      guest_ifname = "arc1";
      opts.find_ipv6_routes_legacy = false;
    }

    opts.ipv6_enabled = true;
    opts.fwd_multicast = true;
    opts.use_default_interface = true;
    opts.is_android = true;
    opts.is_sticky = true;
  } else {
    if (name == kAndroidDevice) {
      host_ifname = "arcbr0";
      opts.is_android = true;
      opts.is_sticky = true;
    } else {
      guest = AddressManager::Guest::ARC_NET;
      host_ifname = base::StringPrintf("arc_%s", name.c_str());
      opts.fwd_multicast = IsMulticastInterface(name);
    }
    guest_ifname = name;
    // TODO(crbug/726815) Also enable |ipv6_enabled| for cellular networks
    // once IPv6 is enabled on cellular networks in shill.
    opts.ipv6_enabled =
        IsEthernetInterface(guest_ifname) || IsWifiInterface(guest_ifname);
  }

  auto ipv4_subnet = addr_mgr_->AllocateIPv4Subnet(guest);
  if (!ipv4_subnet) {
    LOG(ERROR) << "Subnet already in use or unavailable. Cannot make device: "
               << name;
    return nullptr;
  }
  auto host_ipv4_addr = ipv4_subnet->AllocateAtOffset(0);
  if (!host_ipv4_addr) {
    LOG(ERROR)
        << "Bridge address already in use or unavailable. Cannot make device: "
        << name;
    return nullptr;
  }
  auto guest_ipv4_addr = ipv4_subnet->AllocateAtOffset(1);
  if (!guest_ipv4_addr) {
    LOG(ERROR)
        << "ARC address already in use or unavailable. Cannot make device: "
        << name;
    return nullptr;
  }

  auto config = std::make_unique<Device::Config>(
      host_ifname, guest_ifname, addr_mgr_->GenerateMacAddress(),
      std::move(ipv4_subnet), std::move(host_ipv4_addr),
      std::move(guest_ipv4_addr), std::move(lxd_subnet));

  // ARC guest is used for any variant (N, P, VM).
  return std::make_unique<Device>(name, std::move(config), opts,
                                  guest == AddressManager::Guest::VM_TERMINA
                                      ? GuestMessage::TERMINA_VM
                                      : GuestMessage::ARC);
}

void DeviceManager::OnDefaultInterfaceChanged(const std::string& new_ifname,
                                              const std::string& prev_ifname) {
  if (new_ifname == default_ifname_)
    return;

  LOG(INFO) << "Default interface changed from [" << default_ifname_ << "] to ["
            << new_ifname << "]";

  for (const auto& d : devices_) {
    if (d.second->UsesDefaultInterface() && d.second->IsFullyUp())
      StopForwarding(*d.second);
  }

  default_ifname_ = new_ifname;

  for (const auto& d : devices_) {
    if (d.second->UsesDefaultInterface() && d.second->IsFullyUp())
      StartForwarding(*d.second);
  }

  for (const auto& h : default_iface_handlers_) {
    h.second.Run(default_ifname_, prev_ifname);
  }
}

void DeviceManager::StartForwarding(const Device& device) {
  forwarder_->StartForwarding(
      device.UsesDefaultInterface() ? default_ifname_ : device.ifname(),
      device.config().host_ifname(), device.config().guest_ipv4_addr(),
      device.options().ipv6_enabled &&
          !device.options().find_ipv6_routes_legacy,
      device.options().fwd_multicast);
}

void DeviceManager::StopForwarding(const Device& device) {
  forwarder_->StopForwarding(
      device.UsesDefaultInterface() ? default_ifname_ : device.ifname(),
      device.config().host_ifname(), device.options().ipv6_enabled,
      device.options().fwd_multicast);
}

void DeviceManager::OnDevicesChanged(const std::set<std::string>& devices) {
  std::vector<std::string> removed;
  for (const auto& d : devices_) {
    const std::string& name = d.first;
    if (!d.second->options().is_sticky && devices.find(name) == devices.end())
      removed.emplace_back(name);
  }

  for (const std::string& name : removed)
    Remove(name);

  for (const std::string& name : devices)
    Add(name);
}

AddressManager* DeviceManager::addr_mgr() const {
  return addr_mgr_;
}

}  // namespace arc_networkd
