// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_VM_VSOCK_PROXY_FILE_DESCRIPTOR_UTIL_H_
#define ARC_VM_VSOCK_PROXY_FILE_DESCRIPTOR_UTIL_H_

#include <string>
#include <utility>

#include <base/optional.h>
#include <base/files/scoped_file.h>

namespace base {
class FilePath;
}  // namespace base

namespace arc {

// Creates a pair of pipe file descriptors, and returns it.
// Returns nullopt if failed.
base::Optional<std::pair<base::ScopedFD, base::ScopedFD>> CreatePipe();

// Creates a pair of socketpair file desciprotrs, and returns it.
// Returns nullopt if failed.
base::Optional<std::pair<base::ScopedFD, base::ScopedFD>> CreateSocketPair(
    int type);

// Creates a socket at |path|, and starts listening.
base::ScopedFD CreateUnixDomainSocket(const base::FilePath& path);

// Accepts a connection request to |raw_fd|, and returns the connected file
// descriptor.
base::ScopedFD AcceptSocket(int raw_fd);

// Connects to a unix domain socket at |path|. Returns errno (which is 0 on
// success), and the connected fd if succeeded.
// Note: in C++17, the return type should be std::variant<int, base::ScopedFD>.
std::pair<int, base::ScopedFD> ConnectUnixDomainSocket(
    const base::FilePath& path);

// Returns the type of the socket.
int GetSocketType(int fd);

}  // namespace arc

#endif  // ARC_VM_VSOCK_PROXY_FILE_DESCRIPTOR_UTIL_H_
