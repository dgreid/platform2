// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/address_manager.h"

#include "patchpanel/net_util.h"

namespace patchpanel {

namespace {

// The 100.115.92.0/24 subnet is reserved and not publicly routable. This subnet
// is sliced into the following IP pools for use among the various usages:
// +---------------+------------+----------------------------------------------+
// |   IP Range    |    Guest   |                                              |
// +---------------+------------+----------------------------------------------+
// | 0       (/30) | ARC        | Used for ARC management interface            |
// | 4-20    (/30) | ARC        | Used to expose multiple host networks to ARC |
// | 24-124  (/30) | Termina VM | Used by Crostini                             |
// | 128-160 (/30) | Host netns | Used for netns hosting minijailed services   |
// | 164-192       | Reserved   |                                              |
// | 192-252 (/28) | Containers | Used by Crostini                             |
// +---------------+------------+----------------------------------------------+
//
// The 100.115.93.0/24 subnet is reserved for plugin VMs.

}  // namespace

AddressManager::AddressManager() {
  for (auto g : {Guest::ARC, Guest::ARC_NET, Guest::VM_TERMINA,
                 Guest::VM_PLUGIN, Guest::CONTAINER, Guest::MINIJAIL_NETNS}) {
    uint32_t base_addr;
    uint32_t prefix_length = 30;
    uint32_t subnets = 1;
    switch (g) {
      case Guest::ARC:
        base_addr = Ipv4Addr(100, 115, 92, 0);
        break;
      case Guest::ARC_NET:
        base_addr = Ipv4Addr(100, 115, 92, 4);
        subnets = 5;
        break;
      case Guest::VM_TERMINA:
        base_addr = Ipv4Addr(100, 115, 92, 24);
        subnets = 26;
        break;
      case Guest::MINIJAIL_NETNS:
        base_addr = Ipv4Addr(100, 115, 92, 128);
        prefix_length = 30;
        subnets = 8;
        break;
      case Guest::CONTAINER:
        base_addr = Ipv4Addr(100, 115, 92, 192);
        prefix_length = 28;
        subnets = 4;
        break;
      case Guest::VM_PLUGIN:
        base_addr = Ipv4Addr(100, 115, 93, 0);
        prefix_length = 29;
        subnets = 32;
        break;
    }
    pools_.emplace(g, SubnetPool::New(base_addr, prefix_length, subnets));
  }
}

MacAddress AddressManager::GenerateMacAddress(uint8_t index) {
  return index == kAnySubnetIndex ? mac_addrs_.Generate()
                                  : mac_addrs_.GetStable(index);
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

}  // namespace patchpanel
