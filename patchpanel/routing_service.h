// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_ROUTING_SERVICE_H_
#define PATCHPANEL_ROUTING_SERVICE_H_

#include <stdint.h>
#include <sys/socket.h>

#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

namespace patchpanel {

// Service implementing routing features of patchpanel.
// TODO(hugobenichi) Explain how this coordinates with shill's RoutingTable.
class RoutingService {
 public:
  RoutingService();
  RoutingService(const RoutingService&) = delete;
  RoutingService& operator=(const RoutingService&) = delete;
  virtual ~RoutingService() = default;

  // Sets the VPN bits of the fwmark for the given socket according to the
  // given policy. Preserves any other bits of the fwmark already set.
  bool SetVpnFwmark(int sockfd,
                    patchpanel::SetVpnIntentRequest::VpnRoutingPolicy policy);

  // Sets the fwmark on the given socket with the given mask.
  // Preserves any other bits of the fwmark already set.
  bool SetFwmark(int sockfd, uint32_t mark, uint32_t mask);

 protected:
  // Can be overridden in tests.
  virtual int GetSockopt(
      int sockfd, int level, int optname, void* optval, socklen_t* optlen);
  virtual int SetSockopt(
      int sockfd, int level, int optname, const void* optval, socklen_t optlen);
};

}  // namespace patchpanel

#endif  // PATCHPANEL_ROUTING_SERVICE_H_
