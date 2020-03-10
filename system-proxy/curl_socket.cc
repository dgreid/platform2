// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-proxy/curl_socket.h"

#include <memory>
#include <utility>

namespace system_proxy {

CurlSocket::CurlSocket(base::ScopedFD fd, ScopedCurlEasyhandle curl_easyhandle)
    : arc_networkd::Socket(std::move(fd)),
      curl_easyhandle_(std::move(curl_easyhandle)) {}

CurlSocket::~CurlSocket() = default;

}  // namespace system_proxy
