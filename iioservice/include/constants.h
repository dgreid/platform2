// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_INCLUDE_CONSTANTS_H_
#define IIOSERVICE_INCLUDE_CONSTANTS_H_

namespace cros {

namespace iioservice {

// Default size of StringPiece for mojo MessagePipe.
const int kUnixTokenSize = 32;
const char kIioserviceServerSocketPathString[] = "/run/iioservice/server.sock";
const char kIioserviceClientSocketPathString[] = "/run/iioservice/client.sock";

}  // namespace iioservice

}  // namespace cros

#endif  // IIOSERVICE_INCLUDE_CONSTANTS_H_
