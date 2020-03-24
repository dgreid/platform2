// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-proxy/curl_socket.h"

#include <memory>
#include <utility>

namespace system_proxy {

CurlSocket::CurlSocket(base::ScopedFD fd, ScopedCurlEasyhandle curl_easyhandle)
    : patchpanel::Socket(std::move(fd)),
      curl_easyhandle_(std::move(curl_easyhandle)) {}

CurlSocket::~CurlSocket() {
  // TODO(acostinas,https://crbug.com/1070732) Allow SocketForwarder creation
  // with raw sockets and defer closing the socket to libcurl via a callback
  // instead of releaing the socket in the destructor.
  int fd = release();
  VLOG(1) << "Released " << fd << " to be closed by the curl handler";
}

}  // namespace system_proxy
