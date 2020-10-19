// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

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

constexpr char kCrosConfigModelNamePath[] = "/";
constexpr char kCrosConfigModelNameKey[] = "name";
constexpr char kUsrLocal[] = "usr/local";
constexpr char kRuntimeProbeConfigDir[] = "etc/runtime_probe";
constexpr char kRuntimeProbeConfigName[] = "probe_config.json";

void GetModelName(std::string* model_name) {
  auto cros_config = std::make_unique<brillo::CrosConfig>();

  if (cros_config->Init() &&
      cros_config->GetString(kCrosConfigModelNamePath, kCrosConfigModelNameKey,
                             model_name))
    return;

  // Fallback to sys_info.
  *model_name = base::SysInfo::GetLsbReleaseBoard();
}

bool GetProbeConfigPathByBase(const base::FilePath& root_path,
                              std::string* probe_config_path) {
  probe_config_path->clear();
  std::string model_name;
  GetModelName(&model_name);
  std::vector<base::FilePath> config_paths{
      root_path.Append(kRuntimeProbeConfigDir)
          .Append(model_name)
          .Append(kRuntimeProbeConfigName),
      root_path.Append(kRuntimeProbeConfigDir).Append(kRuntimeProbeConfigName)};

  for (const auto& config_path : config_paths) {
    if (base::PathExists(config_path)) {
      *probe_config_path = config_path.value();
      break;
    }
  }

  return !probe_config_path->empty();
}

std::string GetProbeConfigSHA1Hash(const std::string& content) {
  const auto& hash_val = base::SHA1HashString(content);
  return base::HexEncode(hash_val.data(), hash_val.size());
}

bool IsCrosDebugOn() {
  return VbGetSystemPropertyInt("cros_debug") == 1;
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

bool GetProbeConfigPath(const std::string& probe_config_path_from_cli,
                        std::string* probe_config_path) {
  probe_config_path->clear();
  if (!probe_config_path_from_cli.empty()) {
    if (IsCrosDebugOn()) {
      LOG(ERROR) << "Arbitrary ProbeConfig is only allowed with cros_debug=1";
      return false;
    }
    *probe_config_path = probe_config_path_from_cli;
  } else {
    VLOG(1) << "No config_file_path specified, picking default config.";
    base::FilePath root{"/"};
    if (IsCrosDebugOn())
      GetProbeConfigPathByBase(root.Append(kUsrLocal), probe_config_path);
    if (probe_config_path->empty())
      GetProbeConfigPathByBase(root, probe_config_path);
  }

  VLOG(1) << "Selected config file: " << *probe_config_path;

  return !probe_config_path->empty();
}

}  // namespace runtime_probe
