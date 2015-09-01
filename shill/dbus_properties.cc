// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus_properties.h"

#include <dbus/dbus.h>

#include "shill/logging.h"

using std::map;
using std::string;
using std::vector;

namespace shill {

namespace Logging {
static string ObjectID(DBusProperties* d) { return "(dbus_properties)"; }
}

namespace {

template <typename ValueType>
bool GetValue(const DBusPropertiesMap& properties,
              const string& key,
              ValueType* value) {
  CHECK(value);

  DBusPropertiesMap::const_iterator it = properties.find(key);
  if (it == properties.end()) {
    SLOG(DBus, nullptr, 2) << "Key '" << key << "' not found.";
    return false;
  }

  string actual_type = it->second.signature();
  string expected_type = DBus::type<ValueType>::sig();
  if (actual_type != expected_type) {
    SLOG(DBus, nullptr, 2) << "Key '" << key << "' type mismatch (expected '"
                           << expected_type << "', actual '" << actual_type
                           << "').";
    return false;
  }

  *value = it->second.operator ValueType();
  return true;
}

}  // namespace

// static
bool DBusProperties::GetBool(const DBusPropertiesMap& properties,
                             const string& key,
                             bool* value) {
  return GetValue<bool>(properties, key, value);
}

// static
bool DBusProperties::GetByteArrays(const DBusPropertiesMap& properties,
                                   const string& key,
                                   vector<vector<uint8_t>>* value) {
  return GetValue<vector<vector<uint8_t>>>(properties, key, value);
}

// static
bool DBusProperties::GetDBusPropertiesMap(const DBusPropertiesMap& properties,
                                          const string& key,
                                          DBusPropertiesMap* value) {
  return GetValue<DBusPropertiesMap>(properties, key, value);
}

// static
bool DBusProperties::GetDouble(const DBusPropertiesMap& properties,
                               const string& key,
                               double* value) {
  return GetValue<double>(properties, key, value);
}

// static
bool DBusProperties::GetInt16(const DBusPropertiesMap& properties,
                              const string& key,
                              int16_t* value) {
  return GetValue<int16_t>(properties, key, value);
}

// static
bool DBusProperties::GetInt32(const DBusPropertiesMap& properties,
                              const string& key,
                              int32_t* value) {
  return GetValue<int32_t>(properties, key, value);
}

// static
bool DBusProperties::GetInt64(const DBusPropertiesMap& properties,
                              const string& key,
                              int64_t* value) {
  return GetValue<int64_t>(properties, key, value);
}

// static
bool DBusProperties::GetObjectPath(const DBusPropertiesMap& properties,
                                   const string& key,
                                   DBus::Path* value) {
  return GetValue<DBus::Path>(properties, key, value);
}

// static
bool DBusProperties::GetString(const DBusPropertiesMap& properties,
                               const string& key,
                               string* value) {
  return GetValue<string>(properties, key, value);
}

// static
bool DBusProperties::GetStringmap(const DBusPropertiesMap& properties,
                                  const string& key,
                                  map<string, string>* value) {
  return GetValue<map<string, string>>(properties, key, value);
}

// static
bool DBusProperties::GetStrings(const DBusPropertiesMap& properties,
                                const string& key,
                                vector<string>* value) {
  return GetValue<vector<string>>(properties, key, value);
}

// static
bool DBusProperties::GetUint8(const DBusPropertiesMap& properties,
                               const string& key,
                               uint8_t* value) {
  return GetValue<uint8_t>(properties, key, value);
}

// static
bool DBusProperties::GetUint16(const DBusPropertiesMap& properties,
                               const string& key,
                               uint16_t* value) {
  return GetValue<uint16_t>(properties, key, value);
}

// static
bool DBusProperties::GetUint32(const DBusPropertiesMap& properties,
                               const string& key,
                               uint32_t* value) {
  return GetValue<uint32_t>(properties, key, value);
}

// static
bool DBusProperties::GetUint64(const DBusPropertiesMap& properties,
                               const string& key,
                               uint64_t* value) {
  return GetValue<uint64_t>(properties, key, value);
}

// static
bool DBusProperties::GetRpcIdentifiers(const DBusPropertiesMap& properties,
                                       const string& key,
                                       RpcIdentifiers* value) {
  vector<DBus::Path> paths;
  if (GetValue<vector<DBus::Path>>(properties, key, &paths)) {
    ConvertPathsToRpcIdentifiers(paths, value);
    return true;
  }
  return false;
}

// static
bool DBusProperties::GetUint8s(const DBusPropertiesMap& properties,
                               const string& key,
                               vector<uint8_t>* value) {
  return GetValue<vector<uint8_t>>(properties, key, value);
}

// static
bool DBusProperties::GetUint32s(const DBusPropertiesMap& properties,
                               const string& key,
                               vector<uint32_t>* value) {
  return GetValue<vector<uint32_t>>(properties, key, value);
}

// static
void DBusProperties::ConvertPathsToRpcIdentifiers(
    const vector<DBus::Path>& dbus_paths, RpcIdentifiers* rpc_identifiers) {
  CHECK(rpc_identifiers);
  rpc_identifiers->assign(dbus_paths.begin(), dbus_paths.end());
}

// static
void DBusProperties::ConvertKeyValueStoreToMap(
    const KeyValueStore& store, DBusPropertiesMap* properties) {
  CHECK(properties);
  properties->clear();
  for (const auto& key_value_pair : store.properties()) {
    if (key_value_pair.second.GetType() == typeid(string)) {
      (*properties)[key_value_pair.first].writer()
          .append_string(key_value_pair.second.Get<string>().c_str());
    } else if (key_value_pair.second.GetType() == typeid(Stringmap)) {
      DBus::MessageIter writer = (*properties)[key_value_pair.first].writer();
      writer << key_value_pair.second.Get<Stringmap>();
    } else if (key_value_pair.second.GetType() == typeid(Strings)) {
      DBus::MessageIter writer = (*properties)[key_value_pair.first].writer();
      writer << key_value_pair.second.Get<Strings>();
    } else if (key_value_pair.second.GetType() == typeid(bool)) {    // NOLINT
      (*properties)[key_value_pair.first].writer()
          .append_bool(key_value_pair.second.Get<bool>());
    } else if (key_value_pair.second.GetType() == typeid(int32_t)) {
      (*properties)[key_value_pair.first].writer()
          .append_int32(key_value_pair.second.Get<int32_t>());
    } else if (key_value_pair.second.GetType() == typeid(int16_t)) {
      (*properties)[key_value_pair.first].writer()
          .append_int16(key_value_pair.second.Get<int16_t>());
    } else if (key_value_pair.second.GetType() == typeid(uint32_t)) {
      (*properties)[key_value_pair.first].writer()
          .append_uint32(key_value_pair.second.Get<uint32_t>());
    } else if (key_value_pair.second.GetType() == typeid(uint16_t)) {
      (*properties)[key_value_pair.first].writer()
          .append_uint16(key_value_pair.second.Get<uint16_t>());
    } else if (key_value_pair.second.GetType() == typeid(uint8_t)) {
      (*properties)[key_value_pair.first].writer()
          .append_byte(key_value_pair.second.Get<uint8_t>());
    } else if (key_value_pair.second.GetType() == typeid(vector<uint8_t>)) {
      DBus::MessageIter writer = (*properties)[key_value_pair.first].writer();
      writer << key_value_pair.second.Get<vector<uint8_t>>();
    } else if (key_value_pair.second.GetType() == typeid(vector<uint32_t>)) {
      DBus::MessageIter writer = (*properties)[key_value_pair.first].writer();
      writer << key_value_pair.second.Get<vector<uint32_t>>();
    } else if (key_value_pair.second.GetType() ==
        typeid(vector<vector<uint8_t>>)) {
      DBus::MessageIter writer = (*properties)[key_value_pair.first].writer();
      writer << key_value_pair.second.Get<vector<vector<uint8_t>>>();
    } else if (key_value_pair.second.GetType() == typeid(KeyValueStore)) {
      DBusPropertiesMap props;
      ConvertKeyValueStoreToMap(key_value_pair.second.Get<KeyValueStore>(),
                                &props);
      DBus::MessageIter writer = (*properties)[key_value_pair.first].writer();
      writer << props;
    } else if (key_value_pair.second.GetType() == typeid(dbus::ObjectPath)) {
      (*properties)[key_value_pair.first].writer()
          .append_path(
              key_value_pair.second.Get<dbus::ObjectPath>().value().c_str());
    }
  }
}

// static
void DBusProperties::ConvertMapToKeyValueStore(
    const DBusPropertiesMap& properties,
    KeyValueStore* store,
    Error* error) {  // TODO(quiche): Should be ::DBus::Error?
  for (const auto& key_value_pair : properties) {
    string key = key_value_pair.first;

    if (key_value_pair.second.signature() == ::DBus::type<bool>::sig()) {
      SLOG(DBus, nullptr, 5) << "Got bool property " << key;
      store->SetBool(key, key_value_pair.second.reader().get_bool());
    } else if (key_value_pair.second.signature() ==
        ::DBus::type<vector<vector<uint8_t>>>::sig()) {
      SLOG(DBus, nullptr, 5) << "Got byte_arrays property " << key;
      store->SetByteArrays(
          key, key_value_pair.second.operator vector<vector<uint8_t>>());
    } else if (key_value_pair.second.signature() ==
        ::DBus::type<int32_t>::sig()) {
      SLOG(DBus, nullptr, 5) << "Got int32_t property " << key;
      store->SetInt(key, key_value_pair.second.reader().get_int32());
    } else if (key_value_pair.second.signature() ==
        ::DBus::type<int16_t>::sig()) {
      SLOG(DBus, nullptr, 5) << "Got int16_t property " << key;
      store->SetInt16(key, key_value_pair.second.reader().get_int16());
    } else if (key_value_pair.second.signature() ==
        ::DBus::type<map<string, ::DBus::Variant>>::sig()) {
      SLOG(DBus, nullptr, 5) << "Got variant map property " << key;
      // Unwrap a recursive KeyValueStore object.
      KeyValueStore convert_store;
      Error convert_error;
      ConvertMapToKeyValueStore(
          key_value_pair.second.operator map<string, ::DBus::Variant>(),
          &convert_store,
          &convert_error);
      if (convert_error.IsSuccess()) {
          store->SetKeyValueStore(key, convert_store);
      } else {
          Error::PopulateAndLog(FROM_HERE, error, convert_error.type(),
                                convert_error.message() + " in sub-key " + key);
          return;  // Skip remaining args after error.
      }
    } else if (key_value_pair.second.signature() ==
        ::DBus::type<::DBus::Path>::sig()) {
      SLOG(DBus, nullptr, 5) << "Got Path property " << key;
      store->SetRpcIdentifier(key, key_value_pair.second.reader().get_path());
    } else if (key_value_pair.second.signature() ==
        ::DBus::type<string>::sig()) {
      SLOG(DBus, nullptr, 5) << "Got string property " << key;
      store->SetString(key, key_value_pair.second.reader().get_string());
    } else if (key_value_pair.second.signature() ==
        ::DBus::type<Strings>::sig()) {
      SLOG(DBus, nullptr, 5) << "Got strings property " << key;
      store->SetStrings(key, key_value_pair.second.operator vector<string>());
    } else if (key_value_pair.second.signature() ==
        ::DBus::type<Stringmap>::sig()) {
      SLOG(DBus, nullptr, 5) << "Got stringmap property " << key;
      store->SetStringmap(
          key, key_value_pair.second.operator map<string, string>());
    } else if (key_value_pair.second.signature() ==
        ::DBus::type<uint32_t>::sig()) {
      SLOG(DBus, nullptr, 5) << "Got uint32_t property " << key;
      store->SetUint(key, key_value_pair.second.reader().get_uint32());
    } else if (key_value_pair.second.signature() ==
        ::DBus::type<uint16_t>::sig()) {
      SLOG(DBus, nullptr, 5) << "Got uint16_t property " << key;
      store->SetUint16(key, key_value_pair.second.reader().get_uint16());
    } else if (key_value_pair.second.signature() ==
        ::DBus::type<uint8_t>::sig()) {
      SLOG(DBus, nullptr, 5) << "Got uint8_t property " << key;
      store->SetUint8(key, key_value_pair.second.reader().get_byte());
    } else if (key_value_pair.second.signature() ==
        ::DBus::type<vector<uint8_t>>::sig()) {
      SLOG(DBus, nullptr, 5) << "Got vector<uint8_t> property " << key;
      store->SetUint8s(key, key_value_pair.second.operator vector<uint8_t>());
    } else if (key_value_pair.second.signature() ==
        ::DBus::type<vector<uint32_t>>::sig()) {
      SLOG(DBus, nullptr, 5) << "Got vector<uint32_t> property " << key;
      store->SetUint32s(key, key_value_pair.second.operator vector<uint32_t>());
    } else if (key_value_pair.second.signature() ==
        ::DBus::type<vector<::DBus::Path>>::sig()) {
      SLOG(DBus, nullptr, 5) << "Got vector<Path> property " << key;
      auto path_vec = key_value_pair.second.operator vector<::DBus::Path>();
      vector<string> str_vec;
      ConvertPathsToRpcIdentifiers(path_vec, &str_vec);
      store->SetRpcIdentifiers(key, str_vec);
    } else {
      Error::PopulateAndLog(FROM_HERE, error, Error::kInternalError,
                            "unsupported type for property " + key);
      return;  // Skip remaining args after error.
    }
  }
}

// static
string DBusProperties::KeysToString(const DBusPropertiesMap& properties) {
  string keys;
  for (const auto& key_value_pair : properties) {
    keys.append(" ");
    keys.append(key_value_pair.first);
  }
  return keys;
}

}  // namespace shill
