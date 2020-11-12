// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ArcvmNativeCollector handles crashes of native binaries in ARCVM. When the
// ARCVM kernel detects a crash, it executes arc-native-crash-dispatcher via its
// /proc/sys/kernel/core_pattern. arc-native-crash-dispatcher calls
// arc-native-crash-collector32 or arc-native-crash-collector64 and writes dump
// files at /data/vendor/arc_native_crash_reports in the ARCVM filesystem.
// ArcCrashCollector, which is a service running in ARCVM, monitors the
// /data/vendor/arc_native_crash_reports directory and sends detected dump files
// to Chrome via Mojo. Finally Chrome invokes this collector.

#ifndef CRASH_REPORTER_ARCVM_NATIVE_COLLECTOR_H_
#define CRASH_REPORTER_ARCVM_NATIVE_COLLECTOR_H_

#include "crash-reporter/arc_util.h"
#include "crash-reporter/crash_collector.h"

#include <string>

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>

// Collector for native crashes in ARCVM.
class ArcvmNativeCollector : public CrashCollector {
 public:
  // The basic information about crash. They are used for the filename of files
  // passed to crash_sender.
  struct CrashInfo {
    time_t time;            // The time when the crash happens.
    pid_t pid;              // The process ID (in ARCVM) of the crashed process.
    std::string exec_name;  // The name of crashed binary.
  };

  ArcvmNativeCollector();
  ~ArcvmNativeCollector() override;

  // Handles a native crash in ARCVM.
  bool HandleCrash(const arc_util::BuildProperty& build_property,
                   const CrashInfo& crash_info);

 private:
  friend class ArcvmNativeCollectorMock;
  friend class ArcvmNativeCollectorTest;
  FRIEND_TEST(ArcvmNativeCollectorTest, HandleCrashWithMinidumpFD);
  FRIEND_TEST(ArcvmNativeCollectorTest, AddArcMetadata);

  // Handles a native crash in ARCVM using the given FD for minidump.
  // TODO(kimiyuki): Replace |minidump_fd| with a path and make "/dev/stdin" the
  // default argument.
  bool HandleCrashWithMinidumpFD(const arc_util::BuildProperty& build_property,
                                 const CrashInfo& crash_info,
                                 base::ScopedFD minidump_fd);

  // Adds ARC-related metadata to the crash report.
  void AddArcMetadata(const arc_util::BuildProperty& build_property,
                      const CrashInfo& crash_info);

  // Reads the content from FD and writes it to the specified path.
  bool DumpFdToFile(base::ScopedFD fd, const base::FilePath& path);
};

#endif  // CRASH_REPORTER_ARCVM_NATIVE_COLLECTOR_H_
