/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <memory>
#include <utility>

#include <base/values.h>

#include "runtime_probe/component_category.h"

namespace runtime_probe {

std::unique_ptr<ComponentCategory> ComponentCategory::FromValue(
    const std::string& category_name, const base::Value& dv) {
  if (!dv.is_dict()) {
    LOG(ERROR) << "ComponentCategory::FromValue takes a dictionary as"
               << " parameter";
    return nullptr;
  }

  std::unique_ptr<ComponentCategory> instance{new ComponentCategory()};
  instance->category_name_ = category_name;

  for (const auto& entry : dv.DictItems()) {
    const auto& component_name = entry.first;
    const auto& value = entry.second;
    auto probe_statement = ProbeStatement::FromValue(component_name, value);
    if (!probe_statement) {
      LOG(ERROR) << "Component " << component_name
                 << " doesn't contain a valid probe statement.";
      return nullptr;
    }
    instance->component_[component_name] = std::move(probe_statement);
  }

  return instance;
}

base::Value ComponentCategory::Eval() const {
  base::Value::ListStorage results;

  for (const auto& entry : component_) {
    const auto& component_name = entry.first;
    const auto& probe_statement = entry.second;
    for (auto& probe_statement_dv : probe_statement->Eval()) {
      base::Value result(base::Value::Type::DICTIONARY);
      result.SetStringKey("name", component_name);
      result.SetKey("values", std::move(probe_statement_dv));
      auto information_dv = probe_statement->GetInformation();
      if (information_dv)
        result.SetKey("information", std::move(*information_dv));
      results.push_back(std::move(result));
    }
  }

  return base::Value(std::move(results));
}

}  // namespace runtime_probe
