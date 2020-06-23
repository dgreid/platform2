// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/utils/value_utils.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/values.h>

namespace runtime_probe {

// Append the given |prefix| to each key in the |dict_value|.
void PrependToDVKey(base::Value* dict_value, const std::string& prefix) {
  if (!dict_value->is_dict())
    return;
  if (prefix.empty())
    return;
  std::vector<std::string> original_keys;
  for (const auto& entry : dict_value->DictItems()) {
    original_keys.push_back(entry.first);
  }
  for (const auto& key : original_keys) {
    auto value = dict_value->ExtractKey(key);
    dict_value->SetKey(prefix + key, std::move(*value));
  }
}

bool RenameKey(base::Value* dv,
               const std::string& old_key,
               const std::string& new_key) {
  if (!dv->is_dict())
    return false;
  auto value = dv->ExtractKey(old_key);
  if (!value)
    return false;
  dv->SetKey(new_key, std::move(*value));
  return true;
}

}  // namespace runtime_probe
