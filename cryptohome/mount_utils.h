// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOUNT_UTILS_H_
#define CRYPTOHOME_MOUNT_UTILS_H_

#include <string>

#include <google/protobuf/message_lite.h>

#include "cryptohome/platform.h"

namespace cryptohome {

constexpr bool IsolateUserSession() {
  return USE_USER_SESSION_ISOLATION;
}

// Cryptohome uses protobufs to communicate with the out-of-process mount
// helper.
bool ReadProtobuf(int fd, google::protobuf::MessageLite* message);
bool WriteProtobuf(int fd, const google::protobuf::MessageLite& message);

// Forks a child process that immediately prints |message| and crashes.
// This is useful to report an error through crash reporting without taking
// down the entire process, therefore allowing it to clean up and exit
// normally.
void ForkAndCrash(const std::string& message);

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOUNT_UTILS_H_
