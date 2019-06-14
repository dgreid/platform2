// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/key_value_store.h"

#include <gtest/gtest.h>

using std::map;
using std::string;
using std::vector;
using testing::Test;

namespace shill {

namespace {
const char kBoolKey[] = "BoolKey";
const char kBoolsKey[] = "BoolsKey";
const char kByteArraysKey[] = "ByteArraysKey";
const char kIntKey[] = "IntKey";
const char kIntsKey[] = "IntsKey";
const char kInt16Key[] = "Int16Key";
const char kInt64Key[] = "Int64Key";
const char kInt64sKey[] = "Int64sKey";
const char kDoubleKey[] = "DoubleKey";
const char kDoublesKey[] = "DoublesKey";
const char kKeyValueStoreKey[] = "KeyValueStoreKey";
const char kRpcIdentifierKey[] = "RpcIdentifierKey";
const char kRpcIdentifiersKey[] = "RpcIdentifiersKey";
const char kStringKey[] = "StringKey";
const char kStringmapKey[] = "StringmapKey";
const char kStringsKey[] = "StringsKey";
const char kUintKey[] = "UintKey";
const char kUint16Key[] = "Uint16Key";
const char kUint8Key[] = "Uint8Key";
const char kUint8sKey[] = "Uint8sKey";
const char kUint32sKey[] = "Uint32sKey";
const char kNestedInt32Key[] = "NestedInt32Key";

const bool kBoolValue = true;
const vector<bool> kBoolsValue{true, false, false};
const vector<vector<uint8_t>> kByteArraysValue{{1}, {2}};
const int32_t kIntValue = 123;
const vector<int32_t> kIntsValue{123, 456, 789};
const int16_t kInt16Value = 123;
const int64_t kInt64Value = 0x1234000000000000;
const vector<int64_t> kInt64sValue{0x2345000000000000, 0x6789000000000000};
const double kDoubleValue = 1.1;
const vector<double> kDoublesValue{2.2, 3.3};
const size_t kDoublesValueSize = kDoublesValue.size();
const RpcIdentifier kRpcIdentifierValue("/org/chromium/test");
const vector<RpcIdentifier> kRpcIdentifiersValue{
    RpcIdentifier("/org/chromium/test0"), RpcIdentifier("/org/chromium/test1"),
    RpcIdentifier("/org/chromium/test2")};
const char kStringValue[] = "StringValue";
const map<string, string> kStringmapValue = {{"key", "value"}};
const vector<string> kStringsValue = {"StringsValue1", "StringsValue2"};
const uint32_t kUintValue = 654;
const uint16_t kUint16Value = 123;
const uint8_t kUint8Value = 3;
const vector<uint8_t> kUint8sValue{1, 2};
const vector<uint32_t> kUint32sValue{1, 2};
const int32_t kNestedInt32Value = 1;
}  // namespace

class KeyValueStoreTest : public Test {
 public:
  KeyValueStoreTest() = default;

  void SetOneOfEachType(KeyValueStore* store,
                        const KeyValueStore& nested_key_value_store_value) {
    store->SetBool(kBoolKey, kBoolValue);
    store->SetBools(kBoolsKey, kBoolsValue);
    store->SetByteArrays(kByteArraysKey, kByteArraysValue);
    store->SetInt(kIntKey, kIntValue);
    store->SetInts(kIntsKey, kIntsValue);
    store->SetInt16(kInt16Key, kInt16Value);
    store->SetInt64(kInt64Key, kInt64Value);
    store->SetInt64s(kInt64sKey, kInt64sValue);
    store->SetDouble(kDoubleKey, kDoubleValue);
    store->SetDoubles(kDoublesKey, kDoublesValue);
    store->SetKeyValueStore(kKeyValueStoreKey, nested_key_value_store_value);
    store->SetRpcIdentifier(kRpcIdentifierKey, kRpcIdentifierValue);
    store->SetRpcIdentifiers(kRpcIdentifiersKey, kRpcIdentifiersValue);
    store->SetString(kStringKey, kStringValue);
    store->SetStringmap(kStringmapKey, kStringmapValue);
    store->SetStrings(kStringsKey, kStringsValue);
    store->SetUint(kUintKey, kUintValue);
    store->SetUint16(kUint16Key, kUint16Value);
    store->SetUint8(kUint8Key, kUint8Value);
    store->SetUint8s(kUint8sKey, kUint8sValue);
    store->SetUint32s(kUint32sKey, kUint32sValue);
  }

 protected:
  KeyValueStore store_;
};

TEST_F(KeyValueStoreTest, Any) {
  EXPECT_FALSE(store_.ContainsVariant(kStringKey));
  store_.SetVariant(kStringKey, brillo::Any(string(kStringValue)));
  EXPECT_TRUE(store_.ContainsVariant(kStringKey));
  EXPECT_EQ(string(kStringValue), store_.GetVariant(kStringKey).Get<string>());
  store_.Remove(kStringKey);
  EXPECT_FALSE(store_.ContainsVariant(kStringKey));
}

TEST_F(KeyValueStoreTest, Bool) {
  const bool kDefaultValue = true;
  const bool kValue = false;
  EXPECT_FALSE(store_.Contains<bool>(kBoolKey));
  EXPECT_EQ(kDefaultValue, store_.LookupBool(kBoolKey, kDefaultValue));
  store_.SetBool(kBoolKey, kValue);
  EXPECT_TRUE(store_.Contains<bool>(kBoolKey));
  // TODO(shenhan): investigate if a newer version of gtest handles EXPECT_EQ
  // for bools in a manner that gcc 4.7 is happy with. (Improper conversion from
  // "false" to "NULL").
  EXPECT_EQ(static_cast<int>(kValue),
            static_cast<int>(store_.LookupBool(kBoolKey, kDefaultValue)));
  EXPECT_EQ(static_cast<int>(kValue),
            static_cast<int>(store_.Get<bool>(kBoolKey)));
}

TEST_F(KeyValueStoreTest, Bools) {
  EXPECT_FALSE(store_.Contains<vector<bool>>(kBoolsKey));
  store_.SetBools(kBoolsKey, kBoolsValue);
  EXPECT_TRUE(store_.Contains<vector<bool>>(kBoolsKey));
  EXPECT_EQ(kBoolsValue, store_.Get<vector<bool>>(kBoolsKey));
}

TEST_F(KeyValueStoreTest, ByteArrays) {
  EXPECT_FALSE(store_.Contains<ByteArrays>(kByteArraysKey));
  store_.SetByteArrays(kByteArraysKey, kByteArraysValue);
  EXPECT_TRUE(store_.Contains<ByteArrays>(kByteArraysKey));
  EXPECT_EQ(kByteArraysValue, store_.Get<ByteArrays>(kByteArraysKey));
  store_.Remove(kByteArraysKey);
  EXPECT_FALSE(store_.Contains<ByteArrays>(kByteArraysKey));
}

TEST_F(KeyValueStoreTest, Int) {
  EXPECT_FALSE(store_.Contains<int32_t>(kIntKey));
  const int kDefaultValue = 789;
  const int kValue = 456;
  EXPECT_EQ(kDefaultValue, store_.LookupInt(kIntKey, kDefaultValue));
  store_.SetInt(kIntKey, kValue);
  EXPECT_TRUE(store_.Contains<int32_t>(kIntKey));
  EXPECT_EQ(kValue, store_.Get<int32_t>(kIntKey));
  EXPECT_EQ(kValue, store_.LookupInt(kIntKey, kDefaultValue));
  store_.Remove(kIntKey);
  EXPECT_FALSE(store_.Contains<int32_t>(kIntKey));
}

TEST_F(KeyValueStoreTest, Ints) {
  EXPECT_FALSE(store_.Contains<vector<int32_t>>(kIntsKey));
  store_.SetInts(kIntsKey, kIntsValue);
  EXPECT_TRUE(store_.Contains<vector<int32_t>>(kIntsKey));
  EXPECT_EQ(kIntsValue, store_.Get<vector<int32_t>>(kIntsKey));
}

TEST_F(KeyValueStoreTest, Int16) {
  EXPECT_FALSE(store_.Contains<int16_t>(kInt16Key));
  store_.SetInt16(kInt16Key, kInt16Value);
  EXPECT_TRUE(store_.Contains<int16_t>(kInt16Key));
  EXPECT_EQ(kInt16Value, store_.Get<int16_t>(kInt16Key));
  store_.Remove(kInt16Key);
  EXPECT_FALSE(store_.Contains<int16_t>(kInt16Key));
}

TEST_F(KeyValueStoreTest, Int64) {
  EXPECT_FALSE(store_.Contains<int64_t>(kInt64Key));
  store_.SetInt64(kInt64Key, kInt64Value);
  EXPECT_TRUE(store_.Contains<int64_t>(kInt64Key));
  EXPECT_EQ(kInt64Value, store_.Get<int64_t>(kInt64Key));
}

TEST_F(KeyValueStoreTest, Int64s) {
  EXPECT_FALSE(store_.Contains<vector<int64_t>>(kInt64sKey));
  store_.SetInt64s(kInt64sKey, kInt64sValue);
  EXPECT_TRUE(store_.Contains<vector<int64_t>>(kInt64sKey));
  EXPECT_EQ(kInt64sValue, store_.Get<vector<int64_t>>(kInt64sKey));
}

TEST_F(KeyValueStoreTest, Double) {
  EXPECT_FALSE(store_.Contains<double>(kDoubleKey));
  store_.SetDouble(kDoubleKey, kDoubleValue);
  EXPECT_TRUE(store_.Contains<double>(kDoubleKey));
  EXPECT_DOUBLE_EQ(kDoubleValue, store_.Get<double>(kDoubleKey));
}

TEST_F(KeyValueStoreTest, Doubles) {
  EXPECT_FALSE(store_.Contains<vector<double>>(kDoublesKey));
  store_.SetDoubles(kDoublesKey, kDoublesValue);
  EXPECT_TRUE(store_.Contains<vector<double>>(kDoublesKey));
  vector<double> ret = store_.Get<vector<double>>(kDoublesKey);
  EXPECT_EQ(kDoublesValueSize, ret.size());
  for (size_t i = 0; i < kDoublesValueSize; ++i) {
    EXPECT_DOUBLE_EQ(kDoublesValue[i], ret[i]);
  }
}

TEST_F(KeyValueStoreTest, KeyValueStore) {
  KeyValueStore value;
  value.SetStringmap(kStringmapKey, kStringmapValue);
  EXPECT_FALSE(store_.Contains<KeyValueStore>(kKeyValueStoreKey));
  store_.SetKeyValueStore(kKeyValueStoreKey, value);
  EXPECT_TRUE(store_.Contains<KeyValueStore>(kKeyValueStoreKey));
  EXPECT_EQ(value, store_.Get<KeyValueStore>(kKeyValueStoreKey));
  store_.Remove(kKeyValueStoreKey);
  EXPECT_FALSE(store_.Contains<KeyValueStore>(kKeyValueStoreKey));
}

TEST_F(KeyValueStoreTest, RpcIdentifier) {
  EXPECT_FALSE(store_.Contains<RpcIdentifier>(kRpcIdentifierKey));
  store_.SetRpcIdentifier(kRpcIdentifierKey, kRpcIdentifierValue);
  EXPECT_TRUE(store_.Contains<RpcIdentifier>(kRpcIdentifierKey));
  EXPECT_EQ(kRpcIdentifierValue, store_.Get<RpcIdentifier>(kRpcIdentifierKey));
  store_.Remove(kRpcIdentifierKey);
  EXPECT_FALSE(store_.Contains<RpcIdentifier>(kRpcIdentifierKey));
}

TEST_F(KeyValueStoreTest, RpcIdentifiers) {
  EXPECT_FALSE(store_.Contains<RpcIdentifiers>(kRpcIdentifiersKey));
  store_.SetRpcIdentifiers(kRpcIdentifiersKey, kRpcIdentifiersValue);
  EXPECT_TRUE(store_.Contains<RpcIdentifiers>(kRpcIdentifiersKey));
  EXPECT_EQ(kRpcIdentifiersValue,
            store_.Get<RpcIdentifiers>(kRpcIdentifiersKey));
  store_.Remove(kRpcIdentifiersKey);
  EXPECT_FALSE(store_.Contains<RpcIdentifiers>(kRpcIdentifiersKey));
}

TEST_F(KeyValueStoreTest, String) {
  const string kDefaultValue("bar");
  const string kValue("baz");
  EXPECT_FALSE(store_.Contains<string>(kStringKey));
  EXPECT_EQ(kDefaultValue, store_.LookupString(kStringKey, kDefaultValue));
  store_.SetString(kStringKey, kValue);
  EXPECT_TRUE(store_.Contains<string>(kStringKey));
  EXPECT_EQ(kValue, store_.LookupString(kStringKey, kDefaultValue));
  EXPECT_EQ(kValue, store_.Get<string>(kStringKey));
  store_.Remove(kStringKey);
  EXPECT_FALSE(store_.Contains<string>(kStringKey));
  EXPECT_EQ(kDefaultValue, store_.LookupString(kStringKey, kDefaultValue));
}

TEST_F(KeyValueStoreTest, Stringmap) {
  EXPECT_FALSE(store_.Contains<Stringmap>(kStringmapKey));
  store_.SetStringmap(kStringmapKey, kStringmapValue);
  EXPECT_TRUE(store_.Contains<Stringmap>(kStringmapKey));
  EXPECT_EQ(kStringmapValue, store_.Get<Stringmap>(kStringmapKey));
  store_.Remove(kStringmapKey);
  EXPECT_FALSE(store_.Contains<Stringmap>(kStringmapKey));
}

TEST_F(KeyValueStoreTest, Strings) {
  EXPECT_FALSE(store_.Contains<Strings>(kStringsKey));
  store_.SetStrings(kStringsKey, kStringsValue);
  EXPECT_TRUE(store_.Contains<Strings>(kStringsKey));
  EXPECT_EQ(kStringsValue, store_.Get<Strings>(kStringsKey));
  store_.Remove(kStringsKey);
  EXPECT_FALSE(store_.Contains<Strings>(kStringsKey));
}

TEST_F(KeyValueStoreTest, Uint) {
  EXPECT_FALSE(store_.Contains<uint32_t>(kUintKey));
  store_.SetUint(kUintKey, kUintValue);
  EXPECT_TRUE(store_.Contains<uint32_t>(kUintKey));
  EXPECT_EQ(kUintValue, store_.Get<uint32_t>(kUintKey));
}

TEST_F(KeyValueStoreTest, Uint16) {
  EXPECT_FALSE(store_.Contains<uint16_t>(kUint16Key));
  store_.SetUint16(kUint16Key, kUint16Value);
  EXPECT_TRUE(store_.Contains<uint16_t>(kUint16Key));
  EXPECT_EQ(kUint16Value, store_.Get<uint16_t>(kUint16Key));
}

TEST_F(KeyValueStoreTest, Uint8) {
  EXPECT_FALSE(store_.Contains<uint8_t>(kUint8Key));
  store_.SetUint8(kUint8Key, kUint8Value);
  EXPECT_TRUE(store_.Contains<uint8_t>(kUint8Key));
  EXPECT_EQ(kUint8Value, store_.Get<uint8_t>(kUint8Key));
  store_.Remove(kUint8Key);
  EXPECT_FALSE(store_.Contains<uint8_t>(kUint8Key));
}

TEST_F(KeyValueStoreTest, Uint8s) {
  EXPECT_FALSE(store_.Contains<vector<uint8_t>>(kUint8sKey));
  store_.SetUint8s(kUint8sKey, kUint8sValue);
  EXPECT_TRUE(store_.Contains<vector<uint8_t>>(kUint8sKey));
  EXPECT_EQ(kUint8sValue, store_.Get<vector<uint8_t>>(kUint8sKey));
  store_.Remove(kUint8sKey);
  EXPECT_FALSE(store_.Contains<vector<uint8_t>>(kUint8sKey));
}

TEST_F(KeyValueStoreTest, Uint32s) {
  EXPECT_FALSE(store_.Contains<vector<uint32_t>>(kUint32sKey));
  store_.SetUint32s(kUint32sKey, kUint32sValue);
  EXPECT_TRUE(store_.Contains<vector<uint32_t>>(kUint32sKey));
  EXPECT_EQ(kUint32sValue, store_.Get<vector<uint32_t>>(kUint32sKey));
  store_.Remove(kUint32sKey);
  EXPECT_FALSE(store_.Contains<vector<uint32_t>>(kUint32sKey));
}

TEST_F(KeyValueStoreTest, DoubleRemove) {
  const string kKey("foo");
  // Make sure we don't get an exception/infinite loop if we do a
  // "Remove()" when the key does not exist.
  store_.Remove(kKey);
  store_.Remove(kKey);
  store_.Remove(kKey);
  store_.Remove(kKey);
}

TEST_F(KeyValueStoreTest, Clear) {
  EXPECT_TRUE(store_.IsEmpty());
  SetOneOfEachType(&store_, KeyValueStore());

  EXPECT_TRUE(store_.Contains<bool>(kBoolKey));
  EXPECT_TRUE(store_.Contains<vector<bool>>(kBoolsKey));
  EXPECT_TRUE(store_.Contains<ByteArrays>(kByteArraysKey));
  EXPECT_TRUE(store_.Contains<int32_t>(kIntKey));
  EXPECT_TRUE(store_.Contains<vector<int32_t>>(kIntsKey));
  EXPECT_TRUE(store_.Contains<int16_t>(kInt16Key));
  EXPECT_TRUE(store_.Contains<int64_t>(kInt64Key));
  EXPECT_TRUE(store_.Contains<vector<int64_t>>(kInt64sKey));
  EXPECT_TRUE(store_.Contains<double>(kDoubleKey));
  EXPECT_TRUE(store_.Contains<vector<double>>(kDoublesKey));
  EXPECT_TRUE(store_.Contains<KeyValueStore>(kKeyValueStoreKey));
  EXPECT_TRUE(store_.Contains<RpcIdentifier>(kRpcIdentifierKey));
  EXPECT_TRUE(store_.Contains<string>(kStringKey));
  EXPECT_TRUE(store_.Contains<Stringmap>(kStringmapKey));
  EXPECT_TRUE(store_.Contains<Strings>(kStringsKey));
  EXPECT_TRUE(store_.Contains<uint32_t>(kUintKey));
  EXPECT_TRUE(store_.Contains<uint16_t>(kUint16Key));
  EXPECT_TRUE(store_.Contains<vector<uint8_t>>(kUint8sKey));
  EXPECT_TRUE(store_.Contains<vector<uint32_t>>(kUint32sKey));
  EXPECT_FALSE(store_.IsEmpty());
  store_.Clear();
  EXPECT_TRUE(store_.IsEmpty());
  EXPECT_FALSE(store_.Contains<bool>(kBoolKey));
  EXPECT_FALSE(store_.Contains<vector<bool>>(kBoolsKey));
  EXPECT_FALSE(store_.Contains<ByteArrays>(kByteArraysKey));
  EXPECT_FALSE(store_.Contains<int32_t>(kIntKey));
  EXPECT_FALSE(store_.Contains<vector<int32_t>>(kIntsKey));
  EXPECT_FALSE(store_.Contains<int16_t>(kInt16Key));
  EXPECT_FALSE(store_.Contains<int64_t>(kInt64Key));
  EXPECT_FALSE(store_.Contains<vector<int64_t>>(kInt64sKey));
  EXPECT_FALSE(store_.Contains<double>(kDoubleKey));
  EXPECT_FALSE(store_.Contains<vector<double>>(kDoublesKey));
  EXPECT_FALSE(store_.Contains<KeyValueStore>(kKeyValueStoreKey));
  EXPECT_FALSE(store_.Contains<RpcIdentifier>(kRpcIdentifierKey));
  EXPECT_FALSE(store_.Contains<string>(kStringKey));
  EXPECT_FALSE(store_.Contains<Stringmap>(kStringmapKey));
  EXPECT_FALSE(store_.Contains<Strings>(kStringsKey));
  EXPECT_FALSE(store_.Contains<uint32_t>(kUintKey));
  EXPECT_FALSE(store_.Contains<uint16_t>(kUint16Key));
  EXPECT_FALSE(store_.Contains<vector<uint8_t>>(kUint8sKey));
  EXPECT_FALSE(store_.Contains<vector<uint32_t>>(kUint32sKey));
}

TEST_F(KeyValueStoreTest, Equals) {
  KeyValueStore first, second;

  first.SetBool("boolKey", true);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  second.SetBool("boolKey", true);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetBool("boolKey", true);
  second.SetBool("boolOtherKey", true);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetBool("boolKey", true);
  second.SetBool("boolKey", false);
  EXPECT_NE(first, second);

  const vector<bool> kBools1{true, false};
  const vector<bool> kBools2{false, true};

  first.Clear();
  second.Clear();
  first.SetBools("boolsKey", kBools1);
  second.SetBools("boolsOtherKey", kBools1);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetBools("boolsKey", kBools1);
  second.SetBools("boolsKey", kBools2);
  EXPECT_NE(first, second);

  const vector<vector<uint8_t>> kByteArrays1{{1, 2}};
  const vector<vector<uint8_t>> kByteArrays2{{3, 4}};

  first.Clear();
  second.Clear();
  first.SetByteArrays("byteArraysKey", kByteArrays1);
  second.SetByteArrays("byteArraysOtherKey", kByteArrays1);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetByteArrays("byteArraysKey", kByteArrays1);
  second.SetByteArrays("byteArraysKey", kByteArrays2);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetInt("intKey", 123);
  second.SetInt("intOtherKey", 123);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetInt("intKey", 123);
  second.SetInt("intKey", 456);
  EXPECT_NE(first, second);

  const vector<int32_t> kInts1{1, 2};
  const vector<int32_t> kInts2{3, 4};

  first.Clear();
  second.Clear();
  first.SetInts("intsKey", kInts1);
  second.SetInts("intsOtherKey", kInts1);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetInts("intsKey", kInts1);
  second.SetInts("intsKey", kInts2);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetInt16("int16Key", 123);
  second.SetInt16("int16OtherKey", 123);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetInt16("int16Key", 123);
  second.SetInt16("int16Key", 456);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetInt64("int64Key", 0x1234000000000000);
  second.SetInt64("int64OtherKey", 0x1234000000000000);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetInt64("int64Key", 0x6789000000000000);
  second.SetInt64("int64Key", 0x2345000000000000);
  EXPECT_NE(first, second);

  const vector<int64_t> kInt64s1{0x1000000000000000, 0x2000000000000000};
  const vector<int64_t> kInt64s2{0x3000000000000000, 0x4000000000000000};

  first.Clear();
  second.Clear();
  first.SetInt64s("int64sKey", kInt64s1);
  second.SetInt64s("int64sOtherKey", kInt64s1);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetInt64s("int64sKey", kInt64s1);
  second.SetInt64s("int64sKey", kInt64s2);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetDouble("doubleKey", 1.1);
  second.SetDouble("doubleOtherKey", 1.1);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetDouble("doubleKey", 2.3);
  second.SetDouble("doubleKey", 4.5);
  EXPECT_NE(first, second);

  const vector<double> kDoubles1{1.1, 2.2};
  const vector<double> kDoubles2{3.3, 4.4};

  first.Clear();
  second.Clear();
  first.SetDoubles("doublesKey", kDoubles1);
  second.SetDoubles("doublesOtherKey", kDoubles1);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetDoubles("doublesKey", kDoubles1);
  second.SetDoubles("doublesKey", kDoubles2);
  EXPECT_NE(first, second);

  KeyValueStore key_value0;
  key_value0.SetInt("intKey", 123);
  KeyValueStore key_value1;
  key_value1.SetInt("intOtherKey", 123);

  first.Clear();
  second.Clear();
  first.SetKeyValueStore("keyValueKey", key_value0);
  second.SetKeyValueStore("keyValueKey", key_value1);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetKeyValueStore("keyValueKey", key_value0);
  second.SetKeyValueStore("keyValueOtherKey", key_value0);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetRpcIdentifier("rpcIdentifierKey", RpcIdentifier("rpcIdentifier"));
  second.SetRpcIdentifier("rpcIdentifierOtherKey",
                          RpcIdentifier("rpcIdentifier"));
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetRpcIdentifier("rpcIdentifierKey", RpcIdentifier("rpcIdentifier"));
  second.SetRpcIdentifier("rpcIdentifierKey",
                          RpcIdentifier("otherRpcIdentifier"));
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetString("stringKey", "string");
  second.SetString("stringOtherKey", "string");
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetString("stringKey", "string");
  second.SetString("stringKey", "otherString");
  EXPECT_NE(first, second);

  const map<string, string> kStringmap1{{"key", "value"}};
  const map<string, string> kStringmap2{{"otherKey", "value"}};
  const map<string, string> kStringmap3{{"key", "otherValue"}};

  first.Clear();
  second.Clear();
  first.SetStringmap("stringmapKey", kStringmap1);
  second.SetStringmap("stringmapOtherKey", kStringmap1);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetStringmap("stringmapKey", kStringmap1);
  second.SetStringmap("stringmapKey", kStringmap2);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetStringmap("stringmapKey", kStringmap1);
  second.SetStringmap("stringmapKey", kStringmap3);
  EXPECT_NE(first, second);

  const vector<string> kStrings1{"value"};
  const vector<string> kStrings2{"otherValue"};

  first.Clear();
  second.Clear();
  first.SetStrings("stringsKey", kStrings1);
  second.SetStrings("stringsOtherKey", kStrings1);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetStrings("stringsKey", kStrings1);
  second.SetStrings("stringsKey", kStrings2);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetUint("uintKey", 1);
  second.SetUint("uintOtherKey", 1);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetUint("uintKey", 1);
  second.SetUint("uintKey", 2);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetUint16("uint16Key", 1);
  second.SetUint16("uint16OtherKey", 1);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetUint16("uint16Key", 1);
  second.SetUint16("uint16Key", 2);
  EXPECT_NE(first, second);

  const vector<uint8_t> kUint8s1{1};
  const vector<uint8_t> kUint8s2{2};

  first.Clear();
  second.Clear();
  first.SetUint8s("uint8sKey", kUint8s1);
  second.SetUint8s("uint8sOtherKey", kUint8s1);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetUint8s("uint8sKey", kUint8s1);
  second.SetUint8s("uint8sKey", kUint8s2);
  EXPECT_NE(first, second);

  const vector<uint32_t> kUint32s1{1};
  const vector<uint32_t> kUint32s2{2};

  first.Clear();
  second.Clear();
  first.SetUint32s("uint32sKey", kUint32s1);
  second.SetUint32s("uint32sOtherKey", kUint32s1);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetUint32s("uint32sKey", kUint32s1);
  second.SetUint32s("uint32sKey", kUint32s2);
  EXPECT_NE(first, second);

  first.Clear();
  second.Clear();
  first.SetBool("boolKey", true);
  first.SetBools("boolsKey", kBools1);
  first.SetByteArrays("byteArraysKey", kByteArrays1);
  first.SetInt("intKey", 123);
  first.SetInts("intsKey", kInts1);
  first.SetInt16("int16Key", 123);
  first.SetInt64("int64Key", 0x1234000000000000);
  first.SetInt64s("int64sKey", kInt64s1);
  first.SetDouble("doubleKey", 1.1);
  first.SetDoubles("doublesKey", kDoubles1);
  first.SetRpcIdentifier("rpcIdentifierKey", RpcIdentifier("rpcid"));
  first.SetString("stringKey", "value");
  first.SetStringmap("stringmapKey", kStringmap1);
  first.SetStrings("stringsKey", kStrings1);
  first.SetUint("uintKey", 1);
  first.SetUint16("uint16Key", 1);
  first.SetUint8s("uint8sKey", kUint8s1);
  first.SetUint32s("uint32sKey", kUint32s1);
  second.SetBool("boolKey", true);
  second.SetBools("boolsKey", kBools1);
  second.SetByteArrays("byteArraysKey", kByteArrays1);
  second.SetInt("intKey", 123);
  second.SetInts("intsKey", kInts1);
  second.SetInt16("int16Key", 123);
  second.SetInt64("int64Key", 0x1234000000000000);
  second.SetInt64s("int64sKey", kInt64s1);
  second.SetDouble("doubleKey", 1.1);
  second.SetDoubles("doublesKey", kDoubles1);
  second.SetRpcIdentifier("rpcIdentifierKey", RpcIdentifier("rpcid"));
  second.SetString("stringKey", "value");
  second.SetStringmap("stringmapKey", kStringmap1);
  second.SetStrings("stringsKey", kStrings1);
  second.SetUint("uintKey", 1);
  second.SetUint16("uint16Key", 1);
  second.SetUint8s("uint8sKey", kUint8s1);
  second.SetUint32s("uint32sKey", kUint32s1);
  EXPECT_EQ(first, second);
}

TEST_F(KeyValueStoreTest, CopyFrom) {
  KeyValueStore donor;
  KeyValueStore keyValueStoreValue;
  keyValueStoreValue.SetInt(kIntKey, kIntValue);
  SetOneOfEachType(&donor, keyValueStoreValue);

  EXPECT_TRUE(store_.IsEmpty());
  store_.CopyFrom(donor);
  EXPECT_FALSE(store_.IsEmpty());
  EXPECT_EQ(donor, store_);
}

TEST_F(KeyValueStoreTest, ConvertToVariantDictionary) {
  KeyValueStore store;
  KeyValueStore nested_store;
  nested_store.SetInt(kNestedInt32Key, kNestedInt32Value);
  SetOneOfEachType(&store, nested_store);

  brillo::VariantDictionary dict =
      KeyValueStore::ConvertToVariantDictionary(store);
  EXPECT_EQ(21, dict.size());
  EXPECT_EQ(kStringValue, dict[kStringKey].Get<string>());
  map<string, string> stringmap_value =
      dict[kStringmapKey].Get<map<string, string>>();
  EXPECT_EQ(kStringmapValue, stringmap_value);
  EXPECT_EQ(kStringsValue, dict[kStringsKey].Get<vector<string>>());
  EXPECT_EQ(kBoolValue, dict[kBoolKey].Get<bool>());
  EXPECT_EQ(kBoolsValue, dict[kBoolsKey].Get<vector<bool>>());
  EXPECT_EQ(kIntValue, dict[kIntKey].Get<int32_t>());
  EXPECT_EQ(kIntsValue, dict[kIntsKey].Get<vector<int32_t>>());
  EXPECT_EQ(kUintValue, dict[kUintKey].Get<uint32_t>());
  EXPECT_EQ(kByteArraysValue,
            dict[kByteArraysKey].Get<vector<vector<uint8_t>>>());
  EXPECT_EQ(kInt16Value, dict[kInt16Key].Get<int16_t>());
  EXPECT_EQ(kRpcIdentifierValue, dict[kRpcIdentifierKey].Get<RpcIdentifier>());
  EXPECT_EQ(kUint16Value, dict[kUint16Key].Get<uint16_t>());
  EXPECT_EQ(kInt64Value, dict[kInt64Key].Get<int64_t>());
  EXPECT_EQ(kInt64sValue, dict[kInt64sKey].Get<vector<int64_t>>());
  EXPECT_DOUBLE_EQ(kDoubleValue, dict[kDoubleKey].Get<double>());
  vector<double> doubles_value = dict[kDoublesKey].Get<vector<double>>();
  EXPECT_EQ(kDoublesValueSize, doubles_value.size());
  for (size_t i = 0; i < kDoublesValueSize; ++i) {
    EXPECT_DOUBLE_EQ(kDoublesValue[i], doubles_value[i]);
  }
  EXPECT_EQ(kUint8sValue, dict[kUint8sKey].Get<vector<uint8_t>>());
  EXPECT_EQ(kUint32sValue, dict[kUint32sKey].Get<vector<uint32_t>>());
  brillo::VariantDictionary nested_dict =
      dict[kKeyValueStoreKey].Get<brillo::VariantDictionary>();
  EXPECT_EQ(kNestedInt32Value, nested_dict[kNestedInt32Key].Get<int32_t>());
}

TEST_F(KeyValueStoreTest, ConvertFromVariantDictionary) {
  brillo::VariantDictionary dict;
  dict[kStringKey] = brillo::Any(string(kStringValue));
  dict[kStringmapKey] = brillo::Any(kStringmapValue);
  dict[kStringsKey] = brillo::Any(kStringsValue);
  dict[kBoolKey] = brillo::Any(kBoolValue);
  dict[kBoolsKey] = brillo::Any(kBoolsValue);
  dict[kIntKey] = brillo::Any(kIntValue);
  dict[kIntsKey] = brillo::Any(kIntsValue);
  dict[kUintKey] = brillo::Any(kUintValue);
  dict[kByteArraysKey] = brillo::Any(kByteArraysValue);
  dict[kInt16Key] = brillo::Any(kInt16Value);
  dict[kInt64Key] = brillo::Any(kInt64Value);
  dict[kInt64sKey] = brillo::Any(kInt64sValue);
  dict[kDoubleKey] = brillo::Any(kDoubleValue);
  dict[kDoublesKey] = brillo::Any(kDoublesValue);
  dict[kRpcIdentifierKey] = brillo::Any(kRpcIdentifierValue);
  dict[kUint16Key] = brillo::Any(kUint16Value);
  dict[kUint8sKey] = brillo::Any(kUint8sValue);
  dict[kUint32sKey] = brillo::Any(kUint32sValue);
  brillo::VariantDictionary nested_dict;
  nested_dict[kNestedInt32Key] = brillo::Any(kNestedInt32Value);
  dict[kKeyValueStoreKey] = brillo::Any(nested_dict);

  KeyValueStore store = KeyValueStore::ConvertFromVariantDictionary(dict);
  EXPECT_TRUE(store.Contains<string>(kStringKey));
  EXPECT_EQ(kStringValue, store.Get<string>(kStringKey));
  EXPECT_TRUE(store.Contains<Stringmap>(kStringmapKey));
  EXPECT_EQ(kStringmapValue, store.Get<Stringmap>(kStringmapKey));
  EXPECT_TRUE(store.Contains<Strings>(kStringsKey));
  EXPECT_EQ(kStringsValue, store.Get<Strings>(kStringsKey));
  EXPECT_TRUE(store.Contains<bool>(kBoolKey));
  EXPECT_EQ(kBoolValue, store.Get<bool>(kBoolKey));
  EXPECT_TRUE(store.Contains<vector<bool>>(kBoolsKey));
  EXPECT_EQ(kBoolsValue, store.Get<vector<bool>>(kBoolsKey));
  EXPECT_TRUE(store.Contains<int32_t>(kIntKey));
  EXPECT_EQ(kIntValue, store.Get<int32_t>(kIntKey));
  EXPECT_TRUE(store.Contains<vector<int32_t>>(kIntsKey));
  EXPECT_EQ(kIntsValue, store.Get<vector<int32_t>>(kIntsKey));
  EXPECT_TRUE(store.Contains<uint32_t>(kUintKey));
  EXPECT_EQ(kUintValue, store.Get<uint32_t>(kUintKey));
  EXPECT_TRUE(store.Contains<ByteArrays>(kByteArraysKey));
  EXPECT_EQ(kByteArraysValue, store.Get<ByteArrays>(kByteArraysKey));
  EXPECT_TRUE(store.Contains<int16_t>(kInt16Key));
  EXPECT_EQ(kInt16Value, store.Get<int16_t>(kInt16Key));
  EXPECT_TRUE(store.Contains<int64_t>(kInt64Key));
  EXPECT_EQ(kInt64Value, store.Get<int64_t>(kInt64Key));
  EXPECT_TRUE(store.Contains<vector<int64_t>>(kInt64sKey));
  EXPECT_EQ(kInt64sValue, store.Get<vector<int64_t>>(kInt64sKey));
  EXPECT_TRUE(store.Contains<double>(kDoubleKey));
  EXPECT_DOUBLE_EQ(kDoubleValue, store.Get<double>(kDoubleKey));
  EXPECT_TRUE(store.Contains<vector<double>>(kDoublesKey));
  vector<double> doubles_value = store.Get<vector<double>>(kDoublesKey);
  EXPECT_EQ(kDoublesValueSize, doubles_value.size());
  for (size_t i = 0; i < kDoublesValueSize; ++i) {
    EXPECT_DOUBLE_EQ(kDoublesValue[i], doubles_value[i]);
  }
  EXPECT_TRUE(store.Contains<RpcIdentifier>(kRpcIdentifierKey));
  EXPECT_EQ(kRpcIdentifierValue, store.Get<RpcIdentifier>(kRpcIdentifierKey));
  EXPECT_TRUE(store.Contains<uint16_t>(kUint16Key));
  EXPECT_EQ(kUint16Value, store.Get<uint16_t>(kUint16Key));
  EXPECT_TRUE(store.Contains<vector<uint8_t>>(kUint8sKey));
  EXPECT_EQ(kUint8sValue, store.Get<vector<uint8_t>>(kUint8sKey));
  EXPECT_TRUE(store.Contains<vector<uint32_t>>(kUint32sKey));
  EXPECT_EQ(kUint32sValue, store.Get<vector<uint32_t>>(kUint32sKey));
  EXPECT_TRUE(store.Contains<KeyValueStore>(kKeyValueStoreKey));
  KeyValueStore nested_store;
  nested_store.SetInt(kNestedInt32Key, kNestedInt32Value);
  EXPECT_EQ(nested_store, store.Get<KeyValueStore>(kKeyValueStoreKey));
}

}  // namespace shill
