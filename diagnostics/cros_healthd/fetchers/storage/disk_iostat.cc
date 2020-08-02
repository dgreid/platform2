// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/storage/disk_iostat.h"

#include <cstdint>
#include <sstream>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/optional.h>
#include <base/time/time.h>

#include "diagnostics/common/statusor.h"

namespace diagnostics {

namespace {

constexpr char kStatFile[] = "stat";

}  // namespace

DiskIoStat::DiskIoStat(const base::FilePath& dev_sys_path)
    : dev_sys_path_(dev_sys_path) {}

Status DiskIoStat::Update() {
  base::FilePath stat_path = dev_sys_path_.Append(kStatFile);

  std::string stat_string;
  if (!ReadFileToString(stat_path, &stat_string)) {
    return Status(StatusCode::kUnavailable,
                  "Unable to read " + stat_path.value());
  }

  std::stringstream stat_stream(stat_string);
  stat_stream >> read_ios >> read_merges >> read_sectors >> read_ticks >>
      write_ios >> write_merges >> write_sectors >> write_ticks >> in_flight >>
      io_ticks >> time_in_queue;

  if (stat_stream.fail() || stat_stream.bad()) {
    return Status(StatusCode::kInvalidArgument,
                  "Failed to parse " + stat_path.value());
  }

  // Might not be present on older kernels, thus we consider those fields
  // best effort and ignore parsing errors.
  stat_stream >> discard_ios >> discard_merges >> discard_sectors >>
      discard_ticks;
  if (!stat_stream.fail() && !stat_stream.bad())
    extended_iostat_ = true;
  iostat_populated_ = true;

  return Status::OkStatus();
}

base::TimeDelta DiskIoStat::GetReadTime() const {
  DCHECK(iostat_populated_);
  return base::TimeDelta::FromMilliseconds(static_cast<int64_t>(read_ticks));
}

base::TimeDelta DiskIoStat::GetWriteTime() const {
  DCHECK(iostat_populated_);
  return base::TimeDelta::FromMilliseconds(static_cast<int64_t>(write_ticks));
}

uint64_t DiskIoStat::GetReadSectors() const {
  DCHECK(iostat_populated_);
  return read_sectors;
}

uint64_t DiskIoStat::GetWrittenSectors() const {
  DCHECK(iostat_populated_);
  return write_sectors;
}

base::TimeDelta DiskIoStat::GetIoTime() const {
  DCHECK(iostat_populated_);
  return base::TimeDelta::FromMilliseconds(static_cast<int64_t>(io_ticks));
}

base::Optional<base::TimeDelta> DiskIoStat::GetDiscardTime() const {
  DCHECK(iostat_populated_);
  if (extended_iostat_) {
    return base::TimeDelta::FromMilliseconds(
        static_cast<int64_t>(discard_ticks));
  }
  return base::nullopt;
}

}  // namespace diagnostics
