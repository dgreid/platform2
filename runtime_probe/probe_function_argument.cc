// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include "runtime_probe/probe_function.h"
#include "runtime_probe/probe_function_argument.h"

namespace runtime_probe {

#define _DEFINE_PARSE_ARGUMENT(type, GetType)                             \
  template <>                                                             \
  bool ParseArgumentInternal<type>(const char* function_name,             \
                                   const char* member_name, type* member, \
                                   const base::Value& value) {            \
    if (!value.is_##type()) {                                             \
      LOG(ERROR) << function_name << ": `" << member_name                 \
                 << "` should be " #type;                                 \
      return false;                                                       \
    }                                                                     \
    *member = value.GetType();                                            \
    return true;                                                          \
  }

using std::string;
_DEFINE_PARSE_ARGUMENT(string, GetString);
_DEFINE_PARSE_ARGUMENT(bool, GetBool);
_DEFINE_PARSE_ARGUMENT(double, GetDouble);
_DEFINE_PARSE_ARGUMENT(int, GetInt);

template <>
bool ParseArgumentInternal<std::vector<std::string>>(
    const char* function_name,
    const char* member_name,
    std::vector<std::string>* member,
    const base::Value& value) {
  if (!value.is_list()) {
    LOG(ERROR) << "failed to parse " << value << " as a list of string.";
    return false;
  }

  std::vector<std::string> tmp;

  for (const auto& v : value.GetList()) {
    if (!v.is_string()) {
      LOG(ERROR) << "failed to parse " << value << " as a list of string.";
      return false;
    }

    tmp.push_back(v.GetString());
  }
  member->swap(tmp);
  return true;
}

template <>
bool ParseArgumentInternal<std::vector<std::unique_ptr<ProbeFunction>>>(
    const char* function_name,
    const char* member_name,
    std::vector<std::unique_ptr<ProbeFunction>>* member,
    const base::Value& value) {
  if (!value.is_list()) {
    LOG(ERROR) << "failed to parse " << value
               << " as a list of probe functions.";
    return false;
  }

  std::vector<std::unique_ptr<ProbeFunction>> tmp;

  for (const auto& v : value.GetList()) {
    auto ptr = runtime_probe::ProbeFunction::FromValue(v);
    if (!ptr) {
      LOG(ERROR) << "failed to parse " << value
                 << " as a list of probe functions.";
      return false;
    }

    tmp.push_back(std::move(ptr));
  }

  member->swap(tmp);
  return true;
}

}  // namespace runtime_probe
