// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_CROS_FP_DEVICE_INTERFACE_H_
#define BIOD_CROS_FP_DEVICE_INTERFACE_H_

#include <bitset>
#include <string>
#include <vector>

#include <brillo/secure_blob.h>
#include <chromeos/ec/ec_commands.h>

#include "biod/ec_command.h"
#include "biod/fp_mode.h"

/**
 * The template is encrypted, so it's not strictly necessary to use
 * SecureVector, but we do so as part of a defense-in-depth strategy in case
 * there's a bug in the encryption/FPMCU.
 */
using VendorTemplate = brillo::SecureVector;

namespace biod {

class CrosFpDeviceInterface {
 public:
  using MkbpCallback = base::Callback<void(const uint32_t event)>;
  CrosFpDeviceInterface() = default;
  virtual ~CrosFpDeviceInterface() = default;

  struct EcVersion {
    std::string ro_version;
    std::string rw_version;
    ec_current_image current_image = EC_IMAGE_UNKNOWN;
  };

  virtual void SetMkbpEventCallback(MkbpCallback callback) = 0;

  virtual bool SetFpMode(const FpMode& mode) = 0;
  /**
   * @return mode on success, FpMode(FpMode::Mode::kModeInvalid) on failure
   */
  virtual FpMode GetFpMode() = 0;
  virtual bool GetFpStats(int* capture_ms,
                          int* matcher_ms,
                          int* overall_ms) = 0;
  virtual bool GetDirtyMap(std::bitset<32>* bitmap) = 0;
  virtual bool SupportsPositiveMatchSecret() = 0;
  virtual bool GetPositiveMatchSecret(int index,
                                      brillo::SecureVector* secret) = 0;
  virtual bool GetTemplate(int index, VendorTemplate* out) = 0;
  virtual bool UploadTemplate(const VendorTemplate& tmpl) = 0;
  virtual bool SetContext(std::string user_id) = 0;
  virtual bool ResetContext() = 0;
  // Initialise the entropy in the SBP. If |reset| is true, the old entropy
  // will be deleted. If |reset| is false, we will only add entropy, and only
  // if no entropy had been added before.
  virtual bool InitEntropy(bool reset) = 0;
  virtual bool UpdateFpInfo() = 0;

  virtual int MaxTemplateCount() = 0;
  virtual int TemplateVersion() = 0;
  virtual int DeadPixelCount() = 0;

  virtual EcCmdVersionSupportStatus EcCmdVersionSupported(uint16_t cmd,
                                                          uint32_t ver) = 0;

  DISALLOW_COPY_AND_ASSIGN(CrosFpDeviceInterface);
};

}  // namespace biod

#endif  // BIOD_CROS_FP_DEVICE_INTERFACE_H_
