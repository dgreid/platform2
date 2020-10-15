// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "biod/ec_command_async.h"

using testing::_;
using testing::InvokeWithoutArgs;
using testing::Return;

namespace biod {
namespace {

constexpr int kDummyFd = 0;
constexpr int kIoctlFailureRetVal = -1;

template <typename O, typename I>
class MockEcCommandAsync : public EcCommandAsync<O, I> {
 public:
  using EcCommandAsync<O, I>::EcCommandAsync;
  ~MockEcCommandAsync() override = default;

  using Data = typename EcCommandAsync<O, I>::Data;
  MOCK_METHOD(int, ioctl, (int fd, uint32_t request, Data* data), (override));
};

class MockAddEntropyCommand
    : public MockEcCommandAsync<struct ec_params_rollback_add_entropy,
                                EmptyParam> {
 public:
  explicit MockAddEntropyCommand(const Options& options)
      : MockEcCommandAsync(
            EC_CMD_ADD_ENTROPY, ADD_ENTROPY_GET_RESULT, options) {}
};

// ioctl behavior for EC commands:
//   returns sizeof(EC response) (>=0) on success, -1 on failure
//   cmd.result is error code from EC (EC_RES_SUCCESS, etc)

TEST(EcCommandAsync, Run_Success) {
  MockAddEntropyCommand mock_cmd(
      {.poll_for_result_num_attempts = 2,
       .poll_interval = base::TimeDelta::FromMilliseconds(1)});
  EXPECT_CALL(mock_cmd, ioctl)
      .Times(3)
      // First call to ioctl() to start the command; EC returns success.
      .WillOnce([](int, uint32_t, MockAddEntropyCommand::Data* data) {
        data->cmd.result = EC_RES_SUCCESS;
        return data->cmd.insize;
      })
      // Second call to ioctl() to get the result; EC returns busy.
      .WillOnce([](int, uint32_t, MockAddEntropyCommand::Data* data) {
        data->cmd.result = EC_RES_BUSY;
        return data->cmd.insize;
      })
      // Third call to ioctl() to get the result; EC returns success.
      .WillOnce([](int, uint32_t, MockAddEntropyCommand::Data* data) {
        data->cmd.result = EC_RES_SUCCESS;
        return data->cmd.insize;
      });

  EXPECT_TRUE(mock_cmd.Run(kDummyFd));
  EXPECT_EQ(mock_cmd.Result(), EC_RES_SUCCESS);
}

TEST(EcCommandAsync, Run_TimeoutFailure) {
  MockAddEntropyCommand mock_cmd(
      {.poll_for_result_num_attempts = 2,
       .poll_interval = base::TimeDelta::FromMilliseconds(1)});

  EXPECT_CALL(mock_cmd, ioctl)
      .Times(3)
      // First call to ioctl() to start the command; EC returns success.
      .WillOnce([](int, uint32_t, MockAddEntropyCommand::Data* data) {
        data->cmd.result = EC_RES_SUCCESS;
        return data->cmd.insize;
      })
      // All remaining ioctl() calls; EC returns busy.
      .WillRepeatedly([](int, uint32_t, MockAddEntropyCommand::Data* data) {
        data->cmd.result = EC_RES_BUSY;
        return data->cmd.insize;
      });

  EXPECT_FALSE(mock_cmd.Run(kDummyFd));
  EXPECT_EQ(mock_cmd.Result(), EC_RES_BUSY);
}

TEST(EcCommandAsync, Run_Failure) {
  MockAddEntropyCommand mock_cmd(
      {// With the number of attempts set to 2, there will be at most
       // 3 ioctl calls (the extra one starts the command). In this
       // test case, we're validating that the last ioctl() call will
       // not be performed because we got an error on the second
       // ioctl() call.
       .poll_for_result_num_attempts = 2,
       .poll_interval = base::TimeDelta::FromMilliseconds(1)});
  EXPECT_CALL(mock_cmd, ioctl)
      .Times(2)
      // First call to ioctl() to start the command; EC returns success.
      .WillOnce([](int, uint32_t, MockAddEntropyCommand::Data* data) {
        data->cmd.result = EC_RES_SUCCESS;
        return data->cmd.insize;
      })
      // Second call to ioctl() to get the result; EC returns error.
      .WillOnce([](int, uint32_t, MockAddEntropyCommand::Data* data) {
        data->cmd.result = EC_RES_ERROR;
        return data->cmd.insize;
      });

  EXPECT_FALSE(mock_cmd.Run(kDummyFd));
  EXPECT_EQ(mock_cmd.Result(), EC_RES_ERROR);
}

TEST(EcCommandAsync, Run_IoctlTimesOut) {
  MockAddEntropyCommand mock({
      // With the number of attempts set to 2, there will be at
      // most 3 ioctl calls (the extra one starts the command). In
      // this test case, we're validating that the last ioctl()
      // call will not be performed because we got an error on
      // the second ioctl() call.
      .poll_for_result_num_attempts = 2,
      .poll_interval = base::TimeDelta::FromMilliseconds(1),
  });
  EXPECT_CALL(mock, ioctl)
      .Times(2)
      // First call to ioctl() to start the command; EC returns success.
      .WillOnce([](int, uint32_t, MockAddEntropyCommand::Data* data) {
        data->cmd.result = EC_RES_SUCCESS;
        return data->cmd.insize;
      })
      // Second call to ioctl() to get the result returns error (EC not
      // responding).
      .WillOnce([](int, uint32_t, MockAddEntropyCommand::Data* data) {
        errno = ETIMEDOUT;
        return kIoctlFailureRetVal;
      });

  EXPECT_FALSE(mock.Run(kDummyFd));
  EXPECT_EQ(mock.Result(), kEcCommandUninitializedResult);
}

TEST(EcCommandAsync, Run_IoctlTimesOut_IgnoreFailure) {
  MockAddEntropyCommand mock(
      {.poll_for_result_num_attempts = 2,
       .poll_interval = base::TimeDelta::FromMilliseconds(1),
       .validate_poll_result = false});
  EXPECT_CALL(mock, ioctl)
      .Times(3)
      // First call to ioctl() to start the command; EC returns success.
      .WillOnce([](int, uint32_t, MockAddEntropyCommand::Data* data) {
        data->cmd.result = EC_RES_SUCCESS;
        return data->cmd.insize;
      })
      // Second call to ioctl() to get the result returns error; EC not
      // responding.
      .WillOnce([](int, uint32_t, MockAddEntropyCommand::Data* data) {
        errno = ETIMEDOUT;
        return kIoctlFailureRetVal;
      })
      // Third call to ioctl() to get the result; EC returns success.
      .WillOnce([](int, uint32_t, MockAddEntropyCommand::Data* data) {
        data->cmd.result = EC_RES_SUCCESS;
        return data->cmd.insize;
      });

  EXPECT_TRUE(mock.Run(kDummyFd));
  EXPECT_EQ(mock.Result(), EC_RES_SUCCESS);
}

TEST(EcCommandAsync, Run_InvalidOptions_ZeroPollAttempts) {
  MockAddEntropyCommand mock({.poll_for_result_num_attempts = 0});
  EXPECT_DEATH(mock.Run(kDummyFd), "poll_for_result_num_attempts > 0");
}

TEST(EcCommandAsync, Run_InvalidOptions_NegativePollAttempts) {
  MockAddEntropyCommand mock({.poll_for_result_num_attempts = -1});
  EXPECT_DEATH(mock.Run(kDummyFd), "poll_for_result_num_attempts > 0");
}

TEST(EcCommandAsync, DefaultOptions) {
  MockAddEntropyCommand::Options options;
  EXPECT_EQ(options.validate_poll_result, true);
  EXPECT_EQ(options.poll_for_result_num_attempts, 20);
  EXPECT_EQ(options.poll_interval, base::TimeDelta::FromMilliseconds(100));
}

}  // namespace
}  // namespace biod
