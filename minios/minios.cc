// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>

#include "minios/minios.h"

namespace {
constexpr char kDebugConsole[] = "/dev/pts/2";
}  // namespace

int MiniOs::Run() {
  LOG(INFO) << "Starting miniOS.";

  // Start the shell on DEBUG console.
  return ProcessManager().RunCommand({"/bin/sh"}, kDebugConsole, kDebugConsole);
}
