/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "runtime_probe/functions/input_device.h"

#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <pcrecpp.h>

namespace runtime_probe {

namespace {
constexpr auto kInputDevicesPath = "/proc/bus/input/devices";

base::Value LoadInputDevices() {
  base::Value results(base::Value::Type::LIST);
  std::string input_devices_str;
  if (!base::ReadFileToString(base::FilePath(kInputDevicesPath),
                              &input_devices_str)) {
    LOG(ERROR) << "Failed to read " << kInputDevicesPath << ".";
    return results;
  }

  base::Value data(base::Value::Type::DICTIONARY);
  pcrecpp::RE kEventPatternRe(R"(event[\d]+)");
  auto input_devices_lines = base::SplitStringPiece(
      input_devices_str, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  for (const auto& line : input_devices_lines) {
    if (line.length() > 0) {
      const auto prefix = line[0];
      const auto& content = line.substr(3);
      if (prefix == 'I') {
        if (!data.DictEmpty()) {
          results.GetList().push_back(std::move(data));
          data = base::Value(base::Value::Type::DICTIONARY);
        }
        base::StringPairs keyVals;
        if (!base::SplitStringIntoKeyValuePairs(content, '=', ' ', &keyVals)) {
          LOG(ERROR) << "Failed to parse input devices (I).";
          return base::Value(base::Value::Type::LIST);
        }
        for (const auto& keyVal : keyVals) {
          base::StringPiece key, value;
          std::tie(key, value) = keyVal;
          data.SetKey(base::ToLowerASCII(key), base::Value(value));
        }
      } else if (prefix == 'N' || prefix == 'S') {
        base::StringPairs keyVals;
        if (!base::SplitStringIntoKeyValuePairs(content, '=', '\n', &keyVals)) {
          LOG(ERROR) << "Failed to parse input devices (N/S).";
          return base::Value(base::Value::Type::LIST);
        }
        base::StringPiece key, value;
        std::tie(key, value) = keyVals[0];
        data.SetKey(base::ToLowerASCII(key),
                    base::Value(base::TrimString(value, "\"", base::TRIM_ALL)));
      } else if (prefix == 'H') {
        base::StringPairs keyVals;
        if (!base::SplitStringIntoKeyValuePairs(content, '=', '\n', &keyVals)) {
          LOG(ERROR) << "Failed to parse input devices (H).";
          return base::Value(base::Value::Type::LIST);
        }
        base::StringPiece value = keyVals[0].second;
        const auto& handlers = base::SplitStringPiece(
            value, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
        for (const auto& handler : handlers) {
          if (kEventPatternRe.FullMatch(handler.as_string())) {
            data.SetKey("event", base::Value(handler));
            break;
          }
        }
      }
    }
  }
  if (!data.DictEmpty()) {
    results.GetList().push_back(std::move(data));
  }
  return results;
}

}  // namespace

std::unique_ptr<ProbeFunction> InputDeviceFunction::FromDictionaryValue(
    const base::DictionaryValue& dict_value) {
  auto instance = std::make_unique<InputDeviceFunction>();

  if (dict_value.DictSize() != 0) {
    LOG(ERROR) << function_name << " does not take any arguments.";
    return nullptr;
  }
  return instance;
}

InputDeviceFunction::DataType InputDeviceFunction::Eval() const {
  DataType result;

  auto json_output = InvokeHelperToJSON();
  if (!json_output) {
    LOG(ERROR) << "Failed to invoke helper to retrieve sysfs results.";
    return result;
  }

  auto helper_results = std::move(*json_output);
  if (!helper_results.is_list()) {
    return result;
  }
  for (auto& helper_result : helper_results.GetList()) {
    result.push_back(
        std::move(static_cast<base::DictionaryValue&>(helper_result)));
  }
  return result;
}

int InputDeviceFunction::EvalInHelper(std::string* output) const {
  auto results = LoadInputDevices();

  if (!base::JSONWriter::Write(results, output)) {
    LOG(ERROR) << "Failed to serialize usb probed result to json string";
    return -1;
  }
  return 0;
}

}  // namespace runtime_probe
