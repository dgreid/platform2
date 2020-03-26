// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_SANE_CLIENT_IMPL_H_
#define LORGNETTE_SANE_CLIENT_IMPL_H_

#include <memory>
#include <string>
#include <unordered_map>

#include <base/synchronization/lock.h>
#include <brillo/errors/error.h>
#include <sane/sane.h>

#include "lorgnette/manager.h"
#include "lorgnette/sane_client.h"

namespace lorgnette {

class SaneClientImpl : public SaneClient {
 public:
  static std::unique_ptr<SaneClientImpl> Create();
  ~SaneClientImpl();

  bool ListDevices(brillo::ErrorPtr* error,
                   Manager::ScannerInfo* info_out) override;

  static bool DeviceListToScannerInfo(const SANE_Device** device_list,
                                      Manager::ScannerInfo* info_out);

 private:
  SaneClientImpl();

  base::Lock lock_;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SANE_CLIENT_IMPL_H_
