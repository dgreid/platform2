// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains assorted functions used in mount-related classed.

#include "cryptohome/storage/mount_utils.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <vector>

#include <base/files/file_util.h>

namespace cryptohome {

bool ReadProtobuf(int in_fd, google::protobuf::MessageLite* message) {
  size_t proto_size = 0;
  if (!base::ReadFromFD(in_fd, reinterpret_cast<char*>(&proto_size),
                        sizeof(proto_size))) {
    PLOG(ERROR) << "Failed to read protobuf size";
    return false;
  }

  std::vector<char> buf(proto_size);
  if (!base::ReadFromFD(in_fd, buf.data(), buf.size())) {
    PLOG(ERROR) << "Failed to read protobuf";
    return false;
  }

  if (!message->ParseFromArray(buf.data(), buf.size())) {
    LOG(ERROR) << "Failed to parse protobuf";
    return false;
  }

  return true;
}

bool WriteProtobuf(int out_fd, const google::protobuf::MessageLite& message) {
  size_t size = message.ByteSizeLong();
  std::vector<char> buf(message.ByteSizeLong());
  if (!message.SerializeToArray(buf.data(), buf.size())) {
    LOG(ERROR) << "Failed to serialize protobuf";
    return false;
  }

  if (!base::WriteFileDescriptor(out_fd, reinterpret_cast<char*>(&size),
                                 sizeof(size))) {
    PLOG(ERROR) << "Failed to write protobuf size";
    return false;
  }

  if (!base::WriteFileDescriptor(out_fd, buf.data(), size)) {
    PLOG(ERROR) << "Failed to write protobuf";
    return false;
  }

  return true;
}

void ForkAndCrash(const std::string& message) {
  pid_t child_pid = fork();

  if (child_pid < 0) {
    PLOG(ERROR) << "fork() failed";
  } else if (child_pid == 0) {
    // Child process: crash with |message|.
    LOG(FATAL) << message;
  } else {
    // |child_pid| > 0
    // Parent process: reap the child process in a best-effort way and return
    // normally.
    waitpid(child_pid, nullptr, 0);
  }
}

}  // namespace cryptohome
