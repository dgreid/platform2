// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_METRICS_H_
#define CROS_DISKS_METRICS_H_

#include <string>
#include <unordered_map>

#include <base/macros.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest_prod.h>
#include <metrics/metrics_library.h>

namespace cros_disks {

// A class for collecting cros-disks related UMA metrics.
class Metrics {
 public:
  Metrics() = default;
  ~Metrics() = default;

  // Records the type of archive that cros-disks is trying to mount.
  void RecordArchiveType(const std::string& archive_type);

  // Records the type of filesystem that cros-disks is trying to mount.
  void RecordFilesystemType(const std::string& filesystem_type);

  // Records the type of device media that cros-disks is trying to mount.
  void RecordDeviceMediaType(DeviceMediaType device_media_type);

  // Records the error code returned by a FUSE mounter program.
  void RecordFuseMounterErrorCode(const std::string& mounter_path,
                                  int error_code);

 private:
  // Don't renumber these values. They are recorded in UMA metrics.
  enum ArchiveType {
    kArchiveUnknown = 0,
    kArchiveZip = 1,
    kArchiveRar = 2,
    kArchiveTar = 3,
    kArchiveTarBzip2 = 4,
    kArchiveTarGzip = 5,
    kArchiveMaxValue = 6,
  };

  // Don't renumber these values. They are recorded in UMA metrics.
  enum FilesystemType {
    kFilesystemUnknown = 0,
    kFilesystemOther = 1,
    kFilesystemVFAT = 2,
    kFilesystemExFAT = 3,
    kFilesystemNTFS = 4,
    kFilesystemHFSPlus = 5,
    kFilesystemExt2 = 6,
    kFilesystemExt3 = 7,
    kFilesystemExt4 = 8,
    kFilesystemISO9660 = 9,
    kFilesystemUDF = 10,
    kFilesystemMaxValue = 11,
  };

  // Returns the MetricsArchiveType enum value for the specified archive type
  // string.
  ArchiveType GetArchiveType(const std::string& archive_type) const;

  // Returns the MetricsFilesystemType enum value for the specified filesystem
  // type string.
  FilesystemType GetFilesystemType(const std::string& filesystem_type) const;

  MetricsLibrary metrics_library_;

  // Mapping from an archive type to its corresponding metric value.
  const std::unordered_map<std::string, ArchiveType> archive_type_map_{
      {"rar", kArchiveRar},          {"tar", kArchiveTar},
      {"tar.bz2", kArchiveTarBzip2}, {"tbz", kArchiveTarBzip2},
      {"tbz2", kArchiveTarBzip2},    {"tar.gz", kArchiveTarGzip},
      {"tgz", kArchiveTarGzip},      {"zip", kArchiveZip}};

  // Mapping from a filesystem type to its corresponding metric value.
  const std::unordered_map<std::string, FilesystemType> filesystem_type_map_{
      {"", kFilesystemUnknown},        {"exfat", kFilesystemExFAT},
      {"ext2", kFilesystemExt2},       {"ext3", kFilesystemExt3},
      {"ext4", kFilesystemExt4},       {"hfsplus", kFilesystemHFSPlus},
      {"iso9660", kFilesystemISO9660}, {"ntfs", kFilesystemNTFS},
      {"udf", kFilesystemUDF},         {"vfat", kFilesystemVFAT}};

  FRIEND_TEST(MetricsTest, GetArchiveType);
  FRIEND_TEST(MetricsTest, GetFilesystemType);

  DISALLOW_COPY_AND_ASSIGN(Metrics);
};

}  // namespace cros_disks

#endif  // CROS_DISKS_METRICS_H_
