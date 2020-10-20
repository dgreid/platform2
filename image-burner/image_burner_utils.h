// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IMAGE_BURNER_IMAGE_BURNER_UTILS_H_
#define IMAGE_BURNER_IMAGE_BURNER_UTILS_H_

#include <unistd.h>

#include <string>

#include <base/files/file.h>
#include <base/callback.h>
#include <base/macros.h>

#include "image-burner/image_burner_utils_interfaces.h"

namespace imageburn {

class BurnWriter : public FileSystemWriter {
 public:
  using FstatCallback =
      base::RepeatingCallback<int(int, base::stat_wrapper_t*)>;

  BurnWriter();
  ~BurnWriter() override = default;

  bool Open(const char* path) override;
  bool Close() override;
  int Write(const char* data_block, int data_size) override;

  const base::File& file() const { return file_; }
  void set_fstat_for_test(const FstatCallback& fstat_callback) {
    fstat_callback_ = fstat_callback;
  }

 private:
  base::File file_;
  int writes_count_{0};

  FstatCallback fstat_callback_;

  DISALLOW_COPY_AND_ASSIGN(BurnWriter);
};

class BurnReader : public FileSystemReader {
 public:
  BurnReader();
  ~BurnReader() override = default;

  bool Open(const char* path) override;
  bool Close() override;
  int Read(char* data_block, int data_size) override;
  int64_t GetSize() override;

 private:
  base::File file_;

  DISALLOW_COPY_AND_ASSIGN(BurnReader);
};

class BurnPathGetter : public PathGetter {
 public:
  BurnPathGetter() = default;
  ~BurnPathGetter() override = default;

  bool GetRealPath(const char* path, std::string* real_path) override;
  bool GetRootPath(std::string* path) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(BurnPathGetter);
};

}  // namespace imageburn

#endif  // IMAGE_BURNER_IMAGE_BURNER_UTILS_H_
