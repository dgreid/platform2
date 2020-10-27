// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_FP_FRAME_COMMAND_H_
#define BIOD_FP_FRAME_COMMAND_H_

#include <array>
#include <vector>

#include <brillo/secure_blob.h>
#include "biod/ec_command.h"

namespace biod {

// Upper bound of the host command packet transfer size.
constexpr static int kMaxPacketSize = 544;
using FpFramePacket = std::array<uint8_t, kMaxPacketSize>;

class FpFrameCommand
    : public EcCommand<struct ec_params_fp_frame, FpFramePacket> {
 public:
  FpFrameCommand(int index, uint32_t frame_size, uint16_t max_read_size)
      : EcCommand(EC_CMD_FP_FRAME),
        frame_index_(index),
        max_read_size_(max_read_size),
        frame_data_(frame_size) {}
  ~FpFrameCommand() override = default;

  bool Run(int fd) override;

  const brillo::SecureVector& frame() const;

 protected:
  virtual bool EcCommandRun(int fd);

 private:
  constexpr static int kMaxRetries = 50;
  constexpr static int kRetryDelayMs = 100;

  int frame_index_ = 0;
  uint16_t max_read_size_ = 0;
  brillo::SecureVector frame_data_;
};

static_assert(!std::is_copy_constructible<FpFrameCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<FpFrameCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace biod

#endif  // BIOD_FP_FRAME_COMMAND_H_
