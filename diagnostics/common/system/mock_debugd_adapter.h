// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_COMMON_SYSTEM_MOCK_DEBUGD_ADAPTER_H_
#define DIAGNOSTICS_COMMON_SYSTEM_MOCK_DEBUGD_ADAPTER_H_

#include <base/macros.h>
#include <gmock/gmock.h>

#include "diagnostics/common/system/debugd_adapter.h"

namespace diagnostics {

class MockDebugdAdapter : public DebugdAdapter {
 public:
  MockDebugdAdapter();
  ~MockDebugdAdapter() override;

  MOCK_METHOD(void,
              GetSmartAttributes,
              (const StringResultCallback&),
              (override));
  MOCK_METHOD(void, GetNvmeIdentity, (const StringResultCallback&), (override));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockDebugdAdapter);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_COMMON_SYSTEM_MOCK_DEBUGD_ADAPTER_H_
