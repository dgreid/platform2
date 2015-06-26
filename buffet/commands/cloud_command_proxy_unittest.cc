// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/commands/cloud_command_proxy.h"

#include <memory>
#include <queue>

#include <base/test/simple_test_clock.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "buffet/commands/command_dictionary.h"
#include "buffet/commands/command_instance.h"
#include "buffet/commands/unittest_utils.h"
#include "buffet/states/mock_state_change_queue_interface.h"

using testing::SaveArg;
using testing::Invoke;
using testing::Return;
using testing::ReturnPointee;
using testing::_;

namespace buffet {

using unittests::CreateDictionaryValue;
using unittests::CreateValue;

namespace {

const char kCmdID[] = "abcd";

MATCHER_P(MatchJson, str, "") {
  return arg.Equals(CreateValue(str).get());
}

class MockCloudCommandUpdateInterface : public CloudCommandUpdateInterface {
 public:
  MOCK_METHOD4(UpdateCommand, void(const std::string&,
                                   const base::DictionaryValue&,
                                   const base::Closure&,
                                   const base::Closure&));
};

// Mock-like task runner that allow the tests to inspect the calls to
// TaskRunner::PostDelayedTask and verify the delays.
class TestTaskRunner : public base::TaskRunner {
 public:
  MOCK_METHOD3(PostDelayedTask, bool(const tracked_objects::Location&,
                                     const base::Closure&,
                                     base::TimeDelta));

  bool RunsTasksOnCurrentThread() const override { return true; }
};

// Test back-off entry that uses the test clock.
class TestBackoffEntry : public chromeos::BackoffEntry {
 public:
  TestBackoffEntry(const Policy* const policy, base::Clock* clock)
      : chromeos::BackoffEntry{policy}, clock_{clock} {
    creation_time_ = clock->Now();
  }

 private:
  // Override from chromeos::BackoffEntry to use the custom test clock for
  // the backoff calculations.
  base::TimeTicks ImplGetTimeNow() const override {
    return base::TimeTicks::FromInternalValue(clock_->Now().ToInternalValue());
  }

  base::Clock* clock_;
  base::Time creation_time_;
};

class CloudCommandProxyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Set up the test StateChangeQueue.
    auto callback = [this](
        const base::Callback<void(StateChangeQueueInterface::UpdateID)>& call) {
      return callbacks_.Add(call).release();
    };
    EXPECT_CALL(state_change_queue_, MockAddOnStateUpdatedCallback(_))
        .WillRepeatedly(Invoke(callback));
    EXPECT_CALL(state_change_queue_, GetLastStateChangeId())
        .WillRepeatedly(testing::ReturnPointee(&current_state_update_id_));

    // Set up the task runner.
    task_runner_ = new TestTaskRunner();

    auto on_post_task = [this](const tracked_objects::Location& from_here,
                               const base::Closure& task,
                               base::TimeDelta delay) -> bool {
      clock_.Advance(delay);
      task_queue_.push(task);
      return true;
    };

    ON_CALL(*task_runner_, PostDelayedTask(_, _, _))
        .WillByDefault(testing::Invoke(on_post_task));

    clock_.SetNow(base::Time::Now());

    // Set up the command schema.
    auto json = CreateDictionaryValue(R"({
      'calc': {
        'add': {
          'parameters': {
            'value1': 'integer',
            'value2': 'integer'
          },
          'progress': {
            'status' : 'string'
          },
          'results': {
            'sum' : 'integer'
          }
        }
      }
    })");
    CHECK(json.get());
    CHECK(command_dictionary_.LoadCommands(*json, "calcd", nullptr, nullptr))
        << "Failed to parse test command dictionary";

    CreateCommandInstance();
  }

  void CreateCommandInstance() {
    auto command_json = CreateDictionaryValue(R"({
      'name': 'calc.add',
      'id': 'abcd',
      'parameters': {
        'value1': 10,
        'value2': 20
      }
    })");
    CHECK(command_json.get());

    command_instance_ = CommandInstance::FromJson(
        command_json.get(), "cloud", command_dictionary_, nullptr, nullptr);
    CHECK(command_instance_.get());

    // Backoff - start at 1s and double with each backoff attempt and no jitter.
    static const chromeos::BackoffEntry::Policy policy{
        0, 1000, 2.0, 0.0, 20000, -1, false};
    std::unique_ptr<TestBackoffEntry> backoff{
        new TestBackoffEntry{&policy, &clock_}};

    // Finally construct the CloudCommandProxy we are going to test here.
    std::unique_ptr<CloudCommandProxy> proxy{new CloudCommandProxy{
        command_instance_.get(), &cloud_updater_, &state_change_queue_,
        std::move(backoff), task_runner_}};
    command_instance_->AddProxy(std::move(proxy));
  }

  StateChangeQueueInterface::UpdateID current_state_update_id_{0};
  base::CallbackList<void(StateChangeQueueInterface::UpdateID)> callbacks_;
  testing::StrictMock<MockCloudCommandUpdateInterface> cloud_updater_;
  testing::StrictMock<MockStateChangeQueueInterface> state_change_queue_;
  base::SimpleTestClock clock_;
  scoped_refptr<TestTaskRunner> task_runner_;
  std::queue<base::Closure> task_queue_;
  CommandDictionary command_dictionary_;
  std::unique_ptr<CommandInstance> command_instance_;
};

}  // anonymous namespace

TEST_F(CloudCommandProxyTest, ImmediateUpdate) {
  const char expected[] = "{'state':'done'}";
  EXPECT_CALL(cloud_updater_, UpdateCommand(kCmdID, MatchJson(expected), _, _));
  command_instance_->Done();
}

TEST_F(CloudCommandProxyTest, DelayedUpdate) {
  // Simulate that the current device state has changed.
  current_state_update_id_ = 20;
  // No command update is expected here.
  command_instance_->Done();
  // Still no command update here...
  callbacks_.Notify(19);
  // Now we should get the update...
  const char expected[] = "{'state':'done'}";
  EXPECT_CALL(cloud_updater_, UpdateCommand(kCmdID, MatchJson(expected), _, _));
  callbacks_.Notify(20);
}

TEST_F(CloudCommandProxyTest, InFlightRequest) {
  // SetProgress causes two consecutive updates:
  //    state=inProgress
  //    progress={...}
  // The first state update is sent immediately, the second should be delayed.
  base::Closure on_success;
  EXPECT_CALL(cloud_updater_,
              UpdateCommand(kCmdID, MatchJson("{'state':'inProgress'}"), _, _))
      .WillOnce(SaveArg<2>(&on_success));
  command_instance_->SetProgress(
      {{"status", unittests::make_string_prop_value("ready")}});

  // Now simulate the first request completing.
  // The second request should be sent now.
  const char expected[] = "{'progress':{'status':'ready'}}";
  EXPECT_CALL(cloud_updater_, UpdateCommand(kCmdID, MatchJson(expected), _, _));
  on_success.Run();
}

TEST_F(CloudCommandProxyTest, CombineMultiple) {
  // Simulate that the current device state has changed.
  current_state_update_id_ = 20;
  // SetProgress causes two consecutive updates:
  //    state=inProgress
  //    progress={...}
  // Both updates will be held until device state is updated.
  command_instance_->SetProgress(
      {{"status", unittests::make_string_prop_value("ready")}});

  // Now simulate the device state updated. Both updates should come in one
  // request.
  const char expected[] = R"({
    'progress': {'status':'ready'},
    'state':'inProgress'
  })";
  EXPECT_CALL(cloud_updater_, UpdateCommand(kCmdID, MatchJson(expected), _, _));
  callbacks_.Notify(20);
}

TEST_F(CloudCommandProxyTest, RetryFailed) {
  base::Closure on_error;
  const char expect1[] = "{'state':'inProgress'}";
  EXPECT_CALL(cloud_updater_, UpdateCommand(kCmdID, MatchJson(expect1), _, _))
      .WillOnce(SaveArg<3>(&on_error));
  command_instance_->SetProgress(
      {{"status", unittests::make_string_prop_value("ready")}});

  // Now pretend the first command update request has failed.
  // We should retry with both state and progress fields updated this time,
  // after the initial backoff (which should be 1s in our case).
  base::TimeDelta expected_delay = base::TimeDelta::FromSeconds(1);
  EXPECT_CALL(*task_runner_, PostDelayedTask(_, _, expected_delay));
  on_error.Run();

  // Execute the delayed request. But pretend that it failed too.
  const char expect2[] = R"({
    'progress': {'status':'ready'},
    'state':'inProgress'
  })";
  EXPECT_CALL(cloud_updater_, UpdateCommand(kCmdID, MatchJson(expect2), _, _))
      .WillOnce(SaveArg<3>(&on_error));
  task_queue_.back().Run();
  task_queue_.pop();

  // Now backoff should be 2 seconds.
  expected_delay = base::TimeDelta::FromSeconds(2);
  EXPECT_CALL(*task_runner_, PostDelayedTask(_, _, expected_delay));
  on_error.Run();

  // Retry the task.
  base::Closure on_success;
  EXPECT_CALL(cloud_updater_, UpdateCommand(kCmdID, MatchJson(expect2), _, _))
      .WillOnce(SaveArg<2>(&on_success));
  task_queue_.back().Run();
  task_queue_.pop();

  // Pretend it succeeds this time.
  on_success.Run();
}

TEST_F(CloudCommandProxyTest, GateOnStateUpdates) {
  current_state_update_id_ = 20;
  command_instance_->SetProgress(
      {{"status", unittests::make_string_prop_value("ready")}});
  current_state_update_id_ = 21;
  command_instance_->SetProgress(
      {{"status", unittests::make_string_prop_value("busy")}});
  current_state_update_id_ = 22;
  command_instance_->Done();

  // Device state #20 updated.
  base::Closure on_success;
  const char expect1[] = R"({
    'progress': {'status':'ready'},
    'state':'inProgress'
  })";
  EXPECT_CALL(cloud_updater_, UpdateCommand(kCmdID, MatchJson(expect1), _, _))
      .WillOnce(SaveArg<2>(&on_success));
  callbacks_.Notify(20);
  on_success.Run();

  // Device state #21 updated.
  const char expect2[] = "{'progress': {'status':'busy'}}";
  EXPECT_CALL(cloud_updater_, UpdateCommand(kCmdID, MatchJson(expect2), _, _))
      .WillOnce(SaveArg<2>(&on_success));
  callbacks_.Notify(21);

  // Device state #22 updated. Nothing happens here since the previous command
  // update request hasn't completed yet.
  callbacks_.Notify(22);

  // Now the command update is complete, send out the patch that happened after
  // the state #22 was updated.
  const char expect3[] = "{'state': 'done'}";
  EXPECT_CALL(cloud_updater_, UpdateCommand(kCmdID, MatchJson(expect3), _, _))
      .WillOnce(SaveArg<2>(&on_success));
  on_success.Run();
}

TEST_F(CloudCommandProxyTest, CombineSomeStates) {
  current_state_update_id_ = 20;
  command_instance_->SetProgress(
      {{"status", unittests::make_string_prop_value("ready")}});
  current_state_update_id_ = 21;
  command_instance_->SetProgress(
      {{"status", unittests::make_string_prop_value("busy")}});
  current_state_update_id_ = 22;
  command_instance_->Done();

  // Device state 20-21 updated.
  base::Closure on_success;
  const char expect1[] = R"({
    'progress': {'status':'busy'},
    'state':'inProgress'
  })";
  EXPECT_CALL(cloud_updater_, UpdateCommand(kCmdID, MatchJson(expect1), _, _))
      .WillOnce(SaveArg<2>(&on_success));
  callbacks_.Notify(21);
  on_success.Run();

  // Device state #22 updated.
  const char expect2[] = "{'state': 'done'}";
  EXPECT_CALL(cloud_updater_, UpdateCommand(kCmdID, MatchJson(expect2), _, _))
      .WillOnce(SaveArg<2>(&on_success));
  callbacks_.Notify(22);
  on_success.Run();
}

TEST_F(CloudCommandProxyTest, CombineAllStates) {
  current_state_update_id_ = 20;
  command_instance_->SetProgress(
      {{"status", unittests::make_string_prop_value("ready")}});
  current_state_update_id_ = 21;
  command_instance_->SetProgress(
      {{"status", unittests::make_string_prop_value("busy")}});
  current_state_update_id_ = 22;
  command_instance_->Done();

  // Device state 30 updated.
  const char expected[] = R"({
    'progress': {'status':'busy'},
    'state':'done'
  })";
  EXPECT_CALL(cloud_updater_, UpdateCommand(kCmdID, MatchJson(expected), _, _));
  callbacks_.Notify(30);
}

TEST_F(CloudCommandProxyTest, CoalesceUpdates) {
  current_state_update_id_ = 20;
  command_instance_->SetProgress(
      {{"status", unittests::make_string_prop_value("ready")}});
  command_instance_->SetProgress(
      {{"status", unittests::make_string_prop_value("busy")}});
  command_instance_->SetProgress(
      {{"status", unittests::make_string_prop_value("finished")}});
  command_instance_->SetResults({{"sum", unittests::make_int_prop_value(30)}});
  command_instance_->Done();

  const char expected[] = R"({
    'progress': {'status':'finished'},
    'results': {'sum':30},
    'state':'done'
  })";
  EXPECT_CALL(cloud_updater_, UpdateCommand(kCmdID, MatchJson(expected), _, _));
  callbacks_.Notify(30);
}

TEST_F(CloudCommandProxyTest, EmptyStateChangeQueue) {
  // Assume the device state update queue was empty and was at update ID 20.
  current_state_update_id_ = 20;

  // Recreate the command instance and proxy with the new state change queue.
  CreateCommandInstance();

  // Empty queue will immediately call back with the state change notification.
  callbacks_.Notify(20);

  // As soon as we change the command, the update to the server should be sent.
  const char expected[] = "{'state':'done'}";
  EXPECT_CALL(cloud_updater_, UpdateCommand(kCmdID, MatchJson(expected), _, _));
  command_instance_->Done();
}

TEST_F(CloudCommandProxyTest, NonEmptyStateChangeQueue) {
  // Assume the device state update queue was NOT empty when the command
  // instance was created.
  current_state_update_id_ = 20;

  // Recreate the command instance and proxy with the new state change queue.
  CreateCommandInstance();

  // No command updates right now.
  command_instance_->Done();

  // Only when the state #20 is published we should update the command
  const char expected[] = "{'state':'done'}";
  EXPECT_CALL(cloud_updater_, UpdateCommand(kCmdID, MatchJson(expected), _, _));
  callbacks_.Notify(20);
}

}  // namespace buffet
