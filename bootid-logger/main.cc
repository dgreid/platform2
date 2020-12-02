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

  if (WriteCurrentBootEntry(base::FilePath(kBootLogFile), kBootLogMaxEntries))
    return 0;
  else
    return EXIT_FAILURE;
}
