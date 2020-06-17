/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_UTILS_CAMERA_CONFIG_IMPL_H_
#define CAMERA_COMMON_UTILS_CAMERA_CONFIG_IMPL_H_

#include <memory>
#include <string>

#include <base/values.h>
#include <base/memory/ptr_util.h>

#include "cros-camera/utils/camera_config.h"

namespace cros {

// Read config from camera configure file.
// Reference for all options from: include/cros-camera/constants.h
class CameraConfigImpl final : public CameraConfig {
 public:
  ~CameraConfigImpl() final;

  bool HasKey(const std::string& key) const final;

  bool GetBoolean(const std::string& path, bool default_value) const final;

  int GetInteger(const std::string& path, int default_value) const final;

  std::string GetString(const std::string& path,
                        const std::string& default_value) const final;

 private:
  explicit CameraConfigImpl(base::Value config);
  friend std::unique_ptr<CameraConfig> CameraConfig::Create(
      const std::string& config_path_string);

  base::Value config_;
};

}  // namespace cros

#endif  // CAMERA_COMMON_UTILS_CAMERA_CONFIG_IMPL_H_
