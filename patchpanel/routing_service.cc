// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/routing_service.h"

#include <iostream>

#include <base/logging.h>

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

bool RoutingService::SetFwmark(int sockfd, Fwmark mark, Fwmark mask) {
  uint32_t fwmark_value = 0;
  socklen_t fwmark_len = sizeof(fwmark_value);
  if (GetSockopt(sockfd, SOL_SOCKET, SO_MARK, &fwmark_value, &fwmark_len) < 0) {
    PLOG(ERROR) << "SetFwmark mark=" << mark.ToString()
                << " mask=" << mask.ToString()
                << " getsockopt SOL_SOCKET SO_MARK failed";
    return false;
  }

  fwmark_value = (mark & mask).Value() | (fwmark_value & ~mask.Value());

  fwmark_len = sizeof(fwmark_value);
  if (SetSockopt(sockfd, SOL_SOCKET, SO_MARK, &fwmark_value, fwmark_len) < 0) {
    PLOG(ERROR) << "SetFwmark mark=" << mark.ToString()
                << " mask=" << mask.ToString()
                << " setsockopt SOL_SOCKET SO_MARK failed";
    return false;
  }

  return true;
}

bool RoutingService::SetVpnFwmark(
    int sockfd, patchpanel::SetVpnIntentRequest::VpnRoutingPolicy policy) {
  Fwmark mark = {};
  switch (policy) {
    case patchpanel::SetVpnIntentRequest::DEFAULT_ROUTING:
      break;
    case patchpanel::SetVpnIntentRequest::ROUTE_ON_VPN:
      mark = kFwmarkRouteOnVpn;
      break;
    case patchpanel::SetVpnIntentRequest::BYPASS_VPN:
      mark = kFwmarkBypassVpn;
      break;
    default:
      LOG(ERROR) << "Incorrect SetVpnIntent policy value " << policy;
      return false;
  }
  LOG(INFO) << "SetFwmark mark=" << mark.ToString()
            << " mask=" << kFwmarkVpnMask.ToString()
            << " getsockopt SOL_SOCKET SO_MARK";
  return SetFwmark(sockfd, mark, kFwmarkVpnMask);
}

const char* TrafficSourceName(TrafficSource source) {
  switch (source) {
    case CHROME:
      return "CHROME";
    case USER:
      return "USER";
    case UPDATE_ENGINE:
      return "UPDATE_ENGINE";
    case SYSTEM:
      return "SYSTEM";
    case HOST_VPN:
      return "HOST_VPN";
    case ARC:
      return "ARC";
    case CROSVM:
      return "CROSVM";
    case PLUGINVM:
      return "PLUGINVM";
    case TETHER_DOWNSTREAM:
      return "TETHER_DOWNSTREAM";
    case ARC_VPN:
      return "ARC_VPN";
    case UNKNOWN:
    default:
      return "UNKNOWN";
  }
}

std::ostream& operator<<(std::ostream& stream, const LocalSourceSpecs& source) {
  return stream << "{source: " << TrafficSourceName(source.source_type)
                << ", uid: " << (source.uid_name ? source.uid_name : "")
                << ", classid: " << source.classid
                << ", is_on_vpn: " << (source.is_on_vpn ? "true" : "false")
                << "}";
}

}  // namespace patchpanel
