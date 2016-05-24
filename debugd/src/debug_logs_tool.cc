// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/debug_logs_tool.h"

#include <base/files/file_util.h>
#include <brillo/process.h>


namespace debugd {

const char* const kTar = "/bin/tar";
const char* const kSystemLogs = "/var/log";

void DebugLogsTool::GetDebugLogs(bool is_compressed,
                                 const DBus::FileDescriptor& fd,
                                 DBus::Error* error) {
  brillo::ProcessImpl p;
  p.AddArg(kTar);
  p.AddArg("-c");
  if (is_compressed)
    p.AddArg("-z");

  p.AddArg(kSystemLogs);
  p.BindFd(fd.get(), STDOUT_FILENO);
  p.Run();
  close(fd.get());
}

}  // namespace debugd
