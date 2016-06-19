// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc-networkd/router_finder.h"

#include <stdint.h>

#include <ndp.h>

#include <base/logging.h>
#include <base/time/time.h>

#include "arc-networkd/arc_ip_config.h"

namespace {

const int kInitialRtrSolicitationIntervalMs = 2000;
const int kRtrSolicitationIntervalMs = 4000;
const int kMaxRtrSolicitations = 3;

}  // namespace

namespace arc_networkd {

bool RouterFinder::Start(const std::string& ifname,
    const base::Callback<void(const struct in6_addr& prefix,
                              int prefix_len,
                              const struct in6_addr& router)>& callback) {
  result_callback_ = callback;

  have_prefix_ = false;
  ifname_ = ifname;

  // FIXME: This magic delay is needed or else the sendto() may return
  // EADDRNOTAVAIL.  Figure out why.
  rs_attempts_ = 0;
  base::MessageLoopForIO::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&RouterFinder::CheckForRouter,
                 weak_factory_.GetWeakPtr()),
      base::TimeDelta::FromMilliseconds(kInitialRtrSolicitationIntervalMs));

  return true;
}

void RouterFinder::CheckForRouter() {
  if (have_prefix_)
    return;

  if (!ndp_)
    StartNdp(ifname_, NDP_MSG_RA);
  SendRouterSolicitation();

  if (++rs_attempts_ < kMaxRtrSolicitations) {
    base::MessageLoopForIO::current()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&RouterFinder::CheckForRouter,
                   weak_factory_.GetWeakPtr()),
        base::TimeDelta::FromMilliseconds(kRtrSolicitationIntervalMs));
  }
}

void RouterFinder::SendRouterSolicitation() {
  struct ndp_msg* msg;

  CHECK_EQ(ndp_msg_new(&msg, NDP_MSG_RS), 0);
  ndp_msg_ifindex_set(msg, ifindex_);
  if (ndp_msg_send(ndp_, msg))
    LOG(WARNING) << "Error sending router solicitation";
  ndp_msg_destroy(msg);
}

int RouterFinder::OnNdpMsg(struct ndp* ndp, struct ndp_msg* msg) {
  if (ndp_msg_type(msg) != NDP_MSG_RA)
    return -1;

  int offset;

  // TODO(cernekee): Validate RA fields per the RFC.
  // (I think some of this happens in libndp, although our version
  // might be out of date.)

  ndp_msg_opt_for_each_offset(offset, msg, NDP_MSG_OPT_PREFIX) {
    struct in6_addr* prefix = ndp_msg_opt_prefix(msg, offset);
    uint32_t valid_time = ndp_msg_opt_prefix_valid_time(msg, offset);

    // TODO(cernekee): handle expiration and other special cases.
    // For now just try to stick with the first prefix that was found.
    if (valid_time && !have_prefix_) {
      memcpy(&prefix_, prefix, sizeof(prefix_));
      prefix_len_ = ndp_msg_opt_prefix_len(msg, offset);
      memcpy(&router_, ndp_msg_addrto(msg), sizeof(router_));
      have_prefix_ = true;
      result_callback_.Run(prefix_, prefix_len_, router_);
    } else if (!valid_time) {
      memcpy(&prefix_, prefix, sizeof(prefix_));
      have_prefix_ = false;
      result_callback_.Run(prefix_, 0, router_);
    }
  }

  return 0;
}

}  // namespace arc_networkd
