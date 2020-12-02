// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <sys/stat.h>

#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"

#include "bootid-logger/bootid_logger.h"

namespace {

constexpr char kBootIdProcPath[] = "/proc/sys/kernel/random/boot_id";
constexpr char kBootLogFile[] = "/var/log/boot_id.log";
constexpr size_t kBootLogMaxEntries = 500;

}  // anonymous namespace

int main(int argc, char* argv[]) {
  if (argc > 2) {
    LOG(ERROR) << "Doesn't support any command line options.";
    exit(EXIT_FAILURE);
  }

  struct stat sb;
  stat(kBootLogFile, &sb);
  if ((sb.st_mode & S_IFMT) != S_IFREG) {
    // The file is not a regular file. Remove this.
    unlink(kBootLogFile);
  }

  std::string boot_id;
  if (!base::ReadFileToString(base::FilePath(kBootIdProcPath), &boot_id)) {
    LOG(FATAL) << "Reading the log fail failed";
    exit(EXIT_FAILURE);
  }
  base::RemoveChars(boot_id, "-", &boot_id);
  CHECK_EQ(kBootIdLength, boot_id.length());

  struct timespec boot_timespec;
  if (clock_gettime(CLOCK_BOOTTIME, &boot_timespec) == -1) {
    PLOG(FATAL) << "clock_gettime failed";
    exit(EXIT_FAILURE);
  }

  base::Time boot_time =
      base::Time::Now() - base::TimeDelta::FromTimeSpec(boot_timespec);

  if (WriteBootEntry(base::FilePath(kBootLogFile), boot_id, boot_time,
                     kBootLogMaxEntries))
    return 0;
  else
    return EXIT_FAILURE;
}
