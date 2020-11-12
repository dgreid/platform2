// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/arc_collector.h"

#include <memory>
#include <unordered_map>
#include <utility>

#include <brillo/syslog_logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using brillo::ClearLog;
using brillo::FindLog;
using brillo::GetLog;

namespace {

const char k32BitAuxv[] = R"(
20 00 00 00 20 ba 7a ef 21 00 00 00 00 b0 7a ef
10 00 00 00 ff fb eb bf 06 00 00 00 00 10 00 00
11 00 00 00 64 00 00 00 03 00 00 00 34 d0 bb 5e
04 00 00 00 20 00 00 00 05 00 00 00 09 00 00 00
07 00 00 00 00 d0 7a ef 08 00 00 00 00 00 00 00
09 00 00 00 4d e6 bb 5e 0b 00 00 00 00 00 00 00
0c 00 00 00 00 00 00 00 0d 00 00 00 00 00 00 00
0e 00 00 00 00 00 00 00 17 00 00 00 01 00 00 00
19 00 00 00 3b 52 c6 ff 1f 00 00 00 de 6f c6 ff
0f 00 00 00 4b 52 c6 ff 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
)";

const char k64BitAuxv[] = R"(
21 00 00 00 00 00 00 00 00 30 db e6 fe 7f 00 00
10 00 00 00 00 00 00 00 ff fb eb bf 00 00 00 00
06 00 00 00 00 00 00 00 00 10 00 00 00 00 00 00
11 00 00 00 00 00 00 00 64 00 00 00 00 00 00 00
03 00 00 00 00 00 00 00 40 c0 a6 54 a5 5d 00 00
04 00 00 00 00 00 00 00 38 00 00 00 00 00 00 00
05 00 00 00 00 00 00 00 09 00 00 00 00 00 00 00
07 00 00 00 00 00 00 00 00 10 3c 97 9c 7a 00 00
08 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
09 00 00 00 00 00 00 00 c8 de a6 54 a5 5d 00 00
0b 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0c 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0d 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0e 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
17 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00
19 00 00 00 00 00 00 00 39 bc da e6 fe 7f 00 00
1f 00 00 00 00 00 00 00 de cf da e6 fe 7f 00 00
0f 00 00 00 00 00 00 00 49 bc da e6 fe 7f 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
)";

}  // namespace

class MockArcCollector : public ArcCollector {
 public:
  using ArcCollector::ArcCollector;
  MOCK_METHOD(void, SetUpDBus, (), (override));
};

class Test : public ::testing::Test {
 protected:
  void Initialize() {
    EXPECT_CALL(*collector_, SetUpDBus()).WillRepeatedly(testing::Return());

    collector_->Initialize(false, false);
    ClearLog();
  }

  std::unique_ptr<MockArcCollector> collector_;
};

class ArcCollectorTest : public Test {
 protected:
  class MockContext : public ArcCollector::Context {
   public:
    void SetArcPid(pid_t pid) { arc_pid_ = pid; }
    void AddProcess(pid_t pid,
                    const char* ns,
                    const char* exe,
                    const char* cmd,
                    const char* auxv) {
      DCHECK_EQ(processes_.count(pid), 0u);
      DCHECK(ns);
      DCHECK(exe);
      auto& process = processes_[pid];
      process.ns = ns;
      process.exe = exe;
      process.cmd = cmd;
      process.auxv = auxv;
    }

   private:
    struct Process {
      const char* ns;
      const char* exe;
      const char* cmd;
      const char* auxv;
    };

    bool GetArcPid(pid_t* pid) const override {
      if (arc_pid_ == 0)
        return false;
      *pid = arc_pid_;
      return true;
    }
    bool GetPidNamespace(pid_t pid, std::string* ns) const override {
      const auto it = processes_.find(pid);
      if (it == processes_.end())
        return false;
      ns->assign(it->second.ns);
      return true;
    }
    bool GetExeBaseName(pid_t pid, std::string* exe) const override {
      const auto it = processes_.find(pid);
      if (it == processes_.end())
        return false;
      exe->assign(it->second.exe);
      return true;
    }
    bool GetCommand(pid_t pid, std::string* command) const override {
      const auto it = processes_.find(pid);
      if (it == processes_.end())
        return false;
      const auto cmd = it->second.cmd;
      if (!cmd)
        return false;
      command->assign(cmd);
      return true;
    }
    bool ReadAuxvForProcess(pid_t pid, std::string* contents) const override {
      const auto it = processes_.find(pid);
      if (it == processes_.end())
        return false;
      const auto* auxv = it->second.auxv;
      if (!auxv)
        return false;
      std::istringstream ss(auxv);
      contents->clear();
      uint32_t byte;
      ss >> std::hex;
      while (ss >> byte) {
        contents->push_back(byte);
      }
      return true;
    }

    pid_t arc_pid_ = 0;
    std::unordered_map<pid_t, Process> processes_;
  };

  MockContext* context_;  // Owned by collector.

 private:
  void SetUp() override {
    context_ = new MockContext;
    collector_.reset(new MockArcCollector(ArcCollector::ContextPtr(context_)));
    Initialize();
  }
};

class ArcContextTest : public Test {
 protected:
  pid_t pid_;

 private:
  void SetUp() override {
    collector_.reset(new MockArcCollector);
    Initialize();
    pid_ = getpid();
  }
};

TEST_F(ArcCollectorTest, IsArcProcess) {
  EXPECT_FALSE(collector_->IsArcProcess(123));
  EXPECT_TRUE(FindLog("Failed to get PID of ARC container"));
  ClearLog();

  context_->SetArcPid(100);

  EXPECT_FALSE(collector_->IsArcProcess(123));
  EXPECT_TRUE(FindLog("Failed to get PID namespace of ARC container"));
  ClearLog();

  context_->AddProcess(100, "arc", "init", "/sbin/init", k32BitAuxv);

  EXPECT_FALSE(collector_->IsArcProcess(123));
  EXPECT_TRUE(FindLog("Failed to get PID namespace of process"));
  ClearLog();

  context_->AddProcess(50, "cros", "chrome", "/opt/google/chrome/chrome",
                       k32BitAuxv);
  context_->AddProcess(123, "arc", "arc_service", "/sbin/arc_service",
                       k32BitAuxv);

  EXPECT_TRUE(collector_->IsArcProcess(123));
  EXPECT_TRUE(GetLog().empty());

  EXPECT_FALSE(collector_->IsArcProcess(50));
  EXPECT_TRUE(GetLog().empty());
}

TEST_F(ArcCollectorTest, GetExeBaseNameForUserCrash) {
  context_->SetArcPid(100);
  context_->AddProcess(100, "arc", "init", "/sbin/init", k32BitAuxv);
  context_->AddProcess(50, "cros", "chrome", "/opt/google/chrome/chrome",
                       k32BitAuxv);

  std::string exe;
  EXPECT_TRUE(collector_->GetExecutableBaseNameFromPid(50, &exe));
  EXPECT_EQ("chrome", exe);
}

TEST_F(ArcCollectorTest, GetExeBaseNameForArcCrash) {
  context_->SetArcPid(100);
  context_->AddProcess(100, "arc", "init", "/sbin/init", k32BitAuxv);
  context_->AddProcess(123, "arc", "arc_service", "/sbin/arc_service",
                       k32BitAuxv);
  context_->AddProcess(456, "arc", "app_process32", nullptr, k32BitAuxv);
  context_->AddProcess(789, "arc", "app_process32", "com.arc.app", k32BitAuxv);

  std::string exe;

  EXPECT_TRUE(collector_->GetExecutableBaseNameFromPid(123, &exe));
  EXPECT_EQ("arc_service", exe);

  EXPECT_TRUE(collector_->GetExecutableBaseNameFromPid(456, &exe));
  EXPECT_TRUE(FindLog("Failed to get package name"));
  EXPECT_EQ("app_process32", exe);

  EXPECT_TRUE(collector_->GetExecutableBaseNameFromPid(789, &exe));
  EXPECT_EQ("com.arc.app", exe);
}

TEST_F(ArcCollectorTest, ShouldDump) {
  context_->SetArcPid(100);
  context_->AddProcess(50, "cros", "chrome", "/opt/google/chrome/chrome",
                       k32BitAuxv);
  context_->AddProcess(100, "arc", "init", "/sbin/init", k32BitAuxv);
  context_->AddProcess(123, "arc", "arc_service", "/sbin/arc_service",
                       k32BitAuxv);
  context_->AddProcess(789, "arc", "app_process32", "com.arc.app", k32BitAuxv);

  std::string reason;
  EXPECT_FALSE(collector_->ShouldDump(50, 1234, "chrome", &reason));
  EXPECT_EQ("ignoring - crash origin is not ARC", reason);

  EXPECT_TRUE(collector_->ShouldDump(123, 0, "arc_service", &reason));
  EXPECT_EQ("handling", reason);

  EXPECT_FALSE(collector_->ShouldDump(123, ArcCollector::kSystemUserEnd,
                                      "com.arc.app", &reason));
  EXPECT_EQ("ignoring - not a system process", reason);
}

TEST_F(ArcCollectorTest, CorrectlyDetectBitness) {
  bool is_64_bit;

  context_->AddProcess(100, "arc", "app_process64", "zygote64", k64BitAuxv);
  EXPECT_EQ(ArcCollector::kErrorNone,
            collector_->Is64BitProcess(100, &is_64_bit));
  EXPECT_TRUE(is_64_bit);

  context_->AddProcess(101, "arc", "app_process32", "zygote32", k32BitAuxv);
  EXPECT_EQ(ArcCollector::kErrorNone,
            collector_->Is64BitProcess(101, &is_64_bit));
  EXPECT_FALSE(is_64_bit);
}

TEST_F(ArcContextTest, GetArcPid) {
  EXPECT_FALSE(ArcCollector::IsArcRunning());

  pid_t pid;
  EXPECT_FALSE(collector_->context().GetArcPid(&pid));
}

TEST_F(ArcContextTest, GetPidNamespace) {
  std::string ns;
  EXPECT_TRUE(collector_->context().GetPidNamespace(pid_, &ns));
  EXPECT_THAT(ns, testing::MatchesRegex("^pid:\\[[0-9]+\\]$"));
}

TEST_F(ArcContextTest, GetExeBaseName) {
  std::string exe;
  EXPECT_TRUE(collector_->context().GetExeBaseName(pid_, &exe));
  EXPECT_EQ("crash_reporter_test", exe);
}

// TODO(crbug.com/590044)
TEST_F(ArcContextTest, DISABLED_GetCommand) {
  std::string command;
  EXPECT_TRUE(collector_->context().GetCommand(pid_, &command));

  // TODO(domlaskowski): QEMU mishandles emulation of /proc/self/cmdline,
  // prepending QEMU flags to the command line of the emulated program.
  // Keep in sync with qargv[1] in qemu-binfmt-wrapper.c for now.
  EXPECT_EQ("-0", command);
}
