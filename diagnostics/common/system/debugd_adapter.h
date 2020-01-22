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

  // Sends async request to debugd via D-Bus call. On success, debugd runs
  // nvme util to start NVMe short-time self-test and returns start result
  // output via callback.
  virtual void RunNvmeShortSelfTest(const StringResultCallback& callback) = 0;

  // Sends async request to debugd via D-Bus call. On success, debugd runs
  // nvme util to start NVMe long-time self-test and returns start result
  // via callback.
  virtual void RunNvmeLongSelfTest(const StringResultCallback& callback) = 0;

  // Sends async request to debugd via D-Bus call. On success, debugd runs
  // nvme util to abort NVMe self-test..
  virtual void StopNvmeSelfTest(const StringResultCallback& callback) = 0;

  // Sends async request to debugd via D-Bus call. On success, debugd runs
  // nvme util to retrieve NVMe info from log page and returns output via
  // callback. Parameter page_id indicates which log page is required; length
  // indicates the size of required byte data (this parameter also means precise
  // length of decoded data if raw_binary is set); raw_binary indicates if data
  // shall be returned with raw binary format and encoded with Base64.
  virtual void GetNvmeLog(uint32_t page_id,
                          uint32_t length,
                          bool raw_binary,
                          const StringResultCallback& callback) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_COMMON_SYSTEM_DEBUGD_ADAPTER_H_
