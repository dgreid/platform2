/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <memory>

#include <base/values.h>

#include "runtime_probe/probe_statement.h"

namespace runtime_probe {

namespace {

void FilterDictionaryValueByKey(base::DictionaryValue* dv,
                                const std::set<std::string>& keys) {
  std::vector<std::string> keys_to_delete;
  for (base::DictionaryValue::Iterator it{*dv}; !it.IsAtEnd(); it.Advance()) {
    if (keys.find(it.key()) == keys.end()) {
      keys_to_delete.push_back(it.key());
    }
  }
  for (const auto& k : keys_to_delete) {
    dv->Remove(k, nullptr);
  }
}

}  // namespace

std::unique_ptr<ProbeStatement> ProbeStatement::FromDictionaryValue(
    std::string component_name, const base::DictionaryValue& dict_value) {
  std::unique_ptr<ProbeStatement> instance{new ProbeStatement};

  instance->component_name_ = component_name;

  // Parse required field "eval"
  const base::Value* eval_value =
      dict_value.FindKeyOfType("eval", base::Value::Type::DICTIONARY);
  if (!eval_value) {
    LOG(ERROR) << "eval should be a DictionaryValue: " << *eval_value;
    return nullptr;
  }

  instance->eval_ = ProbeFunction::FromValue(*eval_value);
  // Check the required field eval
  if (!instance->eval_) {
    LOG(ERROR) << "Failed to parse " << dict_value << " as ProbeStatement";
    return nullptr;
  }

  // Parse optional field "keys"
  const base::Value* keys_value =
      dict_value.FindKeyOfType("keys", base::Value::Type::LIST);
  if (!keys_value) {
    VLOG(1) << "keys does not exist or is not a ListValue";
  } else {
    for (const auto& v : keys_value->GetList()) {
      // Currently, destroy all previously inserted valid elems
      if (!v.is_string()) {
        LOG(ERROR) << "keys should be a list of string: " << *keys_value;
        instance->key_.clear();
      }
      instance->key_.insert(v.GetString());
    }
  }

  // Parse optional field "expect"
  // TODO(b:121354690): Make expect useful
  const base::DictionaryValue* expect_dict_value;
  if (!dict_value.GetDictionary("expect", &expect_dict_value)) {
    VLOG(1) << "expect does not exist or is not a DictionaryValue";
  } else {
    instance->expect_ =
        ProbeResultChecker::FromDictionaryValue(*expect_dict_value);
    if (!instance->expect_)
      VLOG(1) << "Failed to parse attribute expect: " << *expect_dict_value;
  }

  // Parse optional field "information"
  if (!dict_value.GetDictionary("information", &instance->information_)) {
    VLOG(1) << "information does not exist or is not a DictionaryValue";
  }

  return instance;
}

ProbeFunction::DataType ProbeStatement::Eval() const {
  auto results = eval_->Eval();

  if (!key_.empty()) {
    std::for_each(results.begin(), results.end(), [this](auto& result) {
      FilterDictionaryValueByKey(&result, key_);
    });
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
