// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/fp_frame_command.h"

#include <algorithm>
#include <vector>

#include <base/threading/platform_thread.h>

namespace biod {

bool FpFrameCommand::Run(int fd) {
  uint32_t offset = frame_index_ << FP_FRAME_INDEX_SHIFT;
  FpFramePacket payload = *Resp();
  auto pos = frame_data_.begin();
  while (pos < frame_data_.end()) {
    uint32_t len = std::min(max_read_size_, frame_data_.end() - pos);
    SetReq({.offset = offset, .size = len});
    SetRespSize(len);
    int retries = 0;
    while (!EcCommandRun(fd)) {
      if (!(offset & FP_FRAME_OFFSET_MASK)) {
        // On the first request, the EC might still be rate-limiting. Retry in
        // that case.
        if (Result() == EC_RES_BUSY && retries < kMaxRetries) {
          retries++;
          LOG(INFO) << "Retrying FP_FRAME, attempt " << retries;
          base::PlatformThread::Sleep(
              base::TimeDelta::FromMilliseconds(kRetryDelayMs));
          continue;
        }
      }
      LOG(ERROR) << "FP_FRAME command failed @ 0x" << std::hex << offset;
      return false;
    }
    std::copy(payload.begin(), payload.begin() + len, pos);
    offset += len;
    pos += len;
  }
  return true;
}

const brillo::SecureVector& FpFrameCommand::frame() const {
  return frame_data_;
}

bool FpFrameCommand::EcCommandRun(int fd) {
  return EcCommand::Run(fd);
}

}  // namespace biod
