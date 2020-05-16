// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/daemon.h"

namespace typecd {

Daemon::Daemon()
    : weak_factory_(this) {}

Daemon::~Daemon() {}

int Daemon::OnInit() {
  LOG(INFO) << "Daemon started.";
  return 0;
}

}  // namespace typecd
