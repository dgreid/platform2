// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
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

#include "runtime_probe/probe_config_loader_impl.h"
#include "runtime_probe/system_property_impl.h"

namespace runtime_probe {

namespace {

constexpr char kUsrLocal[] = "usr/local";

std::string GetProbeConfigSHA1Hash(const std::string& content) {
  const auto& hash_val = base::SHA1HashString(content);
  return base::HexEncode(hash_val.data(), hash_val.size());
}

base::Optional<ProbeConfigData> LoadProbeConfig(
    const base::FilePath& file_path) {
  DVLOG(2) << "LoadProbeConfig: " << file_path;
  std::string config_json;
  if (!base::ReadFileToString(file_path, &config_json)) {
    DVLOG(2) << "Failed to read probe config";
    return base::nullopt;
  }
  const auto probe_config_sha1_hash = GetProbeConfigSHA1Hash(config_json);
  DVLOG(3) << "SHA1 hash of probe config: " << probe_config_sha1_hash;

  auto json_val = base::JSONReader::Read(config_json, base::JSON_PARSE_RFC);
  if (!json_val || !json_val->is_dict()) {
    DVLOG(2) << "Failed to parse probe config as JSON.";
    DVLOG(3) << "Input: " << config_json;
    return base::nullopt;
  }

  const auto absolute_path = base::MakeAbsoluteFilePath(file_path);
  return ProbeConfigData{.path = absolute_path,
                         .config = std::move(*json_val),
                         .sha1_hash = std::move(probe_config_sha1_hash)};
}

}  // namespace

ProbeConfigLoaderImpl::ProbeConfigLoaderImpl() : root_("/") {
  auto config = std::make_unique<brillo::CrosConfig>();
  if (config->Init()) {
    cros_config_ = std::move(config);
  }
  system_property_ = std::make_unique<SystemPropertyImpl>();
}

base::Optional<ProbeConfigData> ProbeConfigLoaderImpl::LoadDefault() const {
  for (const auto& file_path : GetDefaultPaths()) {
    auto ret = LoadProbeConfig(file_path);
    if (ret) {
      DVLOG(1) << "Load default config from: " << file_path;
      return ret;
    }
  }
  DVLOG(1) << "Cannot find any default probe configs";
  return base::nullopt;
}

base::Optional<ProbeConfigData> ProbeConfigLoaderImpl::LoadFromFile(
    const base::FilePath& file_path) const {
  if (GetCrosDebug() != 1) {
    LOG(ERROR) << "Arbitrary probe config is only allowed with cros_debug=1";
    return base::nullopt;
  }
  auto ret = LoadProbeConfig(file_path);
  DVLOG(1) << "Load config from: " << file_path;
  return ret;
}

std::vector<base::FilePath> ProbeConfigLoaderImpl::GetDefaultPaths() const {
  std::vector<base::FilePath> file_paths;
  std::string model_name = GetModelName();
  if (GetCrosDebug() == 1) {
    auto probe_config_dir =
        root_.Append(kUsrLocal).Append(kRuntimeProbeConfigDir);
    file_paths.push_back(
        probe_config_dir.Append(model_name).Append(kRuntimeProbeConfigName));
    file_paths.push_back(probe_config_dir.Append(kRuntimeProbeConfigName));
  }
  auto probe_config_dir = root_.Append(kRuntimeProbeConfigDir);
  file_paths.push_back(
      probe_config_dir.Append(model_name).Append(kRuntimeProbeConfigName));
  file_paths.push_back(probe_config_dir.Append(kRuntimeProbeConfigName));
  return file_paths;
}

void ProbeConfigLoaderImpl::SetCrosConfigForTesting(
    std::unique_ptr<brillo::CrosConfigInterface> cros_config) {
  cros_config_ = std::move(cros_config);
}

void ProbeConfigLoaderImpl::SetSystemProertyForTesting(
    std::unique_ptr<SystemProperty> system_property) {
  system_property_ = std::move(system_property);
}

void ProbeConfigLoaderImpl::SetRootForTest(const base::FilePath& root) {
  root_ = root;
}

int ProbeConfigLoaderImpl::GetCrosDebug() const {
  int cros_config;

  if (system_property_->GetInt("cros_debug", &cros_config))
    return cros_config;

  // Fallback to disabled cros_debug.
  return 0;
}

std::string ProbeConfigLoaderImpl::GetModelName() const {
  std::string model_name;

  if (cros_config_->GetString(kCrosConfigModelNamePath, kCrosConfigModelNameKey,
                              &model_name))
    return model_name;

  // Fallback to sys_info.
  return base::SysInfo::GetLsbReleaseBoard();
}

}  // namespace runtime_probe
