// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/metrics.h"

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

namespace cros_disks {
namespace {

const char kArchiveTypeMetricName[] = "CrosDisks.ArchiveType";
const char kDeviceMediaTypeMetricName[] = "CrosDisks.DeviceMediaType";
const char kFilesystemTypeMetricName[] = "CrosDisks.FilesystemType";

}  // namespace

Metrics::ArchiveType Metrics::GetArchiveType(
    const std::string& archive_type) const {
  const auto map_iter = archive_type_map_.find(archive_type);
  if (map_iter != archive_type_map_.end())
    return map_iter->second;
  return kArchiveUnknown;
}

Metrics::FilesystemType Metrics::GetFilesystemType(
    const std::string& filesystem_type) const {
  const auto map_iter = filesystem_type_map_.find(filesystem_type);
  if (map_iter != filesystem_type_map_.end())
    return map_iter->second;
  return kFilesystemOther;
}

void Metrics::RecordArchiveType(const std::string& archive_type) {
  if (!metrics_library_.SendEnumToUMA(kArchiveTypeMetricName,
                                      GetArchiveType(archive_type),
                                      kArchiveMaxValue))
    LOG(WARNING) << "Failed to send archive type sample to UMA";
}

void Metrics::RecordFilesystemType(const std::string& filesystem_type) {
  if (!metrics_library_.SendEnumToUMA(kFilesystemTypeMetricName,
                                      GetFilesystemType(filesystem_type),
                                      kFilesystemMaxValue))
    LOG(WARNING) << "Failed to send filesystem type sample to UMA";
}

void Metrics::RecordDeviceMediaType(DeviceMediaType device_media_type) {
  if (!metrics_library_.SendEnumToUMA(kDeviceMediaTypeMetricName,
                                      device_media_type,
                                      DEVICE_MEDIA_NUM_VALUES))
    LOG(WARNING) << "Failed to send device media type sample to UMA";
}

void Metrics::RecordFuseMounterErrorCode(const std::string& mounter_path,
                                         const int error_code) {
  // Extract the mounter program name.
  std::string mounter_name = base::FilePath(mounter_path).BaseName().value();
  if (mounter_name.empty())
    return;

  // Make its first letter an uppercase.
  mounter_name.front() = base::ToUpperASCII(mounter_name.front());
  metrics_library_.SendSparseToUMA("CrosDisks.Fuse." + mounter_name,
                                   error_code);
}

}  // namespace cros_disks
