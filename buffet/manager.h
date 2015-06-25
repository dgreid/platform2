// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_MANAGER_H_
#define BUFFET_MANAGER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/values.h>
#include <chromeos/dbus/data_serialization.h>
#include <chromeos/dbus/dbus_object.h>
#include <chromeos/dbus/exported_property_set.h>
#include <chromeos/errors/error.h>

#include "buffet/commands/command_manager.h"
#include "buffet/device_registration_info.h"
#include "buffet/org.chromium.Buffet.Manager.h"

namespace chromeos {
namespace dbus_utils {
class ExportedObjectManager;
}  // namespace dbus_utils
}  // namespace chromeos

namespace buffet {

class BaseApiHandler;
class BuffetConfig;
class StateChangeQueue;
class StateManager;

template<typename... Types>
using DBusMethodResponsePtr =
    std::unique_ptr<chromeos::dbus_utils::DBusMethodResponse<Types...>>;

template<typename... Types>
using DBusMethodResponse =
    chromeos::dbus_utils::DBusMethodResponse<Types...>;

// The Manager is responsible for global state of Buffet.  It exposes
// interfaces which affect the entire device such as device registration and
// device state.
class Manager final : public org::chromium::Buffet::ManagerInterface {
 public:
  explicit Manager(
      const base::WeakPtr<chromeos::dbus_utils::ExportedObjectManager>&
          object_manager);
  ~Manager();

  void Start(
      const base::FilePath& config_path,
      const base::FilePath& state_path,
      const base::FilePath& test_definitions_path,
      bool xmpp_enabled,
      const chromeos::dbus_utils::AsyncEventSequencer::CompletionAction& cb);

 private:
  // DBus methods:
  void CheckDeviceRegistered(
      DBusMethodResponsePtr<std::string> response) override;
  void GetDeviceInfo(DBusMethodResponsePtr<std::string> response) override;
  void RegisterDevice(DBusMethodResponsePtr<std::string> response,
                      const std::string& ticket_id) override;
  bool UpdateDeviceInfo(chromeos::ErrorPtr* error,
                        const std::string& in_name,
                        const std::string& in_description,
                        const std::string& in_location) override;
  bool UpdateServiceConfig(chromeos::ErrorPtr* error,
                           const std::string& client_id,
                           const std::string& client_secret,
                           const std::string& api_key,
                           const std::string& oauth_url,
                           const std::string& service_url) override;
  void UpdateState(DBusMethodResponsePtr<> response,
                   const chromeos::VariantDictionary& property_set) override;
  bool GetState(chromeos::ErrorPtr* error, std::string* state) override;
  void AddCommand(DBusMethodResponsePtr<std::string> response,
                  const std::string& json_command) override;
  void GetCommand(DBusMethodResponsePtr<std::string> response,
                  const std::string& id) override;
  void SetCommandVisibility(DBusMethodResponsePtr<> response,
                            const std::vector<std::string>& in_names,
                            const std::string& in_visibility) override;
  std::string TestMethod(const std::string& message) override;

  void OnGetDeviceInfoSuccess(
      const std::shared_ptr<DBusMethodResponse<std::string>>& response,
      const base::DictionaryValue& device_info);
  void OnGetDeviceInfoError(
      const std::shared_ptr<DBusMethodResponse<std::string>>& response,
      const chromeos::Error* error);

  void OnCommandDefsChanged();
  void OnStateChanged();
  void OnRegistrationChanged(RegistrationStatus status);
  void OnConfigChanged(const BuffetConfig& config);

  org::chromium::Buffet::ManagerAdaptor dbus_adaptor_{this};
  chromeos::dbus_utils::DBusObject dbus_object_;

  std::shared_ptr<CommandManager> command_manager_;
  std::unique_ptr<StateChangeQueue> state_change_queue_;
  std::shared_ptr<StateManager> state_manager_;
  std::unique_ptr<DeviceRegistrationInfo> device_info_;
  std::unique_ptr<BaseApiHandler> base_api_handler_;
  std::unique_ptr<privetd::ShillClient> shill_client_;

  base::WeakPtrFactory<Manager> weak_ptr_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(Manager);
};

}  // namespace buffet

#endif  // BUFFET_MANAGER_H_
