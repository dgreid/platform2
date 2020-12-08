// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/mock_disk_cleanup_routines.h"

namespace cryptohome {

MockDiskCleanupRoutines::MockDiskCleanupRoutines()
    : DiskCleanupRoutines(nullptr, nullptr) {}
MockDiskCleanupRoutines::~MockDiskCleanupRoutines() = default;

}  // namespace cryptohome
