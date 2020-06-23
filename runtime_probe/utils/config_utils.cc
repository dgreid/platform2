// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/hash/sha1.h>
#include <base/json/json_reader.h>
#include <base/strings/string_number_conversions.h>
#include <base/system/sys_info.h>
#include <chromeos-config/libcros_config/cros_config.h>
#include <vboot/crossystem.h>

#include "runtime_probe/utils/config_utils.h"

namespace {

const char kCrosConfigModelNamePath[] = "/";
const char kCrosConfigModelNameKey[] = "name";
const char kRuntimeProbeConfigDir[] = "/etc/runtime_probe";
const char kRuntimeProbeConfigName[] = "probe_config.json";

void GetModelName(std::string* model_name) {
  auto cros_config = std::make_unique<brillo::CrosConfig>();

  if (cros_config->Init() &&
      cros_config->GetString(kCrosConfigModelNamePath, kCrosConfigModelNameKey,
                             model_name))
    return;

  // Fallback to sys_info.
  *model_name = base::SysInfo::GetLsbReleaseBoard();
}

std::string GetPathOfRootfsProbeConfig() {
  std::string model_name;
  const auto default_config =
      base::FilePath{kRuntimeProbeConfigDir}.Append(kRuntimeProbeConfigName);

  GetModelName(&model_name);
  auto config_path = base::FilePath{kRuntimeProbeConfigDir}
                         .Append(model_name)
                         .Append(kRuntimeProbeConfigName);
  if (base::PathExists(config_path))
    return config_path.value();

  VLOG(1) << "Model specific probe config " << config_path.value()
          << " doesn't exist";

  return default_config.value();
}

std::string GetProbeConfigSHA1Hash(const std::string& content) {
  const auto& hash_val = base::SHA1HashString(content);
  return base::HexEncode(hash_val.data(), hash_val.size());
}
}  // namespace

namespace runtime_probe {

base::Optional<ProbeConfigData> ParseProbeConfig(
    const std::string& config_file_path) {
  std::string config_json;
  if (!base::ReadFileToString(base::FilePath(config_file_path), &config_json)) {
    LOG(ERROR) << "Config file doesn't exist. "
               << "Input config file path is: " << config_file_path;
    return base::nullopt;
  }
  const auto probe_config_sha1_hash = GetProbeConfigSHA1Hash(config_json);
  LOG(INFO) << "SHA1 hash of probe config read from " << config_file_path
            << ": " << probe_config_sha1_hash;

  auto json_val = base::JSONReader::Read(config_json, base::JSON_PARSE_RFC);
  if (!json_val || !json_val->is_dict()) {
    LOG(ERROR) << "Failed to parse ProbeConfig from : [" << config_file_path
               << "]\nInput JSON string is:\n"
               << config_json;
    return base::nullopt;
  }
  return ProbeConfigData{.config = std::move(*json_val),
                         .sha1_hash = std::move(probe_config_sha1_hash)};
}

bool GetProbeConfigPath(std::string* probe_config_path,
                        const std::string& probe_config_path_from_cli) {
  // Caller not assigned. Using default one in rootfs.
  if (probe_config_path_from_cli.empty()) {
    VLOG(1) << "No config_file_path specified, picking default config.";
    *probe_config_path = GetPathOfRootfsProbeConfig();
    VLOG(1) << "Selected config file: " << *probe_config_path;
    return true;
  }

  // Caller assigned, check permission.
  if (VbGetSystemPropertyInt("cros_debug") != 1) {
    LOG(ERROR) << "Arbitrary ProbeConfig is only allowed with cros_debug=1";
    return false;
  }

  *probe_config_path = probe_config_path_from_cli;
  return true;
}

}  // namespace runtime_probe
