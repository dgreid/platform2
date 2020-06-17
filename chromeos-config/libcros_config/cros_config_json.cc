// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Libary to provide access to the Chrome OS master configuration in YAML / JSON
// format

#include "chromeos-config/libcros_config/cros_config_json.h"

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include "chromeos-config/libcros_config/cros_config.h"
#include "chromeos-config/libcros_config/identity.h"
#include "chromeos-config/libcros_config/identity_arm.h"
#include "chromeos-config/libcros_config/identity_x86.h"

namespace brillo {

constexpr char CrosConfigJson::kRootName[];
constexpr char CrosConfigJson::kConfigListName[];

CrosConfigJson::CrosConfigJson() : config_dict_(nullptr) {}

CrosConfigJson::~CrosConfigJson() {}

bool CrosConfigJson::SelectConfigByIdentityInternal(
    const CrosConfigIdentity& identity) {
  if (!json_config_.is_dict())
    return false;

  const base::Value* chromeos = json_config_.FindDictKey(kRootName);
  if (!chromeos)
    return false;

  const base::Value* configs_list = chromeos->FindListKey(kConfigListName);
  if (!configs_list)
    return false;

  const std::string& find_whitelabel_name = identity.GetVpdId();
  const int find_sku_id = identity.GetSkuId();

  for (size_t i = 0; i < configs_list->GetList().size(); ++i) {
    const base::Value& config_dict = configs_list->GetList()[i];
    if (!config_dict.is_dict())
      continue;

    const base::Value* identity_dict = config_dict.FindDictKey("identity");
    if (!identity_dict)
      continue;

    // Check SMBIOS name matches (x86) or dt-compatible (arm)
    if (!identity.PlatformIdentityMatch(*identity_dict))
      continue;

    // Check that either the SKU is less than zero, or the current
    // entry has a matching SKU id. If sku-id is not defined in the
    // identity dictionary, this entry will match any SKU id.
    if (find_sku_id != kDefaultSkuId) {
      base::Optional<int> current_sku_id = identity_dict->FindIntKey("sku-id");
      if (current_sku_id && current_sku_id != find_sku_id)
        continue;
    }

    // Currently, the find_whitelabel_name can be either the
    // whitelabel-tag or the customization-id.
    const std::string* current_vpd_tag =
        identity_dict->FindStringKey("whitelabel-tag");
    if (!current_vpd_tag)
      current_vpd_tag = identity_dict->FindStringKey("customization-id");
    if (!current_vpd_tag)
      current_vpd_tag = &base::EmptyString();
    if (*current_vpd_tag != find_whitelabel_name)
      continue;

    // SMBIOS name matches/dt-compatible, SKU matches, and VPD tag
    // matches. This is the config.
    config_dict_ = &config_dict;
    device_index_ = i;
    return true;
  }
  return false;
}

bool CrosConfigJson::SelectConfigByIdentity(
    const CrosConfigIdentity& identity) {
  if (!SelectConfigByIdentityInternal(identity)) {
    CROS_CONFIG_LOG(ERROR) << "Failed to find config for "
                           << identity.DebugString();
    return false;
  }
  inited_ = true;
  return true;
}

bool CrosConfigJson::GetString(const std::string& path,
                               const std::string& property,
                               std::string* val_out) {
  if (!InitCheck()) {
    return false;
  }

  if (path.empty()) {
    LOG(ERROR) << "Path must be specified";
    return false;
  }

  if (path[0] != '/') {
    LOG(ERROR) << "Path must start with / specifying the root node";
    return false;
  }

  const base::Value* attr_dict = config_dict_;

  if (path.length() > 1) {
    std::string path_no_root = path.substr(1);
    for (const auto& path :
         base::SplitStringPiece(path_no_root, "/", base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_ALL)) {
      attr_dict = attr_dict->FindDictKey(path);
      if (!attr_dict) {
        CROS_CONFIG_LOG(ERROR) << "Failed to find path: " << path;
        return false;
      }
    }
  }

  const base::Value* value = attr_dict->FindKey(property);
  if (!value)
    return false;
  switch (value->type()) {
    case base::Value::Type::STRING:
      val_out->assign(value->GetString());
      return true;
    case base::Value::Type::INTEGER:
      val_out->assign(std::to_string(value->GetInt()));
      return true;
    case base::Value::Type::BOOLEAN:
      val_out->assign(value->GetBool() ? "true" : "false");
      return true;
    default:
      return false;
  }
}

bool CrosConfigJson::GetDeviceIndex(int* device_index_out) {
  if (!InitCheck()) {
    return false;
  }
  *device_index_out = device_index_;
  return true;
}

bool CrosConfigJson::ReadConfigFile(const base::FilePath& filepath) {
  std::string json_data;
  if (!base::ReadFileToString(filepath, &json_data)) {
    CROS_CONFIG_LOG(ERROR) << "Could not read file " << filepath.MaybeAsASCII();
    return false;
  }
  auto json_root = base::JSONReader::ReadAndReturnValueWithError(
      json_data, base::JSON_PARSE_RFC);
  if (!json_root.value) {
    CROS_CONFIG_LOG(ERROR) << "Fail to parse config.json: "
                           << json_root.error_message;
    return false;
  }
  json_config_ = std::move(json_root.value.value());

  return true;
}

}  // namespace brillo
