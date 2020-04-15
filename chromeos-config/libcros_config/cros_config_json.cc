// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Libary to provide access to the Chrome OS master configuration in YAML / JSON
// format

#include "chromeos-config/libcros_config/cros_config_json.h"

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/values.h>
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
  const base::DictionaryValue* root_dict;
  if (!json_config_->GetAsDictionary(&root_dict))
    return false;

  const base::DictionaryValue* chromeos;
  if (!root_dict->GetDictionary(kRootName, &chromeos))
    return false;

  const base::ListValue* configs_list;
  if (!chromeos->GetList(kConfigListName, &configs_list))
    return false;

  const std::string& find_whitelabel_name = identity.GetVpdId();
  const int find_sku_id = identity.GetSkuId();

  for (size_t i = 0; i < configs_list->GetSize(); ++i) {
    const base::DictionaryValue* config_dict;
    if (!configs_list->GetDictionary(i, &config_dict))
      continue;

    const base::DictionaryValue* identity_dict;
    if (!config_dict->GetDictionary("identity", &identity_dict))
      continue;

    // Check SMBIOS name matches (x86) or dt-compatible (arm)
    if (!identity.PlatformIdentityMatch(*identity_dict))
      continue;

    // Check that either the SKU is less than zero, or the current
    // entry has a matching SKU id. If sku-id is not defined in the
    // identity dictionary, this entry will match any SKU id.
    int current_sku_id;
    if (find_sku_id != kDefaultSkuId &&
        identity_dict->GetInteger("sku-id", &current_sku_id) &&
        current_sku_id != find_sku_id)
      continue;

    // Currently, the find_whitelabel_name can be either the
    // whitelabel-tag or the customization-id.
    std::string current_vpd_tag;
    identity_dict->GetString("whitelabel-tag", &current_vpd_tag);
    if (current_vpd_tag.empty()) {
      identity_dict->GetString("customization-id", &current_vpd_tag);
    }
    if (current_vpd_tag != find_whitelabel_name)
      continue;

    // SMBIOS name matches/dt-compatible, SKU matches, and VPD tag
    // matches. This is the config.
    config_dict_ = config_dict;
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

  bool valid_path = true;
  const base::DictionaryValue* attr_dict = config_dict_;

  if (path.length() > 1) {
    std::vector<std::string> path_tokens = base::SplitString(
        path.substr(1), "/", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    for (const std::string& path : path_tokens) {
      valid_path = attr_dict->GetDictionary(path, &attr_dict);
      if (!valid_path) {
        CROS_CONFIG_LOG(ERROR) << "Failed to find path: " << path;
        break;
      }
    }
  }

  if (valid_path) {
    std::string value;
    if (attr_dict->GetString(property, &value)) {
      val_out->assign(value);
      return true;
    }

    int int_value;
    if (attr_dict->GetInteger(property, &int_value)) {
      val_out->assign(std::to_string(int_value));
      return true;
    }

    bool bool_value;
    if (attr_dict->GetBoolean(property, &bool_value)) {
      if (bool_value) {
        val_out->assign("true");
      } else {
        val_out->assign("false");
      }
      return true;
    }
  }
  return false;
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
  std::string error_msg;
  // TODO(crbug.com/1054279): use base::JSONReader::ReadAndReturnValueWithError
  // after uprev to r680000.
  json_config_ = base::JSONReader::ReadAndReturnErrorDeprecated(
      json_data, base::JSON_PARSE_RFC, nullptr /* error_code_out */, &error_msg,
      nullptr /* error_line_out */, nullptr /* error_column_out */);
  if (!json_config_) {
    CROS_CONFIG_LOG(ERROR) << "Fail to parse config.json: " << error_msg;
    return false;
  }

  return true;
}

}  // namespace brillo
