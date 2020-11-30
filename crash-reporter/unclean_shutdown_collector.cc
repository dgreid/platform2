// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/unclean_shutdown_collector.h"

#include <base/files/file_util.h>
#include <base/logging.h>

namespace {

const char kOsRelease[] = "/etc/os-release";

const char kUncleanShutdownFile[] =
    "/var/lib/crash_reporter/pending_clean_shutdown";

// Files created by power manager used for crash reporting.
const char kPowerdTracePath[] = "/var/lib/power_manager";
// Presence of this file indicates that the system was suspended
const char kPowerdSuspended[] = "powerd_suspended";

}  // namespace

using base::FilePath;

UncleanShutdownCollector::UncleanShutdownCollector()
    : CrashCollector("unclean_shutdown"),
      unclean_shutdown_file_(kUncleanShutdownFile),
      powerd_trace_path_(kPowerdTracePath),
      powerd_suspended_file_(powerd_trace_path_.Append(kPowerdSuspended)),
      os_release_path_(kOsRelease) {}

UncleanShutdownCollector::~UncleanShutdownCollector() {}

bool UncleanShutdownCollector::Enable() {
  FilePath file_path(unclean_shutdown_file_);
  base::CreateDirectory(file_path.DirName());
  if (base::WriteFile(file_path, "", 0) != 0) {
    PLOG(ERROR) << "Unable to create shutdown check file";
    return false;
  }
  return true;
}

bool UncleanShutdownCollector::DeleteUncleanShutdownFiles() {
  if (!base::DeleteFile(FilePath(unclean_shutdown_file_))) {
    PLOG(ERROR) << "Failed to delete unclean shutdown file "
                << unclean_shutdown_file_;
    return false;
  }
  // Delete power manager state file if it exists.
  base::DeleteFile(powerd_suspended_file_);
  return true;
}

bool UncleanShutdownCollector::Collect() {
  FilePath unclean_file_path(unclean_shutdown_file_);
  if (!base::PathExists(unclean_file_path)) {
    return false;
  }
  LOG(WARNING) << "Last shutdown was not clean";
  if (DeadBatteryCausedUncleanShutdown()) {
    DeleteUncleanShutdownFiles();
    return false;
  }
  DeleteUncleanShutdownFiles();

  return true;
}

bool UncleanShutdownCollector::Disable() {
  LOG(INFO) << "Clean shutdown signalled";
  return DeleteUncleanShutdownFiles();
}

bool UncleanShutdownCollector::SaveVersionData() {
  FilePath crash_directory(crash_reporter_state_path_);
  FilePath saved_lsb_release = crash_directory.Append(lsb_release_.BaseName());
  if (!base::CopyFile(lsb_release_, saved_lsb_release)) {
    PLOG(ERROR) << "Failed to copy " << lsb_release_.value() << " to "
                << saved_lsb_release.value();
    return false;
  }

  FilePath saved_os_release =
      crash_directory.Append(os_release_path_.BaseName());
  if (!base::CopyFile(os_release_path_, saved_os_release)) {
    PLOG(ERROR) << "Failed to copy " << os_release_path_.value() << " to "
                << saved_os_release.value();
    return false;
  }

  // TODO(bmgordon): When crash_sender reads from os-release.d, copy it also.

  return true;
}

bool UncleanShutdownCollector::DeadBatteryCausedUncleanShutdown() {
  // Check for case of battery running out while suspended.
  if (base::PathExists(powerd_suspended_file_)) {
    LOG(INFO) << "Unclean shutdown occurred while suspended. Not counting "
              << "toward unclean shutdown statistic.";
    return true;
  }
  return false;
}
