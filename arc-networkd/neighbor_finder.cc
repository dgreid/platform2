// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc-networkd/neighbor_finder.h"

#include <arpa/inet.h>
#include <netinet/icmp6.h>
#include <string.h>

#include <ndp.h>

#include <base/message_loop/message_loop.h>

namespace {

const int kTimeoutMs = 1000;

}  // namespace

namespace arc_networkd {

bool NeighborFinder::Check(const std::string& ifname,
                           const struct in6_addr& addr,
                           const base::Callback<void(bool)>& callback) {
  struct ndp_msg* msg;

  CHECK(!running_);
  memcpy(&check_addr_, &addr, sizeof(check_addr_));
  result_callback_ = callback;

  running_ = true;
  StartNdp(ifname, NDP_MSG_NA);
  base::MessageLoopForIO::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&NeighborFinder::Timeout,
                 weak_factory_.GetWeakPtr()),
      base::TimeDelta::FromMilliseconds(kTimeoutMs));

  CHECK_EQ(ndp_msg_new(&msg, NDP_MSG_NS), 0);
  ndp_msg_ifindex_set(msg, ifindex_);

  struct nd_neighbor_solicit** ns =
      reinterpret_cast<struct nd_neighbor_solicit**>(ndp_msgns(msg));
  memcpy(&(*ns)->nd_ns_target, &addr, sizeof((*ns)->nd_ns_target));

  if (ndp_msg_send(ndp_, msg))
    LOG(WARNING) << "Error sending neighbor solicitation";
  ndp_msg_destroy(msg);

  return true;
}

void NeighborFinder::Timeout() {
  if (!running_)
    return;

  running_ = false;
  StopNdp();
  result_callback_.Run(false);
}

int NeighborFinder::OnNdpMsg(struct ndp* ndp, struct ndp_msg* msg) {
  if (!running_)
    return 0;

  if (memcmp(&check_addr_, ndp_msg_addrto(msg), sizeof(check_addr_)) == 0) {
    running_ = false;
    StopNdp();
    result_callback_.Run(true);
  }
  return 0;
}

}  // namespace arc_networkd
