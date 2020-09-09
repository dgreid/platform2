// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/vpd_cached.h"

#include <utility>

#include <base/json/json_writer.h>

#include "runtime_probe/utils/file_utils.h"

namespace runtime_probe {

std::unique_ptr<VPDCached> VPDCached::FromKwargsValue(
    const base::Value& dict_value) {
  if (dict_value.DictSize() != 1) {
    LOG(ERROR) << function_name << " expect 1 arguments.";
    return nullptr;
  }

  auto instance = std::make_unique<VPDCached>();
  bool result = true;

  result &= PARSE_ARGUMENT(vpd_name);

  if (result)
    return instance;
  return nullptr;
}

VPDCached::DataType VPDCached::Eval() const {
  auto json_output = InvokeHelperToJSON();
  if (!json_output) {
    LOG(ERROR) << "Failed to invoke helper to retrieve cached vpd information.";
    return {};
  }
  if (!json_output->is_list()) {
    LOG(ERROR) << "Failed to parse json output as list.";
    return {};
  }

  return DataType(json_output->TakeList());
}
int VPDCached::EvalInHelper(std::string* output) const {
  constexpr char kSysfsVPDCached[] = "/sys/firmware/vpd/ro/";

  std::vector<std::string> allowed_require_keys;
  std::vector<std::string> allowed_optional_keys;

  // sku_number is defined in public partner documentation:
  // https://www.google.com/chromeos/partner/fe/docs/factory/vpd.html#field-sku_number
  // sku_number is allowed to be exposed as stated in b/130322365#c28
  allowed_optional_keys.push_back("sku_number");

  const base::FilePath vpd_ro_path{kSysfsVPDCached};
  const auto dict_value =
      MapFilesToDict(vpd_ro_path, allowed_require_keys, allowed_optional_keys);
  base::Value dict_with_prefix(base::Value::Type::DICTIONARY);

  if (dict_value) {
    auto* vpd_value = dict_value->FindStringKey(vpd_name_);
    if (!vpd_value) {
      LOG(WARNING) << "vpd field " << vpd_name_
                   << " does not exist or is not allowed to be probed.";
    } else {
      // Add vpd_ prefix to every field.
      dict_with_prefix.SetStringKey("vpd_" + vpd_name_, *vpd_value);
    }
  }

  base::Value result(base::Value::Type::LIST);
  if (!dict_with_prefix.DictEmpty()) {
    result.Append(std::move(dict_with_prefix));
  }

  if (!base::JSONWriter::Write(result, output)) {
    LOG(ERROR)
        << "Failed to serialize generic battery probed result to json string";
    return -1;
  }

  return 0;
}

}  // namespace runtime_probe
