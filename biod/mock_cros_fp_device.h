// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_MOCK_CROS_FP_DEVICE_H_
#define BIOD_MOCK_CROS_FP_DEVICE_H_

#include <bitset>
#include <string>

#include "biod/cros_fp_device_interface.h"
#include "biod/fp_mode.h"

namespace biod {

class MockCrosFpDevice : public CrosFpDeviceInterface {
 public:
  MockCrosFpDevice() = default;
  ~MockCrosFpDevice() override = default;

  MOCK_METHOD(bool, SetFpMode, (const FpMode& mode));
  MOCK_METHOD(bool, GetFpMode, (FpMode * mode));
  MOCK_METHOD(bool,
              GetFpStats,
              (int* capture_ms, int* matcher_ms, int* overall_ms));
  MOCK_METHOD(bool, GetDirtyMap, (std::bitset<32> * bitmap));
  MOCK_METHOD(bool, GetTemplate, (int index, VendorTemplate* out));
  MOCK_METHOD(bool, UploadTemplate, (const VendorTemplate& tmpl));
  MOCK_METHOD(bool, SetContext, (std::string user_id));
  MOCK_METHOD(bool, ResetContext, ());
  MOCK_METHOD(bool, InitEntropy, (bool reset));
  MOCK_METHOD(int, MaxTemplateCount, ());
  MOCK_METHOD(int, TemplateVersion, ());
  MOCK_METHOD(EcCmdVersionSupportStatus,
              EcCmdVersionSupported,
              (uint16_t cmd, uint32_t ver));
  MOCK_METHOD(bool, SupportsPositiveMatchSecret, ());
  MOCK_METHOD(bool,
              GetPositiveMatchSecret,
              (int index, brillo::SecureBlob* secret));
};

}  // namespace biod

#endif  // BIOD_MOCK_CROS_FP_DEVICE_H_
