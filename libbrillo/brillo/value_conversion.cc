// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/value_conversion.h>

#include <string>
#include <vector>

namespace brillo {

bool FromValue(const base::Value& in_value,
               std::unique_ptr<base::ListValue>* out_value) {
  const base::ListValue* list = nullptr;
  if (!in_value.GetAsList(&list))
    return false;
  *out_value = list->CreateDeepCopy();
  return true;
}

bool FromValue(const base::Value& in_value,
               std::unique_ptr<base::DictionaryValue>* out_value) {
  const base::DictionaryValue* dict = nullptr;
  if (!in_value.GetAsDictionary(&dict))
    return false;
  *out_value = dict->CreateDeepCopy();
  return true;
}

std::unique_ptr<base::Value> ToValue(int value) {
  return std::make_unique<base::Value>(value);
}

std::unique_ptr<base::Value> ToValue(bool value) {
  return std::make_unique<base::Value>(value);
}

std::unique_ptr<base::Value> ToValue(double value) {
  return std::make_unique<base::Value>(value);
}

std::unique_ptr<base::Value> ToValue(const char* value) {
  return std::make_unique<base::Value>(value);
}

std::unique_ptr<base::Value> ToValue(const std::string& value) {
  return std::make_unique<base::Value>(value);
}

}  // namespace brillo
