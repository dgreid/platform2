// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_STATES_STATE_MANAGER_H_
#define BUFFET_STATES_STATE_MANAGER_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/macros.h>
#include <chromeos/errors/error.h>
#include <chromeos/variant_dictionary.h>

#include "buffet/states/state_change_queue_interface.h"
#include "buffet/states/state_package.h"

namespace base {
class DictionaryValue;
class FilePath;
class Time;
}  // namespace base

namespace buffet {

// StateManager is the class that aggregates the device state fragments
// provided by device daemons and makes the aggregate device state available
// to the GCD cloud server and local clients.
class StateManager final {
 public:
  explicit StateManager(StateChangeQueueInterface* state_change_queue);

  void AddOnChangedCallback(const base::Closure& callback);

  // Initializes the state manager and load device state fragments.
  // Called by Buffet daemon at startup.
  void Startup();

  // Returns aggregated state properties across all registered packages as
  // a JSON object that can be used to send the device state to the GCD server.
  std::unique_ptr<base::DictionaryValue> GetStateValuesAsJson(
      chromeos::ErrorPtr* error) const;

  // Updates a multiple property values.
  bool SetProperties(const chromeos::VariantDictionary& property_set,
                     chromeos::ErrorPtr* error);

  // Returns all the categories the state properties are registered from.
  // As with GCD command handling, the category normally represent a device
  // service (daemon) that is responsible for a set of properties.
  const std::set<std::string>& GetCategories() const {
    return categories_;
  }

  // Returns the recorded state changes since last time this method has been
  // called.
  std::vector<StateChange> GetAndClearRecordedStateChanges();

 private:
  friend class StateManagerTest;

  // Updates a single property value. |full_property_name| must be the full
  // name of the property to update in format "package.property".
  bool SetPropertyValue(const std::string& full_property_name,
                        const chromeos::Any& value,
                        const base::Time& timestamp,
                        chromeos::ErrorPtr* error);

  // Loads a device state fragment from a JSON object. |category| represents
  // a device daemon providing the state fragment or empty string for the
  // base state fragment.
  bool LoadStateDefinition(const base::DictionaryValue& json,
                           const std::string& category,
                           chromeos::ErrorPtr* error);

  // Loads a device state fragment JSON file. The file name (without extension)
  // is used as the state fragment category.
  bool LoadStateDefinition(const base::FilePath& json_file_path,
                           chromeos::ErrorPtr* error);

  // Loads the base device state fragment JSON file. This state fragment
  // defines the standard state properties from the 'base' package as defined
  // by GCD specification.
  bool LoadBaseStateDefinition(const base::FilePath& json_file_path,
                               chromeos::ErrorPtr* error);

  // Loads state default values from JSON object.
  bool LoadStateDefaults(const base::DictionaryValue& json,
                         chromeos::ErrorPtr* error);

  // Loads state default values from JSON file.
  bool LoadStateDefaults(const base::FilePath& json_file_path,
                         chromeos::ErrorPtr* error);

  // Finds a package by its name. Returns nullptr if not found.
  StatePackage* FindPackage(const std::string& package_name);
  // Finds a package by its name. If none exists, one will be created.
  StatePackage* FindOrCreatePackage(const std::string& package_name);

  StateChangeQueueInterface* state_change_queue_;  // Owned by buffet::Manager.
  std::map<std::string, std::unique_ptr<StatePackage>> packages_;
  std::set<std::string> categories_;

  std::vector<base::Closure> on_changed_;

  DISALLOW_COPY_AND_ASSIGN(StateManager);
};

}  // namespace buffet

#endif  // BUFFET_STATES_STATE_MANAGER_H_
