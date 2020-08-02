// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STORAGE_DISK_IOSTAT_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STORAGE_DISK_IOSTAT_H_

#include <cstdint>

#include <base/files/file_path.h>
#include <base/optional.h>
#include <base/time/time.h>

#include "diagnostics/common/statusor.h"

namespace diagnostics {

// Class for accessing I/O statistics of a device.
class DiskIoStat {
 public:
  explicit DiskIoStat(const base::FilePath& dev_sys_path);
  DiskIoStat(const DiskIoStat&) = delete;
  DiskIoStat(DiskIoStat&&) = delete;
  DiskIoStat& operator=(const DiskIoStat&) = delete;
  DiskIoStat& operator=(DiskIoStat&&) = delete;

  base::TimeDelta GetReadTime() const;
  base::TimeDelta GetWriteTime() const;
  uint64_t GetReadSectors() const;
  uint64_t GetWrittenSectors() const;
  base::TimeDelta GetIoTime() const;
  base::Optional<base::TimeDelta> GetDiscardTime() const;

  // Retrieves current I/O statistics for the device.
  // Must be called before using getters of the class.
  Status Update();

 private:
  const base::FilePath dev_sys_path_;

  // Whether Update() was called at least once.
  bool iostat_populated_ = false;
  // Whether the iostat contains the fields added in 4.18 kernel
  bool extended_iostat_ = false;

  // All fields are read, but there are accessors only to the ones which are
  // actually used.
  uint64_t read_ios = 0;
  uint64_t read_merges = 0;
  uint64_t read_sectors = 0;
  uint64_t read_ticks = 0;
  uint64_t write_ios = 0;
  uint64_t write_merges = 0;
  uint64_t write_sectors = 0;
  uint64_t write_ticks = 0;
  uint64_t in_flight = 0;
  uint64_t io_ticks = 0;
  uint64_t time_in_queue = 0;
  uint64_t discard_ios = 0;
  uint64_t discard_merges = 0;
  uint64_t discard_sectors = 0;
  uint64_t discard_ticks = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STORAGE_DISK_IOSTAT_H_
