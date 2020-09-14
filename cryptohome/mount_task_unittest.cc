// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for MountTask.

#include "cryptohome/mount_task.h"

#include <base/at_exit.h>
#include <base/atomic_sequence_num.h>
#include <base/bind.h>
#include <base/logging.h>
#include <base/synchronization/waitable_event.h>
#include <base/threading/platform_thread.h>
#include <base/threading/thread.h>
#include <base/time/time.h>
#include <gtest/gtest.h>

#include "cryptohome/mock_homedirs.h"
#include "cryptohome/mock_mount.h"

using base::PlatformThread;

namespace cryptohome {
using ::testing::_;
using ::testing::Return;

class MountTaskTest : public ::testing::Test {
 public:
  MountTaskTest()
      : runner_("RunnerThread"),
        event_(base::WaitableEvent::ResetPolicy::MANUAL,
               base::WaitableEvent::InitialState::NOT_SIGNALED),
        mount_(new MockMount),
        homedirs_(),
        result_() {
    wait_time_ = base::TimeDelta::FromSeconds(180);
  }

  virtual ~MountTaskTest() {}

  void SetUp() { runner_.Start(); }

  void TearDown() {
    if (runner_.IsRunning()) {
      runner_.Stop();
    }
  }

  // Return the next sequence number.
  int NextSequence() { return sequence_holder_.GetNext() + 1; }

 protected:
  base::Thread runner_;
  base::WaitableEvent event_;
  scoped_refptr<MockMount> mount_;
  MockHomeDirs homedirs_;
  MountTaskResult result_;
  base::TimeDelta wait_time_;

  // An atomic incrementing sequence for setting asynchronous call ids.
  base::AtomicSequenceNumber sequence_holder_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MountTaskTest);
};

class MountTaskNotifier : public MountTaskObserver {
 public:
  MountTaskNotifier() : notified_(false) {}
  virtual ~MountTaskNotifier() {}

  // MountTaskObserver
  virtual bool MountTaskObserve(const MountTaskResult& result) {
    notified_ = true;
    return false;
  }

  bool notified_;
};

TEST_F(MountTaskTest, ResultCopyConstructorTest) {
  MountTaskResult result1;

  result1.set_sequence_id(1337);
  result1.set_return_status(true);
  result1.set_return_code(MOUNT_ERROR_FATAL);

  MountTaskResult result2(result1);

  ASSERT_EQ(result1.sequence_id(), result2.sequence_id());
  ASSERT_EQ(result1.return_status(), result2.return_status());
  ASSERT_EQ(result1.return_code(), result2.return_code());
}

TEST_F(MountTaskTest, ResultEqualsTest) {
  MountTaskResult result1;

  result1.set_sequence_id(1337);
  result1.set_return_status(true);
  result1.set_return_code(MOUNT_ERROR_FATAL);

  MountTaskResult result2;
  result2 = result1;

  ASSERT_EQ(result1.sequence_id(), result2.sequence_id());
  ASSERT_EQ(result1.return_status(), result2.return_status());
  ASSERT_EQ(result1.return_code(), result2.return_code());
}

TEST_F(MountTaskTest, EventTest) {
  ASSERT_FALSE(event_.IsSignaled());
  scoped_refptr<MountTask> mount_task =
      new MountTask(NULL, NULL, NextSequence());
  mount_task->set_complete_event(&event_);
  mount_task->set_result(&result_);
  runner_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&MountTask::Run, mount_task.get()));
  event_.TimedWait(wait_time_);
  ASSERT_TRUE(event_.IsSignaled());
}

TEST_F(MountTaskTest, ObserveTest) {
  MountTaskNotifier notifier;
  scoped_refptr<MountTask> mount_task =
      new MountTask(&notifier, NULL, NextSequence());
  mount_task->set_result(&result_);
  runner_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&MountTask::Run, mount_task.get()));
  for (unsigned int i = 0; i < 64; i++) {
    if (!notifier.notified_) {
      PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(100));
    } else {
      break;
    }
  }
  ASSERT_TRUE(notifier.notified_);
}

}  // namespace cryptohome
