// Copyright (c) 2010 The Chromium OS Authors. All rights reser.
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
//
// File mock class
#ifndef VERITY_SIMPLE_FILE_MOCK_FILE_H_
#define VERITY_SIMPLE_FILE_MOCK_FILE_H_

#include "verity/simple_file/file.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace simple_file {

class MockFile : public File {
 public:
  MockFile() {}
  ~MockFile() {}
  MOCK_CONST_METHOD0(env, const Env*());
  MOCK_METHOD3(Initialize, bool(const char*, int, const Env*));
  MOCK_METHOD2(Read, bool(int, uint8_t*));
  MOCK_METHOD3(ReadAt, bool(int, uint8_t*, off_t));
  MOCK_METHOD2(Write, bool(int, const uint8_t*));
  MOCK_METHOD3(WriteAt, bool(int, const uint8_t*, off_t));
  MOCK_CONST_METHOD0(Size, int64_t());
  MOCK_CONST_METHOD0(Whence, off_t());
  MOCK_METHOD2(Seek, bool(off_t, bool));
  MOCK_METHOD0(Reset, void());
};

}  // namespace simple_file

#endif  // VERITY_SIMPLE_FILE_MOCK_FILE_H_
