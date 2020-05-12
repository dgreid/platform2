// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/ephemeral_crash_collector.h"

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <brillo/process.h>

#include <string>

#include "crash-reporter/paths.h"
#include "crash-reporter/util.h"

EphemeralCrashCollector::EphemeralCrashCollector()
    : CrashCollector("ephemeral_crash_collector"),
      early_(false),
      source_directories_({base::FilePath(paths::kSystemRunCrashDirectory)}) {}

void EphemeralCrashCollector::Initialize(
    IsFeedbackAllowedFunction is_feedback_allowed_function,
    bool preserve_across_clobber) {
  // For preserving crash reports across clobbers, the consent file may not be
  // available. Instead, collect the crashes into the encrypted reboot vault
  // directory and let crash-sender decide how to deal with these reports.
  if (preserve_across_clobber) {
    system_crash_path_ =
        base::FilePath(paths::kEncryptedRebootVaultCrashDirectory);
    is_feedback_allowed_function = []() { return true; };
  } else {
    source_directories_.push_back(
        base::FilePath(paths::kEncryptedRebootVaultCrashDirectory));
  }

  // Disable early mode.
  CrashCollector::Initialize(is_feedback_allowed_function, false /* early */);
}

bool EphemeralCrashCollector::Collect() {
#if USE_DIRENCRYPTION
  // Join the session keyring, if one exists.
  util::JoinSessionKeyring();
#endif  // USE_DIRENCRYPTION

  if (is_feedback_allowed_function_()) {
    for (auto& dir : source_directories_) {
      base::FileEnumerator source_directory_enumerator(
          dir, false /* recursive */, base::FileEnumerator::FILES);

      for (auto source_path = source_directory_enumerator.Next();
           !source_path.empty();
           source_path = source_directory_enumerator.Next()) {
        // Get crash directory to put logs in.
        base::FilePath destination_directory;

        // If the crash reporter directory is already fully occupied, then exit.
        if (!GetCreatedCrashDirectoryByEuid(0, &destination_directory, nullptr))
          break;

        base::FilePath destination_path =
            destination_directory.Append(source_path.BaseName());
        LOG(INFO) << "Copying early crash to: " << destination_path.value();

        if (!base::Move(source_path, destination_path)) {
          PLOG(WARNING) << "Unable to copy " << source_path.value();
          continue;
        }
      }
    }
  } else {
    LOG(INFO) << "Not collecting early crashes: No user consent available.";
  }

  // Cleanup crash directory.
  for (auto& dir : source_directories_)
    base::DeleteFile(dir, true /* recursive */);

  return true;
}
