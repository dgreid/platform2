// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/address_manager.h"

#include <map>
#include <utility>
#include <vector>

#include <arpa/inet.h>

#include "patchpanel/net_util.h"

#include <base/rand_util.h>
#include <gtest/gtest.h>

namespace patchpanel {

TEST(AddressManager, BaseAddresses) {
  std::map<AddressManager::Guest, size_t> addrs = {
      {AddressManager::Guest::ARC, Ipv4Addr(100, 115, 92, 0)},
      {AddressManager::Guest::ARC_NET, Ipv4Addr(100, 115, 92, 4)},
      {AddressManager::Guest::VM_TERMINA, Ipv4Addr(100, 115, 92, 24)},
      {AddressManager::Guest::VM_PLUGIN, Ipv4Addr(100, 115, 93, 0)},
      {AddressManager::Guest::CONTAINER, Ipv4Addr(100, 115, 92, 192)},
      {AddressManager::Guest::MINIJAIL_NETNS, Ipv4Addr(100, 115, 92, 128)},
  };
  AddressManager mgr;
  for (const auto a : addrs) {
    auto subnet = mgr.AllocateIPv4Subnet(a.first);
    ASSERT_TRUE(subnet != nullptr);
    // The first address (offset 0) returned by Subnet is not the base address,
    // rather it's the first usable IP address... so the base is 1 less.
    EXPECT_EQ(a.second, htonl(ntohl(subnet->AddressAtOffset(0)) - 1));
  }
}

TEST(AddressManager, AddressesPerSubnet) {
  std::map<AddressManager::Guest, size_t> addrs = {
      {AddressManager::Guest::ARC, 2},
      {AddressManager::Guest::ARC_NET, 2},
      {AddressManager::Guest::VM_TERMINA, 2},
      {AddressManager::Guest::VM_PLUGIN, 6},
      {AddressManager::Guest::CONTAINER, 14},
      {AddressManager::Guest::MINIJAIL_NETNS, 2},
  };
  AddressManager mgr;
  for (const auto a : addrs) {
    auto subnet = mgr.AllocateIPv4Subnet(a.first);
    ASSERT_TRUE(subnet != nullptr);
    EXPECT_EQ(a.second, subnet->AvailableCount());
  }
}

TEST(AddressManager, SubnetsPerPool) {
  std::map<AddressManager::Guest, size_t> addrs = {
      {AddressManager::Guest::ARC, 1},
      {AddressManager::Guest::ARC_NET, 5},
      {AddressManager::Guest::VM_TERMINA, 26},
      {AddressManager::Guest::VM_PLUGIN, 32},
      {AddressManager::Guest::CONTAINER, 4},
      {AddressManager::Guest::MINIJAIL_NETNS, 8},
  };
  AddressManager mgr;
  for (const auto a : addrs) {
    std::vector<std::unique_ptr<Subnet>> subnets;
    for (size_t i = 0; i < a.second; ++i) {
      auto subnet = mgr.AllocateIPv4Subnet(a.first);
      EXPECT_TRUE(subnet != nullptr);
      subnets.emplace_back(std::move(subnet));
    }
    auto subnet = mgr.AllocateIPv4Subnet(a.first);
    EXPECT_TRUE(subnet == nullptr);
  }
}

TEST(AddressManager, SubnetIndexing) {
  AddressManager mgr;
  EXPECT_FALSE(mgr.AllocateIPv4Subnet(AddressManager::Guest::ARC, 1));
  EXPECT_FALSE(mgr.AllocateIPv4Subnet(AddressManager::Guest::ARC_NET, 1));
  EXPECT_FALSE(mgr.AllocateIPv4Subnet(AddressManager::Guest::VM_TERMINA, 1));
  EXPECT_TRUE(mgr.AllocateIPv4Subnet(AddressManager::Guest::VM_PLUGIN, 1));
  EXPECT_FALSE(mgr.AllocateIPv4Subnet(AddressManager::Guest::CONTAINER, 1));
  EXPECT_FALSE(
      mgr.AllocateIPv4Subnet(AddressManager::Guest::MINIJAIL_NETNS, 1));
}

TEST(AddressManager, StableMacAddresses) {
  AddressManager mgr;
  EXPECT_NE(mgr.GenerateMacAddress(), mgr.GenerateMacAddress());
  EXPECT_NE(mgr.GenerateMacAddress(kAnySubnetIndex),
            mgr.GenerateMacAddress(kAnySubnetIndex));
  for (int i = 0; i < 100; ++i) {
    uint8_t index = 0;
    while (index == 0) {
      base::RandBytes(&index, 1);
    }
    EXPECT_EQ(mgr.GenerateMacAddress(index), mgr.GenerateMacAddress(index));
  }
}

}  // namespace patchpanel
