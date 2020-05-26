// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <csignal>

#include <base/logging.h>

#include "iioservice/daemon/daemon.h"

int main() {
  LOG(INFO) << "Starting iioservice";
  iioservice::Daemon daemon;
  daemon.Run();
  LOG(INFO) << "Daemon stopped";

  return 0;
}
