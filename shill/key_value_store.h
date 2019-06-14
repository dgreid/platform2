// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_KEY_VALUE_STORE_H_
#define SHILL_KEY_VALUE_STORE_H_

#include <map>
#include <string>
#include <vector>

#include <brillo/type_list.h>
#include <brillo/type_name_undecorate.h>
#include <brillo/variant_dictionary.h>

#include "shill/data_types.h"

namespace shill {

class KeyValueStore;

using KeyValueTypes = brillo::TypeList<bool,
                                       uint8_t,
                                       uint16_t,
                                       uint32_t,
                                       int16_t,
                                       int32_t,
                                       int64_t,
                                       double,

                                       std::vector<bool>,
                                       std::vector<uint8_t>,
                                       std::vector<std::vector<uint8_t>>,
                                       std::vector<uint32_t>,
                                       std::vector<int32_t>,
                                       std::vector<int64_t>,
                                       std::vector<double>,

                                       KeyValueStore,
                                       std::string,
                                       Stringmap,
                                       Strings,
                                       RpcIdentifier,
                                       RpcIdentifiers>;

class KeyValueStore {
  // A simple store for key-value pairs, which supports (a limited set of)
  // heterogeneous value types, as defined in the KeyValueTypes typelist above.
  //
  // Compare to PropertyStore, which enables a class to (selectively)
  // expose its instance members as properties accessible via
  // RPC. (RPC support for ProperyStore is implemented in a
  // protocol-specific adaptor. e.g. dbus_adpator.)
  //
  // Implemented separately from PropertyStore, to avoid complicating
  // the PropertyStore interface. In particular, objects implementing the
  // PropertyStore interface always provide the storage themselves. In
  // contrast, users of KeyValueStore expect KeyValueStore to provide
  // storage.
 public:
  KeyValueStore();

  // Required for equality comparison when KeyValueStore is wrapped inside a
  // brillo::Any object.
  bool operator==(const KeyValueStore& rhs) const;
  bool operator!=(const KeyValueStore& rhs) const;

  void Clear();
  void CopyFrom(const KeyValueStore& b);
  bool IsEmpty();

  template <typename T, typename = brillo::EnableIfIsOneOf<T, KeyValueTypes>>
  bool Contains(const std::string& name) const {
    return ContainsVariant(name) &&
           properties_.find(name)->second.IsTypeCompatible<T>();
  }
  bool ContainsVariant(const std::string& name) const;

  template <typename T,
            typename brillo::EnableIfIsOneOfArithmetic<T, KeyValueTypes> = 0>
  T Get(const std::string& name) const {
    const auto& value = GetVariant(name);
    CHECK(value.IsTypeCompatible<T>())
        << "for " << brillo::GetTypeTag<T>() << " property " << name;
    return value.Get<T>();
  }

  template <typename T,
            typename brillo::EnableIfIsOneOfNonArithmetic<T, KeyValueTypes> = 0>
  const T& Get(const std::string& name) const {
    const auto& value = GetVariant(name);
    CHECK(value.IsTypeCompatible<T>())
        << "for " << brillo::GetTypeTag<T>() << " property " << name;
    return value.Get<T>();
  }

  const brillo::Any& GetVariant(const std::string& name) const;

  // TODO(crbug.com/936111): remove type specific set functions and add a
  // generic set function instead.  This way, we don't need to add new functions
  // every time we need to support a new type.
  void SetBool(const std::string& name, bool value);
  void SetBools(const std::string& name, const std::vector<bool>& value);
  void SetByteArrays(const std::string& name,
                     const std::vector<std::vector<uint8_t>>& value);
  void SetInt(const std::string& name, int32_t value);
  void SetInts(const std::string& name, const std::vector<int32_t>& value);
  void SetInt16(const std::string& name, int16_t value);
  void SetInt64(const std::string& name, int64_t value);
  void SetInt64s(const std::string& name, const std::vector<int64_t>& value);
  void SetDouble(const std::string& name, double value);
  void SetDoubles(const std::string& name, const std::vector<double>& value);
  void SetKeyValueStore(const std::string& name, const KeyValueStore& value);
  void SetRpcIdentifier(const std::string& name, const RpcIdentifier& value);
  void SetRpcIdentifiers(const std::string& name, const RpcIdentifiers& value);
  void SetString(const std::string& name, const std::string& value);
  void SetStringmap(const std::string& name,
                    const std::map<std::string, std::string>& value);
  void SetStrings(const std::string& name,
                  const std::vector<std::string>& value);
  void SetUint(const std::string& name, uint32_t value);
  void SetUint16(const std::string& name, uint16_t value);
  void SetUint8(const std::string& name, uint8_t value);
  void SetUint8s(const std::string& name, const std::vector<uint8_t>& value);
  void SetUint32s(const std::string& name, const std::vector<uint32_t>& value);
  void SetVariant(const std::string& name, const brillo::Any& value);

  void Remove(const std::string& name);

  // If |name| is in this store returns its value, otherwise returns
  // |default_value|.
  bool LookupBool(const std::string& name, bool default_value) const;
  int LookupInt(const std::string& name, int default_value) const;
  std::string LookupString(const std::string& name,
                           const std::string& default_value) const;

  const brillo::VariantDictionary& properties() const { return properties_; }

  // Conversion function between KeyValueStore and brillo::VariantDictionary.
  // Since we already use brillo::VariantDictionary for storing key value
  // pairs, all conversions will be trivial except nested KeyValueStore and
  // nested brillo::VariantDictionary.
  static brillo::VariantDictionary ConvertToVariantDictionary(
      const KeyValueStore& in_store);
  static KeyValueStore ConvertFromVariantDictionary(
      const brillo::VariantDictionary& in_dict);

 private:
  brillo::VariantDictionary properties_;
};

}  // namespace shill

#endif  // SHILL_KEY_VALUE_STORE_H_
