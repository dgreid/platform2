// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of bootstat_log(), part of the Chromium OS 'bootstat'
// facility.

#include <assert.h>
#include <libgen.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <brillo/brillo_export.h>
#include <rootdev/rootdev.h>

#include "bootstat.h"
#include "bootstat_test.h"

//
// Default path to directory where output statistics will be stored.
//
static const char kDefaultOutputDirectoryName[] = "/tmp";

//
// Paths to the statistics files we snapshot as part of the data to
// be logged.
//
static const char kDefaultUptimeStatisticsFileName[] = "/proc/uptime";

static const char* output_directory_name = kDefaultOutputDirectoryName;
static const char* uptime_statistics_file_name =
    kDefaultUptimeStatisticsFileName;

static const char* disk_statistics_file_name_for_test = NULL;

// Stores the path representing the stats file for the root disk in
// |stats_path| of length |len|.
// Returns |stats_path| on success, NULL on failure.
static char* get_disk_statistics_file_name(char* stats_path, size_t len) {
  char boot_path[PATH_MAX];
  int ret = rootdev(boot_path, sizeof(boot_path),
                    true,    // Do full resolution.
                    false);  // Do not remove partition number.
  if (ret < 0) {
    return NULL;
  }

  // The general idea is to use the the root device's sysfs entry to
  // get the path to the root disk's sysfs entry.
  // Example:
  // - rootdev() returns "/dev/sda3"
  // - Use /sys/class/block/sda3/../ to get to root disk (sda) sysfs entry.
  //   This is because /sys/class/block/sda3 is a symlink that maps to:
  //     /sys/devices/pci.../.../ata./host./target.../.../block/sda/sda3
  const char* root_device_name = basename(boot_path);
  if (!root_device_name) {
    return NULL;
  }

  ret = snprintf(stats_path, len, "/sys/class/block/%s/../stat",
                 root_device_name);
  if (ret >= PATH_MAX || ret < 0) {
    return NULL;
  }
  return stats_path;
}

static void append_logdata(const char* input_path,
                           const char* output_name_prefix,
                           const char* event_name) {
  if (!input_path || !output_name_prefix || !event_name)
    return;
  const mode_t kFileCreationMode =
      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
  char output_path[PATH_MAX];
  char buffer[256];
  ssize_t num_read;
  int ifd, ofd;
  int output_path_len;

  ifd = open(input_path, O_RDONLY);
  if (ifd < 0) {
    return;
  }

  //
  // For those not up on the more esoteric features of printf
  // formats:  the "%.*s" format is used to truncate the event name
  // to the proper number of characters..
  //
  // The assertion for output_path overflow should only be able to
  // fail if output_directory_name is changed from its default,
  // which can only happen in unit tests, and then only in the event
  // of a serious test bug.
  //
  output_path_len = snprintf(output_path, sizeof(output_path), "%s/%s-%.*s",
                             output_directory_name, output_name_prefix,
                             BOOTSTAT_MAX_EVENT_LEN - 1, event_name);
  // output_path_len is unused when the assert() is disabled. Prevent the
  // warning.
  (void)output_path_len;
  assert(output_path_len < sizeof(output_path));
  ofd = open(output_path, O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW,
             kFileCreationMode);
  if (ofd < 0) {
    (void)close(ifd);
    return;
  }

  while ((num_read = read(ifd, buffer, sizeof(buffer))) > 0) {
    ssize_t num_written = write(ofd, buffer, num_read);
    if (num_written != num_read)
      break;
  }
  (void)close(ofd);
  (void)close(ifd);
}

BRILLO_EXPORT
void bootstat_log(const char* event_name) {
  const char* disk_statistics_file_name;
  char stats_path[PATH_MAX];
  append_logdata(uptime_statistics_file_name, "uptime", event_name);
  if (disk_statistics_file_name_for_test) {
    disk_statistics_file_name = disk_statistics_file_name_for_test;
  } else {
    disk_statistics_file_name =
        get_disk_statistics_file_name(stats_path, sizeof(stats_path));
  }
  append_logdata(disk_statistics_file_name, "disk", event_name);
}

BRILLO_EXPORT
void bootstat_set_output_directory_for_test(const char* dirname) {
  if (dirname != NULL)
    output_directory_name = dirname;
  else
    output_directory_name = kDefaultOutputDirectoryName;
}

BRILLO_EXPORT
void bootstat_set_uptime_file_name_for_test(const char* filename) {
  if (filename != NULL)
    uptime_statistics_file_name = filename;
  else
    uptime_statistics_file_name = kDefaultUptimeStatisticsFileName;
}

BRILLO_EXPORT
void bootstat_set_disk_file_name_for_test(const char* filename) {
  disk_statistics_file_name_for_test = filename;
}
