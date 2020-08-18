// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_MOCK_EXECUTOR_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_MOCK_EXECUTOR_ADAPTER_H_

#include <gmock/gmock.h>

#include "diagnostics/cros_healthd/executor/executor_adapter.h"
#include "mojo/cros_healthd_executor.mojom.h"

namespace diagnostics {

// Mock implementation of the ExecutorAdapter interface.
class MockExecutorAdapter final : public ExecutorAdapter {
 public:
  MockExecutorAdapter();
  MockExecutorAdapter(const MockExecutorAdapter&) = delete;
  MockExecutorAdapter& operator=(const MockExecutorAdapter&) = delete;
  ~MockExecutorAdapter() override;

  // ExecutorAdapter overrides:
  MOCK_METHOD(void, Connect, (mojo::PlatformChannelEndpoint), (override));
  MOCK_METHOD(void, GetFanSpeed, (Executor::GetFanSpeedCallback), (override));
  MOCK_METHOD(void, RunMemtester, (Executor::RunMemtesterCallback), (override));
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_MOCK_EXECUTOR_ADAPTER_H_
