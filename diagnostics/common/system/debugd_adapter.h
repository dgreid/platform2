// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_COMMON_SYSTEM_DEBUGD_ADAPTER_H_
#define DIAGNOSTICS_COMMON_SYSTEM_DEBUGD_ADAPTER_H_

#include <string>

#include <base/callback.h>
#include <brillo/errors/error.h>

namespace diagnostics {

// Adapter for communication with debugd daemon.
class DebugdAdapter {
 public:
  using StringResultCallback =
      base::Callback<void(const std::string& result, brillo::Error* error)>;

  virtual ~DebugdAdapter() = default;

  // Sends async request to debugd via D-Bus call. On success, debugd runs
  // smartctl util to retrieve SMART attributes and returns output via callback.
  virtual void GetSmartAttributes(const StringResultCallback& callback) = 0;

  // Sends async request to debugd via D-Bus call. On success, debugd runs
  // nvme util to retrieve NVMe identity data and returns output via callback.
  virtual void GetNvmeIdentity(const StringResultCallback& callback) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_COMMON_SYSTEM_DEBUGD_ADAPTER_H_
