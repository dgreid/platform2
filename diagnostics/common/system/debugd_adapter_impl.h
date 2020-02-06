// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_COMMON_SYSTEM_DEBUGD_ADAPTER_IMPL_H_
#define DIAGNOSTICS_COMMON_SYSTEM_DEBUGD_ADAPTER_IMPL_H_

#include <memory>

#include "debugd/dbus-proxies.h"
#include "diagnostics/common/system/debugd_adapter.h"

namespace diagnostics {

class DebugdAdapterImpl final : public DebugdAdapter {
 public:
  explicit DebugdAdapterImpl(
      std::unique_ptr<org::chromium::debugdProxyInterface> debugd_proxy);

  ~DebugdAdapterImpl() override;

  // DebugdAdapter overrides:
  void GetSmartAttributes(const StringResultCallback& callback) override;
  void GetNvmeIdentity(const StringResultCallback& callback) override;

 private:
  std::unique_ptr<org::chromium::debugdProxyInterface> debugd_proxy_;

  DISALLOW_COPY_AND_ASSIGN(DebugdAdapterImpl);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_COMMON_SYSTEM_DEBUGD_ADAPTER_IMPL_H_
