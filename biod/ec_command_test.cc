// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "biod/ec_command.h"

using testing::_;
using testing::InvokeWithoutArgs;
using testing::Return;

namespace biod {
namespace {

constexpr int kDummyFd = 0;
constexpr int kIoctlFailureRetVal = -1;

template <typename O, typename I>
class MockEcCommand : public EcCommand<O, I> {
 public:
  using EcCommand<O, I>::EcCommand;
  ~MockEcCommand() override = default;

  using Data = typename EcCommand<O, I>::Data;
  MOCK_METHOD(int, ioctl, (int fd, uint32_t request, Data* data));
};

class MockFpModeCommand : public MockEcCommand<struct ec_params_fp_mode,
                                               struct ec_response_fp_mode> {
 public:
  MockFpModeCommand() : MockEcCommand(EC_CMD_FP_MODE, 0, {.mode = 1}) {}
};

// ioctl behavior for EC commands:
//   returns sizeof(EC response) (>=0) on success, -1 on failure
//   cmd.result is error code from EC (EC_RES_SUCCESS, etc)

TEST(EcCommand, Run_Success) {
  MockFpModeCommand mock;
  EXPECT_CALL(mock, ioctl).WillOnce(Return(mock.RespSize()));
  EXPECT_TRUE(mock.Run(kDummyFd));
}

TEST(EcCommand, Run_IoctlFailure) {
  MockFpModeCommand mock;
  EXPECT_CALL(mock, ioctl).WillOnce(Return(kIoctlFailureRetVal));
  EXPECT_FALSE(mock.Run(kDummyFd));
}

TEST(EcCommand, Run_CommandFailure) {
  MockFpModeCommand mock;
  EXPECT_CALL(mock, ioctl)
      .WillOnce([](int, uint32_t, MockFpModeCommand::Data* data) {
        // Test the case where the ioctl itself succeeds, the but the EC
        // command did not. In this case, "result" will be set, but the
        // response size will not match the command's response size.
        data->cmd.result = EC_RES_ACCESS_DENIED;
        return 0;
      });

  EXPECT_FALSE(mock.Run(kDummyFd));
}

TEST(EcCommand, ConstReq) {
  const MockFpModeCommand mock;
  EXPECT_TRUE(mock.Req());
}

TEST(EcCommand, ConstResp) {
  const MockFpModeCommand mock;
  EXPECT_TRUE(mock.Resp());
}

TEST(EcCommand, Run_CheckResult_Success) {
  constexpr int kExpectedResult = 42;
  MockFpModeCommand mock;
  EXPECT_CALL(mock, ioctl)
      .WillOnce([](int, uint32_t, MockFpModeCommand::Data* data) {
        data->cmd.result = kExpectedResult;
        return data->cmd.insize;
      });
  EXPECT_TRUE(mock.Run(kDummyFd));
  EXPECT_EQ(mock.Result(), kExpectedResult);
}

TEST(EcCommand, Run_CheckResult_Failure) {
  MockFpModeCommand mock;
  EXPECT_CALL(mock, ioctl)
      .WillOnce([](int, uint32_t, MockFpModeCommand::Data* data) {
        // Note that it's not expected that the result would be set by the
        // kernel driver in this case, but we want to be defensive against
        // the behavior in case there is an instance where it does.
        data->cmd.result = EC_RES_ERROR;
        return kIoctlFailureRetVal;
      });
  EXPECT_FALSE(mock.Run(kDummyFd));
  EXPECT_EQ(mock.Result(), kEcCommandUninitializedResult);
}

TEST(EcCommand, RunWithMultipleAttempts_Success) {
  constexpr int kNumAttempts = 2;
  MockFpModeCommand mock;
  EXPECT_CALL(mock, ioctl)
      .Times(kNumAttempts)
      // First ioctl() fails
      .WillOnce(InvokeWithoutArgs([]() {
        errno = ETIMEDOUT;
        return kIoctlFailureRetVal;
      }))
      // Second ioctl() succeeds
      .WillOnce(Return(mock.RespSize()));
  EXPECT_TRUE(mock.RunWithMultipleAttempts(kDummyFd, kNumAttempts));
}

TEST(EcCommand, RunWithMultipleAttempts_Timeout_Failure) {
  constexpr int kNumAttempts = 2;
  MockFpModeCommand mock;
  EXPECT_CALL(mock, ioctl)
      .Times(kNumAttempts)
      // All calls to ioctl() timeout
      .WillRepeatedly(InvokeWithoutArgs([]() {
        errno = ETIMEDOUT;
        return kIoctlFailureRetVal;
      }));
  EXPECT_FALSE(mock.RunWithMultipleAttempts(kDummyFd, kNumAttempts));
}

TEST(EcCommand, RunWithMultipleAttempts_ErrorNotTimeout_Failure) {
  constexpr int kNumAttempts = 2;
  MockFpModeCommand mock;
  EXPECT_CALL(mock, ioctl)
      // Errors other than timeout should cause immediate failure even when
      // attempting retries.
      .Times(1)
      .WillOnce(InvokeWithoutArgs([]() {
        errno = EINVAL;
        return kIoctlFailureRetVal;
      }));
  EXPECT_FALSE(mock.RunWithMultipleAttempts(kDummyFd, kNumAttempts));
}

}  // namespace
}  // namespace biod
