// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORKD_NDP_HANDLER_H_
#define ARC_NETWORKD_NDP_HANDLER_H_

#include <ndp.h>

#include <memory>
#include <string>

#include <base/bind.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/message_loop/message_loop.h>

using MessageLoopForIO = base::MessageLoopForIO;

namespace arc_networkd {

// Uses libndp to listen for multicast messages of type |msg_type| on
// network interface |ifname|, then calls OnNdpMsg() when one is received.
class NdpHandler : public MessageLoopForIO::Watcher {
 public:
  NdpHandler() {}
  virtual ~NdpHandler();

 protected:
  bool StartNdp(const std::string& ifname, enum ndp_msg_type msg_type);
  void StopNdp();

  void OnFileCanReadWithoutBlocking(int fd) override;
  void OnFileCanWriteWithoutBlocking(int fd) override {}

  virtual int OnNdpMsg(struct ndp* ndp, struct ndp_msg* msg) = 0;

  struct ndp* ndp_{nullptr};
  int ifindex_;
  enum ndp_msg_type msg_type_;

  base::WeakPtrFactory<NdpHandler> weak_factory_{this};

 private:
  static int LibNdpCallback(struct ndp* ndp, struct ndp_msg* msg, void* priv);
  int fd_;
  MessageLoopForIO::FileDescriptorWatcher watcher_;
};

}  // namespace arc_networkd

#endif  // ARC_NETWORKD_NDP_HANDLER_H_
