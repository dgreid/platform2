// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_COMMON_MOJO_TEST_UTILS_H_
#define DIAGNOSTICS_COMMON_MOJO_TEST_UTILS_H_

#include <memory>
#include <string>

#include <base/files/scoped_file.h>
#include <mojo/public/cpp/system/handle.h>

namespace diagnostics {

// Helper class that allows to obtain fake file descriptors for use in tests
// where a valid file descriptor is expected.
class FakeMojoFdGenerator final {
 public:
  FakeMojoFdGenerator();
  FakeMojoFdGenerator(const FakeMojoFdGenerator&) = delete;
  FakeMojoFdGenerator& operator=(const FakeMojoFdGenerator&) = delete;
  ~FakeMojoFdGenerator();

  // Returns a duplicate of the file descriptor held by this instance.
  base::ScopedFD MakeFd() const;

  // Returns whether the given file descriptor points to the same underlying
  // object as the file descriptor held by this instance.
  bool IsDuplicateFd(int another_fd) const;

 private:
  base::ScopedFD fd_;
};

// Gets a content of a passed mojo::Handle.
// Makes an unnecessary copying of data, should be used only for testing.
//
// Returns an empty string if |handle| is not valid.
std::string GetStringFromMojoHandle(mojo::ScopedHandle handle);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_COMMON_MOJO_TEST_UTILS_H_
