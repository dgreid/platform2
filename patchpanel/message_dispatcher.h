// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MESSAGE_DISPATCHER_H_
#define PATCHPANEL_MESSAGE_DISPATCHER_H_

#include <memory>
#include <string>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>

#include "patchpanel/ipc.pb.h"

namespace patchpanel {

// Helper message processor
class MessageDispatcher {
 public:
  explicit MessageDispatcher(base::ScopedFD fd, bool start = true);

  void Start();

  void RegisterFailureHandler(const base::Callback<void()>& handler);

  void RegisterNDProxyMessageHandler(
      const base::Callback<void(const NDProxyMessage&)>& handler);

  void RegisterGuestMessageHandler(
      const base::Callback<void(const GuestMessage&)>& handler);

  void RegisterDeviceMessageHandler(
      const base::Callback<void(const DeviceMessage&)>& handler);

  void SendMessage(const google::protobuf::MessageLite& proto) const;

 private:
  // Overrides MessageLoopForIO callbacks for new data on |control_fd_|.
  void OnFileCanReadWithoutBlocking();

  base::ScopedFD fd_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;
  base::Callback<void()> failure_handler_;
  base::Callback<void(const NDProxyMessage&)> ndproxy_handler_;
  base::Callback<void(const GuestMessage&)> guest_handler_;
  base::Callback<void(const DeviceMessage&)> device_handler_;

  IpHelperMessage msg_;

  base::WeakPtrFactory<MessageDispatcher> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(MessageDispatcher);
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MESSAGE_DISPATCHER_H_
