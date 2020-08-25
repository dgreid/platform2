// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/sandboxed_init.h"

#include <functional>

#include <stdlib.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace cros_disks {

namespace {

constexpr auto kTimeout = base::TimeDelta::FromSeconds(30);

int CallFunc(std::function<int()> func) {
  return func();
}

pid_t RunInFork(std::function<int()> func) {
  pid_t pid = fork();
  CHECK_NE(-1, pid);
  if (pid == 0) {
    CHECK_NE(-1, prctl(PR_SET_CHILD_SUBREAPER, 1));
    exit(func());
  }
  return pid;
}

class SandboxedInitTest : public testing::Test {
 public:
  SandboxedInitTest() = default;

  ~SandboxedInitTest() override = default;

 protected:
  void TearDown() override {
    if (pid_ > 0) {
      kill(pid_, SIGKILL);
    }
  }

  void RunUnderInit(std::function<int()> func) {
    SandboxedInit init(
        SubprocessPipe::Open(SubprocessPipe::kParentToChild, &in_),
        SubprocessPipe::Open(SubprocessPipe::kChildToParent, &out_),
        SubprocessPipe::Open(SubprocessPipe::kChildToParent, &err_),
        SubprocessPipe::Open(SubprocessPipe::kChildToParent, &ctrl_));
    pid_ = RunInFork([&init, func]() -> int {
      CHECK_EQ(signal(SIGUSR1, [](int sig) { CHECK_EQ(sig, SIGUSR1); }),
               SIG_DFL);
      init.RunInsideSandboxNoReturn(base::BindOnce(CallFunc, func));
      NOTREACHED();
    });
    CHECK(base::SetNonBlocking(ctrl_.get()));
  }

  bool Wait(int* status, bool no_hang) {
    CHECK_LT(0, pid_);
    int ret = waitpid(pid_, status, no_hang ? WNOHANG : 0);
    if (ret < 0) {
      PLOG(FATAL) << "waitpid failed.";
      return true;
    }
    if (ret == 0) {
      return false;
    }
    if (WIFEXITED(*status) || WIFSIGNALED(*status)) {
      pid_ = -1;
      return true;
    }
    return false;
  }

  bool Poll(const base::TimeDelta& timeout, std::function<bool()> func) {
    constexpr int64_t kUSleepDelay = 100000;
    auto counter = timeout.InMicroseconds() / kUSleepDelay;
    while (!func()) {
      if (counter-- <= 0) {
        return false;
      }
      usleep(kUSleepDelay);
    }
    return true;
  }

  bool PollForExitStatus(const base::TimeDelta& timeout, int* status) {
    return Poll(timeout, [status, this]() {
      return SandboxedInit::PollLauncherStatus(&ctrl_, status);
    });
  }

  bool PollWait(const base::TimeDelta& timeout, int* status) {
    return Poll(timeout, [status, this]() { return Wait(status, true); });
  }

  pid_t pid_ = -1;
  base::ScopedFD in_, out_, err_, ctrl_;
};

}  // namespace

TEST_F(SandboxedInitTest, BasicReturnCode) {
  pid_ = RunInFork([]() { return 42; });

  int status;
  ASSERT_TRUE(Wait(&status, false));
  ASSERT_EQ(42, WEXITSTATUS(status));
}

TEST_F(SandboxedInitTest, RunInitNoDaemon_WaitForTermination) {
  RunUnderInit([]() { return 12; });

  int status;
  ASSERT_TRUE(Wait(&status, false));
  ASSERT_EQ(12, WEXITSTATUS(status));
}

TEST_F(SandboxedInitTest, RunInitNoDaemon_Crash) {
  RunUnderInit([]() -> int { _exit(1); });

  int status;
  ASSERT_TRUE(Wait(&status, false));
  ASSERT_EQ(1, WEXITSTATUS(status));
}

TEST_F(SandboxedInitTest, RunInitNoDaemon_IO) {
  RunUnderInit([]() {
    EXPECT_EQ(4, write(STDOUT_FILENO, "abcd", 4));
    return 12;
  });

  char buffer[5] = {0};
  ssize_t rd = read(out_.get(), buffer, 4);
  EXPECT_EQ(4, rd);
  EXPECT_STREQ("abcd", buffer);

  int status;
  ASSERT_TRUE(Wait(&status, false));
  ASSERT_EQ(12, WEXITSTATUS(status));
}

TEST_F(SandboxedInitTest, RunInitNoDaemon_UndisturbedBySignal) {
  RunUnderInit([]() {
    // Signal that process started
    HANDLE_EINTR(write(STDOUT_FILENO, "Begin", 5));

    // Wait to be unblocked
    char buffer[PIPE_BUF];
    HANDLE_EINTR(read(STDIN_FILENO, buffer, PIPE_BUF));

    // Signal that process was unblocked
    HANDLE_EINTR(write(STDOUT_FILENO, "End", 3));
    return 12;
  });

  // Wait for process to start.
  char buffer[PIPE_BUF];
  ssize_t rd = read(out_.get(), buffer, PIPE_BUF);
  ASSERT_GT(rd, 0);
  EXPECT_EQ(base::StringPiece(buffer, rd), "Begin");

  // Send SIGUSR1 to init process.
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(kill(pid_, SIGUSR1), 0);
    usleep(100'000);
  }

  // Unblock the process.
  ASSERT_GT(write(in_.get(), "Continue", 8), 0);

  // Wait for process to continue.
  rd = read(out_.get(), buffer, PIPE_BUF);
  ASSERT_GT(rd, 0);
  EXPECT_EQ(base::StringPiece(buffer, rd), "End");

  // Wait for init process to finish.
  int status;
  ASSERT_TRUE(Wait(&status, false));
  ASSERT_EQ(WEXITSTATUS(status), 12);
}

TEST_F(SandboxedInitTest, RunInitNoDaemon_ReadLauncherCode) {
  RunUnderInit([]() { return 12; });

  ASSERT_TRUE(ctrl_.is_valid());
  int status;
  ASSERT_TRUE(PollForExitStatus(kTimeout, &status));
  ASSERT_FALSE(ctrl_.is_valid());
  EXPECT_EQ(12, status);

  ASSERT_TRUE(Wait(&status, false));
  ASSERT_EQ(12, WEXITSTATUS(status));
}

TEST_F(SandboxedInitTest, RunInitWithDaemon) {
  int comm[2];
  CHECK_NE(-1, pipe(comm));
  RunUnderInit([comm]() {
    if (daemon(0, 0) == -1) {
      PLOG(FATAL) << "Can't daemon";
    }
    char buffer[4] = {0};
    EXPECT_EQ(4, read(comm[0], buffer, 4));
    return 42;
  });

  int status;
  ASSERT_TRUE(PollForExitStatus(kTimeout, &status));
  EXPECT_EQ(0, status);

  EXPECT_FALSE(Wait(&status, true));

  // Tell the daemon to stop.
  EXPECT_EQ(4, write(comm[1], "die", 4));
  EXPECT_TRUE(Wait(&status, false));
  close(comm[0]);
  close(comm[1]);
  EXPECT_EQ(42, WEXITSTATUS(status));
}

TEST_F(SandboxedInitTest, RunInitNoDaemon_NonBlockingWait) {
  int comm[2];
  CHECK_NE(-1, pipe(comm));
  RunUnderInit([comm]() {
    char buffer[4] = {0};
    EXPECT_EQ(4, read(comm[0], buffer, 4));
    return 6;
  });

  int status;
  EXPECT_FALSE(Wait(&status, true));

  EXPECT_EQ(4, write(comm[1], "die", 4));
  EXPECT_TRUE(PollWait(kTimeout, &status));
  close(comm[0]);
  close(comm[1]);
  EXPECT_EQ(6, WEXITSTATUS(status));
}

TEST_F(SandboxedInitTest, RunInitWithDaemon_NonBlockingWait) {
  int comm[2];
  CHECK_NE(-1, pipe(comm));
  RunUnderInit([comm]() {
    if (daemon(0, 0) == -1) {
      PLOG(FATAL) << "Can't daemon";
    }
    sigset_t s = {};
    PCHECK(0 == sigemptyset(&s));
    PCHECK(0 == sigaddset(&s, SIGPIPE));
    PCHECK(0 == sigprocmask(SIG_BLOCK, &s, nullptr));
    char buffer[4] = {0};
    EXPECT_EQ(4, read(comm[0], buffer, 4));
    return 42;
  });

  int status;
  ASSERT_TRUE(PollForExitStatus(kTimeout, &status));
  EXPECT_EQ(0, status);

  EXPECT_FALSE(Wait(&status, true));

  // Tell the daemon to stop.
  EXPECT_EQ(4, write(comm[1], "die", 4));
  close(comm[0]);
  close(comm[1]);

  EXPECT_TRUE(PollWait(kTimeout, &status));
  EXPECT_EQ(42, WEXITSTATUS(status));
}

}  // namespace cros_disks
