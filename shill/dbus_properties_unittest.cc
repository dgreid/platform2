// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus_properties.h"

#include <limits>

#include <gtest/gtest.h>

using std::map;
using std::numeric_limits;
using std::string;
using std::vector;
using testing::Test;

namespace shill {

class DBusPropertiesTest : public Test {};

TEST_F(DBusPropertiesTest, ConvertPathsToRpcIdentifiers) {
  static const char kOldTestPath[] = "/org/chromium/Something/Old";
  static const char kTestPath0[] = "/org/chromium/Something";
  static const char kTestPath1[] = "/org/chromium/Else";

  RpcIdentifiers ids(1, kOldTestPath);
  vector<DBus::Path> paths;
  paths.push_back(kTestPath0);
  paths.push_back(kTestPath1);
  DBusProperties::ConvertPathsToRpcIdentifiers(paths, &ids);
  ASSERT_EQ(2, ids.size());
  EXPECT_EQ(kTestPath0, ids[0]);
  EXPECT_EQ(kTestPath1, ids[1]);
}

TEST_F(DBusPropertiesTest, ConvertKeyValueStoreToMap) {
  static const char kStringKey[] = "StringKey";
  static const char kStringValue[] = "StringValue";
  static const char kStringmapKey[] = "StringmapKey";
  const map<string, string> kStringmapValue = { { "key", "value" } };
  static const char kStringsKey[] = "StringsKey";
  const vector<string> kStringsValue = {"StringsValue1", "StringsValue2"};
  static const char kBoolKey[] = "BoolKey";
  const bool kBoolValue = true;
  static const char kInt32Key[] = "Int32Key";
  const int32_t kInt32Value = 123;
  static const char kUint32Key[] = "Uint32Key";
  const uint32_t kUint32Value = 654;
  static const char kByteArraysKey[] = "ByteArraysKey";
  const vector<vector<uint8_t>> kByteArraysValue{ {1}, {2} };
  static const char kInt16Key[] = "Int16Key";
  const int16_t kInt16Value = 123;
  static const char kRpcIdentifierKey[] = "RpcIdentifierKey";
  static const char kRpcIdentifierValue[] = "/org/chromium/test";
  static const char kUint16Key[] = "Uint16Key";
  const uint16_t kUint16Value = 123;
  static const char kUint8Key[] = "Uint8Key";
  const uint8_t kUint8Value = 123;
  static const char kUint8sKey[] = "Uint8sKey";
  const vector<uint8_t> kUint8sValue{ 1, 2 };
  static const char kUint32sKey[] = "Uint32sKey";
  const vector<uint32_t> kUint32sValue{ 1, 2 };
  static const char kKeyValueStoreKey[] = "KeyValueStoreKey";
  static const char kNestedInt32Key[] = "NestedKey32Key";
  const int32_t kNestedInt32Value = 1;
  KeyValueStore nested_store;
  nested_store.SetInt(kNestedInt32Key, kNestedInt32Value);

  KeyValueStore store;
  store.SetString(kStringKey, kStringValue);
  store.SetStringmap(kStringmapKey, kStringmapValue);
  store.SetStrings(kStringsKey, kStringsValue);
  store.SetBool(kBoolKey, kBoolValue);
  store.SetInt(kInt32Key, kInt32Value);
  store.SetUint(kUint32Key, kUint32Value);
  store.SetByteArrays(kByteArraysKey, kByteArraysValue);
  store.SetInt16(kInt16Key, kInt16Value);
  store.SetRpcIdentifier(kRpcIdentifierKey, kRpcIdentifierValue);
  store.SetUint16(kUint16Key, kUint16Value);
  store.SetUint8(kUint8Key, kUint8Value);
  store.SetUint8s(kUint8sKey, kUint8sValue);
  store.SetUint32s(kUint32sKey, kUint32sValue);
  store.SetKeyValueStore(kKeyValueStoreKey, nested_store);

  DBusPropertiesMap props;
  props["RandomKey"].writer().append_string("RandomValue");
  DBusProperties::ConvertKeyValueStoreToMap(store, &props);
  EXPECT_EQ(14, props.size());
  string string_value;
  EXPECT_TRUE(DBusProperties::GetString(props, kStringKey, &string_value));
  EXPECT_EQ(kStringValue, string_value);
  map<string, string> stringmap_value;
  EXPECT_TRUE(
      DBusProperties::GetStringmap(props, kStringmapKey, &stringmap_value));
  EXPECT_EQ(kStringmapValue, stringmap_value);
  vector<string> strings_value;
  EXPECT_TRUE(DBusProperties::GetStrings(props, kStringsKey, &strings_value));
  EXPECT_EQ(kStringsValue, strings_value);
  bool bool_value = !kBoolValue;
  EXPECT_TRUE(DBusProperties::GetBool(props, kBoolKey, &bool_value));
  EXPECT_EQ(kBoolValue, bool_value);
  int32_t int32_value = ~kInt32Value;
  EXPECT_TRUE(DBusProperties::GetInt32(props, kInt32Key, &int32_value));
  EXPECT_EQ(kInt32Value, int32_value);
  uint32_t uint32_value = ~kUint32Value;
  EXPECT_TRUE(DBusProperties::GetUint32(props, kUint32Key, &uint32_value));
  EXPECT_EQ(kUint32Value, uint32_value);
  vector<vector<uint8_t>> byte_arrays_value;
  EXPECT_TRUE(
      DBusProperties::GetByteArrays(props, kByteArraysKey, &byte_arrays_value));
  EXPECT_EQ(kByteArraysValue, byte_arrays_value);
  int16_t int16_value = ~kInt16Value;
  EXPECT_TRUE(
      DBusProperties::GetInt16(props, kInt16Key, &int16_value));
  EXPECT_EQ(kInt16Value, int16_value);
  DBus::Path rpc_identifier_value;
  EXPECT_TRUE(
      DBusProperties::GetObjectPath(props,
                                    kRpcIdentifierKey,
                                    &rpc_identifier_value));
  EXPECT_EQ(kRpcIdentifierValue, rpc_identifier_value);
  uint16_t uint16_value = ~kUint16Value;
  EXPECT_TRUE(
      DBusProperties::GetUint16(props, kUint16Key, &uint16_value));
  EXPECT_EQ(kUint16Value, uint16_value);
  uint8_t uint8_value = ~kUint8Value;
  EXPECT_TRUE(
      DBusProperties::GetUint8(props, kUint8Key, &uint8_value));
  EXPECT_EQ(kUint8Value, uint8_value);
  vector<uint8_t> uint8s_value;
  EXPECT_TRUE(
      DBusProperties::GetUint8s(props, kUint8sKey, &uint8s_value));
  EXPECT_EQ(kUint8sValue, uint8s_value);
  vector<uint32_t> uint32s_value;
  EXPECT_TRUE(
      DBusProperties::GetUint32s(props, kUint32sKey, &uint32s_value));
  EXPECT_EQ(kUint32sValue, uint32s_value);
  DBusPropertiesMap nested_map;
  EXPECT_TRUE(
      DBusProperties::GetDBusPropertiesMap(props,
                                           kKeyValueStoreKey,
                                           &nested_map));
  int32_t nested_int32_value = ~kNestedInt32Value;
  EXPECT_TRUE(
      DBusProperties::GetInt32(nested_map,
                               kNestedInt32Key,
                               &nested_int32_value));
  EXPECT_EQ(kNestedInt32Value, nested_int32_value);
}

TEST_F(DBusPropertiesTest, ConvertMapToKeyValueStore) {
  static const char kStringKey[] = "StringKey";
  static const char kStringValue[] = "StringValue";
  static const char kStringmapKey[] = "StringmapKey";
  const map<string, string> kStringmapValue = { { "key", "value" } };
  static const char kStringsKey[] = "StringsKey";
  const vector<string> kStringsValue = {"StringsValue1", "StringsValue2"};
  static const char kBoolKey[] = "BoolKey";
  const bool kBoolValue = true;
  static const char kInt32Key[] = "Int32Key";
  const int32_t kInt32Value = 123;
  static const char kUint32Key[] = "Uint32Key";
  const uint32_t kUint32Value = 654;
  static const char kByteArraysKey[] = "ByteArraysKey";
  const vector<vector<uint8_t>> kByteArraysValue{ {1}, {2} };
  static const char kInt16Key[] = "Int16Key";
  const int16_t kInt16Value = 123;
  static const char kRpcIdentifierKey[] = "RpcIdentifierKey";
  static const char kRpcIdentifierValue[] = "/org/chromium/test";
  static const char kRpcIdentifiersKey[] = "RpcIdentifiersKey";
  const vector<string> kRpcIdentifiersValue = {"/obj/3", "/obj/4", "/obj/5"};
  static const char kUint16Key[] = "Uint16Key";
  const uint16_t kUint16Value = 123;
  static const char kUint8Key[] = "Uint8Key";
  const uint8_t kUint8Value = 123;
  static const char kUint8sKey[] = "Uint8sKey";
  const vector<uint8_t> kUint8sValue{ 1, 2 };
  static const char kUint32sKey[] = "Uint32sKey";
  const vector<uint32_t> kUint32sValue{ 1, 2 };
  static const char kKeyValueStoreKey[] = "KeyValueStoreKey";
  static const char kNestedInt32Key[] = "NestedKey32Key";
  const int32_t kNestedInt32Value = 1;
  DBusPropertiesMap props;
  props[kStringKey].writer().append_string(kStringValue);
  {
    DBus::MessageIter writer = props[kStringmapKey].writer();
    writer << kStringmapValue;
  }
  {
    DBus::MessageIter writer = props[kStringsKey].writer();
    writer << kStringsValue;
  }
  props[kBoolKey].writer().append_bool(kBoolValue);
  props[kInt32Key].writer().append_int32(kInt32Value);
  props[kUint32Key].writer().append_uint32(kUint32Value);
  {
    DBus::MessageIter writer = props[kByteArraysKey].writer();
    writer << kByteArraysValue;
  }
  props[kInt16Key].writer().append_int16(kInt16Value);
  props[kRpcIdentifierKey].writer().append_path(kRpcIdentifierValue);
  {
    // Do type conversion,
    // otherwise the signature will be 'as' instead of 'ao'.
    vector<DBus::Path> paths;
    for (string path : kRpcIdentifiersValue) {
      paths.push_back(path);
    }
    DBus::MessageIter writer = props[kRpcIdentifiersKey].writer();
    writer << paths;
  }
  props[kUint16Key].writer().append_uint16(kUint16Value);
  props[kUint8Key].writer().append_byte(kUint8Value);
  {
    DBus::MessageIter writer = props[kUint8sKey].writer();
    writer << kUint8sValue;
  }
  {
    DBus::MessageIter writer = props[kUint32sKey].writer();
    writer << kUint32sValue;
  }
  {
    DBusPropertiesMap nested_props;
    nested_props[kNestedInt32Key].writer().append_int32(kNestedInt32Value);
    DBus::MessageIter writer = props[kKeyValueStoreKey].writer();
    writer << nested_props;
  }
  KeyValueStore store;
  Error error;
  DBusProperties::ConvertMapToKeyValueStore(props, &store, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_TRUE(store.ContainsString(kStringKey));
  EXPECT_EQ(kStringValue, store.GetString(kStringKey));
  EXPECT_TRUE(store.ContainsStringmap(kStringmapKey));
  EXPECT_EQ(kStringmapValue, store.GetStringmap(kStringmapKey));
  EXPECT_TRUE(store.ContainsStrings(kStringsKey));
  EXPECT_EQ(kStringsValue, store.GetStrings(kStringsKey));
  EXPECT_TRUE(store.ContainsBool(kBoolKey));
  EXPECT_EQ(kBoolValue, store.GetBool(kBoolKey));
  EXPECT_TRUE(store.ContainsInt(kInt32Key));
  EXPECT_EQ(kInt32Value, store.GetInt(kInt32Key));
  EXPECT_TRUE(store.ContainsUint(kUint32Key));
  EXPECT_EQ(kUint32Value, store.GetUint(kUint32Key));
  EXPECT_TRUE(store.ContainsByteArrays(kByteArraysKey));
  EXPECT_EQ(kByteArraysValue, store.GetByteArrays(kByteArraysKey));
  EXPECT_TRUE(store.ContainsInt16(kInt16Key));
  EXPECT_EQ(kInt16Value, store.GetInt16(kInt16Key));
  EXPECT_TRUE(store.ContainsRpcIdentifier(kRpcIdentifierKey));
  EXPECT_EQ(kRpcIdentifierValue, store.GetRpcIdentifier(kRpcIdentifierKey));
  EXPECT_TRUE(store.ContainsRpcIdentifiers(kRpcIdentifiersKey));
  EXPECT_EQ(kRpcIdentifiersValue, store.GetRpcIdentifiers(kRpcIdentifiersKey));
  EXPECT_TRUE(store.ContainsUint16(kUint16Key));
  EXPECT_EQ(kUint16Value, store.GetUint16(kUint16Key));
  EXPECT_TRUE(store.ContainsUint8(kUint8Key));
  EXPECT_EQ(kUint8Value, store.GetUint8(kUint8Key));
  EXPECT_TRUE(store.ContainsUint8s(kUint8sKey));
  EXPECT_EQ(kUint8sValue, store.GetUint8s(kUint8sKey));
  EXPECT_TRUE(store.ContainsUint32s(kUint32sKey));
  EXPECT_EQ(kUint32sValue, store.GetUint32s(kUint32sKey));
  EXPECT_TRUE(store.ContainsKeyValueStore(kKeyValueStoreKey));
  KeyValueStore nested_store;
  nested_store.SetInt(kNestedInt32Key, kNestedInt32Value);
  EXPECT_EQ(nested_store, store.GetKeyValueStore(kKeyValueStoreKey));
}

template <typename T> class DBusPropertiesGetterTest : public Test {};

template <typename DerivedT, typename ValueT, typename DBusT = ValueT>
struct TestTraits {
  typedef ValueT ValueType;
  typedef DBusT DBusType;
  typedef bool (*GetterType)(const DBusPropertiesMap& properties,
                             const string& key,
                             ValueType* value);

  static DBusType GetTestValue() { return DerivedT::GetNewValue(); }

  static void CheckValueEqual(const ValueType& expected,
                              const ValueType& actual) {
    EXPECT_EQ(expected, actual);
  }
};

struct BoolTestTraits : public TestTraits<BoolTestTraits, bool> {
  static bool GetOldValue() { return false; }
  static bool GetNewValue() { return true; }
  static constexpr GetterType kMethodUnderTest = &DBusProperties::GetBool;
};

template <typename DerivedT, typename ValueT>
struct IntTestTraits : public TestTraits<DerivedT, ValueT> {
  static ValueT GetOldValue() { return numeric_limits<ValueT>::min(); }
  static ValueT GetNewValue() { return numeric_limits<ValueT>::max(); }
};

struct Int16TestTraits : public IntTestTraits<Int16TestTraits, int16_t> {
  static constexpr GetterType kMethodUnderTest = &DBusProperties::GetInt16;
};

struct Int32TestTraits : public IntTestTraits<Int32TestTraits, int32_t> {
  static constexpr GetterType kMethodUnderTest = &DBusProperties::GetInt32;
};

struct Int64TestTraits : public IntTestTraits<Int64TestTraits, int64_t> {
  static constexpr GetterType kMethodUnderTest = &DBusProperties::GetInt64;
};

struct Uint8TestTraits : public IntTestTraits<Uint8TestTraits, uint8_t> {
  static constexpr GetterType kMethodUnderTest = &DBusProperties::GetUint8;
};

struct Uint16TestTraits : public IntTestTraits<Uint16TestTraits, uint16_t> {
  static constexpr GetterType kMethodUnderTest = &DBusProperties::GetUint16;
};

struct Uint32TestTraits : public IntTestTraits<Uint32TestTraits, uint32_t> {
  static constexpr GetterType kMethodUnderTest = &DBusProperties::GetUint32;
};

struct Uint64TestTraits : public IntTestTraits<Uint64TestTraits, uint64_t> {
  static constexpr GetterType kMethodUnderTest = &DBusProperties::GetUint64;
};

struct DoubleTestTraits : public TestTraits<DoubleTestTraits, double> {
  static double GetOldValue() { return -1.1; }
  static double GetNewValue() { return 3.14; }
  static constexpr GetterType kMethodUnderTest = &DBusProperties::GetDouble;
};

struct StringTestTraits : public TestTraits<StringTestTraits, string> {
  static string GetOldValue() { return "old"; }
  static string GetNewValue() { return "new"; }
  static constexpr GetterType kMethodUnderTest = &DBusProperties::GetString;
};

struct StringmapTestTraits
    : public TestTraits<StringmapTestTraits, map<string, string>> {
  static map<string, string> GetOldValue() {
    return { { "oldKey", "oldValue" } };
  }
  static map<string, string> GetNewValue() {
    return { { "newKey", "newValue" } };
  }
  static constexpr GetterType kMethodUnderTest = &DBusProperties::GetStringmap;
};

struct StringsTestTraits
    : public TestTraits<StringsTestTraits, vector<string>> {
  static vector<string> GetOldValue() { return {"1", "2", "3"}; }
  static vector<string> GetNewValue() { return {"a", "b"}; }
  static constexpr GetterType kMethodUnderTest = &DBusProperties::GetStrings;
};

struct ObjectPathTestTraits
    : public TestTraits<ObjectPathTestTraits, DBus::Path> {
  static DBus::Path GetOldValue() { return "/old/path"; }
  static DBus::Path GetNewValue() { return "/new/path"; }
  static constexpr GetterType kMethodUnderTest = &DBusProperties::GetObjectPath;
};

struct RpcIdentifiersTestTraits : public TestTraits<RpcIdentifiersTestTraits,
                                                    RpcIdentifiers,
                                                    vector<DBus::Path>> {
  static RpcIdentifiers GetOldValue() { return {"/device/1", "/service/2"}; }
  static RpcIdentifiers GetNewValue() { return {"/obj/3", "/obj/4", "/obj/5"}; }
  static vector<DBus::Path> GetTestValue() {
    return {"/obj/3", "/obj/4", "/obj/5"};
  }
  static constexpr GetterType kMethodUnderTest =
      &DBusProperties::GetRpcIdentifiers;
};

struct DBusPropertiesMapTestTraits
    : public TestTraits<DBusPropertiesMapTestTraits, DBusPropertiesMap> {
  static DBusPropertiesMap GetOldValue() {
    DBusPropertiesMap value;
    value["OldKey"].writer().append_bool(false);
    return value;
  }

  static DBusPropertiesMap GetNewValue() {
    DBusPropertiesMap value;
    value["BoolKey"].writer().append_bool(true);
    value["Int16Key"].writer().append_int16(numeric_limits<int16_t>::max());
    value["Int32Key"].writer().append_int32(numeric_limits<int32_t>::max());
    value["Int64Key"].writer().append_int64(numeric_limits<int64_t>::max());
    value["Uint8Key"].writer().append_byte(numeric_limits<uint8_t>::max());
    value["Uint16Key"].writer().append_uint16(numeric_limits<uint16_t>::max());
    value["Uint32Key"].writer().append_uint32(numeric_limits<uint32_t>::max());
    value["Uint64Key"].writer().append_uint64(numeric_limits<uint64_t>::max());
    value["DoubleKey"].writer().append_double(3.14);
    value["StringKey"].writer().append_string("new");
    return value;
  }

  template <typename T>
  static void CheckDBusVariantEqual(const string& key,
                                    const DBus::Variant& expected,
                                    const DBus::Variant& actual) {
    T expected_value = expected.operator T();
    T actual_value = actual.operator T();
    EXPECT_EQ(expected_value, actual_value) << "Value mismatch - key: " << key;
  }

  static void CheckValueEqual(const DBusPropertiesMap& expected,
                              const DBusPropertiesMap& actual) {
    ASSERT_EQ(expected.size(), actual.size()) << "Map size mismatch";

    for (const auto& key_value_pair : expected) {
      const string& key = key_value_pair.first;

      const auto& actual_it = actual.find(key);
      ASSERT_TRUE(actual_it != actual.end()) << "Key '" << key << "' not found";

      const DBus::Variant& actual_value = actual_it->second;
      const DBus::Variant& expected_value = key_value_pair.second;

      string actual_signature = actual_value.signature();
      string expected_signature = expected_value.signature();
      ASSERT_EQ(expected_signature, actual_signature)
          << "Value type mismatch - key: " << key;

      if (expected_signature == DBus::type<bool>::sig()) {
        CheckDBusVariantEqual<bool>(key, expected_value, actual_value);
      } else if (expected_signature == DBus::type<int16_t>::sig()) {
        CheckDBusVariantEqual<int16_t>(key, expected_value, actual_value);
      } else if (expected_signature == DBus::type<int32_t>::sig()) {
        CheckDBusVariantEqual<int32_t>(key, expected_value, actual_value);
      } else if (expected_signature == DBus::type<int64_t>::sig()) {
        CheckDBusVariantEqual<int64_t>(key, expected_value, actual_value);
      } else if (expected_signature == DBus::type<uint8_t>::sig()) {
        CheckDBusVariantEqual<uint8_t>(key, expected_value, actual_value);
      } else if (expected_signature == DBus::type<uint16_t>::sig()) {
        CheckDBusVariantEqual<uint16_t>(key, expected_value, actual_value);
      } else if (expected_signature == DBus::type<uint32_t>::sig()) {
        CheckDBusVariantEqual<uint32_t>(key, expected_value, actual_value);
      } else if (expected_signature == DBus::type<uint64_t>::sig()) {
        CheckDBusVariantEqual<uint64_t>(key, expected_value, actual_value);
      } else if (expected_signature == DBus::type<double>::sig()) {
        CheckDBusVariantEqual<double>(key, expected_value, actual_value);
      } else if (expected_signature == DBus::type<string>::sig()) {
        CheckDBusVariantEqual<string>(key, expected_value, actual_value);
      } else if (expected_signature == DBus::type<DBus::Path>::sig()) {
        CheckDBusVariantEqual<DBus::Path>(key, expected_value, actual_value);
      } else {
        // TODO(benchan): Comparison of other types is not yet implemented.
        FAIL() << "Value type '" << expected_signature << "' not implemented.";
      }
    }
  }

  static constexpr GetterType kMethodUnderTest =
      &DBusProperties::GetDBusPropertiesMap;
};

typedef testing::Types<BoolTestTraits,
                       Int16TestTraits,
                       Int32TestTraits,
                       Int64TestTraits,
                       Uint8TestTraits,
                       Uint16TestTraits,
                       Uint32TestTraits,
                       Uint64TestTraits,
                       DoubleTestTraits,
                       StringTestTraits,
                       StringmapTestTraits,
                       StringsTestTraits,
                       ObjectPathTestTraits,
                       RpcIdentifiersTestTraits,
                       DBusPropertiesMapTestTraits> DBusPropertyValueTypes;
TYPED_TEST_CASE(DBusPropertiesGetterTest, DBusPropertyValueTypes);

TYPED_TEST(DBusPropertiesGetterTest, GetValue) {
  static const char kTestProperty[] = "TestProperty";
  const typename TypeParam::ValueType kOldValue = TypeParam::GetOldValue();
  const typename TypeParam::ValueType kNewValue = TypeParam::GetNewValue();
  const typename TypeParam::DBusType kTestValue = TypeParam::GetTestValue();
  typename TypeParam::ValueType value = kOldValue;
  DBusPropertiesMap properties;

  // Property key is not found. |value| should remain the initial value.
  EXPECT_FALSE(TypeParam::kMethodUnderTest(properties, kTestProperty, &value));
  TypeParam::CheckValueEqual(kOldValue, value);

  // Property value type mismatch. |value| should remain the initial value.
  properties[kTestProperty].writer().append_fd(1);
  EXPECT_FALSE(TypeParam::kMethodUnderTest(properties, kTestProperty, &value));
  TypeParam::CheckValueEqual(kOldValue, value);

  // Property key is found. |value| should be set to the test value.
  properties.clear();
  DBus::MessageIter writer = properties[kTestProperty].writer();
  writer << kTestValue;
  EXPECT_TRUE(TypeParam::kMethodUnderTest(properties, kTestProperty, &value));
  TypeParam::CheckValueEqual(kNewValue, value);
}

}  // namespace shill
