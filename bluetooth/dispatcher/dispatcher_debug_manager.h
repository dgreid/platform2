// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLUETOOTH_DISPATCHER_DISPATCHER_DEBUG_MANAGER_H_
#define BLUETOOTH_DISPATCHER_DISPATCHER_DEBUG_MANAGER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>
#include <brillo/errors/error.h>
#include <dbus/bus.h>
#include <dbus/message.h>

#include "bluetooth/common/exported_object_manager_wrapper.h"

namespace bluetooth {

// This class exposes four D-Bus properties, the verbosity of debug logs for
// each of the dispatcher, newblue, bluez, and kernel. The values can be set
// with a D-Bus method that is also exposed here.
class DispatcherDebugManager final {
 public:
  // |exported_object_manager_wrapper| not owned, caller must make
  // sure it outlives this object.
  DispatcherDebugManager(
      scoped_refptr<dbus::Bus> bus,
      ExportedObjectManagerWrapper* exported_object_manager_wrapper);
  DispatcherDebugManager(const DispatcherDebugManager&) = delete;
  DispatcherDebugManager& operator=(const DispatcherDebugManager&) = delete;

  ~DispatcherDebugManager() = default;

  // Initializes the D-Bus operations.
  void Init();

 private:
  // Registers the properties to the interface, and inits the value according to
  // the config file.
  void RegisterProperties();

  // Parses the debug config file. Returns true and populates values vector if
  // parsing is successful, returns false otherwise.
  bool ParseConfigFile(int expected_num_of_values,
                       std::vector<uint8_t>* values);

  // Handler for the D-Bus Debug.SetLevels() method.
  // Stores the debug levels in D-Bus properties and writes to config file.
  bool HandleSetLevels(brillo::ErrorPtr* error,
                       dbus::Message* message,
                       uint8_t dispatcher_level,
                       uint8_t newblue_level,
                       uint8_t bluez_level,
                       uint8_t kernel_level);

  // Sets debug verbosity for btdispatch
  void SetDispatcherLogLevel(int verbosity);

  scoped_refptr<dbus::Bus> bus_;

  ExportedInterface* debug_interface_;

  ExportedObjectManagerWrapper* exported_object_manager_wrapper_;

  int current_verbosity_ = 0;

  // Must come last so that weak pointers will be invalidated before other
  // members are destroyed.
  base::WeakPtrFactory<DispatcherDebugManager> weak_ptr_factory_;
};

}  // namespace bluetooth

#endif  // BLUETOOTH_DISPATCHER_DISPATCHER_DEBUG_MANAGER_H_
