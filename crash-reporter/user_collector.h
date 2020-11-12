// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// UserCollector handles program crashes in userspace. When the kernel detects
// a crashing program, it invokes this collector via
// /proc/sys/kernel/core_pattern.
// This handler ignores chrome crashes (letting chrome_collector handle them
// when it is directly invoked).

#ifndef CRASH_REPORTER_USER_COLLECTOR_H_
#define CRASH_REPORTER_USER_COLLECTOR_H_

#include <functional>
#include <string>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "crash-reporter/user_collector_base.h"

// User crash collector.
class UserCollector : public UserCollectorBase {
 public:
  UserCollector();
  UserCollector(const UserCollector&) = delete;
  UserCollector& operator=(const UserCollector&) = delete;

  // Initialize the user crash collector for detection of crashes,
  // given the path to this executable, metrics collection enabled
  // oracle, and system logger facility. Crash detection/reporting
  // is not enabled until Enable is called.
  void Initialize(const std::string& our_path,
                  bool core2md_failure,
                  bool directory_failure,
                  bool early);

  ~UserCollector() override;

  // Enable collection.
  bool Enable(bool early) { return SetUpInternal(true /* enabled */, early); }

  // Disable collection.
  bool Disable() {
    return SetUpInternal(false /* enabled */, false /* early */);
  }

  // Set (override the default) core file pattern.
  void set_core_pattern_file(const std::string& pattern) {
    core_pattern_file_ = pattern;
  }

  // Set (override the default) core pipe limit file.
  void set_core_pipe_limit_file(const std::string& path) {
    core_pipe_limit_file_ = path;
  }

  void set_filter_path(const std::string& filter_path) {
    filter_path_ = filter_path;
  }

 protected:
  void FinishCrash(const base::FilePath& meta_path,
                   const std::string& exec_name,
                   const std::string& payload_name) override;

 private:
  friend class UserCollectorTest;
  FRIEND_TEST(UserCollectorTest, ClobberContainerDirectory);
  FRIEND_TEST(UserCollectorTest, CopyOffProcFilesBadPid);
  FRIEND_TEST(UserCollectorTest, CopyOffProcFilesOK);
  FRIEND_TEST(UserCollectorTest, GetExecutableBaseNameFromPid);
  FRIEND_TEST(UserCollectorTest, GetFirstLineWithPrefix);
  FRIEND_TEST(UserCollectorTest, GetIdFromStatus);
  FRIEND_TEST(UserCollectorTest, GetStateFromStatus);
  FRIEND_TEST(UserCollectorTest, GetProcessPath);
  FRIEND_TEST(UserCollectorTest, ParseCrashAttributes);
  FRIEND_TEST(UserCollectorTest, ShouldDumpFiltering);
  FRIEND_TEST(UserCollectorTest, ShouldDumpChromeOverridesDeveloperImage);
  FRIEND_TEST(UserCollectorTest, ShouldDumpDeveloperImageOverridesConsent);
  FRIEND_TEST(UserCollectorTest, ShouldDumpUserConsentProductionImage);
  FRIEND_TEST(UserCollectorTest, ValidateProcFiles);
  FRIEND_TEST(UserCollectorTest, ValidateCoreFile);

  std::string GetPattern(bool enabled, bool early) const;
  bool SetUpInternal(bool enabled, bool early);

  bool CopyOffProcFiles(pid_t pid, const base::FilePath& container_dir);

  // Validates the proc files at |container_dir| and returns true if they
  // are usable for the core-to-minidump conversion later. For instance, if
  // a process is reaped by the kernel before the copying of its proc files
  // takes place, some proc files like /proc/<pid>/maps may contain nothing
  // and thus become unusable.
  bool ValidateProcFiles(const base::FilePath& container_dir) const;

  // Validates the core file at |core_path| and returns kErrorNone if
  // the file contains the ELF magic bytes and an ELF class that matches the
  // platform (i.e. 32-bit ELF on a 32-bit platform or 64-bit ELF on a 64-bit
  // platform), which is due to the limitation in core2md. It returns an error
  // type otherwise.
  ErrorType ValidateCoreFile(const base::FilePath& core_path) const;
  bool CopyStdinToCoreFile(const base::FilePath& core_path);
  bool RunCoreToMinidump(const base::FilePath& core_path,
                         const base::FilePath& procfs_directory,
                         const base::FilePath& minidump_path,
                         const base::FilePath& temp_directory);

  bool RunFilter(pid_t pid);

  bool ShouldDump(pid_t pid,
                  bool handle_chrome_crashes,
                  const std::string& exec,
                  std::string* reason);

  // UserCollectorBase overrides.
  bool ShouldDump(pid_t pid,
                  uid_t uid,
                  const std::string& exec,
                  std::string* reason) override;
  ErrorType ConvertCoreToMinidump(pid_t pid,
                                  const base::FilePath& container_dir,
                                  const base::FilePath& core_path,
                                  const base::FilePath& minidump_path) override;

  std::string core_pattern_file_;
  std::string core_pipe_limit_file_;
  std::string our_path_;
  std::string filter_path_;

  // Force a core2md failure for testing.
  bool core2md_failure_;
};

#endif  // CRASH_REPORTER_USER_COLLECTOR_H_
