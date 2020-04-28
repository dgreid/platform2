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
    return std::make_unique<CameraConfigImpl>(
        std::make_unique<base::DictionaryValue>());
  }

  std::string content;
  if (!base::ReadFileToString(config_path, &content)) {
    LOGF(ERROR) << "Failed to read camera configuration file:"
                << config_path_string;
    return nullptr;
  }

  int error_code_out;
  std::string error_msg_out;
  auto value = base::JSONReader::ReadAndReturnErrorDeprecated(
      content, 0, &error_code_out, &error_msg_out);
  if (!value) {
    LOGF(ERROR) << "Invalid JSON format of camera configuration file:"
                << error_msg_out;
    return nullptr;
  }

  auto dict = base::DictionaryValue::From(std::move(value));
  if (!dict) {
    LOGF(ERROR) << "value of JSON result is not a dictionary";
    return nullptr;
  }

  return std::make_unique<CameraConfigImpl>(std::move(dict));
}

CameraConfigImpl::CameraConfigImpl(
    std::unique_ptr<base::DictionaryValue> config) {
  config_ = std::move(config);
}

CameraConfigImpl::~CameraConfigImpl() {}

bool CameraConfigImpl::HasKey(const std::string& key) const {
  return config_->HasKey(key);
}

bool CameraConfigImpl::GetBoolean(const std::string& path,
                                  bool default_value) const {
  bool value;
  return config_->GetBoolean(path, &value) ? value : default_value;
}

int CameraConfigImpl::GetInteger(const std::string& path,
                                 int default_value) const {
  int value;
  return config_->GetInteger(path, &value) ? value : default_value;
}

std::string CameraConfigImpl::GetString(
    const std::string& path, const std::string& default_value) const {
  std::string value;
  return config_->GetString(path, &value) ? value : default_value;
}

}  // namespace cros
