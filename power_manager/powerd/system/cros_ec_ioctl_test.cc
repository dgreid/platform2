// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "power_manager/powerd/system/cros_ec_ioctl.h"

using testing::_;
using testing::Return;

namespace power_manager {
namespace system {
namespace cros_ec_ioctl {

constexpr int kDummyFd = 0;
constexpr int kIoctlZeroRetVal = 0;
constexpr int kIoctlFailureRetVal = -1;

template <typename Request, typename Response>
class MockIoctlCommand : public cros_ec_ioctl::IoctlCommand<Request, Response> {
 public:
  using cros_ec_ioctl::IoctlCommand<Request, Response>::IoctlCommand;
  ~MockIoctlCommand() override = default;

  using Data = typename cros_ec_ioctl::IoctlCommand<Request, Response>::Data;
  MOCK_METHOD(int, ioctl, (int fd, uint32_t request, Data* data));
};

class MockSmartDischargeCommand
    : public MockIoctlCommand<struct ec_params_smart_discharge,
                              struct ec_response_smart_discharge> {
 public:
  MockSmartDischargeCommand() : MockIoctlCommand(EC_CMD_SMART_DISCHARGE) {}
};

// ioctl behavior for EC commands:
//   returns sizeof(EC response) (>=0) on success, -1 on failure.
//   cmd.result is error code from EC (EC_RES_SUCCESS, etc).

TEST(IoctlCommand, Run_Success) {
  MockSmartDischargeCommand mock;
  EXPECT_CALL(mock, ioctl)
      .WillOnce(Return(realsizeof<ec_response_smart_discharge>()));
  EXPECT_TRUE(mock.Run(kDummyFd));
}

TEST(IoctlCommand, Run_Failure) {
  MockSmartDischargeCommand mock;
  EXPECT_CALL(mock, ioctl).WillOnce(Return(kIoctlFailureRetVal));
  EXPECT_FALSE(mock.Run(kDummyFd));
}

TEST(IoctlCommand, Run_Success_Expected_Result) {
  constexpr int kExpectedResult = 42;
  MockSmartDischargeCommand mock;
  EXPECT_CALL(mock, ioctl)
      .WillOnce([](int, uint32_t, MockSmartDischargeCommand::Data* data) {
        data->cmd.result = kExpectedResult;
        return data->cmd.insize;
      });
  EXPECT_TRUE(mock.Run(kDummyFd));
  EXPECT_EQ(mock.Result(), kExpectedResult);
}

TEST(IoctlCommand, Run_Success_EC_Error) {
  MockSmartDischargeCommand mock;
  EXPECT_CALL(mock, ioctl)
      .WillOnce([](int, uint32_t, MockSmartDischargeCommand::Data* data) {
        data->cmd.result = EC_RES_ERROR;
        return kIoctlZeroRetVal;
      });
  EXPECT_FALSE(mock.Run(kDummyFd));
  EXPECT_EQ(mock.Result(), EC_RES_ERROR);
}

TEST(IoctlCommand, Run_Failure_Expected_Result) {
  MockSmartDischargeCommand mock;
  EXPECT_CALL(mock, ioctl)
      .WillOnce([](int, uint32_t, MockSmartDischargeCommand::Data* data) {
        // Note that it's not expected that the result would be set by the
        // kernel driver in this case, but we want to be defensive against
        // the behavior in case there is an instance where it does.
        data->cmd.result = EC_RES_ERROR;
        return kIoctlFailureRetVal;
      });
  EXPECT_FALSE(mock.Run(kDummyFd));
  EXPECT_EQ(mock.Result(), EC_RES_ERROR);
}

}  // namespace cros_ec_ioctl
}  // namespace system
}  // namespace power_manager
