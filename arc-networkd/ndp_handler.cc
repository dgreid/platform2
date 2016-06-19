// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc-networkd/ndp_handler.h"

#include <net/if.h>

#include <base/logging.h>

namespace arc_networkd {

NdpHandler::~NdpHandler() {
  StopNdp();
}

bool NdpHandler::StartNdp(const std::string& ifname,
                          enum ndp_msg_type msg_type) {
  CHECK(!ndp_);

  msg_type_ = msg_type;
  ifindex_ = if_nametoindex(ifname.c_str());
  if (!ifindex_) {
    LOG(WARNING) << "Can't find ifindex for " << ifname;
    return false;
  }

  CHECK_EQ(ndp_open(&ndp_), 0);

  if (ndp_msgrcv_handler_register(ndp_,
                                  &NdpHandler::LibNdpCallback,
                                  msg_type_, ifindex_, this)) {
    LOG(WARNING) << "Can't register NDP receiver";
    ndp_close(ndp_);
    ndp_ = nullptr;
    return false;
  }

  fd_ = ndp_get_eventfd(ndp_);
  MessageLoopForIO::current()->WatchFileDescriptor(
      fd_, true, MessageLoopForIO::WATCH_READ, &watcher_, this);

  return true;
}

void NdpHandler::StopNdp() {
  if (ndp_) {
    watcher_.StopWatchingFileDescriptor();
    ndp_msgrcv_handler_unregister(ndp_,
                                  &NdpHandler::LibNdpCallback,
                                  msg_type_, ifindex_, this);
    ndp_close(ndp_);
    ndp_ = nullptr;
  }
}

void NdpHandler::OnFileCanReadWithoutBlocking(int fd) {
  CHECK_EQ(fd_, fd);
  if (ndp_call_eventfd_handler(ndp_))
    LOG(WARNING) << "NDP event handler failed";
}

// static
int NdpHandler::LibNdpCallback(struct ndp* ndp,
                               struct ndp_msg* msg,
                               void* priv) {
  NdpHandler* that = reinterpret_cast<NdpHandler*>(priv);
  return that->OnNdpMsg(ndp, msg);
}

}  // namespace arc_networkd
