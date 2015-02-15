// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_MANAGER_H_
#define BUFFET_MANAGER_H_

#include <map>
#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/values.h>
#include <chromeos/dbus/data_serialization.h>
#include <chromeos/dbus/dbus_object.h>
#include <chromeos/dbus/exported_property_set.h>
#include <chromeos/errors/error.h>

#include "buffet/device_registration_info.h"
#include "buffet/org.chromium.Buffet.Manager.h"

namespace chromeos {
namespace dbus_utils {
class ExportedObjectManager;
}  // namespace dbus_utils
}  // namespace chromeos

namespace buffet {

class CommandManager;
class StateChangeQueue;
class StateManager;

template<typename... Types>
using DBusMethodResponse =
    scoped_ptr<chromeos::dbus_utils::DBusMethodResponse<Types...>>;

// The Manager is responsible for global state of Buffet.  It exposes
// interfaces which affect the entire device such as device registration and
// device state.
class Manager final : public org::chromium::Buffet::ManagerInterface {
 public:
  explicit Manager(
      const base::WeakPtr<chromeos::dbus_utils::ExportedObjectManager>&
          object_manager);
  ~Manager();

  void RegisterAsync(
      const base::FilePath& config_path,
      const base::FilePath& state_path,
      const base::FilePath& test_definitions_path,
      const chromeos::dbus_utils::AsyncEventSequencer::CompletionAction& cb);

 private:
  // DBus methods:
  // Handles calls to org.chromium.Buffet.Manager.StartDevice().
  void StartDevice(DBusMethodResponse<> response) override;
  // Handles calls to org.chromium.Buffet.Manager.CheckDeviceRegistered().
  void CheckDeviceRegistered(DBusMethodResponse<std::string> response) override;
  // Handles calls to org.chromium.Buffet.Manager.GetDeviceInfo().
  void GetDeviceInfo(DBusMethodResponse<std::string> response) override;
  // Handles calls to org.chromium.Buffet.Manager.RegisterDevice().
  void RegisterDevice(DBusMethodResponse<std::string> response,
                      const chromeos::VariantDictionary& params) override;
  // Handles calls to org.chromium.Buffet.Manager.UpdateState().
  void UpdateState(DBusMethodResponse<> response,
                   const chromeos::VariantDictionary& property_set) override;
  // Handles calls to org.chromium.Buffet.Manager.GetState().
  bool GetState(chromeos::ErrorPtr* error, std::string* state) override;
  // Handles calls to org.chromium.Buffet.Manager.AddCommand().
  void AddCommand(DBusMethodResponse<> response,
                  const std::string& json_command) override;
  // Handles calls to org.chromium.Buffet.Manager.Test()
  std::string TestMethod(const std::string& message) override;

  void OnRegistrationStatusChange(RegistrationStatus status);

  org::chromium::Buffet::ManagerAdaptor dbus_adaptor_{this};
  chromeos::dbus_utils::DBusObject dbus_object_;

  std::shared_ptr<CommandManager> command_manager_;
  std::unique_ptr<StateChangeQueue> state_change_queue_;
  std::shared_ptr<StateManager> state_manager_;
  std::unique_ptr<DeviceRegistrationInfo> device_info_;

  DISALLOW_COPY_AND_ASSIGN(Manager);
};

}  // namespace buffet

#endif  // BUFFET_MANAGER_H_
