// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_SOCKET_STREAM_H_
#define BUFFET_SOCKET_STREAM_H_

#include <memory>
#include <string>
#include <utility>

#include <base/callback.h>
#include <base/macros.h>
#include <brillo/streams/stream.h>
#include <weave/provider/network.h>
#include <weave/stream.h>

namespace buffet {

class SocketStream : public weave::Stream {
 public:
  explicit SocketStream(brillo::StreamPtr ptr) : ptr_{std::move(ptr)} {}
  SocketStream(const SocketStream&) = delete;
  SocketStream& operator=(const SocketStream&) = delete;

  ~SocketStream() override = default;

  void Read(void* buffer,
            size_t size_to_read,
            const ReadCallback& callback) override;

  void Write(const void* buffer,
             size_t size_to_write,
             const WriteCallback& callback) override;

  void CancelPendingOperations() override;

  static std::unique_ptr<weave::Stream> ConnectBlocking(const std::string& host,
                                                        uint16_t port);

  static void TlsConnect(
      std::unique_ptr<weave::Stream> socket,
      const std::string& host,
      const weave::provider::Network::OpenSslSocketCallback& callback);

 private:
  brillo::StreamPtr ptr_;
};

}  // namespace buffet

#endif  // BUFFET_SOCKET_STREAM_H_
