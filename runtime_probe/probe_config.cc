/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <memory>

#include <base/values.h>
#include <brillo/map_utils.h>

#include "runtime_probe/component_category.h"
#include "runtime_probe/probe_config.h"

namespace runtime_probe {

std::unique_ptr<ProbeConfig> ProbeConfig::FromDictionaryValue(
    const base::DictionaryValue& dict_value) {
  std::unique_ptr<ProbeConfig> instance{new ProbeConfig};

  for (base::DictionaryValue::Iterator it{dict_value}; !it.IsAtEnd();
       it.Advance()) {
    const auto category = it.key();
    const base::DictionaryValue* components;

    if (!it.value().GetAsDictionary(&components)) {
      LOG(ERROR) << "Category " << category
                 << " is not a DictionaryValue: " << it.value();
      instance = nullptr;
      break;
    }

    instance->category_[category] =
        ComponentCategory::FromDictionaryValue(category, *components);

    if (!instance->category_[category]) {
      instance = nullptr;
      break;
    }
  }

  if (!instance)
    LOG(ERROR) << "Failed to parse " << dict_value << " as ProbeConfig";
  return instance;
}

std::unique_ptr<base::DictionaryValue> ProbeConfig::Eval() const {
  return Eval(brillo::GetMapKeysAsVector(category_));
}

std::unique_ptr<base::DictionaryValue> ProbeConfig::Eval(
    const std::vector<std::string>& category) const {
  std::unique_ptr<base::DictionaryValue> result{new base::DictionaryValue};

  for (const auto& c : category) {
    auto it = category_.find(c);
    if (it == category_.end()) {
      LOG(ERROR) << "Category " << c << " is not defined";
      continue;
    }

    result->Set(c, it->second->Eval());
  }

  return result;
}

}  // namespace runtime_probe
