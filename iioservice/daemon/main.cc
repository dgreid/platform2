// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <brillo/daemons/daemon.h>

int main() {
  brillo::Daemon daemon;
  daemon.Run();
  LOG(INFO) << "Daemon stopped";

  return 0;
}
