// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/libiioservice_ipc/ipc_util.h"

#include <fcntl.h>

#include <utility>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/posix/eintr_wrapper.h>
#include <mojo/public/cpp/platform/named_platform_channel.h>
#include <mojo/public/cpp/platform/platform_channel.h>
#include <mojo/public/cpp/platform/socket_utils_posix.h>
#include <mojo/public/cpp/system/invitation.h>

#include "iioservice/include/common.h"
#include "iioservice/include/constants.h"

namespace cros {

namespace iioservice {

MojoResult CreateMojoChannelToParentByUnixDomainSocket(
    const std::string& path, mojo::ScopedMessagePipeHandle* child_pipe) {
  mojo::PlatformChannelEndpoint endpoint =
      mojo::NamedPlatformChannel::ConnectToServer(path);
  base::ScopedFD client_socket_fd = endpoint.TakePlatformHandle().TakeFD();

  if (!client_socket_fd.is_valid()) {
    LOGF(WARNING) << "Failed to connect to " << path;
    return MOJO_RESULT_INTERNAL;
  }

  // Set socket to blocking
  int flags = HANDLE_EINTR(fcntl(client_socket_fd.get(), F_GETFL));
  if (flags == -1) {
    PLOGF(ERROR) << "fcntl(F_GETFL) failed:";
    return MOJO_RESULT_INTERNAL;
  }
  if (HANDLE_EINTR(
          fcntl(client_socket_fd.get(), F_SETFL, flags & ~O_NONBLOCK)) == -1) {
    PLOGF(ERROR) << "fcntl(F_SETFL) failed:";
    return MOJO_RESULT_INTERNAL;
  }

  char token[kUnixTokenSize] = {};
  std::vector<base::ScopedFD> platformHandles;
  ssize_t result =
      mojo::SocketRecvmsg(client_socket_fd.get(), token, sizeof(token),
                          &platformHandles, true /* block */);
  if (result != kUnixTokenSize) {
    LOGF(ERROR) << "Unexpected read size: " << result;
    return MOJO_RESULT_INTERNAL;
  }
  mojo::IncomingInvitation invitation =
      mojo::IncomingInvitation::Accept(mojo::PlatformChannelEndpoint(
          mojo::PlatformHandle(std::move(platformHandles.back()))));
  platformHandles.pop_back();

  *child_pipe =
      invitation.ExtractMessagePipe(std::string(token, kUnixTokenSize));

  return MOJO_RESULT_OK;
}

}  // namespace iioservice

}  // namespace cros
