// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_CRASH_SENDER_TOOL_H_
#define DEBUGD_SRC_CRASH_SENDER_TOOL_H_

#include <string>
#include <tuple>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <brillo/errors/error.h>

#include "debugd/src/subprocess_tool.h"

namespace brillo {
namespace dbus_utils {
class ExportedPropertyBase;
}  // namespace dbus_utils
}  // namespace brillo

namespace debugd {

class CrashSenderTool : public SubprocessTool {
 public:
  static constexpr char kErrorBadFileName[] =
      "org.chromium.debugd.error.BadFileName";

  CrashSenderTool() = default;
  CrashSenderTool(const CrashSenderTool&) = delete;
  CrashSenderTool& operator=(const CrashSenderTool&) = delete;

  ~CrashSenderTool() override = default;

  // Run crash_sender to upload any crashes currently on the system.
  void UploadCrashes();

  // Run crash_sender to upload the crash given in the files in |in_files|.
  bool UploadSingleCrash(
      const std::vector<std::tuple<std::string, base::ScopedFD>>& in_files,
      brillo::ErrorPtr* error);

  // Called when the CrashSenderTestMode dbus property is changed.
  void OnTestModeChanged(
      const brillo::dbus_utils::ExportedPropertyBase* test_mode_property);

 private:
  int next_crash_directory_id_ = 1;

  // If true, pass the "--test_mode" flag to crash_sender. This is bound to the
  // "CrashSenderTestMode" dbus property, so tast tests (and anyone else) can
  // change it easily.
  bool test_mode_ = false;

  void RunCrashSender(bool ignore_hold_off_time,
                      const base::FilePath& crash_directory);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_CRASH_SENDER_TOOL_H_
