// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The ARC collector reports crashes that happen in the ARC++ container (android
// on chrome os).
// If a process crashes (not just exits abnormally), the kernel invokes
// crash_reporter via /proc/sys/kernel/core_pattern, which in turn calls the ARC
// collector if the crash happened in that container namespace.

#ifndef CRASH_REPORTER_ARC_COLLECTOR_H_
#define CRASH_REPORTER_ARC_COLLECTOR_H_

#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>

#include <base/macros.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "crash-reporter/arc_util.h"
#include "crash-reporter/user_collector_base.h"

// Collector for system crashes in the ARC container.
class ArcCollector : public UserCollectorBase {
 public:
  struct Context {
    virtual ~Context() = default;

    virtual bool GetArcPid(pid_t* pid) const = 0;
    virtual bool GetPidNamespace(pid_t pid, std::string* ns) const = 0;
    virtual bool GetExeBaseName(pid_t pid, std::string* exe) const = 0;
    virtual bool GetCommand(pid_t pid, std::string* command) const = 0;
    virtual bool ReadAuxvForProcess(pid_t pid, std::string* contents) const = 0;
  };

  using ContextPtr = std::unique_ptr<Context>;

  ArcCollector();
  explicit ArcCollector(ContextPtr context);
  ~ArcCollector() override = default;

  const Context& context() const { return *context_; }

  // Returns false if the query failed, which may happen during teardown of the
  // ARC container. Since the behavior of user collectors is determined by
  // IsArcProcess, there is a (rare) race condition for crashes that occur
  // during teardown.
  bool IsArcProcess(pid_t pid) const;

  // Reads a Java crash log for the given |crash_type| from standard input, or
  // closes the stream if reporting is disabled.
  bool HandleJavaCrash(const std::string& crash_type,
                       const arc_util::BuildProperty& build_property);

  static bool IsArcRunning();
  static bool GetArcPid(pid_t* arc_pid);

 private:
  FRIEND_TEST(ArcCollectorTest, CorrectlyDetectBitness);
  FRIEND_TEST(ArcCollectorTest, GetExeBaseNameForUserCrash);
  FRIEND_TEST(ArcCollectorTest, GetExeBaseNameForArcCrash);
  FRIEND_TEST(ArcCollectorTest, ShouldDump);

  // Shift for UID namespace in ARC.
  static constexpr uid_t kUserShift = 655360;

  // Upper bound for system UIDs in ARC.
  static constexpr uid_t kSystemUserEnd = kUserShift + 10000;

  class ArcContext : public Context {
   public:
    explicit ArcContext(ArcCollector* collector) : collector_(collector) {}

    bool GetArcPid(pid_t* pid) const override;
    bool GetPidNamespace(pid_t pid, std::string* ns) const override;
    bool GetExeBaseName(pid_t pid, std::string* exe) const override;
    bool GetCommand(pid_t pid, std::string* command) const override;
    bool ReadAuxvForProcess(pid_t pid, std::string* contents) const override;

   private:
    ArcCollector* const collector_;
  };

  // CrashCollector overrides.
  std::string GetOsVersion() const override;
  bool GetExecutableBaseNameFromPid(pid_t pid, std::string* base_name) override;

  // UserCollectorBase overrides.
  bool ShouldDump(pid_t pid,
                  uid_t uid,
                  const std::string& exec,
                  std::string* reason) override;

  ErrorType ConvertCoreToMinidump(pid_t pid,
                                  const base::FilePath& container_dir,
                                  const base::FilePath& core_path,
                                  const base::FilePath& minidump_path) override;

  // Adds the |process|, |crash_type| and Chrome version as metadata. The
  // |add_arc_properties| option requires privilege to access the ARC root.
  void AddArcMetaData(const std::string& process,
                      const std::string& crash_type,
                      bool add_arc_properties);

  using CrashLogHeaderMap = std::unordered_map<std::string, std::string>;

  bool CreateReportForJavaCrash(const std::string& crash_type,
                                const arc_util::BuildProperty& build_property,
                                const CrashLogHeaderMap& map,
                                const std::string& exception_info,
                                const std::string& log,
                                bool* out_of_capacity);

  // Returns whether the process identified by |pid| is 32- or 64-bit.
  ErrorType Is64BitProcess(int pid, bool* is_64_bit) const;

  const ContextPtr context_;

  DISALLOW_COPY_AND_ASSIGN(ArcCollector);
};

#endif  // CRASH_REPORTER_ARC_COLLECTOR_H_
