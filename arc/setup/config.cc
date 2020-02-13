// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/setup/config.h"

#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

namespace arc {

namespace {

// Performs a best-effort conversion of the input string to a boolean type,
// setting |*out| to the result of the conversion.  Returns true for successful
// conversions.
bool StringToBool(const std::string str, bool* out) {
  if (str == "0" || base::EqualsCaseInsensitiveASCII(str, "false")) {
    *out = false;
    return true;
  }
  if (str == "1" || base::EqualsCaseInsensitiveASCII(str, "true")) {
    *out = true;
    return true;
  }
  return false;
}

}  // namespace

Config::Config(const base::FilePath& config_json,
               std::unique_ptr<base::Environment> config_env)
    : env_(std::move(config_env)) {
  if (!config_json.empty())
    CHECK(ParseJsonFile(config_json));
}

Config::~Config() = default;

bool Config::GetString(base::StringPiece name, std::string* out) const {
  base::Value* config = FindConfig(name);
  return config ? config->GetAsString(out)
                : env_->GetVar(name.as_string().c_str(), out);
}

bool Config::GetInt(base::StringPiece name, int* out) const {
  base::Value* config = FindConfig(name);
  if (config)
    return config->GetAsInteger(out);
  std::string env_str;
  return env_->GetVar(name.as_string().c_str(), &env_str) &&
         base::StringToInt(env_str, out);
}

bool Config::GetBool(base::StringPiece name, bool* out) const {
  base::Value* config = FindConfig(name);
  if (config)
    return config->GetAsBoolean(out);
  std::string env_str;
  return env_->GetVar(name.as_string().c_str(), &env_str) &&
         StringToBool(env_str, out);
}

std::string Config::GetStringOrDie(base::StringPiece name) const {
  std::string ret;
  CHECK(GetString(name, &ret)) << name;
  return ret;
}

int Config::GetIntOrDie(base::StringPiece name) const {
  int ret;
  CHECK(GetInt(name, &ret)) << name;
  return ret;
}

bool Config::GetBoolOrDie(base::StringPiece name) const {
  bool ret;
  CHECK(GetBool(name, &ret)) << name;
  return ret;
}

bool Config::ParseJsonFile(const base::FilePath& config_json) {
  std::string json_str;
  if (!base::ReadFileToString(config_json, &json_str)) {
    PLOG(ERROR) << "Failed to read json string from " << config_json.value();
    return false;
  }

  std::string error_msg;
  std::unique_ptr<base::Value> value = base::JSONReader::ReadAndReturnError(
      json_str, base::JSON_PARSE_RFC, nullptr /* error_code_out */, &error_msg);
  if (!value) {
    LOG(ERROR) << "Failed to parse json: " << error_msg;
    return false;
  }

  const base::DictionaryValue* dict = nullptr;
  if (!value->GetAsDictionary(&dict)) {
    LOG(ERROR) << "Failed to read json as dictionary";
    return false;
  }

  for (base::DictionaryValue::Iterator it(*dict); !it.IsAtEnd(); it.Advance()) {
    if (!json_.emplace(it.key(), it.value().CreateDeepCopy()).second) {
      LOG(ERROR) << "The config " << it.key() << " appeared twice in the file.";
      return false;
    }
  }
  return true;
}

base::Value* Config::FindConfig(base::StringPiece name) const {
  auto it = json_.find(name.as_string());
  if (it == json_.end())
    return nullptr;
  return it->second.get();
}

}  // namespace arc
