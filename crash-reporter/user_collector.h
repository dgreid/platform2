// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
  typedef std::function<bool (pid_t)> FilterOutFunction;

  UserCollector();

  // Initialize the user crash collector for detection of crashes,
  // given a crash counting function, the path to this executable,
  // metrics collection enabled oracle, and system logger facility.
  // Crash detection/reporting is not enabled until Enable is called.
  // |generate_diagnostics| is used to indicate whether or not to try
  // to generate a minidump from crashes.
  void Initialize(CountCrashFunction count_crash,
                  const std::string &our_path,
                  IsFeedbackAllowedFunction is_metrics_allowed,
                  bool generate_diagnostics,
                  bool core2md_failure,
                  bool directory_failure,
                  const std::string &filter_in,
                  FilterOutFunction filter_out);

  ~UserCollector() override;

  // Enable collection.
  bool Enable() { return SetUpInternal(true); }

  // Disable collection.
  bool Disable() { return SetUpInternal(false); }

  // Set (override the default) core file pattern.
  void set_core_pattern_file(const std::string &pattern) {
    core_pattern_file_ = pattern;
  }

  // Set (override the default) core pipe limit file.
  void set_core_pipe_limit_file(const std::string &path) {
    core_pipe_limit_file_ = path;
  }

  void set_filter_path(const std::string &filter_path) {
    filter_path_ = filter_path;
  }

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
  FRIEND_TEST(UserCollectorTest, GetSymlinkTarget);
  FRIEND_TEST(UserCollectorTest, GetUserInfoFromName);
  FRIEND_TEST(UserCollectorTest, ParseCrashAttributes);
  FRIEND_TEST(UserCollectorTest, ShouldDumpFiltering);
  FRIEND_TEST(UserCollectorTest, ShouldDumpChromeOverridesDeveloperImage);
  FRIEND_TEST(UserCollectorTest, ShouldDumpDeveloperImageOverridesConsent);
  FRIEND_TEST(UserCollectorTest, ShouldDumpUserConsentProductionImage);
  FRIEND_TEST(UserCollectorTest, ValidateProcFiles);
  FRIEND_TEST(UserCollectorTest, ValidateCoreFile);

  std::string GetPattern(bool enabled) const;
  bool SetUpInternal(bool enabled);

  bool CopyOffProcFiles(pid_t pid, const base::FilePath &container_dir);

  // Validates the proc files at |container_dir| and returns true if they
  // are usable for the core-to-minidump conversion later. For instance, if
  // a process is reaped by the kernel before the copying of its proc files
  // takes place, some proc files like /proc/<pid>/maps may contain nothing
  // and thus become unusable.
  bool ValidateProcFiles(const base::FilePath &container_dir) const;

  // Validates the core file at |core_path| and returns kErrorNone if
  // the file contains the ELF magic bytes and an ELF class that matches the
  // platform (i.e. 32-bit ELF on a 32-bit platform or 64-bit ELF on a 64-bit
  // platform), which is due to the limitation in core2md. It returns an error
  // type otherwise.
  ErrorType ValidateCoreFile(const base::FilePath &core_path) const;
  bool CopyStdinToCoreFile(const base::FilePath &core_path);
  bool RunCoreToMinidump(const base::FilePath &core_path,
                         const base::FilePath &procfs_directory,
                         const base::FilePath &minidump_path,
                         const base::FilePath &temp_directory);

  bool RunFilter(pid_t pid);

  bool ShouldDump(pid_t pid,
                  bool has_owner_consent,
                  bool is_developer,
                  bool handle_chrome_crashes,
                  const std::string &exec,
                  std::string *reason);

  // UserCollectorBase overrides.
  bool ShouldDump(pid_t pid,
                  uid_t uid,
                  const std::string &exec,
                  std::string *reason) override;

  ErrorType ConvertCoreToMinidump(pid_t pid,
                                  const base::FilePath &container_dir,
                                  const base::FilePath &core_path,
                                  const base::FilePath &minidump_path) override;

  std::string core_pattern_file_;
  std::string core_pipe_limit_file_;
  std::string our_path_;
  std::string filter_path_;

  bool core2md_failure_;

  FilterOutFunction filter_out_;

  DISALLOW_COPY_AND_ASSIGN(UserCollector);
};

#endif  // CRASH_REPORTER_USER_COLLECTOR_H_
