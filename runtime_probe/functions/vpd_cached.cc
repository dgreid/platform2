// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/vpd_cached.h"

#include <utility>

#include <base/json/json_writer.h>

#include "runtime_probe/utils/file_utils.h"

namespace runtime_probe {

using base::DictionaryValue;
using base::Value;

VPDCached::DataType VPDCached::Eval() const {
  DataType result;
  auto json_output = InvokeHelperToJSON();
  if (!json_output) {
    LOG(ERROR) << "Failed to invoke helper to retrieve cached vpd information.";
    return result;
  }

  if (!json_output->is_list()) {
    LOG(ERROR) << "Failed to parse json output as list.";
    return result;
  }

  for (auto& helper_result : json_output->GetList()) {
    base::DictionaryValue* result_dict = nullptr;
    if (helper_result.GetAsDictionary(&result_dict))
      result.push_back(std::move(*result_dict));
  }
  return result;
}
int VPDCached::EvalInHelper(std::string* output) const {
  constexpr char kSysfsVPDCached[] = "/sys/firmware/vpd/ro/";

  std::vector<std::string> allowed_require_keys;
  std::vector<std::string> allowed_optional_keys;

  // sku_number is defined in public partner documentation:
  // https://www.google.com/chromeos/partner/fe/docs/factory/vpd.html#field-sku_number
  // sku_number is allowed to be exposed as stated in b/130322365#c28
  allowed_optional_keys.push_back("sku_number");

  base::ListValue result{};

  const base::FilePath vpd_ro_path{kSysfsVPDCached};
  const auto dict_value =
      MapFilesToDict(vpd_ro_path, allowed_require_keys, allowed_optional_keys);

  DictionaryValue dict_with_prefix;
  std::string vpd_value;
  if (!dict_value.GetString(vpd_name_, &vpd_value)) {
    LOG(WARNING) << "vpd field " << vpd_name_
                 << " does not exist or is not allowed to be probed.";
  } else {
    // Add vpd_ prefix to every field.
    dict_with_prefix.SetString("vpd_" + vpd_name_, vpd_value);
  }

  if (!dict_with_prefix.empty()) {
    result.Append(dict_with_prefix.CreateDeepCopy());
  }

  if (!base::JSONWriter::Write(result, output)) {
    LOG(ERROR)
        << "Failed to serialize generic battery probed result to json string";
    return -1;
  }

  return 0;
}

}  // namespace runtime_probe
