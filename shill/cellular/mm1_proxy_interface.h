// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_MM1_PROXY_INTERFACE_H_
#define SHILL_CELLULAR_MM1_PROXY_INTERFACE_H_

#include <string>
#include <vector>

#include "shill/callbacks.h"

namespace shill {

namespace mm1 {

using ModemStateChangedSignalCallback =
    base::Callback<void(int32_t, int32_t, uint32_t)>;

// These are the methods that a org.freedesktop.ModemManager1 proxy must
// support. The interface is provided so that it can be mocked in tests. All
// calls are made asynchronously. Call completion is signalled via the callbacks
// passed to the methods.
class Mm1ProxyInterface {
 public:
  virtual ~Mm1ProxyInterface() = default;

  virtual void ScanDevices(const ResultCallback& callback) = 0;

  virtual void SetLogging(const std::string& level,
                          const ResultCallback& callback) = 0;

  // ReportKernelEvent not implemented.

  virtual void InhibitDevice(const std::string& uid,
                             bool inhibit,
                             const ResultCallback& callback) = 0;
};

}  // namespace mm1
}  // namespace shill

#endif  // SHILL_CELLULAR_MM1_PROXY_INTERFACE_H_
