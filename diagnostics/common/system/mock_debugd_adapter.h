// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_COMMON_SYSTEM_MOCK_DEBUGD_ADAPTER_H_
#define DIAGNOSTICS_COMMON_SYSTEM_MOCK_DEBUGD_ADAPTER_H_

#include <gmock/gmock.h>

#include "diagnostics/common/system/debugd_adapter.h"

namespace diagnostics {

class MockDebugdAdapter : public DebugdAdapter {
 public:
  MockDebugdAdapter();
  MockDebugdAdapter(const MockDebugdAdapter&) = delete;
  MockDebugdAdapter& operator=(const MockDebugdAdapter&) = delete;
  ~MockDebugdAdapter() override;

  MOCK_METHOD(void,
              GetSmartAttributes,
              (const StringResultCallback&),
              (override));
  MOCK_METHOD(void, GetNvmeIdentity, (const StringResultCallback&), (override));
  MOCK_METHOD(void,
              RunNvmeShortSelfTest,
              (const StringResultCallback&),
              (override));
  MOCK_METHOD(void,
              RunNvmeLongSelfTest,
              (const StringResultCallback&),
              (override));
  MOCK_METHOD(void,
              StopNvmeSelfTest,
              (const StringResultCallback&),
              (override));
  MOCK_METHOD(void,
              GetNvmeLog,
              (uint32_t, uint32_t, bool, const StringResultCallback&),
              (override));
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_COMMON_SYSTEM_MOCK_DEBUGD_ADAPTER_H_
