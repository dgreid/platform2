// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/routing_service.h"

#include <iostream>

#include <base/logging.h>

namespace {
// TODO(hugobenichi) Formalize the semantics of fwmark bits with a bitfield
// struct.
constexpr const uint32_t kFwmarkRouteOnVpnBit = 0x80000000;  // 1st MSB
constexpr const uint32_t kFwmarkBypassVpnBit = 0x40000000;   // 2nd MSB
constexpr const uint32_t kFwmarkVpnMask =
    kFwmarkBypassVpnBit | kFwmarkRouteOnVpnBit;
}  // namespace

namespace patchpanel {

RoutingService::RoutingService() {}

int RoutingService::GetSockopt(
    int sockfd, int level, int optname, void* optval, socklen_t* optlen) {
  return getsockopt(sockfd, level, optname, optval, optlen);
}

int RoutingService::SetSockopt(
    int sockfd, int level, int optname, const void* optval, socklen_t optlen) {
  return setsockopt(sockfd, level, optname, optval, optlen);
}

bool RoutingService::SetFwmark(int sockfd, uint32_t mark, uint32_t mask) {
  uint32_t fwmark_value = 0;
  socklen_t fwmark_len = sizeof(fwmark_value);
  if (GetSockopt(sockfd, SOL_SOCKET, SO_MARK, &fwmark_value, &fwmark_len) < 0) {
    PLOG(ERROR) << "SetFwmark mark=0x" << std::hex << mark << " mask=0x"
                << std::hex << mask << " getsockopt SOL_SOCKET SO_MARK failed";
    return false;
  }

  fwmark_value = (mark & mask) | (fwmark_value & ~mask);

  fwmark_len = sizeof(fwmark_value);
  if (SetSockopt(sockfd, SOL_SOCKET, SO_MARK, &fwmark_value, fwmark_len) < 0) {
    PLOG(ERROR) << "SetFwmark mark=0x" << std::hex << mark << " mask=0x"
                << std::hex << mask << " setsockopt SOL_SOCKET SO_MARK failed";
    return false;
  }

  return true;
}

bool RoutingService::SetVpnFwmark(
    int sockfd, patchpanel::SetVpnIntentRequest::VpnRoutingPolicy policy) {
  uint32_t mark;
  switch (policy) {
    case patchpanel::SetVpnIntentRequest::DEFAULT_ROUTING:
      mark = 0;
      break;
    case patchpanel::SetVpnIntentRequest::ROUTE_ON_VPN:
      mark = kFwmarkRouteOnVpnBit;
      break;
    case patchpanel::SetVpnIntentRequest::BYPASS_VPN:
      mark = kFwmarkBypassVpnBit;
      break;
    default:
      LOG(ERROR) << "Incorrect SetVpnIntent policy value " << policy;
      return false;
  }
  return SetFwmark(sockfd, mark, kFwmarkVpnMask);
}

}  // namespace patchpanel
