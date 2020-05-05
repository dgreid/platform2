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
void PrependToDVKey(base::DictionaryValue* dict_value,
                    const std::string& prefix) {
  if (prefix.empty())
    return;
  std::vector<std::string> original_keys;
  for (base::DictionaryValue::Iterator it(*dict_value); !it.IsAtEnd();
       it.Advance()) {
    original_keys.push_back(it.key());
  }
  for (const auto& key : original_keys) {
    std::unique_ptr<base::Value> tmp;
    dict_value->Remove(key, &tmp);
    dict_value->SetString(prefix + key, tmp->GetString());
  }
}

bool RenameKey(base::Value* dv,
               const std::string& old_key,
               const std::string& new_key) {
  auto value = dv->FindKey(old_key);
  if (!value)
    return false;
  dv->SetKey(new_key, std::move(*value));
  dv->RemoveKey(old_key);
  return true;
}

}  // namespace runtime_probe
