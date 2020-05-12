// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_LIBIIOSERVICE_IPC_IPC_UTIL_H_
#define IIOSERVICE_LIBIIOSERVICE_IPC_IPC_UTIL_H_

#include <string>

#include <mojo/public/cpp/system/message_pipe.h>

#include "iioservice/include/export.h"
#include "mojo/sensor.mojom.h"

namespace base {
class FilePath;
}  // namespace base

namespace cros {

namespace iioservice {

IIOSERVICE_EXPORT MojoResult CreateMojoChannelToParentByUnixDomainSocket(
    const std::string& path, mojo::ScopedMessagePipeHandle* child_pipe);

}  // namespace iioservice

}  // namespace cros

#endif  // IIOSERVICE_LIBIIOSERVICE_IPC_IPC_UTIL_H_
