// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/dispatcher/impersonation_object_manager_interface.h"

#include <utility>

#include <base/bind.h>
#include <base/stl_util.h>
#include <brillo/dbus/exported_object_manager.h>
#include <dbus/dbus.h>
#include <dbus/object_manager.h>

#include "bluetooth/dispatcher/dbus_util.h"

namespace bluetooth {

ImpersonationObjectManagerInterface::ImpersonationObjectManagerInterface(
    const scoped_refptr<dbus::Bus>& bus,
    ExportedObjectManagerWrapper* exported_object_manager_wrapper,
    std::unique_ptr<InterfaceHandler> interface_handler,
    const std::string& interface_name,
    ClientManager* client_manager)
    : ObjectManagerInterfaceMultiplexer(interface_name),
      bus_(bus),
      exported_object_manager_wrapper_(exported_object_manager_wrapper),
      interface_handler_(std::move(interface_handler)),
      client_manager_(client_manager),
      weak_ptr_factory_(this) {}

dbus::PropertySet* ImpersonationObjectManagerInterface::CreateProperties(
    const std::string& service_name,
    dbus::ObjectProxy* object_proxy,
    const dbus::ObjectPath& object_path,
    const std::string& interface_name) {
  VLOG(1) << "Service " << service_name << " CreateProperties "
          << object_path.value() << " interface " << interface_name
          << " object proxy " << object_proxy;

  auto property_set = std::make_unique<PropertySet>(
      object_proxy, interface_name,
      base::Bind(&ImpersonationObjectManagerInterface::OnPropertyChanged,
                 weak_ptr_factory_.GetWeakPtr(), service_name, object_path,
                 interface_name));

  for (const auto& kv : interface_handler_->GetPropertyFactoryMap())
    property_set->RegisterProperty(kv.first, kv.second->CreateProperty());

  // When CreateProperties is called that means the source service exports
  // interface |interface_name| on object |object_path|. So here we mimic
  // that to our exported object manager.
  AddImpersonatedServiceForObject(object_path.value(), service_name);
  if (ShouldInterfaceBeExported(object_path.value()) &&
      exported_object_manager_wrapper_->GetExportedInterface(
          object_path, interface_name) == nullptr) {
    exported_object_manager_wrapper_->AddExportedInterface(
        object_path, interface_name,
        base::Bind(&ImpersonationObjectManagerInterface::SetupPropertyHandlers,
                   weak_ptr_factory_.GetWeakPtr()));
    // If the exporting service is not the default service, that means the
    // default service has exported the object before. To avoid missing the
    // properties update by the default service, here we update them.
    std::string default_service =
        GetDefaultServiceForObject(object_path.value());
    if (default_service != service_name)
      TriggerPropertiesChanged(default_service, object_path, interface_name);
  }

  return property_set.release();
}

void ImpersonationObjectManagerInterface::ObjectAdded(
    const std::string& service_name,
    const dbus::ObjectPath& object_path,
    const std::string& interface_name) {
  VLOG(1) << "Service " << service_name << " added object "
          << object_path.value() << " on interface " << interface_name;

  // Whenever we detect that an interface has been added to the impersonated
  // service, we immediately export the same interface to the impersonating
  // service.
  ExportedInterface* exported_interface =
      exported_object_manager_wrapper_->GetExportedInterface(object_path,
                                                             interface_name);
  if (!exported_interface || exported_interface->is_exported()) {
    // Skip exporting the interface if another service has triggered this
    // interface export.
    return;
  }

  // Export the methods that are defined by |interface_handler_|.
  // Any method call will be forwarded the the impersonated service via a
  // specific per-client D-Bus connection.
  for (const auto& kv : interface_handler_->GetMethodForwardings()) {
    const std::string& method_name = kv.first;
    ForwardingRule forwarding_rule = kv.second;
    exported_interface->AddRawMethodHandler(
        method_name,
        base::Bind(&ImpersonationObjectManagerInterface::
                       HandleForwardMessageWithClientConnection,
                   weak_ptr_factory_.GetWeakPtr(), forwarding_rule));
  }

  exported_interface->ExportAndBlock();
}

void ImpersonationObjectManagerInterface::ObjectRemoved(
    const std::string& service_name,
    const dbus::ObjectPath& object_path,
    const std::string& interface_name) {
  VLOG(1) << "Service " << service_name << " removed object "
          << object_path.value() << " on interface " << interface_name;

  RemoveImpersonatedServiceForObject(object_path.value(), service_name);

  // Whenever we detect that an interface has been removed from the impersonated
  // service, we immediately unexport the same interface from the impersonating
  // service if this is the last service exposing this object at this interface.
  if (!ShouldInterfaceBeExported(object_path.value()) &&
      exported_object_manager_wrapper_->GetExportedInterface(
          object_path, interface_name) != nullptr) {
    exported_object_manager_wrapper_->RemoveExportedInterface(object_path,
                                                              interface_name);
  } else if (HasImpersonatedServicesForObject(object_path.value())) {
    // One of the services removed this object, but there is still other
    // services exposing this object. Update all the property values to reflect
    // the properties of the other service's object.
    TriggerPropertiesChanged(GetDefaultServiceForObject(object_path.value()),
                             object_path, interface_name);
  }
}

void ImpersonationObjectManagerInterface::HandleForwardMessage(
    ForwardingRule forwarding_rule,
    scoped_refptr<dbus::Bus> bus,
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  if (!HasImpersonatedServicesForObject(method_call->GetPath().value())) {
    LOG(WARNING) << "No destination to forward method "
                 << method_call->GetInterface() << "."
                 << method_call->GetMember() << " for object  "
                 << method_call->GetPath().value() << " on interface "
                 << interface_name();
    return;
  }

  VLOG(2) << "Method to be forwarded: " << method_call->ToString();

  if (forwarding_rule == ForwardingRule::FORWARD_ALL) {
    // Forward to all services, one after another.
    VLOG(1) << "Impersonation interface " << interface_name()
            << " forwarding method " << method_call->GetInterface() << "."
            << method_call->GetMember() << " to all services";
    // Start with forwarding the method to the first service (index 0).
    ForwardMessageToNextService(bus, method_call, response_sender,
                                0 /* service_index */,
                                nullptr /* last_response */);
    return;
  }

  // Default forwarding: forward to default service only.
  std::string service_name =
      GetDefaultServiceForObject(method_call->GetPath().value());
  VLOG(1) << "Impersonation interface " << interface_name()
          << " forwarding method " << method_call->GetInterface() << "."
          << method_call->GetMember() << " to " << service_name;

  DBusUtil::ForwardMethodCall(bus, service_name, method_call, response_sender);
}

void ImpersonationObjectManagerInterface::TriggerPropertiesChanged(
    const std::string& service,
    const dbus::ObjectPath& object_path,
    const std::string& interface_name) {
  for (const auto& kv : interface_handler_->GetPropertyFactoryMap()) {
    const std::string& property_name = kv.first;
    OnPropertyChanged(service, object_path, interface_name, property_name);
  }
}

bool ImpersonationObjectManagerInterface::ShouldInterfaceBeExported(
    const std::string& object_path) const {
  switch (interface_handler_->GetObjectExportRule()) {
    case ObjectExportRule::ALL_SERVICES:
      return GetImpersonatedServicesCountForObject(object_path) ==
             service_names().size();
    case ObjectExportRule::ANY_SERVICE:
      return HasImpersonatedServicesForObject(object_path);
  }
}

bool ImpersonationObjectManagerInterface::HasImpersonatedServicesForObject(
    const std::string& object_path) const {
  return GetImpersonatedServicesCountForObject(object_path) > 0;
}

int ImpersonationObjectManagerInterface::GetImpersonatedServicesCountForObject(
    const std::string& object_path) const {
  if (!base::Contains(impersonated_services_, object_path))
    return 0;

  return impersonated_services_.at(object_path).size();
}

std::string ImpersonationObjectManagerInterface::GetDefaultServiceForObject(
    const std::string& object_path) const {
  CHECK(base::Contains(impersonated_services_, object_path) &&
        !impersonated_services_.find(object_path)->second.empty());

  for (const std::string& service : service_names()) {
    if (base::Contains(impersonated_services_.find(object_path)->second,
                       service))
      return service;
  }

  LOG(FATAL) << "Default service not found";
  return "";
}

void ImpersonationObjectManagerInterface::AddImpersonatedServiceForObject(
    const std::string& object_path, const std::string& service_name) {
  impersonated_services_[object_path].insert(service_name);
}

void ImpersonationObjectManagerInterface::RemoveImpersonatedServiceForObject(
    const std::string& object_path, const std::string& service_name) {
  impersonated_services_[object_path].erase(service_name);
  if (impersonated_services_[object_path].empty())
    impersonated_services_.erase(object_path);
}

bool ImpersonationObjectManagerInterface::HasImpersonatedServiceForObject(
    const std::string& object_path, const std::string& service_name) {
  auto iter = impersonated_services_.find(object_path);
  if (iter == impersonated_services_.end())
    return false;
  return base::Contains(iter->second, service_name);
}

dbus::ObjectManager* ImpersonationObjectManagerInterface::GetObjectManager(
    const std::string& service_name) {
  auto it = object_managers().find(service_name);
  CHECK(it != object_managers().end())
      << "ObjectManager of service " << service_name << " doesn't exist";
  return it->second;
}

void ImpersonationObjectManagerInterface::OnPropertyChanged(
    const std::string& service_name,
    const dbus::ObjectPath& object_path,
    const std::string& interface_name,
    const std::string& property_name) {
  VLOG(2) << "Property " << property_name << " on interface " << interface_name
          << " of object " << object_path.value() << " from service "
          << service_name << " changed.";

  PropertyFactoryBase* property_factory =
      interface_handler_->GetPropertyFactoryMap()
          .find(property_name)
          ->second.get();

  // When property value change is detected from the impersonated service,
  // we immediately update the corresponding property of the impersonating
  // service.
  ExportedInterface* exported_interface =
      exported_object_manager_wrapper_->GetExportedInterface(object_path,
                                                             interface_name);
  if (exported_interface == nullptr)
    return;

  std::vector<dbus::PropertyBase*> remote_properties;
  for (const std::string& service : service_names()) {
    if (!HasImpersonatedServiceForObject(object_path.value(), service))
      continue;

    auto object_manager = GetObjectManager(service);
    if (object_manager == nullptr) {
      remote_properties.emplace_back(nullptr);
      continue;
    }

    auto properties =
        object_manager->GetProperties(object_path, interface_name);
    if (properties == nullptr) {
      remote_properties.emplace_back(nullptr);
      continue;
    }

    remote_properties.emplace_back(
        static_cast<PropertySet*>(properties)->GetProperty(property_name));
  }

  exported_interface->SyncPropertiesToExportedProperty(
      property_name, remote_properties, property_factory);
}

void ImpersonationObjectManagerInterface::
    HandleForwardMessageWithClientConnection(
        ForwardingRule forwarding_rule,
        dbus::MethodCall* method_call,
        dbus::ExportedObject::ResponseSender response_sender) {
  VLOG(1) << "Method " << method_call->GetMember() << " called by "
          << method_call->GetSender();
  std::string client_address = method_call->GetSender();
  DispatcherClient* client = client_manager_->EnsureClientAdded(client_address);
  VLOG(1) << "client = " << client;
  HandleForwardMessage(forwarding_rule, client->GetClientBus(), method_call,
                       response_sender);
}

void ImpersonationObjectManagerInterface::ForwardMessageToNextService(
    scoped_refptr<dbus::Bus> bus,
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender,
    int service_index,
    std::unique_ptr<dbus::Response> last_response) {
  if (service_index >= service_names().size()) {
    // We have reached the end of services. Send the response back to client.
    CHECK(last_response) << "no last response";
    response_sender.Run(std::move(last_response));
    return;
  }

  if (last_response && !last_response->GetErrorName().empty()) {
    // The last response contains error. Stop forwarding to next service and
    // send this error response back to client.
    response_sender.Run(std::move(last_response));
    return;
  }

  // Do the forwarding to service |service_index|, when the forwarded method
  // has returned recursively initiate the forwarding to next service.
  DBusUtil::ForwardMethodCall(
      bus, service_names()[service_index], method_call,
      base::Bind(
          &ImpersonationObjectManagerInterface::ForwardMessageToNextService,
          weak_ptr_factory_.GetWeakPtr(), bus, method_call, response_sender,
          service_index + 1));
}

void ImpersonationObjectManagerInterface::SetupPropertyHandlers(
    brillo::dbus_utils::DBusInterface* prop_interface,
    brillo::dbus_utils::ExportedPropertySet* property_set) {
  // Install standard property handlers.
  prop_interface->AddSimpleMethodHandler(
      dbus::kPropertiesGetAll, base::Unretained(property_set),
      &brillo::dbus_utils::ExportedPropertySet::HandleGetAll);
  prop_interface->AddSimpleMethodHandlerWithError(
      dbus::kPropertiesGet, base::Unretained(property_set),
      &brillo::dbus_utils::ExportedPropertySet::HandleGet);
  prop_interface->AddRawMethodHandler(
      dbus::kPropertiesSet,
      base::Bind(&ImpersonationObjectManagerInterface::HandleForwardSetProperty,
                 weak_ptr_factory_.GetWeakPtr(), bus_));
}

void ImpersonationObjectManagerInterface::HandleForwardSetProperty(
    scoped_refptr<dbus::Bus> bus,
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  HandleForwardMessage(ForwardingRule::FORWARD_DEFAULT, bus, method_call,
                       response_sender);
}

}  // namespace bluetooth
