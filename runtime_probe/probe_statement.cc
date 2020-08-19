// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <base/values.h>

#include "runtime_probe/probe_statement.h"

namespace runtime_probe {

namespace {

void FilterValueByKey(base::Value* dv, const std::set<std::string>& keys) {
  std::vector<std::string> keys_to_delete;
  for (const auto& entry : dv->DictItems()) {
    if (keys.find(entry.first) == keys.end()) {
      keys_to_delete.push_back(entry.first);
    }
  }
  for (const auto& k : keys_to_delete) {
    dv->RemoveKey(k);
  }
}

}  // namespace

std::unique_ptr<ProbeStatement> ProbeStatement::FromValue(
    std::string component_name, const base::Value& dv) {
  if (!dv.is_dict()) {
    LOG(ERROR) << "ProbeStatement::FromValue takes a dictionary as parameter";
    return nullptr;
  }

  // Parse required field "eval"
  const auto* eval_value = dv.FindDictKey("eval");
  if (!eval_value) {
    LOG(ERROR) << "\"eval\" should be a dictionary: " << *eval_value;
    return nullptr;
  }
  auto function = ProbeFunction::FromValue(*eval_value);
  if (!function) {
    LOG(ERROR) << "Component " << component_name
               << " doesn't contain a valid probe function.";
    return nullptr;
  }
  std::unique_ptr<ProbeStatement> instance{new ProbeStatement()};
  instance->component_name_ = component_name;
  instance->eval_ = std::move(function);

  // Parse optional field "keys"
  const auto* keys_value = dv.FindListKey("keys");
  if (!keys_value) {
    VLOG(3) << "\"keys\" does not exist or is not a list";
  } else {
    for (const auto& v : keys_value->GetList()) {
      // Currently, destroy all previously inserted valid elems
      if (!v.is_string()) {
        LOG(ERROR) << "\"keys\" should be a list of string: " << *keys_value;
        instance->key_.clear();
        break;
      }
      instance->key_.insert(v.GetString());
    }
  }

  // Parse optional field "expect"
  // TODO(b:121354690): Make expect useful
  const auto* expect_value = dv.FindDictKey("expect");
  if (!expect_value) {
    VLOG(3) << "\"expect\" does not exist or is not a dictionary";
  } else {
    auto checker = ProbeResultChecker::FromValue(*expect_value);
    if (!checker) {
      VLOG(1) << "Component " << component_name
              << " doesn't contain a valid checker.";
    } else {
      instance->expect_ = std::move(checker);
    }
  }

  // Parse optional field "information"
  const auto* information = dv.FindDictKey("information");
  if (!information) {
    VLOG(3) << "\"information\" does not exist or is not a dictionary";
  } else {
    instance->information_ = information->Clone();
  }

  return instance;
}

ProbeFunction::DataType ProbeStatement::Eval() const {
  auto results = eval_->Eval();

  if (!key_.empty()) {
    std::for_each(results.begin(), results.end(),
                  [this](auto& result) { FilterValueByKey(&result, key_); });
  }

  if (expect_) {
    // |expect_->Apply| will return false if the probe result is considered
    // invalid.
    // |std::partition| will move failed elements to end of list, |first_fail|
    // will point the the first failed element.
    auto first_failure = std::partition(
        results.begin(), results.end(),
        [this](auto& result) { return expect_->Apply(&result); });
    // Remove failed elements.
    results.erase(first_failure, results.end());
  }

  return results;
}
}  // namespace runtime_probe
