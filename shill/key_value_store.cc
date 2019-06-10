// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/key_value_store.h"

#include <base/stl_util.h>

#include "shill/logging.h"

using std::map;
using std::string;
using std::vector;

namespace shill {

KeyValueStore::KeyValueStore() = default;

void KeyValueStore::Clear() {
  properties_.clear();
}

bool KeyValueStore::IsEmpty() {
  return properties_.empty();
}

void KeyValueStore::CopyFrom(const KeyValueStore& b) {
  properties_ = b.properties_;
}

bool KeyValueStore::operator==(const KeyValueStore& rhs) const {
  return properties_ == rhs.properties_;
}

bool KeyValueStore::operator!=(const KeyValueStore& rhs) const {
  return properties_ != rhs.properties_;
}

bool KeyValueStore::ContainsBool(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)->second.IsTypeCompatible<bool>();
}

bool KeyValueStore::ContainsBools(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)->second.IsTypeCompatible<vector<bool>>();
}

bool KeyValueStore::ContainsByteArrays(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)
             ->second.IsTypeCompatible<vector<vector<uint8_t>>>();
}

bool KeyValueStore::ContainsInt(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)->second.IsTypeCompatible<int32_t>();
}

bool KeyValueStore::ContainsInts(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)->second.IsTypeCompatible<vector<int32_t>>();
}

bool KeyValueStore::ContainsInt16(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)->second.IsTypeCompatible<int16_t>();
}

bool KeyValueStore::ContainsInt64(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)->second.IsTypeCompatible<int64_t>();
}

bool KeyValueStore::ContainsInt64s(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)->second.IsTypeCompatible<vector<int64_t>>();
}

bool KeyValueStore::ContainsDouble(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)->second.IsTypeCompatible<double>();
}

bool KeyValueStore::ContainsDoubles(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)->second.IsTypeCompatible<vector<double>>();
}

bool KeyValueStore::ContainsKeyValueStore(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)->second.IsTypeCompatible<KeyValueStore>();
}

bool KeyValueStore::ContainsRpcIdentifier(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)->second.IsTypeCompatible<dbus::ObjectPath>();
}

bool KeyValueStore::ContainsRpcIdentifiers(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)
             ->second.IsTypeCompatible<vector<dbus::ObjectPath>>();
}

bool KeyValueStore::ContainsString(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)->second.IsTypeCompatible<string>();
}

bool KeyValueStore::ContainsStringmap(const std::string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)->second.IsTypeCompatible<Stringmap>();
}

bool KeyValueStore::ContainsStrings(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)->second.IsTypeCompatible<Strings>();
}

bool KeyValueStore::ContainsUint(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)->second.IsTypeCompatible<uint32_t>();
}

bool KeyValueStore::ContainsUint8(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)->second.IsTypeCompatible<uint8_t>();
}

bool KeyValueStore::ContainsUint16(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)->second.IsTypeCompatible<uint16_t>();
}

bool KeyValueStore::ContainsUint8s(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)->second.IsTypeCompatible<vector<uint8_t>>();
}

bool KeyValueStore::ContainsUint32s(const string& name) const {
  return base::ContainsKey(properties_, name) &&
         properties_.find(name)->second.IsTypeCompatible<vector<uint32_t>>();
}

bool KeyValueStore::ContainsVariant(const string& name) const {
  return base::ContainsKey(properties_, name);
}

bool KeyValueStore::GetBool(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.IsTypeCompatible<bool>())
      << "for bool property " << name;
  return it->second.Get<bool>();
}

const vector<bool>& KeyValueStore::GetBools(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.IsTypeCompatible<vector<bool>>())
      << "for bools property " << name;
  return it->second.Get<vector<bool>>();
}

const vector<vector<uint8_t>>& KeyValueStore::GetByteArrays(
    const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() &&
        it->second.IsTypeCompatible<vector<vector<uint8_t>>>())
      << "for byte arrays property " << name;
  return it->second.Get<vector<vector<uint8_t>>>();
}

int32_t KeyValueStore::GetInt(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.IsTypeCompatible<int32_t>())
      << "for int property " << name;
  return it->second.Get<int32_t>();
}

const vector<int32_t>& KeyValueStore::GetInts(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() &&
        it->second.IsTypeCompatible<vector<int32_t>>())
      << "for ints property " << name;
  return it->second.Get<vector<int32_t>>();
}

int16_t KeyValueStore::GetInt16(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.IsTypeCompatible<int16_t>())
      << "for int16 property " << name;
  return it->second.Get<int16_t>();
}

int64_t KeyValueStore::GetInt64(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.IsTypeCompatible<int64_t>())
      << "for int64 property " << name;
  return it->second.Get<int64_t>();
}

const vector<int64_t>& KeyValueStore::GetInt64s(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() &&
        it->second.IsTypeCompatible<vector<int64_t>>())
      << "for int64s property " << name;
  return it->second.Get<vector<int64_t>>();
}

double KeyValueStore::GetDouble(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.IsTypeCompatible<double>())
      << "for double property " << name;
  return it->second.Get<double>();
}

const vector<double>& KeyValueStore::GetDoubles(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() &&
        it->second.IsTypeCompatible<vector<double>>())
      << "for doubles property " << name;
  return it->second.Get<vector<double>>();
}

const KeyValueStore& KeyValueStore::GetKeyValueStore(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.IsTypeCompatible<KeyValueStore>())
      << "for key value store property " << name;
  return it->second.Get<KeyValueStore>();
}

const RpcIdentifier& KeyValueStore::GetRpcIdentifier(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() &&
        it->second.IsTypeCompatible<dbus::ObjectPath>())
      << "for rpc identifier property " << name;
  return it->second.Get<dbus::ObjectPath>();
}

const RpcIdentifiers& KeyValueStore::GetRpcIdentifiers(
    const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() &&
        it->second.IsTypeCompatible<vector<dbus::ObjectPath>>())
      << "for rpc identifier property " << name;
  return it->second.Get<vector<dbus::ObjectPath>>();
}

const string& KeyValueStore::GetString(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.IsTypeCompatible<string>())
      << "for string property " << name;
  return it->second.Get<string>();
}

const map<string, string>& KeyValueStore::GetStringmap(
    const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.IsTypeCompatible<Stringmap>())
      << "for stringmap property " << name;
  return it->second.Get<Stringmap>();
}

const vector<string>& KeyValueStore::GetStrings(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.IsTypeCompatible<Strings>())
      << "for strings property " << name;
  return it->second.Get<Strings>();
}

uint32_t KeyValueStore::GetUint(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.IsTypeCompatible<uint32_t>())
      << "for uint32 property " << name;
  return it->second.Get<uint32_t>();
}

uint16_t KeyValueStore::GetUint16(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.IsTypeCompatible<uint16_t>())
      << "for uint16 property " << name;
  return it->second.Get<uint16_t>();
}

uint8_t KeyValueStore::GetUint8(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.IsTypeCompatible<uint8_t>())
      << "for uint8 property " << name;
  return it->second.Get<uint8_t>();
}

const vector<uint8_t>& KeyValueStore::GetUint8s(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() &&
        it->second.IsTypeCompatible<vector<uint8_t>>())
      << "for uint8s property " << name;
  return it->second.Get<vector<uint8_t>>();
}

const vector<uint32_t>& KeyValueStore::GetUint32s(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() &&
        it->second.IsTypeCompatible<vector<uint32_t>>())
      << "for uint32s property " << name;
  return it->second.Get<vector<uint32_t>>();
}

const brillo::Any& KeyValueStore::GetVariant(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end());
  return it->second;
}

void KeyValueStore::SetBool(const string& name, bool value) {
  properties_[name] = brillo::Any(value);
}

void KeyValueStore::SetBools(const string& name, const vector<bool>& value) {
  properties_[name] = brillo::Any(value);
}

void KeyValueStore::SetByteArrays(const string& name,
                                  const vector<vector<uint8_t>>& value) {
  properties_[name] = brillo::Any(value);
}

void KeyValueStore::SetInt(const string& name, int32_t value) {
  properties_[name] = brillo::Any(value);
}

void KeyValueStore::SetInts(const string& name, const vector<int32_t>& value) {
  properties_[name] = brillo::Any(value);
}

void KeyValueStore::SetInt16(const string& name, int16_t value) {
  properties_[name] = brillo::Any(value);
}

void KeyValueStore::SetInt64(const string& name, int64_t value) {
  properties_[name] = brillo::Any(value);
}

void KeyValueStore::SetInt64s(const string& name,
                              const vector<int64_t>& value) {
  properties_[name] = brillo::Any(value);
}

void KeyValueStore::SetDouble(const string& name, double value) {
  properties_[name] = brillo::Any(value);
}

void KeyValueStore::SetDoubles(const string& name,
                               const vector<double>& value) {
  properties_[name] = brillo::Any(value);
}

void KeyValueStore::SetKeyValueStore(const string& name,
                                     const KeyValueStore& value) {
  properties_[name] = brillo::Any(value);
}

void KeyValueStore::SetRpcIdentifier(const string& name,
                                     const RpcIdentifier& value) {
  properties_[name] = brillo::Any(dbus::ObjectPath(value));
}

void KeyValueStore::SetRpcIdentifiers(const string& name,
                                      const vector<RpcIdentifier>& value) {
  vector<dbus::ObjectPath> paths;
  for (const auto& rpcid : value) {
    paths.push_back(dbus::ObjectPath(rpcid));
  }
  properties_[name] = brillo::Any(paths);
}

void KeyValueStore::SetString(const string& name, const string& value) {
  properties_[name] = brillo::Any(value);
}

void KeyValueStore::SetStringmap(const string& name,
                                 const map<string, string>& value) {
  properties_[name] = brillo::Any(value);
}

void KeyValueStore::SetStrings(const string& name,
                               const vector<string>& value) {
  properties_[name] = brillo::Any(value);
}

void KeyValueStore::SetUint(const string& name, uint32_t value) {
  properties_[name] = brillo::Any(value);
}

void KeyValueStore::SetUint16(const string& name, uint16_t value) {
  properties_[name] = brillo::Any(value);
}

void KeyValueStore::SetUint8(const string& name, uint8_t value) {
  properties_[name] = brillo::Any(value);
}

void KeyValueStore::SetUint8s(const string& name,
                              const vector<uint8_t>& value) {
  properties_[name] = brillo::Any(value);
}

void KeyValueStore::SetUint32s(const string& name,
                               const vector<uint32_t>& value) {
  properties_[name] = brillo::Any(value);
}

void KeyValueStore::SetVariant(const string& name, const brillo::Any& value) {
  properties_[name] = value;
}

void KeyValueStore::Remove(const string& name) {
  properties_.erase(name);
}

bool KeyValueStore::LookupBool(const string& name, bool default_value) const {
  const auto it(properties_.find(name));
  if (it == properties_.end()) {
    return default_value;
  }
  CHECK(it->second.IsTypeCompatible<bool>()) << "type mismatched";
  return it->second.Get<bool>();
}

int KeyValueStore::LookupInt(const string& name, int default_value) const {
  const auto it(properties_.find(name));
  if (it == properties_.end()) {
    return default_value;
  }
  CHECK(it->second.IsTypeCompatible<int32_t>()) << "type mismatched";
  return it->second.Get<int32_t>();
}

string KeyValueStore::LookupString(const string& name,
                                   const string& default_value) const {
  const auto it(properties_.find(name));
  if (it == properties_.end()) {
    return default_value;
  }
  CHECK(it->second.IsTypeCompatible<string>()) << "type mismatched";
  return it->second.Get<string>();
}

// static.
brillo::VariantDictionary KeyValueStore::ConvertToVariantDictionary(
    const KeyValueStore& in_store) {
  brillo::VariantDictionary out_dict;
  for (const auto& key_value_pair : in_store.properties_) {
    if (key_value_pair.second.IsTypeCompatible<KeyValueStore>()) {
      // Special handling for nested KeyValueStore (convert it to
      // nested brillo::VariantDictionary).
      brillo::VariantDictionary dict = ConvertToVariantDictionary(
          key_value_pair.second.Get<KeyValueStore>());
      out_dict.emplace(key_value_pair.first, dict);
    } else {
      out_dict.insert(key_value_pair);
    }
  }
  return out_dict;
}

// static.
KeyValueStore KeyValueStore::ConvertFromVariantDictionary(
    const brillo::VariantDictionary& in_dict) {
  KeyValueStore out_store;
  for (const auto& key_value_pair : in_dict) {
    if (key_value_pair.second.IsTypeCompatible<brillo::VariantDictionary>()) {
      // Special handling for nested brillo::VariantDictionary (convert it to
      // nested KeyValueStore).
      KeyValueStore store = ConvertFromVariantDictionary(
          key_value_pair.second.Get<brillo::VariantDictionary>());
      out_store.properties_.emplace(key_value_pair.first, store);
    } else {
      out_store.properties_.insert(key_value_pair);
    }
  }
  return out_store;
}

}  // namespace shill
