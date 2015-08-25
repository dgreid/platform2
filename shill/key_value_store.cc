// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/key_value_store.h"

#include <base/stl_util.h>

#include "shill/logging.h"

using std::map;
using std::string;
using std::vector;

namespace shill {

KeyValueStore::KeyValueStore() {}

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
  // TODO(zqiu): Replace typeid comparison with chromeos::Any::IsTypeCompatible.
  return ContainsKey(properties_, name) &&
      properties_.find(name)->second.GetType() == typeid(bool);    // NOLINT
}

bool KeyValueStore::ContainsByteArrays(const string& name) const {
  return ContainsKey(properties_, name) &&
      properties_.find(name)->second.GetType() ==
          typeid(vector<vector<uint8_t>>);
}

bool KeyValueStore::ContainsInt(const string& name) const {
  return ContainsKey(properties_, name) &&
      properties_.find(name)->second.GetType() == typeid(int32_t);
}

bool KeyValueStore::ContainsInt16(const string& name) const {
  return ContainsKey(properties_, name) &&
      properties_.find(name)->second.GetType() == typeid(int16_t);
}

bool KeyValueStore::ContainsKeyValueStore(const string& name) const {
  return ContainsKey(properties_, name) &&
      properties_.find(name)->second.GetType() == typeid(KeyValueStore);
}

bool KeyValueStore::ContainsRpcIdentifier(const string& name) const {
  return ContainsKey(properties_, name) &&
      properties_.find(name)->second.GetType() == typeid(dbus::ObjectPath);
}

bool KeyValueStore::ContainsRpcIdentifiers(const string& name) const {
  return ContainsKey(properties_, name) &&
      properties_.find(name)->second.GetType() ==
          typeid(vector<dbus::ObjectPath>);
}

bool KeyValueStore::ContainsString(const string& name) const {
  return ContainsKey(properties_, name) &&
      properties_.find(name)->second.GetType() == typeid(string);
}

bool KeyValueStore::ContainsStringmap(const std::string& name) const {
  return ContainsKey(properties_, name) &&
      properties_.find(name)->second.GetType() == typeid(Stringmap);
}

bool KeyValueStore::ContainsStrings(const string& name) const {
  return ContainsKey(properties_, name) &&
      properties_.find(name)->second.GetType() == typeid(Strings);
}

bool KeyValueStore::ContainsUint(const string& name) const {
  return ContainsKey(properties_, name) &&
      properties_.find(name)->second.GetType() == typeid(uint32_t);
}

bool KeyValueStore::ContainsUint8(const string& name) const {
  return ContainsKey(properties_, name) &&
      properties_.find(name)->second.GetType() == typeid(uint8_t);
}

bool KeyValueStore::ContainsUint16(const string& name) const {
  return ContainsKey(properties_, name) &&
      properties_.find(name)->second.GetType() == typeid(uint16_t);
}

bool KeyValueStore::ContainsUint8s(const string& name) const {
  return ContainsKey(properties_, name) &&
      properties_.find(name)->second.GetType() == typeid(vector<uint8_t>);
}

bool KeyValueStore::ContainsUint32s(const string& name) const {
  return ContainsKey(properties_, name) &&
      properties_.find(name)->second.GetType() == typeid(vector<uint32_t>);
}

bool KeyValueStore::GetBool(const string& name) const {
  const auto it(properties_.find(name));
  // TODO(zqiu): Replace typeid comparison with chromeos::Any::IsTypeCompatible.
  CHECK(it != properties_.end() && it->second.GetType() == typeid(bool))
      << "for bool property " << name;
  return it->second.Get<bool>();
}

const vector<vector<uint8_t>>& KeyValueStore::GetByteArrays(
    const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.GetType() ==
      typeid(vector<vector<uint8_t>>)) << "for byte arrays property " << name;
  return it->second.Get<vector<vector<uint8_t>>>();
}

int32_t KeyValueStore::GetInt(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.GetType() == typeid(int32_t))
      << "for int property " << name;
  return it->second.Get<int32_t>();
}

int16_t KeyValueStore::GetInt16(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.GetType() == typeid(int16_t))
      << "for int16 property " << name;
  return it->second.Get<int16_t>();
}

const KeyValueStore& KeyValueStore::GetKeyValueStore(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.GetType() ==
      typeid(KeyValueStore)) << "for key value store property " << name;
  return it->second.Get<KeyValueStore>();
}

const string& KeyValueStore::GetRpcIdentifier(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.GetType() ==
      typeid(dbus::ObjectPath)) << "for rpc identifier property " << name;
  return it->second.Get<dbus::ObjectPath>().value();
}

vector<string> KeyValueStore::GetRpcIdentifiers(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.GetType() ==
      typeid(vector<dbus::ObjectPath>))
      << "for rpc identifier property " << name;
  RpcIdentifiers ids;
  KeyValueStore::ConvertPathsToRpcIdentifiers(
      it->second.Get<vector<dbus::ObjectPath>>(), &ids);
  return ids;
}

const string& KeyValueStore::GetString(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.GetType() == typeid(string))
      << "for string property " << name;
  return it->second.Get<string>();
}

const map<string, string>& KeyValueStore::GetStringmap(
    const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.GetType() == typeid(Stringmap))
      << "for stringmap property " << name;
  return it->second.Get<Stringmap>();
}

const vector<string>& KeyValueStore::GetStrings(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.GetType() == typeid(Strings))
      << "for strings property " << name;
  return it->second.Get<Strings>();
}

uint32_t KeyValueStore::GetUint(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.GetType() == typeid(uint32_t))
      << "for uint32 property " << name;
  return it->second.Get<uint32_t>();
}

uint16_t KeyValueStore::GetUint16(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.GetType() == typeid(uint16_t))
      << "for uint16 property " << name;
  return it->second.Get<uint16_t>();
}

uint8_t KeyValueStore::GetUint8(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.GetType() == typeid(uint8_t))
      << "for uint8 property " << name;
  return it->second.Get<uint8_t>();
}

const vector<uint8_t>& KeyValueStore::GetUint8s(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.GetType() ==
      typeid(vector<uint8_t>)) << "for uint8s property " << name;
  return it->second.Get<vector<uint8_t>>();
}

const vector<uint32_t>& KeyValueStore::GetUint32s(const string& name) const {
  const auto it(properties_.find(name));
  CHECK(it != properties_.end() && it->second.GetType() ==
      typeid(vector<uint32_t>)) << "for uint32s property " << name;
  return it->second.Get<vector<uint32_t>>();
}

void KeyValueStore::SetBool(const string& name, bool value) {
  properties_[name] = chromeos::Any(value);
}

void KeyValueStore::SetByteArrays(const string& name,
                                  const vector<vector<uint8_t>>& value) {
  properties_[name] = chromeos::Any(value);
}

void KeyValueStore::SetInt(const string& name, int32_t value) {
  properties_[name] = chromeos::Any(value);
}

void KeyValueStore::SetInt16(const string& name, int16_t value) {
  properties_[name] = chromeos::Any(value);
}

void KeyValueStore::SetKeyValueStore(const string& name,
                                     const KeyValueStore& value) {
  properties_[name] = chromeos::Any(value);
}

void KeyValueStore::SetRpcIdentifier(const string& name, const string& value) {
  properties_[name] = chromeos::Any(dbus::ObjectPath(value));
}

void KeyValueStore::SetRpcIdentifiers(const string& name,
                                      const vector<string>& value) {
  vector<dbus::ObjectPath> paths;
  for (const auto& rpcid : value) {
    paths.push_back(dbus::ObjectPath(rpcid));
  }
  properties_[name] = chromeos::Any(paths);
}

void KeyValueStore::SetString(const string& name, const string& value) {
  properties_[name] = chromeos::Any(value);
}

void KeyValueStore::SetStringmap(const string& name,
                                 const map<string, string>& value) {
  properties_[name] = chromeos::Any(value);
}

void KeyValueStore::SetStrings(const string& name,
                               const vector<string>& value) {
  properties_[name] = chromeos::Any(value);
}

void KeyValueStore::SetUint(const string& name, uint32_t value) {
  properties_[name] = chromeos::Any(value);
}

void KeyValueStore::SetUint16(const string& name, uint16_t value) {
  properties_[name] = chromeos::Any(value);
}

void KeyValueStore::SetUint8(const string& name, uint8_t value) {
  properties_[name] = chromeos::Any(value);
}

void KeyValueStore::SetUint8s(const string& name,
                              const vector<uint8_t>& value) {
  properties_[name] = chromeos::Any(value);
}

void KeyValueStore::SetUint32s(const string& name,
                               const vector<uint32_t>& value) {
  properties_[name] = chromeos::Any(value);
}

void KeyValueStore::RemoveByteArrays(const string& name) {
  properties_.erase(name);
}

void KeyValueStore::RemoveInt(const string& name) {
  properties_.erase(name);
}

void KeyValueStore::RemoveInt16(const string& name) {
  properties_.erase(name);
}

void KeyValueStore::RemoveKeyValueStore(const string& name) {
  properties_.erase(name);
}

void KeyValueStore::RemoveRpcIdentifier(const string& name) {
  properties_.erase(name);
}

void KeyValueStore::RemoveString(const string& name) {
  properties_.erase(name);
}

void KeyValueStore::RemoveStringmap(const string& name) {
  properties_.erase(name);
}

void KeyValueStore::RemoveStrings(const string& name) {
  properties_.erase(name);
}

void KeyValueStore::RemoveUint16(const string& name) {
  properties_.erase(name);
}

void KeyValueStore::RemoveUint8(const string& name) {
  properties_.erase(name);
}

void KeyValueStore::RemoveUint8s(const string& name) {
  properties_.erase(name);
}

void KeyValueStore::RemoveUint32s(const string& name) {
  properties_.erase(name);
}

bool KeyValueStore::LookupBool(const string& name, bool default_value) const {
  const auto it(properties_.find(name));
  if (it == properties_.end()) {
    return default_value;
  }
  CHECK(it->second.GetType() == typeid(bool)) << "type mismatched";
  return it->second.Get<bool>();
}

int KeyValueStore::LookupInt(const string& name, int default_value) const {
  const auto it(properties_.find(name));
  if (it == properties_.end()) {
    return default_value;
  }
  CHECK(it->second.GetType() == typeid(int32_t)) << "type mismatched";
  return it->second.Get<int32_t>();
}

string KeyValueStore::LookupString(const string& name,
                                   const string& default_value) const {
  const auto it(properties_.find(name));
  if (it == properties_.end()) {
    return default_value;
  }
  CHECK(it->second.GetType() == typeid(string)) << "type mismatched";
  return it->second.Get<string>();
}

// static.
void KeyValueStore::ConvertToVariantDictionary(
    const KeyValueStore& in_store, chromeos::VariantDictionary* out_dict) {
  for (const auto& key_value_pair : in_store.properties_) {
    if (key_value_pair.second.IsTypeCompatible<KeyValueStore>()) {
      // Special handling for nested KeyValueStore (convert it to
      // nested chromeos::VariantDictionary).
      chromeos::VariantDictionary dict;
      ConvertToVariantDictionary(
          key_value_pair.second.Get<KeyValueStore>(), &dict);
      out_dict->emplace(key_value_pair.first, dict);
    } else {
      out_dict->insert(key_value_pair);
    }
  }
}

// static.
void KeyValueStore::ConvertFromVariantDictionary(
    const chromeos::VariantDictionary& in_dict, KeyValueStore* out_store) {
  for (const auto& key_value_pair : in_dict) {
    if (key_value_pair.second.IsTypeCompatible<chromeos::VariantDictionary>()) {
      // Special handling for nested chromeos::VariantDictionary (convert it to
      // nested KeyValueStore).
      KeyValueStore store;
      ConvertFromVariantDictionary(
          key_value_pair.second.Get<chromeos::VariantDictionary>(), &store);
      out_store->properties_.emplace(key_value_pair.first, store);
    } else {
      out_store->properties_.insert(key_value_pair);
    }
  }
}

// static.
void KeyValueStore::ConvertPathsToRpcIdentifiers(
  const vector<dbus::ObjectPath>& paths, vector<string>* rpc_identifiers) {
  for (const auto& path : paths) {
    rpc_identifiers->push_back(path.value());
  }
}

}  // namespace shill
