// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORKD_NEIGHBOR_FINDER_H_
#define ARC_NETWORKD_NEIGHBOR_FINDER_H_

#include <netinet/in.h>
#include <sys/socket.h>

#include <memory>
#include <string>

#include <base/bind.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>

#include "arc-networkd/ndp_handler.h"

namespace arc_networkd {

// Performs IPv6 neighbor discovery, to figure out whether some other node on
// the network is already using IP address |addr|.
class NeighborFinder : public NdpHandler {
 public:
  NeighborFinder() {}
  virtual ~NeighborFinder() {}

  bool Check(const std::string& ifname,
             const struct in6_addr& addr,
             const base::Callback<void(bool)>& callback);

 protected:
  // NdpHandler override.
  int OnNdpMsg(struct ndp* ndp, struct ndp_msg* msg) override;

  void Timeout();

  bool running_{false};
  struct in6_addr check_addr_;
  base::Callback<void(bool)> result_callback_;

  base::WeakPtrFactory<NeighborFinder> weak_factory_{this};
};

}  // namespace arc_networkd

#endif  // ARC_NETWORKD_NEIGHBOR_FINDER_H_
