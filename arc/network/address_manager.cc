// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/address_manager.h"

#include "arc/network/net_util.h"

namespace arc_networkd {

namespace {

// The 100.115.92.0/24 subnet is reserved and not publicly routable. This subnet
// is sliced into the following IP pools for use among the various guests:
// +---------------+------------+----------------------------------------------+
// |   IP Range    |    Guest   |                                              |
// +---------------+------------+----------------------------------------------+
// | 0       (/30) | ARC        | Also used for legacy single network ARC++    |
// | 4       (/30) | ARCVM      | Currently a hard-coded reservation           |
// | 8-20    (/30) | ARC        | Used to expose multiple host networks to ARC |
// | 24-124  (/30) | Termina VM | Used by Crostini                             |
// | 128-188       | Reserved   |                                              |
// | 192-252 (/28) | Containers | Used by Crostini                             |
// +---------------+------------+----------------------------------------------+
//
// The 100.115.93.0/24 subnet is reserved for plugin VMs.

}  // namespace

AddressManager::AddressManager(
    std::initializer_list<AddressManager::Guest> guests) {
  for (auto g : guests) {
    uint32_t base_addr;
    uint32_t prefix_length = 30;
    uint32_t subnets = 1;
    switch (g) {
      case Guest::ARC:
        base_addr = Ipv4Addr(100, 115, 92, 0);
        break;
      case Guest::VM_ARC:
        base_addr = Ipv4Addr(100, 115, 92, 4);
        break;
      case Guest::ARC_NET:
        base_addr = Ipv4Addr(100, 115, 92, 8);
        subnets = 4;
        break;
      case Guest::VM_TERMINA:
        base_addr = Ipv4Addr(100, 115, 92, 24);
        subnets = 26;
        break;
      case Guest::VM_PLUGIN:
        base_addr = Ipv4Addr(100, 115, 93, 0);
        prefix_length = 29;
        subnets = 32;
        break;
      case Guest::CONTAINER:
        base_addr = Ipv4Addr(100, 115, 92, 192);
        prefix_length = 28;
        subnets = 4;
        break;
    }
    pools_.emplace(g, SubnetPool::New(base_addr, prefix_length, subnets));
  }
}

MacAddress AddressManager::GenerateMacAddress() {
  return mac_addrs_.Generate();
}

std::unique_ptr<Subnet> AddressManager::AllocateIPv4Subnet(
    AddressManager::Guest guest, uint32_t index) {
  if (index > 0 && guest != AddressManager::Guest::VM_PLUGIN) {
    LOG(ERROR) << "Subnet indexing not supported for guest";
    return nullptr;
  }
  const auto it = pools_.find(guest);
  return (it != pools_.end()) ? it->second->Allocate(index) : nullptr;
}

}  // namespace arc_networkd
