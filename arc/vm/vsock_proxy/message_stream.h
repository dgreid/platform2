// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_VM_VSOCK_PROXY_MESSAGE_STREAM_H_
#define ARC_VM_VSOCK_PROXY_MESSAGE_STREAM_H_

#include <vector>

#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <base/optional.h>

#include "arc/vm/vsock_proxy/message.pb.h"

namespace arc {

// MessageStream exchanges messages with the other proxy process.
class MessageStream {
 public:
  explicit MessageStream(base::ScopedFD fd);
  ~MessageStream();

  // Returns the raw file descriptor.
  int Get() const { return fd_.get(); }

  // Reads the message from the socket. Returns true and stores the read
  // message into |message| on success. Otherwise false.
  bool Read(arc_proxy::VSockMessage* message);

  // Writes the serialized |message| to the socket.
  // Returns true iff the whole message is written.
  bool Write(const arc_proxy::VSockMessage& message);

 private:
  base::ScopedFD fd_;
  std::vector<char> buf_;

  DISALLOW_COPY_AND_ASSIGN(MessageStream);
};

}  // namespace arc

#endif  // ARC_VM_VSOCK_PROXY_MESSAGE_STREAM_H_
