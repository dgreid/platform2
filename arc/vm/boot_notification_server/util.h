// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_VM_BOOT_NOTIFICATION_SERVER_UTIL_H_
#define ARC_VM_BOOT_NOTIFICATION_SERVER_UTIL_H_

#include <sys/socket.h>

#include <string>

#include <base/files/scoped_file.h>
#include <base/optional.h>

// Returns the length of the corresponding sockaddr_XX structure for the given
// socket family.
socklen_t GetSockLen(sa_family_t family);

// Creates a streaming socket bound to addr and starts listening on the
// socket. If successful, returns a socket in the listening state.
base::ScopedFD StartListening(sockaddr* addr);

// Waits for a client to connect to fd and returns the connected socket.
// client_addr will be populated with the address of the client.
base::ScopedFD WaitForClientConnect(int fd);

// Reads from fd until EOF (read() returns 0). If able to read successfully,
// returns read data as a string. Else, returns empty optional.
base::Optional<std::string> ReadFD(int fd);

#endif  // ARC_VM_BOOT_NOTIFICATION_SERVER_UTIL_H_
