// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Netif helper - emits information about network interfaces as json.
// Here's an example of output from my system:
// {
//    "eth0": {
//       "flags": [ "up", "broadcast", "running", "multi", "lower-up" ],
//       "ipv4": {
//          "addrs": [ "172.31.197.126" ],
//          "destination": "172.31.197.255",
//          "mask": "255.255.254.0"
//       },
//       "ipv6": {
//          "addrs": [ "2620:0:1004:1:198:42c6:435c:aa09",
// "2620:0:1004:1:210:60ff:fe3b:c2d0", "fe80::210:60ff:fe3b:c2d0" ]
//       },
//       "mac": "0010603BC2D0"
//    },
//    "lo": {
//       "flags": [ "up", "loopback", "running", "lower-up" ],
//       "ipv4": {
//          "addrs": [ "127.0.0.1" ],
//          "destination": "127.0.0.1",
//          "mask": "255.0.0.0"
//       },
//       "ipv6": {
//          "addrs": [ "::1" ]
//       },
//       "mac": "000000000000"
//    },
//    "wlan0": {
//       "flags": [ "broadcast", "multi" ],
//       "mac": "68A3C41B264C",
//       "signal-strengths": {
//          "A9F1BDF1DAB1NVT4F4F59": 62
//       }
//    },
//    "wwan0": {
//       "flags": [ "broadcast", "multi" ],
//       "mac": "020010ABA636"
//    }
// }
// The meanings of the individual flags are up to Linux's networking stack (and
// sometimes up to the individual cards' drivers); "up" indicates that the
// interface is up.

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <utility>

#include <base/json/json_writer.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/values.h>
#include <chromeos/dbus/service_constants.h>

#include "debugd/src/helpers/shill_proxy.h"

using base::Value;

std::string getmac(int fd, const char* ifname) {
  struct ifreq ifr;
  int ret;
  memset(&ifr, 0, sizeof(ifr));
  ifr.ifr_addr.sa_family = AF_PACKET;
  strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);
  ret = ioctl(fd, SIOCGIFHWADDR, &ifr);
  if (ret < 0)
    return "<can't fetch>";
  return base::HexEncode(ifr.ifr_hwaddr.sa_data, 6);
}

std::string sockaddr2str(struct sockaddr* sa) {
  char buf[INET6_ADDRSTRLEN];
  void* addr;
  // These need NOLINT because cpplint thinks we're taking the address of a
  // cast, which we aren't - we're taking the address of a member after casting
  // a pointer to a different type.
  if (sa->sa_family == AF_INET)
    addr = &((struct sockaddr_in*)sa)->sin_addr;  // NOLINT
  else if (sa->sa_family == AF_INET6)
    addr = &((struct sockaddr_in6*)sa)->sin6_addr;  // NOLINT
  else
    return "unknown";
  const char* result = inet_ntop(sa->sa_family, addr, buf, sizeof(buf));
  return result ?: "invalid";
}

struct ifflag {
  unsigned int bit;
  const char* name;
} ifflags[] = {
    {IFF_UP, "up"},
    {IFF_BROADCAST, "broadcast"},
    {IFF_DEBUG, "debug"},
    {IFF_LOOPBACK, "loopback"},
    {IFF_POINTOPOINT, "point-to-point"},
    {IFF_RUNNING, "running"},
    {IFF_NOARP, "noarp"},
    {IFF_PROMISC, "promisc"},
    {IFF_NOTRAILERS, "notrailers"},
    {IFF_ALLMULTI, "allmulti"},
    {IFF_MASTER, "master"},
    {IFF_SLAVE, "slave"},
    {IFF_MULTICAST, "multi"},
    {IFF_PORTSEL, "portsel"},
    {IFF_AUTOMEDIA, "automedia"},
    {IFF_DYNAMIC, "dynamic"},
    {IFF_LOWER_UP, "lower-up"},
    {IFF_DORMANT, "dormant"},
    {IFF_ECHO, "echo"},
};

Value flags2list(unsigned int flags) {
  Value lv(Value::Type::LIST);
  for (unsigned int i = 0; i < base::size(ifflags); ++i) {
    if (flags & ifflags[i].bit)
      lv.Append(ifflags[i].name);
  }
  return lv;
}

class NetInterface {
 public:
  NetInterface(int fd, const char* name);
  ~NetInterface() = default;

  bool Init();
  void AddAddress(struct ifaddrs* ifa);
  void AddSignalStrength(const std::string& name, int strength);
  Value ToValue() const;

 private:
  int fd_;
  const char* name_;
  Value ipv4_{Value::Type::DICTIONARY};
  Value ipv6_{Value::Type::DICTIONARY};
  Value flags_{Value::Type::LIST};
  std::string mac_;
  Value signal_strengths_{Value::Type::DICTIONARY};

  void AddAddressTo(Value* dv, struct sockaddr* sa);
};

NetInterface::NetInterface(int fd, const char* name) : fd_(fd), name_(name) {}

bool NetInterface::Init() {
  mac_ = getmac(fd_, name_);
  return true;
}

void NetInterface::AddSignalStrength(const std::string& name, int strength) {
  // Use base::Value::SetKey, because |name| may contain ".".
  signal_strengths_.SetIntKey(name, strength);
}

void NetInterface::AddAddressTo(Value* dv, struct sockaddr* sa) {
  Value* lv = dv->FindListKey("addrs");
  if (lv == nullptr)
    lv = dv->SetKey("addrs", Value(Value::Type::LIST));
  lv->Append(sockaddr2str(sa));
}

void NetInterface::AddAddress(struct ifaddrs* ifa) {
  if (flags_.GetList().empty())
    flags_ = flags2list(ifa->ifa_flags);
  if (!ifa->ifa_addr)
    return;
  if (ifa->ifa_addr->sa_family == AF_INET) {
    // An IPv4 address.
    AddAddressTo(&ipv4_, ifa->ifa_addr);
    if (!ipv4_.FindKey("mask")) {
      ipv4_.SetStringKey("mask", sockaddr2str(ifa->ifa_netmask));
    }
    if (!ipv4_.FindKey("destination")) {
      ipv4_.SetStringKey("destination", sockaddr2str(ifa->ifa_broadaddr));
    }
  } else if (ifa->ifa_addr->sa_family == AF_INET6) {
    // An IPv6 address.
    AddAddressTo(&ipv6_, ifa->ifa_addr);
  }
}

Value NetInterface::ToValue() const {
  Value dv(Value::Type::DICTIONARY);
  if (!ipv4_.DictEmpty())
    dv.SetKey("ipv4", ipv4_.Clone());
  if (!ipv6_.DictEmpty())
    dv.SetKey("ipv6", ipv6_.Clone());
  if (flags_.GetList().size())
    dv.SetKey("flags", flags_.Clone());
  if (!signal_strengths_.DictEmpty())
    dv.SetKey("signal-strengths", signal_strengths_.Clone());
  dv.SetStringKey("mac", mac_);
  return dv;
}

std::string DevicePathToName(const std::string& path) {
  static const char kPrefix[] = "/device/";
  if (path.find(kPrefix) == 0)
    return path.substr(strlen(kPrefix));
  return "?";
}

void AddSignalStrengths(
    std::map<std::string, std::unique_ptr<NetInterface>>* interfaces) {
  auto proxy = debugd::ShillProxy::Create();
  if (!proxy)
    return;

  auto manager_properties =
      proxy->GetProperties(shill::kFlimflamManagerInterface,
                           dbus::ObjectPath(shill::kFlimflamServicePath));
  if (!manager_properties)
    return;

  auto service_paths =
      proxy->GetObjectPaths(*manager_properties, shill::kServicesProperty);
  for (const auto& service_path : service_paths) {
    auto service_properties =
        proxy->GetProperties(shill::kFlimflamServiceInterface, service_path);
    base::Optional<int> strength = service_properties->FindIntKey("Strength");
    const std::string* name = service_properties->FindStringKey("Name");
    const std::string* device = service_properties->FindStringKey("Device");
    if (!strength.has_value() || name == nullptr || device == nullptr) {
      continue;
    }
    std::string devname = DevicePathToName(*device);
    if (interfaces->count(devname)) {
      interfaces->find(devname)->second->AddSignalStrength(*name, *strength);
    }
  }
}

int main() {
  struct ifaddrs* ifaddrs;
  int fd;
  Value result(Value::Type::DICTIONARY);
  std::map<std::string, std::unique_ptr<NetInterface>> interfaces;

  if (getifaddrs(&ifaddrs) == -1) {
    perror("getifaddrs");
    exit(1);
  }

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    exit(1);
  }

  for (struct ifaddrs* ifa = ifaddrs; ifa; ifa = ifa->ifa_next) {
    auto& interface = interfaces[ifa->ifa_name];
    if (!interface) {
      interface = std::make_unique<NetInterface>(fd, ifa->ifa_name);
      interface->Init();
    }
    interface->AddAddress(ifa);
  }

  AddSignalStrengths(&interfaces);

  for (const auto& interface : interfaces)
    result.SetKey(interface.first, interface.second->ToValue());

  std::string json;
  base::JSONWriter::WriteWithOptions(
      result, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);
  printf("%s\n", json.c_str());
  return 0;
}
