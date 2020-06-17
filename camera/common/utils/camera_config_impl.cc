/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <iomanip>
#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/values.h>

#include "common/utils/camera_config_impl.h"
#include "cros-camera/common.h"

namespace cros {

// static
std::unique_ptr<CameraConfig> CameraConfig::Create(
    const std::string& config_path_string) {
  const base::FilePath config_path(config_path_string);

  if (!base::PathExists(config_path)) {
    // If there is no config file it means that all are default values.
    base::Value dict(base::Value::Type::DICTIONARY);
    return base::WrapUnique(new CameraConfigImpl(std::move(dict)));
  }

  std::string content;
  if (!base::ReadFileToString(config_path, &content)) {
    LOGF(ERROR) << "Failed to read camera configuration file:"
                << config_path_string;
    return nullptr;
  }

  auto result = base::JSONReader::ReadAndReturnValueWithError(content, 0);
  if (!result.value) {
    LOGF(ERROR) << "Invalid JSON format of camera configuration file:"
                << result.error_message;
    return nullptr;
  }

  if (!result.value->is_dict()) {
    LOGF(ERROR) << "value of JSON result is not a dictionary";
    return nullptr;
  }

  return base::WrapUnique(new CameraConfigImpl(std::move(*result.value)));
}

CameraConfigImpl::CameraConfigImpl(base::Value config) {
  config_ = std::move(config);
}

CameraConfigImpl::~CameraConfigImpl() {}

bool CameraConfigImpl::HasKey(const std::string& key) const {
  return config_.FindKey(key) != nullptr;
}

bool CameraConfigImpl::GetBoolean(const std::string& path,
                                  bool default_value) const {
  base::Optional<bool> result = config_.FindBoolPath(path);
  return result.has_value() ? result.value() : default_value;
}

int CameraConfigImpl::GetInteger(const std::string& path,
                                 int default_value) const {
  base::Optional<int> result = config_.FindIntPath(path);
  return result.has_value() ? result.value() : default_value;
}

std::string CameraConfigImpl::GetString(
    const std::string& path, const std::string& default_value) const {
  const std::string* result = config_.FindStringPath(path);
  return (result != nullptr) ? *result : default_value;
}

}  // namespace cros
