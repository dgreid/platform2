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

bool KeyValueStore::ContainsVariant(const string& name) const {
  return base::ContainsKey(properties_, name);
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
