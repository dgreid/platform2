// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_DEVICE_H_
#define ARC_NETWORK_DEVICE_H_

#include <linux/in6.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <map>
#include <memory>
#include <string>

#include <base/bind.h>
#include <base/memory/weak_ptr.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "arc/network/ipc.pb.h"
#include "arc/network/mac_address_generator.h"
#include "arc/network/subnet.h"

namespace arc_networkd {

// Encapsulates a physical (e.g. eth0) or proxy (e.g. arc) network device and
// its configuration spec (interfaces, addresses) on the host and in the
// container. It manages additional services such as router detection, address
// assignment, and MDNS and SSDP forwarding. This class is the authoritative
// source for configuration events.
class Device {
 public:
  class Config {
   public:
    Config(const std::string& host_ifname,
           const std::string& guest_ifname,
           const MacAddress& guest_mac_addr,
           std::unique_ptr<Subnet> ipv4_subnet,
           std::unique_ptr<SubnetAddress> host_ipv4_addr,
           std::unique_ptr<SubnetAddress> guest_ipv4_addr,
           std::unique_ptr<Subnet> lxd_ipv4_subnet = nullptr);
    ~Config() = default;

    std::string host_ifname() const { return host_ifname_; }
    std::string guest_ifname() const { return guest_ifname_; }
    MacAddress guest_mac_addr() const { return guest_mac_addr_; }
    uint32_t host_ipv4_addr() const { return host_ipv4_addr_->Address(); }
    uint32_t guest_ipv4_addr() const { return guest_ipv4_addr_->Address(); }

    const SubnetAddress* const host_ipv4_subnet_addr() const {
      return host_ipv4_addr_.get();
    }
    const SubnetAddress* const guest_ipv4_subnet_addr() const {
      return guest_ipv4_addr_.get();
    }

    const Subnet* const ipv4_subnet() const { return ipv4_subnet_.get(); }

    const Subnet* const lxd_ipv4_subnet() const {
      return lxd_ipv4_subnet_.get();
    }

    friend std::ostream& operator<<(std::ostream& stream, const Device& device);

   private:
    // The name of the interface created on the CrOS side. This should always
    // be defined.
    std::string host_ifname_;
    // If applicable, the name of the device interface exposed in the guest. For
    // example, for ARC P, this name will match the physical device name.
    std::string guest_ifname_;
    // A random MAC address assigned to the device.
    MacAddress guest_mac_addr_;
    // The IPV4 subnet allocated for this device.
    std::unique_ptr<Subnet> ipv4_subnet_;
    // The address allocated from |ipv4_subnet| for use by the CrOS-side
    // interface associated with this device.
    std::unique_ptr<SubnetAddress> host_ipv4_addr_;
    // The address allocated from |ipv4_subnet| for use by the guest-side
    // interface associated with this device, if applicable.
    std::unique_ptr<SubnetAddress> guest_ipv4_addr_;
    // If applicable, an additional subnet allocated for this device for guests
    // like Crostini to use for assigning addresses to containers running within
    // the VM.
    std::unique_ptr<Subnet> lxd_ipv4_subnet_;

    DISALLOW_COPY_AND_ASSIGN(Config);
  };

  struct Options {
    bool fwd_multicast;
    bool ipv6_enabled;

    // Indicates this device must track shill's default interface.
    // TODO(garrick): Further qualify if this interface is a physical interface
    // or an ARC VPN to match the distinction shill is making; specifically, ARC
    // N should not loop back into itself but for Termina this should flow over
    // the VPN.
    bool use_default_interface;
  };

  Device(const std::string& ifname,
         std::unique_ptr<Config> config,
         const Options& options);
  ~Device() = default;

  const std::string& ifname() const;
  Config& config() const;
  const Options& options() const;

  void set_tap_ifname(const std::string& tap);
  const std::string& tap_ifname() const;

  bool UsesDefaultInterface() const;

  friend std::ostream& operator<<(std::ostream& stream, const Device& device);

 private:
  const std::string ifname_;
  std::unique_ptr<Config> config_;
  const Options options_;
  std::string tap_;

  FRIEND_TEST(DeviceTest, DisableLegacyAndroidDeviceSendsTwoMessages);

  base::WeakPtrFactory<Device> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(Device);
};

}  // namespace arc_networkd

#endif  // ARC_NETWORK_DEVICE_H_
