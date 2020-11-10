// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLUETOOTH_COMMON_EXPORTED_OBJECT_MANAGER_WRAPPER_H_
#define BLUETOOTH_COMMON_EXPORTED_OBJECT_MANAGER_WRAPPER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <brillo/dbus/exported_object_manager.h>

#include "bluetooth/common/property.h"

namespace bluetooth {

// Represents an exported interface on an exported object.
class ExportedInterface {
 public:
  // |dbus_object| is owned by ExportedObject which always outlives
  // ExportedInterface.
  ExportedInterface(const dbus::ObjectPath& object_path,
                    const std::string& interface_name,
                    brillo::dbus_utils::DBusObject* dbus_object);
  ExportedInterface(const ExportedInterface&) = delete;
  ExportedInterface& operator=(const ExportedInterface&) = delete;

  ~ExportedInterface() = default;

  // True if already exported.
  bool is_exported() const { return is_exported_; }

  // Exports the interface asynchronously.
  void ExportAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction&
          callback);

  // Exports the interface synchronously.
  void ExportAndBlock();

  // Unexports the interface and all its exported properties.
  void Unexport();

  // Adds a raw method handler for |method_name| in this interface.
  void AddRawMethodHandler(
      const std::string& method_name,
      const base::Callback<void(
          dbus::MethodCall*, dbus::ExportedObject::ResponseSender)>& handler);

  // Adds a method handler for |method_name| in this interface.
  template <typename Instance, typename Class, typename... Args>
  void AddSimpleMethodHandlerWithErrorAndMessage(
      const std::string& method_name,
      Instance instance,
      bool (Class::*handler)(brillo::ErrorPtr*, dbus::Message*, Args...)) {
    dbus_object_->AddOrGetInterface(interface_name_)
        ->AddSimpleMethodHandlerWithErrorAndMessage(method_name, instance,
                                                    handler);
  }

  // Adds an async method handler for |method_name| in this interface.
  template <typename Response, typename... Args>
  void AddMethodHandlerWithMessage(
      const std::string& method_name,
      const base::Callback<
          void(std::unique_ptr<Response>, dbus::Message*, Args...)>& handler) {
    dbus_object_->AddOrGetInterface(interface_name_)
        ->AddMethodHandlerWithMessage(method_name, handler);
  }

  // Merges the values of the remote properties having name |property_name| to
  // the corresponding exported property, or unregisters the corresponding
  // exported property if property |property_name| is no longer valid. Doesn't
  // own |property_base| and |property_factory| and doesn't keep them.
  void SyncPropertiesToExportedProperty(
      const std::string& property_name,
      const std::vector<dbus::PropertyBase*>& remote_properties,
      PropertyFactoryBase* property_factory);

  // Registers the specified exported property if not already registered.
  // Doesn't own |property_factory| and doesn't keep it.
  brillo::dbus_utils::ExportedPropertyBase* EnsureExportedPropertyRegistered(
      const std::string& property_name, PropertyFactoryBase* property_factory);
  // Unregisters the specified exported property if it's currently registered.
  void EnsureExportedPropertyUnregistered(const std::string& property_name);
  // Returns the exported property |property_name| or nullptr if not registered.
  brillo::dbus_utils::ExportedPropertyBase* GetRegisteredExportedProperty(
      const std::string& property_name);

  // Exports the specified property having the specified type |T|, if not
  // already exported.
  template <typename T>
  brillo::dbus_utils::ExportedProperty<T>* EnsureExportedPropertyRegistered(
      const std::string& property_name) {
    PropertyFactory<T> property_factory;
    return static_cast<brillo::dbus_utils::ExportedProperty<T>*>(
        EnsureExportedPropertyRegistered(property_name, &property_factory));
  }

 private:
  // Object path this interface is on.
  dbus::ObjectPath object_path_;
  // The name of this interface.
  std::string interface_name_;

  // The exported DBusObject, owned by ExportedObject which outlives
  // this ExportedInterface object.
  brillo::dbus_utils::DBusObject* dbus_object_;

  // Whether this interface is already exported.
  bool is_exported_ = false;

  // The currently exported property names.
  std::map<std::string,
           std::unique_ptr<brillo::dbus_utils::ExportedPropertyBase>>
      exported_properties_;
};

// Wrapper of brillo::dbus_utils::DBusObject.
class ExportedObject {
 public:
  // Doesn't own |exported_object_manager|, so callers should make sure that
  // |exported_object_manager| outlives this object.
  ExportedObject(
      brillo::dbus_utils::ExportedObjectManager* exported_object_manager,
      const scoped_refptr<dbus::Bus>& bus,
      const dbus::ObjectPath& object_path,
      brillo::dbus_utils::DBusObject::PropertyHandlerSetupCallback
          property_handler_setup_callback);
  ExportedObject(const ExportedObject&) = delete;
  ExportedObject& operator=(const ExportedObject&) = delete;

  ~ExportedObject();

  // Returns the exported interface having name |interface_name|. The returned
  // pointer is owned by this object so callers should not use use the pointer
  // outside the lifespan of this object.
  ExportedInterface* GetExportedInterface(const std::string& interface_name);

  // Adds an interface on this object. The interface is not yet exported until
  // ExportedInterface::ExportAsync is called.
  void AddExportedInterface(const std::string& interface_name);

  // Removes an interface from being exported.
  void RemoveExportedInterface(const std::string& interface_name);

  // Registers the exported object with D-Bus asynchronously.
  void RegisterAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction&
          callback);

  // Registers the exported object with D-Bus synchronously.
  void RegisterAndBlock();

 private:
  friend class ExportedObjectManagerWrapper;

  dbus::ObjectPath object_path_;
  brillo::dbus_utils::DBusObject dbus_object_;

  std::map<std::string, std::unique_ptr<ExportedInterface>>
      exported_interfaces_;

  bool is_registered_ = false;
};

// A wrapper of brillo::dbus_utils::ExportedObjectManager that provides a higher
// level interface of object management.
class ExportedObjectManagerWrapper {
 public:
  ExportedObjectManagerWrapper(
      scoped_refptr<dbus::Bus> bus,
      std::unique_ptr<brillo::dbus_utils::ExportedObjectManager>
          exported_object_manager);
  ExportedObjectManagerWrapper(const ExportedObjectManagerWrapper&) = delete;
  ExportedObjectManagerWrapper& operator=(const ExportedObjectManagerWrapper&) =
      delete;

  // Set the property handler setup callback that will be used to handle D-Bus'
  // Properties method handlers (Get/Set/GetAll).
  void SetPropertyHandlerSetupCallback(
      const brillo::dbus_utils::DBusObject::PropertyHandlerSetupCallback&
          callback);

  // Adds an exported interface |interface_name| to object |object_path|.
  // If the object |object_path| is not yet exported, it will be exported
  // automatically.
  void AddExportedInterface(
      const dbus::ObjectPath& object_path,
      const std::string& interface_name,
      const brillo::dbus_utils::DBusObject::PropertyHandlerSetupCallback&
          property_handler_setup_callback);

  // Removes the previously exported interface |interface_name| from object
  // |object_path|. If there is no more exported interface to object
  // |object_path| after the removal, the object will also be unexported.
  void RemoveExportedInterface(const dbus::ObjectPath& object_path,
                               const std::string& interface_name);

  // Returns the previously added ExportedInterface or nullptr if there is no
  // such interface or object. The returned pointer is owned by this object
  // so callers should not use the pointer outside the lifespan of this object.
  ExportedInterface* GetExportedInterface(const dbus::ObjectPath& object_path,
                                          const std::string& interface_name);

  // Setup the standard org.freedesktop.DBus.Properties.Get / Set / GetAll
  static void SetupStandardPropertyHandlers(
      brillo::dbus_utils::DBusInterface* prop_interface,
      brillo::dbus_utils::ExportedPropertySet* property_set);

 private:
  // Adds and registers an exported object. Does nothing if an exported object
  // with the same object path already exists.
  void EnsureExportedObjectRegistered(
      const dbus::ObjectPath& object_path,
      const brillo::dbus_utils::DBusObject::PropertyHandlerSetupCallback&
          property_handler_setup_callback);

  // Returns the exported object having the specified object path. The returned
  // pointer is owned by this object so callers should not use the pointer
  // outside the lifespan of this object.
  ExportedObject* GetExportedObject(const dbus::ObjectPath& object_path);

  scoped_refptr<dbus::Bus> bus_;

  std::unique_ptr<brillo::dbus_utils::ExportedObjectManager>
      exported_object_manager_;

  std::map<std::string, std::unique_ptr<ExportedObject>> exported_objects_;

  // Must come last so that weak pointers will be invalidated before other
  // members are destroyed.
  base::WeakPtrFactory<ExportedObjectManagerWrapper> weak_ptr_factory_;
};

}  // namespace bluetooth

#endif  // BLUETOOTH_COMMON_EXPORTED_OBJECT_MANAGER_WRAPPER_H_
