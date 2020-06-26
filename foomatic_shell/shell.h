// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FOOMATIC_SHELL_SHELL_H_
#define FOOMATIC_SHELL_SHELL_H_

#include <string>

namespace foomatic_shell {

// The maximum size of single script is 16KB.
constexpr size_t kMaxSourceSize = 16 * 1024;

// Parse and execute a shell script in |source|. Generated output is saved to
// the file descriptor |output_fd|. When necessary, input data is read from the
// standard input (file descriptor = 0). Error messages are written to standard
// error stream (file descriptor = 2). |output_fd| must be a valid file
// descriptor different that 0 and 2. |verbose_mode| is used to control logging
// level - all logs are dumped to stderr. |recursion_level| is used to control
// maximum recursion depth and should be set to the default value. The function
// returns exit code returned by executed script or value 127 in case of an
// shell error.
int ExecuteShellScript(const std::string& source,
                       const int output_fd,
                       const bool verbose_mode,
                       const int recursion_level = 0);

}  // namespace foomatic_shell

#endif  // FOOMATIC_SHELL_SHELL_H_
