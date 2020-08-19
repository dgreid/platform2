// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/values.h>
#include <brillo/map_utils.h>

#include "runtime_probe/component_category.h"
#include "runtime_probe/probe_config.h"

namespace runtime_probe {

std::unique_ptr<ProbeConfig> ProbeConfig::FromValue(const base::Value& dv) {
  if (!dv.is_dict()) {
    LOG(ERROR) << "ProbeConfig::FromValue takes a dictionary as parameter";
    return nullptr;
  }

  std::unique_ptr<ProbeConfig> instance{new ProbeConfig()};

  for (const auto& entry : dv.DictItems()) {
    const auto& category_name = entry.first;
    const auto& value = entry.second;
    auto category = ComponentCategory::FromValue(category_name, value);
    if (!category) {
      LOG(ERROR) << "Category " << category_name
                 << " doesn't contain a valid probe statement.";
      return nullptr;
    }
    instance->category_[category_name] = std::move(category);
  }

  return instance;
}

base::Value ProbeConfig::Eval() const {
  return Eval(brillo::GetMapKeysAsVector(category_));
}

base::Value ProbeConfig::Eval(const std::vector<std::string>& category) const {
  base::Value result(base::Value::Type::DICTIONARY);

  for (const auto& c : category) {
    auto it = category_.find(c);
    if (it == category_.end()) {
      LOG(ERROR) << "Category " << c << " is not defined";
      continue;
    }

    result.SetKey(c, it->second->Eval());
  }

  return result;
}

}  // namespace runtime_probe
