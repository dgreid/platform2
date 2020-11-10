// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLUETOOTH_COMMON_PROPERTY_H_
#define BLUETOOTH_COMMON_PROPERTY_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <brillo/dbus/exported_property_set.h>
#include <dbus/property.h>

namespace bluetooth {

enum class MergingRule {
  // Don't merge the values, use the one from the default service.
  DEFAULT,
  // Merge the properties based on AND relationship.
  AND,
  // Merge the properties based on OR relationship.
  OR,
  // Merge the properties based on UNION relationship.
  UNION,
  // Merge the properties by Concatenating the values.
  CONCATENATION,
};

// Typeless property factory. This typeless class is needed to generalize many
// types of properties that share the same interface. Contains utilities to
// create properties and value copying.
class PropertyFactoryBase {
 public:
  PropertyFactoryBase() = default;
  PropertyFactoryBase(const PropertyFactoryBase&) = delete;
  PropertyFactoryBase& operator=(const PropertyFactoryBase&) = delete;

  virtual ~PropertyFactoryBase() = default;

  // Instantiates a dbus::Property having the same type as this factory.
  virtual std::unique_ptr<dbus::PropertyBase> CreateProperty() = 0;

  // Instantiates a brillo::dbus_utils::ExportedProperty having the same type
  // as this factory.
  virtual std::unique_ptr<brillo::dbus_utils::ExportedPropertyBase>
  CreateExportedProperty() = 0;

  // Merges the values from a set of dbus::Property to a
  // brillo::dbus_utils::ExportedProperty having the specified type.
  // Doesn't own the argument pointers and doesn't keep them either.
  virtual void MergePropertiesToExportedProperty(
      const std::vector<dbus::PropertyBase*>& remote_properties,
      brillo::dbus_utils::ExportedPropertyBase* exported_property_base) = 0;
};

// The type-specific property factory.
template <typename T>
class PropertyFactory : public PropertyFactoryBase {
 public:
  PropertyFactory() : merging_rule_(MergingRule::DEFAULT) {}
  explicit PropertyFactory(MergingRule merging_rule)
      : merging_rule_(merging_rule) {}
  PropertyFactory(const PropertyFactory&) = delete;
  PropertyFactory& operator=(const PropertyFactory&) = delete;

  ~PropertyFactory() override = default;

  std::unique_ptr<dbus::PropertyBase> CreateProperty() override {
    return std::make_unique<dbus::Property<T>>();
  }

  std::unique_ptr<brillo::dbus_utils::ExportedPropertyBase>
  CreateExportedProperty() override {
    return std::make_unique<brillo::dbus_utils::ExportedProperty<T>>();
  }

  void MergePropertiesToExportedProperty(
      const std::vector<dbus::PropertyBase*>& remote_properties,
      brillo::dbus_utils::ExportedPropertyBase* exported_property_base)
      override {
    brillo::dbus_utils::ExportedProperty<T>* exported_property =
        static_cast<brillo::dbus_utils::ExportedProperty<T>*>(
            exported_property_base);
    std::vector<dbus::Property<T>*> properties;
    for (dbus::PropertyBase* remote_property : remote_properties) {
      properties.emplace_back(static_cast<dbus::Property<T>*>(remote_property));
    }
    switch (merging_rule_) {
      case MergingRule::DEFAULT:
        MergeWithDefault(properties, exported_property);
        break;
      case MergingRule::AND:
        MergeWithAnd(properties, exported_property);
        break;
      case MergingRule::OR:
        MergeWithOr(properties, exported_property);
        break;
      case MergingRule::UNION:
        MergeWithUnion(properties, exported_property);
        break;
      case MergingRule::CONCATENATION:
        MergeWithConcatenation(properties, exported_property);
        break;
    }
  }

 private:
  static void MergeAnd(bool* to, const bool& from) { *to = *to && from; }

  static void MergeOr(bool* to, const bool& from) { *to = *to || from; }

  template <typename R>
  static void MergeUnion(std::vector<R>* to, const std::vector<R>& from) {
    std::set<R> unique_values(to->begin(), to->end());
    unique_values.insert(from.begin(), from.end());
    to->assign(unique_values.begin(), unique_values.end());
  }

  static void MergeConcatenation(std::string* to, const std::string& from) {
    if (from.empty())
      return;

    if (!to->empty())
      to->append(" ");
    to->append(from);
  }

  void MergeWithDefault(
      const std::vector<dbus::Property<T>*>& properties,
      brillo::dbus_utils::ExportedProperty<T>* exported_property) {
    // Order matters here, the first one should be from the default service.
    if (properties[0] != nullptr && properties[0]->is_valid())
      CopyValue(properties[0]->value(), exported_property);
  }

  template <typename R>
  void MergeWithAnd(
      const std::vector<dbus::Property<R>*>& properties,
      brillo::dbus_utils::ExportedProperty<R>* exported_property) {
    LOG(FATAL) << "AND merging not supported for the given value type";
  }

  template <typename R>
  void MergeWithOr(const std::vector<dbus::Property<R>*>& properties,
                   brillo::dbus_utils::ExportedProperty<R>* exported_property) {
    LOG(FATAL) << "OR merging not supported for the given value type";
  }

  template <typename R>
  void MergeWithUnion(
      const std::vector<dbus::Property<R>*>& properties,
      brillo::dbus_utils::ExportedProperty<R>* exported_property) {
    LOG(FATAL) << "UNION merging not supported for the given value type";
  }

  template <typename R>
  void MergeWithConcatenation(
      const std::vector<dbus::Property<R>*>& properties,
      brillo::dbus_utils::ExportedProperty<R>* exported_property) {
    LOG(FATAL)
        << "CONCATENATION merging not supported for the given value type";
  }

  template <typename R>
  void MergeWithMerger(
      const std::vector<dbus::Property<R>*>& properties,
      brillo::dbus_utils::ExportedProperty<R>* exported_property,
      const R& default_value,
      void (*PropertyMerger)(R* to, const R& from)) {
    if (properties.empty())
      return;

    R value = (properties[0] != nullptr && properties[0]->is_valid())
                  ? properties[0]->value()
                  : default_value;

    for (size_t i = 1; i < properties.size(); ++i) {
      if (properties[i] != nullptr && properties[i]->is_valid())
        PropertyMerger(&value, properties[i]->value());
      else
        PropertyMerger(&value, default_value);
    }

    CopyValue(value, exported_property);
  }

  template <>
  void MergeWithAnd(
      const std::vector<dbus::Property<bool>*>& properties,
      brillo::dbus_utils::ExportedProperty<bool>* exported_property) {
    MergeWithMerger(properties, exported_property, /* default_value */ false,
                    MergeAnd);
  }

  template <>
  void MergeWithOr(
      const std::vector<dbus::Property<bool>*>& properties,
      brillo::dbus_utils::ExportedProperty<bool>* exported_property) {
    MergeWithMerger(properties, exported_property, /* default_value */ false,
                    MergeOr);
  }

  template <typename R>
  void MergeWithUnion(
      const std::vector<dbus::Property<std::vector<R>>*>& properties,
      brillo::dbus_utils::ExportedProperty<std::vector<R>>* exported_property) {
    MergeWithMerger(properties, exported_property,
                    /* default_value */ std::vector<R>(), MergeUnion<R>);
  }

  template <>
  void MergeWithConcatenation(
      const std::vector<dbus::Property<std::string>*>& properties,
      brillo::dbus_utils::ExportedProperty<std::string>* exported_property) {
    MergeWithMerger(properties, exported_property,
                    /* default_value */ std::string(), MergeConcatenation);
  }

  template <typename R>
  void CopyValue(const R& property_value,
                 brillo::dbus_utils::ExportedProperty<R>* exported_property) {
    // No need to copy the value if they are already the same. This is useful to
    // prevent unnecessary PropertiesChanged signal being emitted.
    if (property_value == exported_property->value())
      return;
    exported_property->SetValue(property_value);
  }

  MergingRule merging_rule_;
};

// A dbus::PropertySet that also holds the individual properties.
class PropertySet : public dbus::PropertySet {
 public:
  using dbus::PropertySet::PropertySet;

  PropertySet(const PropertySet&) = delete;
  PropertySet& operator=(const PropertySet&) = delete;

  // Holds the specified property |property_base| and registers it with the
  // specified name |property_name|.
  void RegisterProperty(const std::string& property_name,
                        std::unique_ptr<dbus::PropertyBase> property_base);
  // Returns the previously registered property. This object owns the returned
  // pointer so callers should make sure that the returned pointer is not used
  // outside the lifespan of this object.
  dbus::PropertyBase* GetProperty(const std::string& property_name);

 private:
  // Keeps the registered properties.
  std::map<std::string, std::unique_ptr<dbus::PropertyBase>> properties_;
};

}  // namespace bluetooth

#endif  // BLUETOOTH_COMMON_PROPERTY_H_
