// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/routing_service.h"

#include <algorithm>
#include <sstream>

#include <gtest/gtest.h>

namespace patchpanel {
namespace {

auto& BYPASS_VPN = patchpanel::SetVpnIntentRequest::BYPASS_VPN;
auto& DEFAULT_ROUTING = patchpanel::SetVpnIntentRequest::DEFAULT_ROUTING;
auto& ROUTE_ON_VPN = patchpanel::SetVpnIntentRequest::ROUTE_ON_VPN;

std::string hex(uint32_t val) {
  std::stringstream ss;
  ss << "0x" << std::hex << val;
  return ss.str();
}

struct sockopt_data {
  int sockfd;
  int level;
  int optname;
  char optval[256];
  socklen_t optlen;
};

void SetOptval(sockopt_data& sockopt, uint32_t optval) {
  sockopt.optlen = sizeof(optval);
  memcpy(sockopt.optval, &optval, sizeof(optval));
}

uint32_t GetOptval(const sockopt_data& sockopt) {
  uint32_t optval;
  memcpy(&optval, sockopt.optval, sizeof(optval));
  return optval;
}

class TestableRoutingService : public RoutingService {
 public:
  TestableRoutingService() = default;
  ~TestableRoutingService() = default;

  int GetSockopt(int sockfd,
                 int level,
                 int optname,
                 void* optval,
                 socklen_t* optlen) override {
    sockopt.sockfd = sockfd;
    sockopt.level = level;
    sockopt.optname = optname;
    memcpy(optval, sockopt.optval,
           std::min(*optlen, (socklen_t)sizeof(sockopt.optval)));
    *optlen = sockopt.optlen;
    return getsockopt_ret;
  }

  int SetSockopt(int sockfd,
                 int level,
                 int optname,
                 const void* optval,
                 socklen_t optlen) override {
    sockopt.sockfd = sockfd;
    sockopt.level = level;
    sockopt.optname = optname;
    sockopt.optlen = optlen;
    memcpy(sockopt.optval, optval,
           std::min(optlen, (socklen_t)sizeof(sockopt.optval)));
    return setsockopt_ret;
  }

  // Variables used to mock and track interactions with getsockopt and
  // setsockopt.
  int getsockopt_ret;
  int setsockopt_ret;
  sockopt_data sockopt;
};

class RoutingServiceTest : public testing::Test {
 public:
  RoutingServiceTest() = default;

 protected:
  void SetUp() override {}
};

}  // namespace

TEST_F(RoutingServiceTest, SetVpnFwmark) {
  auto svc = std::make_unique<TestableRoutingService>();
  svc->getsockopt_ret = 0;
  svc->setsockopt_ret = 0;

  struct {
    patchpanel::SetVpnIntentRequest::VpnRoutingPolicy policy;
    uint32_t initial_fwmark;
    uint32_t expected_fwmark;
  } testcases[] = {
      {ROUTE_ON_VPN, 0x0, 0x80000000},
      {BYPASS_VPN, 0x0, 0x40000000},
      {ROUTE_ON_VPN, 0x1, 0x80000001},
      {BYPASS_VPN, 0x00abcdef, 0x40abcdef},
      {ROUTE_ON_VPN, 0x11223344, 0x91223344},
      {BYPASS_VPN, 0x11223344, 0x51223344},
      {ROUTE_ON_VPN, 0x80000000, 0x80000000},
      {BYPASS_VPN, 0x40000000, 0x40000000},
      {BYPASS_VPN, 0x80000000, 0x40000000},
      {ROUTE_ON_VPN, 0x40000000, 0x80000000},
      {DEFAULT_ROUTING, 0x80000000, 0x00000000},
      {DEFAULT_ROUTING, 0x40000000, 0x00000000},
  };

  for (const auto& tt : testcases) {
    SetOptval(svc->sockopt, tt.initial_fwmark);
    EXPECT_TRUE(svc->SetVpnFwmark(4, tt.policy));
    EXPECT_EQ(4, svc->sockopt.sockfd);
    EXPECT_EQ(SOL_SOCKET, svc->sockopt.level);
    EXPECT_EQ(SO_MARK, svc->sockopt.optname);
    EXPECT_EQ(hex(tt.expected_fwmark), hex(GetOptval(svc->sockopt)));
  }

  svc->getsockopt_ret = -1;
  svc->setsockopt_ret = 0;
  EXPECT_FALSE(svc->SetVpnFwmark(4, ROUTE_ON_VPN));

  svc->getsockopt_ret = 0;
  svc->setsockopt_ret = -1;
  EXPECT_FALSE(svc->SetVpnFwmark(4, ROUTE_ON_VPN));

  svc->getsockopt_ret = 0;
  svc->setsockopt_ret = 0;
  EXPECT_FALSE(svc->SetVpnFwmark(
      4, (patchpanel::SetVpnIntentRequest::VpnRoutingPolicy)-1));
}

TEST_F(RoutingServiceTest, SetFwmark) {
  auto svc = std::make_unique<TestableRoutingService>();
  svc->getsockopt_ret = 0;
  svc->setsockopt_ret = 0;

  struct {
    uint32_t initial_fwmark;
    uint32_t fwmark_value;
    uint32_t fwmark_mask;
    uint32_t expected_fwmark;
  } testcases[] = {
      {0x0, 0x0, 0x0, 0x0},
      {0x1, 0x0, 0x0, 0x1},
      {0x1, 0x0, 0x1, 0x0},
      {0xaabbccdd, 0x11223344, 0xf0f0f0f0, 0x1a2b3c4d},
      {0xaabbccdd, 0x11223344, 0xffff0000, 0x1122ccdd},
      {0xaabbccdd, 0x11223344, 0x0000ffff, 0xaabb3344},
  };

  for (const auto& tt : testcases) {
    SetOptval(svc->sockopt, tt.initial_fwmark);
    EXPECT_TRUE(svc->SetFwmark(4, tt.fwmark_value, tt.fwmark_mask));
    EXPECT_EQ(4, svc->sockopt.sockfd);
    EXPECT_EQ(SOL_SOCKET, svc->sockopt.level);
    EXPECT_EQ(SO_MARK, svc->sockopt.optname);
    EXPECT_EQ(hex(tt.expected_fwmark), hex(GetOptval(svc->sockopt)));
  }
}

TEST_F(RoutingServiceTest, SetFwmark_Failures) {
  auto svc = std::make_unique<TestableRoutingService>();
  svc->getsockopt_ret = -1;
  svc->setsockopt_ret = 0;
  EXPECT_FALSE(svc->SetFwmark(4, 0x1, 0x1));

  svc = std::make_unique<TestableRoutingService>();
  svc->getsockopt_ret = 0;
  svc->setsockopt_ret = -1;
  EXPECT_FALSE(svc->SetFwmark(5, 0x1, 0x1));

  svc = std::make_unique<TestableRoutingService>();
  svc->getsockopt_ret = 0;
  svc->setsockopt_ret = 0;
  EXPECT_TRUE(svc->SetFwmark(6, 0x1, 0x1));
}

}  // namespace patchpanel
