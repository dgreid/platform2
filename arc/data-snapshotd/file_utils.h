// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_DATA_SNAPSHOTD_FILE_UTILS_H_
#define ARC_DATA_SNAPSHOTD_FILE_UTILS_H_

#include <vector>

#include <base/files/file_path.h>

#include "proto/directory.pb.h"

namespace arc {
namespace data_snapshotd {

// Extracts all files and file info for all files from |dir| and fills in
// |snapshot_directory| object, that should be non-nullptr.
// Returns true in case of success and false in case of any error.
bool ReadSnapshotDirectory(const base::FilePath& dir,
                           SnapshotDirectory* snapshot_directory);

// Calculates SHA256 hash for serialized |dir|.
// In case of any error returns empty hash.
std::vector<uint8_t> CalculateDirectoryCryptographicHash(
    const SnapshotDirectory& dir);

}  // namespace data_snapshotd
}  // namespace arc

#endif  // ARC_DATA_SNAPSHOTD_FILE_UTILS_H_
