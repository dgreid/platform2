// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "authpolicy/authpolicy_flags.h"

#include <algorithm>
#include <memory>

#include <base/base64.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/values.h>

#include "authpolicy/log_colors.h"

namespace authpolicy {
namespace {

// Size for alignment of Dump() output.
const size_t kAlignSize = 30;

// Gets a string with kAlignSize - strlen(str) spaces (at least 1).
std::string Align(const char* str) {
  return std::string(kAlignSize - std::min(kAlignSize - 1, strlen(str)), ' ');
}

// Removes the value with the key |name| from |dict|, checks |type| and returns
// the value if the type matches. Prints an error message otherwise.
std::unique_ptr<base::Value> GetValueOfType(base::DictionaryValue* dict,
                                            const char* name,
                                            base::Value::Type type) {
  std::unique_ptr<base::Value> value;
  if (!dict->Remove(name, &value))
    return nullptr;
  if (value->type() != type) {
    LOG(ERROR) << name << " must be a "
               << base::Value::GetTypeName(value->type());
    return nullptr;
  }
  return value;
}

// Metadata used for defining a bool-type flag.
class BoolFlag {
 public:
  using Setter = void (protos::DebugFlags::*)(bool);
  using Getter = bool (protos::DebugFlags::*)() const;
  constexpr BoolFlag(const char* name, Setter setter, Getter getter)
      : name_(name), setter_(setter), getter_(getter) {}

  // Remove the value with key |name_| from |dict| and puts it into |flags|.
  void Handle(protos::DebugFlags* flags, base::DictionaryValue* dict) const {
    std::unique_ptr<base::Value> value =
        GetValueOfType(dict, name_, base::Value::Type::BOOLEAN);
    if (value) {
      bool bool_value = false;
      CHECK(value->GetAsBoolean(&bool_value));
      (flags->*setter_)(bool_value);
    }
  }

  // Prints out the value of this flag.
  void Log(const protos::DebugFlags* flags) const {
    LOG(INFO) << kColorFlags << "  " << name_ << Align(name_)
              << ((flags->*getter_)() ? "ON" : "OFF") << kColorReset;
  }

 private:
  const char* name_;
  Setter setter_;
  Getter getter_;
};

// Metadata used for defining a string-type flag.
class StringFlag {
 public:
  using Setter = void (protos::DebugFlags::*)(const std::string&);
  using Getter = const std::string& (protos::DebugFlags::*)() const;
  constexpr StringFlag(const char* name, Setter setter, Getter getter)
      : name_(name), setter_(setter), getter_(getter) {}

  // Remove the value with key |name_| from |dict| and puts it into |flags|.
  void Handle(protos::DebugFlags* flags, base::DictionaryValue* dict) const {
    std::unique_ptr<base::Value> value =
        GetValueOfType(dict, name_, base::Value::Type::STRING);
    if (value) {
      std::string string_value;
      CHECK(value->GetAsString(&string_value));
      (flags->*setter_)(string_value);
    }
  }

  // Prints out the value of this flag.
  void Log(const protos::DebugFlags* flags) const {
    LOG(INFO) << kColorFlags << "  " << name_ << Align(name_)
              << (flags->*getter_)() << kColorReset;
  }

 private:
  const char* name_;
  Setter setter_;
  Getter getter_;
};

#define DEFINE_FLAG(name) \
  { #name, &protos::DebugFlags::set_##name, &protos::DebugFlags::name }

// Bool flags.
constexpr BoolFlag kBoolFlags[] = {
    DEFINE_FLAG(disable_seccomp),
    DEFINE_FLAG(log_seccomp),
    DEFINE_FLAG(trace_krb5),
    DEFINE_FLAG(log_policy_values),
    DEFINE_FLAG(log_commands),
    DEFINE_FLAG(log_command_output),
    DEFINE_FLAG(log_command_output_on_error),
    DEFINE_FLAG(log_gpo),
    DEFINE_FLAG(disable_anonymizer),
    DEFINE_FLAG(log_status),
    DEFINE_FLAG(log_caches),
};

// String flags.
constexpr StringFlag kStringFlags[] = {
    DEFINE_FLAG(net_log_level),
};

#undef DEFINE_FLAG

}  // namespace

// static
std::string SerializeFlags(const protos::DebugFlags& flags) {
  std::string proto_blob, proto_encoded;
  CHECK(flags.SerializeToString(&proto_blob));
  base::Base64Encode(proto_blob, &proto_encoded);
  return proto_encoded;
}

// static
bool DeserializeFlags(const std::string& proto_encoded,
                      protos::DebugFlags* flags) {
  std::string proto_blob;
  return base::Base64Decode(proto_encoded, &proto_blob) &&
         flags->ParseFromString(proto_blob);
}

void AuthPolicyFlags::SetDefaults(DefaultLevel default_level) {
  // Wipe all flags.
  flags_ = protos::DebugFlags();

  // Set defaults depending on level.
  switch (default_level) {
    case kQuiet:
      break;
    case kTaciturn:
      flags_.set_log_policy_values(true);
      flags_.set_log_commands(true);
      flags_.set_log_gpo(true);
      flags_.set_log_status(true);
      flags_.set_log_caches(true);
      break;
    case kChatty:
      flags_.set_log_policy_values(true);
      flags_.set_log_commands(true);
      flags_.set_log_command_output_on_error(true);
      flags_.set_log_gpo(true);
      flags_.set_log_status(true);
      flags_.set_log_caches(true);
      flags_.set_net_log_level("3");
      break;
    case kVerbose:
      flags_.set_log_policy_values(true);
      flags_.set_log_commands(true);
      flags_.set_log_command_output_on_error(true);
      flags_.set_log_gpo(true);
      flags_.set_log_status(true);
      flags_.set_log_caches(true);
      flags_.set_net_log_level("10");
      flags_.set_log_seccomp(true);
      flags_.set_trace_krb5(true);
      break;
  }
}

bool AuthPolicyFlags::LoadFromJsonFile(const base::FilePath& path) {
  std::string flags_json;
  if (!base::ReadFileToString(path, &flags_json))
    return false;
  LoadFromJsonString(flags_json);
  return true;
}

void AuthPolicyFlags::LoadFromJsonString(const std::string& flags_json) {
  auto root = base::JSONReader::ReadAndReturnValueWithError(
      flags_json, base::JSON_ALLOW_TRAILING_COMMAS);
  base::DictionaryValue* dict = nullptr;
  if (!root.value || !root.value->GetAsDictionary(&dict)) {
    LOG(ERROR) << "Fail to parse flags: "
               << (root.error_message.empty() ? "Invalid JSON"
                                              : root.error_message);
    return;
  }

  // Check bool flags.
  for (const BoolFlag& bool_flag : kBoolFlags)
    bool_flag.Handle(&flags_, dict);

  // Check string flags.
  for (const StringFlag& string_flag : kStringFlags)
    string_flag.Handle(&flags_, dict);

  // Print warnings for other parameters.
  for (base::DictionaryValue::Iterator it(*dict); !it.IsAtEnd(); it.Advance())
    LOG(WARNING) << "Unhandled flag " << it.key();
}

void AuthPolicyFlags::Dump() const {
  LOG(INFO) << kColorFlags << "Debug flags:" << kColorReset;
  for (const BoolFlag& flag : kBoolFlags)
    flag.Log(&flags_);
  for (const StringFlag& flag : kStringFlags)
    flag.Log(&flags_);
}

}  // namespace authpolicy
