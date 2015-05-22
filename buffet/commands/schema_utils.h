// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_COMMANDS_SCHEMA_UTILS_H_
#define BUFFET_COMMANDS_SCHEMA_UTILS_H_

#include <limits>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include <base/values.h>
#include <chromeos/any.h>
#include <chromeos/errors/error.h>
#include <chromeos/variant_dictionary.h>

namespace buffet {

class PropType;
class PropValue;
class ObjectSchema;
class ObjectValue;

namespace native_types {
// C++ representation of object values.
using Object = std::map<std::string, std::shared_ptr<const PropValue>>;
// C++ representation of array of values.
using Array = std::vector<std::shared_ptr<const PropValue>>;
}  // namespace native_types

// Converts an object to string.
std::string ToString(const native_types::Object& obj);

// Converts an array to string.
std::string ToString(const native_types::Array& arr);

// InheritableAttribute class is used for specifying various command parameter
// attributes that can be inherited from a base (parent) schema.
// The |value| still specifies the actual attribute values, whether it
// is inherited or overridden, while |is_inherited| can be used to identify
// if the attribute was inherited (true) or overridden (false).
template<typename T>
class InheritableAttribute final {
 public:
  InheritableAttribute() = default;
  explicit InheritableAttribute(T val)
      : value(std::move(val)), is_inherited(true) {}
  InheritableAttribute(T val, bool inherited)
      : value(std::move(val)), is_inherited(inherited) {}
  T value{};
  bool is_inherited{true};
};

// A bunch of helper function to create base::Value for specific C++ classes,
// including vectors of types. These are used in template classes below
// to simplify specialization logic.
std::unique_ptr<base::Value> TypedValueToJson(bool value,
                                              chromeos::ErrorPtr* error);
std::unique_ptr<base::Value> TypedValueToJson(int value,
                                              chromeos::ErrorPtr* error);
std::unique_ptr<base::Value> TypedValueToJson(double value,
                                              chromeos::ErrorPtr* error);
std::unique_ptr<base::Value> TypedValueToJson(const std::string& value,
                                              chromeos::ErrorPtr* error);
std::unique_ptr<base::Value> TypedValueToJson(const native_types::Object& value,
                                              chromeos::ErrorPtr* error);
std::unique_ptr<base::Value> TypedValueToJson(const native_types::Array& value,
                                              chromeos::ErrorPtr* error);
template<typename T>
std::unique_ptr<base::Value> TypedValueToJson(const std::vector<T>& values,
                                              chromeos::ErrorPtr* error) {
  std::unique_ptr<base::ListValue> list(new base::ListValue);
  for (const auto& v : values) {
    auto json = TypedValueToJson(v, error);
    if (!json)
      return std::unique_ptr<base::Value>();
    list->Append(json.release());
  }
  return std::move(list);
}

// Similarly to TypedValueToJson() function above, the following overloaded
// helper methods allow to extract specific C++ data types from base::Value.
// Also used in template classes below to simplify specialization logic.
bool TypedValueFromJson(const base::Value* value_in,
                        const PropType* type,
                        bool* value_out,
                        chromeos::ErrorPtr* error);
bool TypedValueFromJson(const base::Value* value_in,
                        const PropType* type,
                        int* value_out,
                        chromeos::ErrorPtr* error);
bool TypedValueFromJson(const base::Value* value_in,
                        const PropType* type,
                        double* value_out,
                        chromeos::ErrorPtr* error);
bool TypedValueFromJson(const base::Value* value_in,
                        const PropType* type,
                        std::string* value_out,
                        chromeos::ErrorPtr* error);
bool TypedValueFromJson(const base::Value* value_in,
                        const PropType* type,
                        native_types::Object* value_out,
                        chromeos::ErrorPtr* error);
bool TypedValueFromJson(const base::Value* value_in,
                        const PropType* type,
                        native_types::Array* value_out,
                        chromeos::ErrorPtr* error);

bool operator==(const native_types::Object& obj1,
                const native_types::Object& obj2);
bool operator==(const native_types::Array& arr1,
                const native_types::Array& arr2);

// CompareValue is a helper function to help with implementing EqualsTo operator
// for various data types. For most scalar types it is using operator==(),
// however, for floating point values, rounding errors in binary representation
// of IEEE floats/doubles can cause straight == comparison to fail for seemingly
// equivalent values. For these, use approximate comparison with the error
// margin equal to the epsilon value defined for the corresponding data type.
// This is used when looking up values for implementation of OneOf constraints
// which should work reliably for floating points also ("number" type).

// Compare exact types using ==.
template<typename T>
inline typename std::enable_if<!std::is_floating_point<T>::value, bool>::type
CompareValue(const T& v1, const T& v2) {
  return v1 == v2;
}

// Compare non-exact types (such as double) using precision margin (epsilon).
template<typename T>
inline typename std::enable_if<std::is_floating_point<T>::value, bool>::type
CompareValue(const T& v1, const T& v2) {
  return std::abs(v1 - v2) <= std::numeric_limits<T>::epsilon();
}

// Converts PropValue to Any in a format understood by D-Bus data serialization.
// Has special handling for Object types where native_types::Object are
// converted to chromeos::VariantDictionary.
chromeos::Any PropValueToDBusVariant(const PropValue* value);
// Converts native_types::Object to chromeos::VariantDictionary
// with proper conversion of all nested properties.
chromeos::VariantDictionary
ObjectToDBusVariant(const native_types::Object& object);
// Converts D-Bus variant to PropValue.
// Has special handling for Object types where chromeos::VariantDictionary
// is converted to native_types::Object.
std::unique_ptr<const PropValue> PropValueFromDBusVariant(
    const PropType* type,
    const chromeos::Any& value,
    chromeos::ErrorPtr* error);
// Converts D-Bus variant to ObjectValue.
bool ObjectFromDBusVariant(const ObjectSchema* object_schema,
                           const chromeos::VariantDictionary& dict,
                           native_types::Object* obj,
                           chromeos::ErrorPtr* error);

}  // namespace buffet

#endif  // BUFFET_COMMANDS_SCHEMA_UTILS_H_
