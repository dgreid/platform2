// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <signal.h>

#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/process/process_mock.h>
#include <brillo/test_helpers.h>
#include <gtest/gtest.h>

#include "vpn-manager/daemon.h"

using ::base::FilePath;
using ::base::NumberToString;
using ::brillo::Process;
using ::brillo::ProcessImpl;
using ::brillo::ProcessMock;
using ::std::string;
using ::testing::_;
using ::testing::InvokeWithoutArgs;
using ::testing::Mock;
using ::testing::Return;

namespace vpn_manager {

namespace {
const char kBinSleep[] = "/bin/sleep";
}  // namespace

class DaemonTest : public ::testing::Test {
 public:
  void SetUp() override {
    FilePath cwd;
    CHECK(temp_dir_.CreateUniqueTempDir());
    FilePath test_path = temp_dir_.GetPath().Append("daemon_testdir");
    base::DeletePathRecursively(test_path);
    base::CreateDirectory(test_path);
    pid_file_path_ = test_path.Append("process.pid");
    daemon_.reset(new Daemon(pid_file_path_.value()));
  }

  void TearDown() override {
    if (real_process_)
      KillRealProcess();
  }

  // Needs to be public so we can use the testing::Invoke() family of functions.
  bool KillRealProcess() { return real_process_->Kill(SIGTERM, 5); }

 protected:
  void WritePidFile(const string& pid) {
    if (base::WriteFile(pid_file_path_, pid.c_str(), pid.size()) < 0) {
      LOG(ERROR) << "Unable to create " << pid_file_path_.value();
    }
  }

  void MakeRealProcess() {
    real_process_.reset(new ProcessImpl);
    real_process_->AddArg(kBinSleep);
    real_process_->AddArg("12345");
    CHECK(real_process_->Start());
  }

  const string& GetPidFile() { return daemon_->pid_file_; }

  Process* GetProcess() { return daemon_->process_.get(); }

  void SetProcess(std::unique_ptr<Process> process) {
    daemon_->SetProcess(std::move(process));
  }

  FilePath pid_file_path_;
  std::unique_ptr<Daemon> daemon_;
  std::unique_ptr<Process> real_process_;
  base::ScopedTempDir temp_dir_;
};

TEST_F(DaemonTest, Construction) {
  EXPECT_EQ(nullptr, GetProcess());
  EXPECT_EQ(pid_file_path_.value(), GetPidFile());
  EXPECT_FALSE(daemon_->IsRunning());
}

TEST_F(DaemonTest, FindProcess) {
  EXPECT_FALSE(daemon_->FindProcess());
  EXPECT_FALSE(daemon_->IsRunning());

  // Start a real process and note its pid, then kill it so we know we have
  // a non-running pid.
  MakeRealProcess();
  pid_t pid = real_process_->pid();
  KillRealProcess();

  WritePidFile(NumberToString(pid));
  EXPECT_FALSE(daemon_->FindProcess());
  EXPECT_EQ(nullptr, GetProcess());

  MakeRealProcess();
  pid = real_process_->pid();

  WritePidFile(NumberToString(pid));
  EXPECT_TRUE(daemon_->FindProcess());
  EXPECT_TRUE(GetProcess());
  EXPECT_EQ(pid, GetProcess()->pid());
}

TEST_F(DaemonTest, IsRunningAndGetPid) {
  EXPECT_FALSE(daemon_->IsRunning());
  EXPECT_EQ(0, daemon_->GetPid());

  MakeRealProcess();
  pid_t pid = real_process_->pid();
  ASSERT_NE(0, pid);
  SetProcess(std::move(real_process_));
  EXPECT_TRUE(daemon_->IsRunning());
  EXPECT_EQ(pid, daemon_->GetPid());

  // Kill the process outside of the view of the process owned by the daemon.
  std::unique_ptr<Process> killed_process(new ProcessImpl);
  killed_process->Reset(pid);
  killed_process->Kill(SIGTERM, 5);
  EXPECT_FALSE(daemon_->IsRunning());
  EXPECT_EQ(pid, daemon_->GetPid());

  SetProcess(nullptr);
  EXPECT_EQ(0, daemon_->GetPid());
}

TEST_F(DaemonTest, SetProcessFromNull) {
  EXPECT_EQ(nullptr, GetProcess());
  SetProcess(nullptr);  // Should be a no-op.
  auto process = std::make_unique<ProcessMock>();
  ProcessMock* process_ptr = process.get();
  SetProcess(std::move(process));
  EXPECT_EQ(process_ptr, GetProcess());
  // Called during destructor.
  EXPECT_CALL(*process_ptr, pid()).WillOnce(Return(0));
}

TEST_F(DaemonTest, SetProcessToNullFromNotRunning) {
  auto process = std::make_unique<ProcessMock>();
  EXPECT_CALL(*process, Release()).Times(0);
  EXPECT_CALL(*process, pid()).WillOnce(Return(0));
  SetProcess(std::move(process));
  SetProcess(nullptr);
  EXPECT_EQ(nullptr, GetProcess());
}

TEST_F(DaemonTest, SetProcessToNullFromRunning) {
  MakeRealProcess();
  auto process = std::make_unique<ProcessMock>();
  EXPECT_CALL(*process, Release()).Times(0);
  EXPECT_CALL(*process, pid()).WillRepeatedly(Return(real_process_->pid()));
  EXPECT_CALL(*process, Kill(SIGKILL, _)).Times(1);
  SetProcess(std::move(process));
  SetProcess(nullptr);
  EXPECT_EQ(nullptr, GetProcess());
}

TEST_F(DaemonTest, SetProcessToDifferentPid) {
  MakeRealProcess();
  auto process0 = std::make_unique<ProcessMock>();
  EXPECT_CALL(*process0, Release()).Times(0);
  EXPECT_CALL(*process0, pid()).WillRepeatedly(Return(real_process_->pid()));
  EXPECT_CALL(*process0, Kill(SIGKILL, _)).Times(1);
  auto process1 = std::make_unique<ProcessMock>();
  ProcessMock* process1_ptr = process1.get();
  EXPECT_CALL(*process1, Release()).Times(0);
  EXPECT_CALL(*process1, pid()).WillOnce(Return(2));
  SetProcess(std::move(process0));
  SetProcess(std::move(process1));
  EXPECT_EQ(process1_ptr, GetProcess());
  // Verify expectations now so we don't trigger on calls during the destructor.
  Mock::VerifyAndClearExpectations(process1_ptr);
  EXPECT_CALL(*process1_ptr, pid()).WillOnce(Return(0));
}

TEST_F(DaemonTest, SetProcessToSamePid) {
  auto process0 = std::make_unique<ProcessMock>();
  EXPECT_CALL(*process0, Release()).Times(1);
  EXPECT_CALL(*process0, pid()).WillOnce(Return(1));
  auto process1 = std::make_unique<ProcessMock>();
  ProcessMock* process1_ptr = process1.get();
  EXPECT_CALL(*process1, Release()).Times(0);
  EXPECT_CALL(*process1, pid()).WillOnce(Return(1));
  SetProcess(std::move(process0));
  SetProcess(std::move(process1));
  EXPECT_EQ(process1_ptr, GetProcess());
  // Reset expectations now so we don't trigger on calls during the destructor.
  Mock::VerifyAndClearExpectations(process1_ptr);
  EXPECT_CALL(*process1_ptr, pid()).WillOnce(Return(0));
}

TEST_F(DaemonTest, TerminateNoProcess) {
  WritePidFile("");
  ASSERT_TRUE(base::PathExists(pid_file_path_));
  EXPECT_TRUE(daemon_->Terminate());
  EXPECT_FALSE(base::PathExists(pid_file_path_));
}

TEST_F(DaemonTest, TerminateDeadProcess) {
  auto process = std::make_unique<ProcessMock>();
  EXPECT_CALL(*process, pid()).Times(2).WillRepeatedly(Return(0));
  EXPECT_CALL(*process, Kill(SIGTERM, _)).Times(0);
  SetProcess(std::move(process));
  WritePidFile("");
  ASSERT_TRUE(base::PathExists(pid_file_path_));
  EXPECT_TRUE(daemon_->Terminate());
  EXPECT_FALSE(base::PathExists(pid_file_path_));
}

TEST_F(DaemonTest, TerminateLiveProcess) {
  MakeRealProcess();
  auto process = std::make_unique<ProcessMock>();
  EXPECT_CALL(*process, pid()).WillRepeatedly(Return(real_process_->pid()));
  EXPECT_CALL(*process, Kill(SIGTERM, _))
      .WillOnce(InvokeWithoutArgs(this, &DaemonTest::KillRealProcess));
  EXPECT_CALL(*process, Kill(SIGKILL, _)).Times(0);
  SetProcess(std::move(process));
  WritePidFile("");
  ASSERT_TRUE(base::PathExists(pid_file_path_));
  // Returns false since the daemon was unable to terminate the process.
  EXPECT_TRUE(daemon_->Terminate());
  EXPECT_FALSE(base::PathExists(pid_file_path_));
}

TEST_F(DaemonTest, Destructor) {
  // This doesn't directly unit-test the Daemon class, but it does illuminate
  // a side effect of the destruction of the underlying Process it holds.
  MakeRealProcess();
  auto process = std::make_unique<ProcessMock>();
  EXPECT_CALL(*process, pid()).WillRepeatedly(Return(real_process_->pid()));
  EXPECT_CALL(*process, Kill(SIGKILL, _)).Times(1);
  SetProcess(std::move(process));

  EXPECT_TRUE(daemon_->IsRunning());
  SetProcess(nullptr);
  daemon_.reset();
}

}  // namespace vpn_manager
