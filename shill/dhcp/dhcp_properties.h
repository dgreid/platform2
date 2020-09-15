// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DHCP_DHCP_PROPERTIES_H_
#define SHILL_DHCP_DHCP_PROPERTIES_H_

#include <memory>
#include <string>

#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/key_value_store.h"

namespace shill {

class Error;
class Manager;
class PropertyStore;
class StoreInterface;

class DhcpProperties {
 public:
  static const char kPropertyPrefix[];
  static const char kHostnameProperty[];
  static const char kVendorClassProperty[];

  explicit DhcpProperties(Manager* manager);
  virtual ~DhcpProperties() = default;
  // Allow copy for Combine().
  DhcpProperties(const DhcpProperties&) = default;
  DhcpProperties& operator=(const DhcpProperties&) = delete;

  // Adds property accessors to the DhcpProperty parameters in |this|
  // to |store|.
  void InitPropertyStore(PropertyStore* store);

  // Loads DHCP properties from |storage| in group |id|.
  virtual void Load(const StoreInterface* store, const std::string& id);

  // Saves DHCP properties to |storage| in group |id|.
  virtual void Save(StoreInterface* store, const std::string& id) const;

  // Combines two DHCP property objects and returns a DhcpProperties instance
  // that is the union of the key-value pairs in |base| and |to_merge|.
  // For keys which exist in both |base| and |to_merge|, the value is taken from
  // |to_merge|.
  static DhcpProperties Combine(const DhcpProperties& base,
                                const DhcpProperties& to_merge);

  // Retrieves the value for a property with |name| in |value| if it is set.
  // Returns true if the property was found.
  bool GetValueForProperty(const std::string& name, std::string* value) const;

  const KeyValueStore& properties() const { return properties_; }
  KeyValueStore* properties_for_testing() { return &properties_; }

 private:
  friend class DhcpPropertiesTest;
  FRIEND_TEST(DhcpPropertiesTest, DhcpPropertyChanged);

  void ClearMappedStringProperty(const size_t& index, Error* error);
  std::string GetMappedStringProperty(const size_t& index, Error* error);
  bool SetMappedStringProperty(const size_t& index,
                               const std::string& value,
                               Error* error);

  // KeyValueStore tracking values for DhcpProperties settings.
  KeyValueStore properties_;

  // Unowned Manager. May be null in tests.
  Manager* manager_;
};

}  // namespace shill

#endif  // SHILL_DHCP_DHCP_PROPERTIES_H_
