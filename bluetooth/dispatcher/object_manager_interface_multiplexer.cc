// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

#include <base/stl_util.h>
#include <dbus/object_manager.h>

#include "bluetooth/dispatcher/object_manager_interface_multiplexer.h"

namespace bluetooth {

ForwardingObjectManagerInterface::ForwardingObjectManagerInterface(
    const std::string& service_name,
    ObjectManagerInterfaceMultiplexer* interface_multiplexer)
    : service_name_(service_name),
      interface_multiplexer_(interface_multiplexer) {}

dbus::PropertySet* ForwardingObjectManagerInterface::CreateProperties(
    dbus::ObjectProxy* object_proxy,
    const dbus::ObjectPath& object_path,
    const std::string& interface_name) {
  return interface_multiplexer_->CreateProperties(service_name_, object_proxy,
                                                  object_path, interface_name);
}

void ForwardingObjectManagerInterface::ObjectAdded(
    const dbus::ObjectPath& object_path, const std::string& interface_name) {
  interface_multiplexer_->ObjectAdded(service_name_, object_path,
                                      interface_name);
}

void ForwardingObjectManagerInterface::ObjectRemoved(
    const dbus::ObjectPath& object_path, const std::string& interface_name) {
  interface_multiplexer_->ObjectRemoved(service_name_, object_path,
                                        interface_name);
}

////////////////////////////////////////////////////////////////////////////////

ObjectManagerInterfaceMultiplexer::ObjectManagerInterfaceMultiplexer(
    const std::string& interface_name)
    : interface_name_(interface_name) {}

void ObjectManagerInterfaceMultiplexer::RegisterToObjectManager(
    dbus::ObjectManager* object_manager, const std::string& service_name) {
  CHECK(!base::Contains(object_manager_interfaces_, service_name))
      << "Interface " << interface_name_ << " for service " << service_name
      << " has been registered before";

  auto forwarding_interface =
      std::make_unique<ForwardingObjectManagerInterface>(service_name, this);
  object_manager->RegisterInterface(interface_name_,
                                    forwarding_interface.get());
  object_manager_interfaces_.emplace(service_name,
                                     std::move(forwarding_interface));
  object_managers_.emplace(service_name, object_manager);
  service_names_.push_back(service_name);
}

}  // namespace bluetooth
