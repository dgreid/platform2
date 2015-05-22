// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_COMMANDS_COMMAND_DICTIONARY_H_
#define BUFFET_COMMANDS_COMMAND_DICTIONARY_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/macros.h>
#include <chromeos/errors/error.h>

#include "buffet/commands/command_definition.h"

namespace base {
class Value;
class DictionaryValue;
}  // namespace base

namespace buffet {

class ObjectSchema;

// CommandDictionary is a wrapper around a map of command name and the
// corresponding command definition schema. The command name (the key in
// the map) is a compound name in a form of "package_name.command_name",
// where "package_name" is a name of command package such as "base", "printers",
// and others. So the full command name could be "base.reboot", for example.
class CommandDictionary final {
 public:
  CommandDictionary() = default;

  // Gets the list of names of commands that belong to the given category.
  std::vector<std::string> GetCommandNamesByCategory(
      const std::string& category) const;

  // Loads command definitions from a JSON object. This is done at the daemon
  // startup and whenever a device daemon decides to update its command list.
  // |json| is a JSON dictionary that describes the complete commands contained
  // in a particular |category|. Usually, |categories| are 1:1 with daemons on
  // a device. For instance, the power manager daemon might provide a category
  // "power_man" that provides the "base.reboot" and "base.shutdown" commands.
  // However, nothing prohibits a daemon providing commands in two categories.
  // When LoadCommands is called, all previous definitions of commands from the
  // same category are removed, effectively replacing all the commands in the
  // given category.
  // Optional |base_commands| parameter specifies the definition of standard
  // GCD commands for parameter schema validation. Can be set to nullptr if
  // no validation is needed.
  // Returns false on failure and |error| provides additional error information
  // when provided.
  bool LoadCommands(const base::DictionaryValue& json,
                    const std::string& category,
                    const CommandDictionary* base_commands,
                    chromeos::ErrorPtr* error);
  // Converts all the command definitions to a JSON object for CDD/Device
  // draft.
  // |filter| is a predicate used to filter out the command definitions to
  // be returned by this method. Only command definitions for which the
  // predicate returns true will be included in the resulting JSON.
  // |full_schema| specifies whether full command definitions must be generated
  // (true) for CDD or only overrides from the base schema (false).
  // Returns empty unique_ptr in case of an error and fills in the additional
  // error details in |error|.
  std::unique_ptr<base::DictionaryValue> GetCommandsAsJson(
      const std::function<bool(const CommandDefinition*)>& filter,
      bool full_schema,
      chromeos::ErrorPtr* error) const;
  // Returns the number of command definitions in the dictionary.
  size_t GetSize() const { return definitions_.size(); }
  // Checks if the dictionary has no command definitions.
  bool IsEmpty() const { return definitions_.empty(); }
  // Remove all the command definitions from the dictionary.
  void Clear();
  // Finds a definition for the given command.
  const CommandDefinition* FindCommand(const std::string& command_name) const;
  CommandDefinition* FindCommand(const std::string& command_name);

 private:
  using CommandMap = std::map<std::string, std::unique_ptr<CommandDefinition>>;

  std::unique_ptr<ObjectSchema> BuildObjectSchema(
      const base::DictionaryValue* command_def_json,
      const char* property_name,
      const ObjectSchema* base_def,
      const std::string& command_name,
      chromeos::ErrorPtr* error);

  CommandMap definitions_;  // List of all available command definitions.
  DISALLOW_COPY_AND_ASSIGN(CommandDictionary);
};

}  // namespace buffet

#endif  // BUFFET_COMMANDS_COMMAND_DICTIONARY_H_
