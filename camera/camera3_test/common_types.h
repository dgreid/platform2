// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_CAMERA3_TEST_COMMON_TYPES_H_
#define CAMERA_CAMERA3_TEST_COMMON_TYPES_H_

namespace camera3_test {

class ResolutionInfo {
 public:
  ResolutionInfo(int32_t width, int32_t height)
      : width_(width), height_(height) {}

  ResolutionInfo() : width_(0), height_(0) {}

  int32_t Width() const;

  int32_t Height() const;

  int32_t Area() const;

  bool operator==(const ResolutionInfo& resolution) const;

  bool operator<(const ResolutionInfo& resolution) const;

  friend std::ostream& operator<<(std::ostream& out,
                                  const ResolutionInfo& info);

 private:
  int32_t width_, height_;
};

}  // namespace camera3_test

#endif  // CAMERA_CAMERA3_TEST_COMMON_TYPES_H_
