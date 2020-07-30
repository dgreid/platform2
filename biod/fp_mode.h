// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_FP_MODE_H_
#define BIOD_FP_MODE_H_

#include <ostream>

namespace biod {

class FpMode {
 public:
  enum class Mode : int {
    // NOTE: These values are used directly by UMA, so the values must not
    // be modified. New values should be added to the end (before kModeInvalid).
    kNone = 0,
    kDeepsleep,
    kFingerDown,
    kFingerUp,
    kCapture,
    kEnrollSession,
    kEnrollSessionFingerUp,
    kEnrollSessionEnrollImage,
    kEnrollImage,
    kMatch,
    kResetSensor,
    kDontChange,
    kSensorMaintenance,

    kModeInvalid  // must be last item
  };

  FpMode() = default;
  explicit FpMode(Mode mode) : mode_(mode) {}
  explicit FpMode(uint32_t mode);

  bool operator==(const FpMode& rhs) const { return mode_ == rhs.mode_; }
  bool operator!=(const FpMode& rhs) const { return !(rhs == *this); }

  friend std::ostream& operator<<(std::ostream& os, const FpMode& mode) {
    return os << "(enum: " << mode.EnumVal() << ", raw: 0x" << std::hex
              << mode.RawVal() << std::dec << ")";
  }

  Mode mode() const { return mode_; }

  uint32_t RawVal() const { return EnumToRawVal(mode_); }

  // TODO(tomhughes): switch to to_utype template instead of casting
  int EnumVal() const { return static_cast<int>(mode_); }
  int MaxEnumVal() const { return static_cast<int>(Mode::kModeInvalid); }

 private:
  Mode RawValToEnum(uint32_t mode) const;
  uint32_t EnumToRawVal(Mode mode) const;

  Mode mode_ = Mode::kModeInvalid;
};

}  // namespace biod

#endif  // BIOD_FP_MODE_H_
