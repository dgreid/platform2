// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_ptr.h>
#include <base/strings/string_util.h>
#include <brillo/message_loops/fake_message_loop.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "login_manager/browser_job.h"
#include "login_manager/fake_browser_job.h"
#include "login_manager/fake_child_process.h"
#include "login_manager/fake_generator_job.h"
#include "login_manager/mock_device_policy_service.h"
#include "login_manager/mock_file_checker.h"
#include "login_manager/mock_liveness_checker.h"
#include "login_manager/mock_metrics.h"
#include "login_manager/mock_object_proxy.h"
#include "login_manager/mock_session_manager.h"
#include "login_manager/mock_system_utils.h"
#include "login_manager/system_utils_impl.h"
#include "power_manager/proto_bindings/suspend.pb.h"

using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::Return;
using ::testing::Sequence;
using ::testing::_;


namespace login_manager {

// Used as a fixture for the tests in this file.
// Gives useful shared functionality.
class SessionManagerProcessTest : public ::testing::Test {
 public:
  SessionManagerProcessTest()
      : manager_(NULL),
        liveness_checker_(new MockLivenessChecker),
        session_manager_impl_(new MockSessionManager),
        must_destroy_mocks_(true) {
  }

  ~SessionManagerProcessTest() override {
    if (must_destroy_mocks_) {
      delete liveness_checker_;
      delete session_manager_impl_;
    }
  }

  void SetUp() override {
    fake_loop_.SetAsCurrent();
    ASSERT_TRUE(tmpdir_.CreateUniqueTempDir());
  }

  void TearDown() override {
    must_destroy_mocks_ = !manager_.get();
    manager_ = NULL;
  }

 protected:
  // kFakeEmail is NOT const so that it can be passed to methods that
  // implement dbus calls, which (of necessity) take bare gchar*.
  static char kFakeEmail[];
  static const pid_t kDummyPid;
  static const int kExit;

  void MockUtils() {
    manager_->test_api().set_systemutils(&utils_);
  }

  void ExpectShutdown() {
    EXPECT_CALL(*session_manager_impl_, AnnounceSessionStoppingIfNeeded())
        .Times(1);
    EXPECT_CALL(*session_manager_impl_, AnnounceSessionStopped())
        .Times(1);
  }

  void ExpectLivenessChecking() {
    EXPECT_CALL(*liveness_checker_, Start()).Times(AtLeast(1));
    EXPECT_CALL(*liveness_checker_, Stop()).Times(AtLeast(1));
  }

  void ExpectOneJobReRun(FakeBrowserJob* job, int exit_status) {
    EXPECT_CALL(*job, KillEverything(SIGKILL, _)).Times(AnyNumber());
    EXPECT_CALL(*session_manager_impl_, ShouldEndSession())
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*job, ShouldStop())
        .WillOnce(Return(false))
        .WillOnce(Return(true));

    job->set_fake_child_process(scoped_ptr<FakeChildProcess>(
        new FakeChildProcess(kDummyPid, exit_status, manager_->test_api())));
  }

  void InitManager(FakeBrowserJob* job) {
    manager_ = new SessionManagerService(scoped_ptr<BrowserJobInterface>(job),
                                         getuid(),
                                         3,
                                         false,
                                         base::TimeDelta(),
                                         &metrics_,
                                         &real_utils_);
    manager_->test_api().set_liveness_checker(liveness_checker_);
    manager_->test_api().set_session_manager(session_manager_impl_);
  }

  void SimpleRunManager() {
    ExpectShutdown();
    manager_->RunBrowser();
    fake_loop_.Run();
  }

  void ForceRunLoop() {
    fake_loop_.Run();
  }

  FakeBrowserJob* CreateMockJobAndInitManager(bool schedule_exit) {
    FakeBrowserJob* job = new FakeBrowserJob("FakeBrowserJob", schedule_exit);
    InitManager(job);

    job->set_fake_child_process(
        scoped_ptr<FakeChildProcess>(
            new FakeChildProcess(kDummyPid, 0, manager_->test_api())));

    return job;
  }

  int PackStatus(int status) { return __W_EXITCODE(status, 0); }
  int PackSignal(int signal) { return __W_EXITCODE(0, signal); }

  scoped_refptr<SessionManagerService> manager_;
  SystemUtilsImpl real_utils_;
  MockMetrics metrics_;
  MockSystemUtils utils_;

  // These are bare pointers, not scoped_ptrs, because we need to give them
  // to a SessionManagerService instance, but also be able to set expectations
  // on them after we hand them off.
  MockLivenessChecker* liveness_checker_;
  MockSessionManager* session_manager_impl_;

 private:
  bool must_destroy_mocks_;
  base::ScopedTempDir tmpdir_;
  brillo::FakeMessageLoop fake_loop_{nullptr};

  DISALLOW_COPY_AND_ASSIGN(SessionManagerProcessTest);
};

// static
char SessionManagerProcessTest::kFakeEmail[] = "cmasone@whaaat.org";
const pid_t SessionManagerProcessTest::kDummyPid = 4;
const int SessionManagerProcessTest::kExit = 1;

class HandleSuspendReadinessMethodMatcher
    : public ::testing::MatcherInterface<dbus::MethodCall*> {
 public:
  HandleSuspendReadinessMethodMatcher(int delay_id, int suspend_id)
      : delay_id_(delay_id),
        suspend_id_(suspend_id) {}

  virtual bool MatchAndExplain(dbus::MethodCall* method_call,
                               ::testing::MatchResultListener* listener) const {
    // Make sure we've got the right kind of method call.
    if (method_call->GetInterface() !=
        power_manager::kPowerManagerInterface) {
      *listener << "interface was " << method_call->GetInterface();
      return false;
    }

    if (method_call->GetMember() !=
        power_manager::kHandleSuspendReadinessMethod) {
      *listener << "method name was " << method_call->GetMember();
      return false;
    }

    // Check proto for correctness.
    power_manager::SuspendReadinessInfo info;
    dbus::MessageReader reader(method_call);
    reader.PopArrayOfBytesAsProto(&info);
    if (info.delay_id() != delay_id_) {
      *listener << "delay ID was " << info.delay_id();
      return false;
    }
    if (info.suspend_id() != suspend_id_) {
      *listener << "suspend ID was " << info.suspend_id();
      return false;
    }

    return true;
  }

  virtual void DescribeTo(::std::ostream* os) const {
    *os << "HandleSuspendReadiness method call with delay ID "
        << delay_id_ << " and suspend ID " << suspend_id_;
  }

  virtual void DescribeNegationTo(::std::ostream* os) const {
    *os << "non-HandleSuspendReadiness method call, or method call "
        << "not with delay ID " << delay_id_ << " and suspend ID "
        << suspend_id_;
  }
 private:
  const int delay_id_;
  const int suspend_id_;
};

inline testing::Matcher<dbus::MethodCall*> HandleSuspendReadinessMethod(
    int delay_id,
    int suspend_id) {
  return MakeMatcher(
      new HandleSuspendReadinessMethodMatcher(delay_id, suspend_id));
}

// Browser processes get correctly terminated.
TEST_F(SessionManagerProcessTest, CleanupBrowser) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(false);
  EXPECT_CALL(*job, Kill(SIGTERM, _)).Times(1);
  EXPECT_CALL(*job, WaitAndAbort(_)).Times(1);
  job->RunInBackground();
  manager_->test_api().CleanupChildren(3);
}

// Gracefully shut down while the browser is running.
TEST_F(SessionManagerProcessTest, BrowserRunningShutdown) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(false);

  ExpectLivenessChecking();
  ExpectShutdown();

  // Expect the job to be killed.
  EXPECT_CALL(*job, Kill(SIGTERM, _)).Times(1);
  EXPECT_CALL(*job, WaitAndAbort(_)).Times(1);

  brillo::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&SessionManagerService::RunBrowser,
                 manager_.get()));

  brillo::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&SessionManagerService::ScheduleShutdown,
                 manager_.get()));

  ForceRunLoop();
}

// If the browser exits and asks to stop, the session manager
// should not restart it.
TEST_F(SessionManagerProcessTest, ChildExitFlagFileStop) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  manager_->test_api().set_exit_on_child_done(true);  // or it'll run forever.
  ExpectLivenessChecking();

  EXPECT_CALL(*job, KillEverything(SIGKILL, _)).Times(AnyNumber());
  EXPECT_CALL(*job, ShouldStop()).WillOnce(Return(false));
  job->set_should_run(false);

  EXPECT_CALL(*session_manager_impl_, ShouldEndSession())
      .WillOnce(Return(false));

  SimpleRunManager();
}

// A child that exits with a signal should get re-run.
TEST_F(SessionManagerProcessTest, BadExitChildOnSignal) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  ExpectLivenessChecking();
  ExpectOneJobReRun(job, PackSignal(SIGILL));
  SimpleRunManager();
}

// A child that exits badly should get re-run.
TEST_F(SessionManagerProcessTest, BadExitChild) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  ExpectLivenessChecking();
  ExpectOneJobReRun(job, PackSignal(kExit));
  SimpleRunManager();
}

// A child that exits cleanly should get re-run.
TEST_F(SessionManagerProcessTest, CleanExitChild) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  ExpectLivenessChecking();
  ExpectOneJobReRun(job, PackSignal(0));
  SimpleRunManager();
}

// If the browser exits while the screen is locked, the session manager
// should exit.
TEST_F(SessionManagerProcessTest, LockedExit) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  ExpectLivenessChecking();

  EXPECT_CALL(*job, KillEverything(SIGKILL, _)).Times(AnyNumber());
  EXPECT_CALL(*job, ShouldStop()).Times(0);

  EXPECT_CALL(*session_manager_impl_, ShouldEndSession())
      .WillOnce(Return(true));

  SimpleRunManager();
}

// Liveness checking should be started and stopped along with the browser.
TEST_F(SessionManagerProcessTest, LivenessCheckingStartStop) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  {
    Sequence start_stop;
    EXPECT_CALL(*liveness_checker_, Start()).Times(2);
    EXPECT_CALL(*liveness_checker_, Stop()).Times(AtLeast(1));
  }
  ExpectOneJobReRun(job, PackSignal(0));
  SimpleRunManager();
}

// If the child indicates it should be stopped, the session manager must honor.
TEST_F(SessionManagerProcessTest, MustStopChild) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  ExpectLivenessChecking();
  EXPECT_CALL(*job, KillEverything(SIGKILL, _)).Times(AnyNumber());
  EXPECT_CALL(*job, ShouldStop()).WillOnce(Return(true));
  EXPECT_CALL(*session_manager_impl_, ShouldEndSession())
      .WillRepeatedly(Return(false));

  SimpleRunManager();
}

TEST_F(SessionManagerProcessTest, TestWipeOnBadState) {
  CreateMockJobAndInitManager(true);

  EXPECT_CALL(*session_manager_impl_, Initialize()).WillOnce(Return(false));

  // Expect Powerwash to be triggered.
  EXPECT_CALL(*session_manager_impl_, InitiateDeviceWipe(_)).Times(1);
  EXPECT_CALL(*session_manager_impl_, Finalize()).Times(1);

  ASSERT_FALSE(manager_->test_api().InitializeImpl());
  ASSERT_EQ(SessionManagerService::MUST_WIPE_DEVICE, manager_->exit_code());
}

TEST_F(SessionManagerProcessTest, SuspendAndResumeArcInstance) {
  CreateMockJobAndInitManager(true);

  const int kSuspendDelayId = 1000;
  const int kSuspendId = 2000;
  scoped_refptr<MockObjectProxy> powerd_object_proxy(new MockObjectProxy);
  std::string cgroup_state;

  manager_->test_api().set_powerd_object_proxy(powerd_object_proxy.get());
  base::FilePath temp_file_path;
  CHECK(base::CreateTemporaryFile(&temp_file_path));
  manager_->test_api().set_arc_cgroup_freezer_state_path(temp_file_path);
  manager_->test_api().set_suspend_delay_id(kSuspendDelayId);

  // Fake the SuspendImminent signal.
  dbus::Signal suspend_signal(
      power_manager::kPowerManagerInterface,
      power_manager::kSuspendImminentSignal);
  power_manager::SuspendImminent suspend_imminent;
  suspend_imminent.set_suspend_id(kSuspendId);
  dbus::MessageWriter suspend_writer(&suspend_signal);
  suspend_writer.AppendProtoAsArrayOfBytes(suspend_imminent);

  // SuspendImminent should trigger a HandleSuspendReadiness response
  // after freezing the ARC instance.
  EXPECT_CALL(*powerd_object_proxy.get(),
      MockCallMethodAndBlock(
          HandleSuspendReadinessMethod(kSuspendDelayId, kSuspendId),
          _));

  manager_->test_api().Suspend(&suspend_signal);

  EXPECT_TRUE(base::ReadFileToString(temp_file_path, &cgroup_state));
  EXPECT_EQ(cgroup_state, SessionManagerService::kFrozen);

  // SuspendDone should just trigger thawing the instance. We don't
  // need to worry about faking a message here, since we don't use
  // the message.
  manager_->test_api().Resume();

  EXPECT_TRUE(base::ReadFileToString(temp_file_path, &cgroup_state));
  EXPECT_EQ(cgroup_state, SessionManagerService::kThawed);
}

}  // namespace login_manager
