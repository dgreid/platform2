// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/dbus_conversion.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

#include <brillo/type_name_undecorate.h>

namespace buffet {

namespace {

// Helpers for JsonToAny().
template <typename T>
brillo::Any ValueToAny(const base::Value& json,
                       bool (base::Value::*fnc)(T*) const) {
  T val;
  CHECK((json.*fnc)(&val));
  return val;
}

brillo::Any ValueToAny(const base::Value& json);

template <typename T>
brillo::Any ListToAny(const base::ListValue& list,
                      bool (base::Value::*fnc)(T*) const) {
  std::vector<T> result;
  result.reserve(list.GetSize());
  for (const base::Value& v : list) {
    T val;
    CHECK((v.*fnc)(&val));
    result.push_back(val);
  }
  return result;
}

brillo::Any DictListToAny(const base::ListValue& list) {
  std::vector<brillo::VariantDictionary> result;
  result.reserve(list.GetSize());
  for (const base::Value& v : list) {
    const base::DictionaryValue* dict = nullptr;
    CHECK(v.GetAsDictionary(&dict));
    result.push_back(DictionaryToDBusVariantDictionary(*dict));
  }
  return result;
}

brillo::Any ListListToAny(const base::ListValue& list) {
  std::vector<brillo::Any> result;
  result.reserve(list.GetSize());
  for (const base::Value& v : list)
    result.push_back(ValueToAny(v));
  return result;
}

// Converts a JSON value into an Any so it can be sent over D-Bus using
// UpdateState D-Bus method from Buffet.
brillo::Any ValueToAny(const base::Value& json) {
  brillo::Any prop_value;
  switch (json.type()) {
    case base::Value::Type::BOOLEAN:
      prop_value = ValueToAny<bool>(json, &base::Value::GetAsBoolean);
      break;
    case base::Value::Type::INTEGER:
      prop_value = ValueToAny<int>(json, &base::Value::GetAsInteger);
      break;
    case base::Value::Type::DOUBLE:
      prop_value = ValueToAny<double>(json, &base::Value::GetAsDouble);
      break;
    case base::Value::Type::STRING:
      prop_value = ValueToAny<std::string>(json, &base::Value::GetAsString);
      break;
    case base::Value::Type::DICTIONARY: {
      const base::DictionaryValue* dict = nullptr;
      CHECK(json.GetAsDictionary(&dict));
      prop_value = DictionaryToDBusVariantDictionary(*dict);
      break;
    }
    case base::Value::Type::LIST: {
      const base::ListValue* list = nullptr;
      CHECK(json.GetAsList(&list));
      if (list->empty()) {
        // We don't know type of objects this list intended for, so we just use
        // vector<brillo::Any>.
        prop_value = ListListToAny(*list);
        break;
      }
      auto type = list->begin()->type();
      for (const base::Value& v : *list)
        CHECK_EQ(v.type(), type) << "Unsupported different type elements";

      switch (type) {
        case base::Value::Type::BOOLEAN:
          prop_value = ListToAny<bool>(*list, &base::Value::GetAsBoolean);
          break;
        case base::Value::Type::INTEGER:
          prop_value = ListToAny<int>(*list, &base::Value::GetAsInteger);
          break;
        case base::Value::Type::DOUBLE:
          prop_value = ListToAny<double>(*list, &base::Value::GetAsDouble);
          break;
        case base::Value::Type::STRING:
          prop_value = ListToAny<std::string>(*list, &base::Value::GetAsString);
          break;
        case base::Value::Type::DICTIONARY:
          prop_value = DictListToAny(*list);
          break;
        case base::Value::Type::LIST:
          // We can't support Any{vector<vector<>>} as the type is only known
          // in runtime when we need to instantiate templates in compile time.
          // We can use Any{vector<Any>} instead.
          prop_value = ListListToAny(*list);
          break;
        default:
          LOG(FATAL) << "Unsupported JSON value type for list element: "
                     << list->begin()->type();
      }
      break;
    }
    default:
      LOG(FATAL) << "Unexpected JSON value type: " << json.type();
      break;
  }
  return prop_value;
}

template <typename T>
std::unique_ptr<base::Value> CreateValue(const T& value,
                                         brillo::ErrorPtr* error) {
  return std::make_unique<base::Value>(value);
}

template <>
std::unique_ptr<base::Value> CreateValue<std::string>(const std::string& value,
                                                      brillo::ErrorPtr* error) {
  return std::make_unique<base::Value>(value);
}

template <>
std::unique_ptr<base::Value> CreateValue<brillo::VariantDictionary>(
    const brillo::VariantDictionary& value, brillo::ErrorPtr* error) {
  return DictionaryFromDBusVariantDictionary(value, error);
}

template <typename T>
std::unique_ptr<base::ListValue> CreateListValue(const std::vector<T>& value,
                                                 brillo::ErrorPtr* error) {
  std::unique_ptr<base::ListValue> list{new base::ListValue};

  for (const T& i : value) {
    auto item = CreateValue(i, error);
    if (!item)
      return nullptr;
    list->Append(std::move(item));
  }

  return list;
}

// Returns false only in case of error. True can be returned if type is not
// matched.
template <typename T>
bool TryCreateValue(const brillo::Any& any,
                    std::unique_ptr<base::Value>* value,
                    brillo::ErrorPtr* error) {
  if (any.IsTypeCompatible<T>()) {
    *value = CreateValue(any.Get<T>(), error);
    return *value != nullptr;
  }

  if (any.IsTypeCompatible<std::vector<T>>()) {
    *value = CreateListValue(any.Get<std::vector<T>>(), error);
    return *value != nullptr;
  }

  return true;  // Not an error, we will try different type.
}

template <>
std::unique_ptr<base::Value> CreateValue<brillo::Any>(const brillo::Any& any,
                                                      brillo::ErrorPtr* error) {
  std::unique_ptr<base::Value> result;
  if (!TryCreateValue<bool>(any, &result, error) || result)
    return result;

  if (!TryCreateValue<int>(any, &result, error) || result)
    return result;

  if (!TryCreateValue<double>(any, &result, error) || result)
    return result;

  if (!TryCreateValue<std::string>(any, &result, error) || result)
    return result;

  if (!TryCreateValue<brillo::VariantDictionary>(any, &result, error) ||
      result) {
    return result;
  }

  // This will collapse Any{Any{T}} and vector{Any{T}}.
  if (!TryCreateValue<brillo::Any>(any, &result, error) || result)
    return result;

  brillo::Error::AddToPrintf(error, FROM_HERE, "buffet", "unknown_type",
                             "Type '%s' is not supported.",
                             any.GetUndecoratedTypeName().c_str());

  return nullptr;
}

}  // namespace

// TODO(vitalybuka): Use in buffet_client.
brillo::VariantDictionary DictionaryToDBusVariantDictionary(
    const base::DictionaryValue& object) {
  brillo::VariantDictionary result;

  for (base::DictionaryValue::Iterator it(object); !it.IsAtEnd(); it.Advance())
    result.emplace(it.key(), ValueToAny(it.value()));

  return result;
}

std::unique_ptr<base::DictionaryValue> DictionaryFromDBusVariantDictionary(
    const brillo::VariantDictionary& object, brillo::ErrorPtr* error) {
  std::unique_ptr<base::DictionaryValue> result{new base::DictionaryValue};

  for (const auto& pair : object) {
    auto value = CreateValue(pair.second, error);
    if (!value)
      return nullptr;
    result->Set(pair.first, std::move(value));
  }

  return result;
}

}  // namespace buffet
