// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_COMMANDS_COMMAND_DEFINITION_H_
#define BUFFET_COMMANDS_COMMAND_DEFINITION_H_

#include <memory>
#include <string>

#include <base/macros.h>

#include "buffet/commands/object_schema.h"
#include "buffet/commands/user_role.h"

namespace buffet {

// A simple GCD command definition. This class contains the command category
// and a full object schema describing the command parameter types and
// constraints. See comments for CommandDefinitions::LoadCommands for the
// detailed description of what command categories are and what they are used
// for.
class CommandDefinition final {
 public:
  struct Visibility {
    Visibility() = default;
    Visibility(bool is_local, bool is_cloud)
        : local{is_local}, cloud{is_cloud} {}

    // Converts a comma-separated string of visibility identifiers into the
    // Visibility bitset (|str| is a string like "local,cloud").
    // Special string value "all" is treated as a list of every possible
    // visibility values and "none" to have all the bits cleared.
    bool FromString(const std::string& str, chromeos::ErrorPtr* error);

    // Converts the visibility bitset to a string.
    std::string ToString() const;

    static Visibility GetAll() { return Visibility{true, true}; }
    static Visibility GetLocal() { return Visibility{true, false}; }
    static Visibility GetCloud() { return Visibility{false, true}; }
    static Visibility GetNone() { return Visibility{false, false}; }

    bool local{false};  // Command is available to local clients.
    bool cloud{false};  // Command is available to cloud clients.
  };

  CommandDefinition(const std::string& category,
                    std::unique_ptr<const ObjectSchema> parameters,
                    std::unique_ptr<const ObjectSchema> progress,
                    std::unique_ptr<const ObjectSchema> results);

  // Gets the category this command belongs to.
  const std::string& GetCategory() const { return category_; }
  // Gets the object schema for command parameters.
  const ObjectSchema* GetParameters() const { return parameters_.get(); }
  // Gets the object schema for command progress.
  const ObjectSchema* GetProgress() const { return progress_.get(); }
  // Gets the object schema for command results.
  const ObjectSchema* GetResults() const { return results_.get(); }
  // Returns the command visibility.
  const Visibility& GetVisibility() const { return visibility_; }
  // Changes the command visibility.
  void SetVisibility(const Visibility& visibility);
  // Returns the role required to execute command.
  UserRole GetMinimalRole() const { return minimal_role_; }
  // Changes the role required to execute command.
  void SetMinimalRole(UserRole minimal_role) { minimal_role_ = minimal_role; }

 private:
  std::string category_;  // Cmd category. Could be "powerd" for "base.reboot".
  std::unique_ptr<const ObjectSchema> parameters_;  // Command parameters def.
  std::unique_ptr<const ObjectSchema> progress_;    // Command progress def.
  std::unique_ptr<const ObjectSchema> results_;  // Command results def.
  Visibility visibility_;  // Available to all by default.
  // Minimal role required to execute command.
  UserRole minimal_role_{UserRole::kUser};

  DISALLOW_COPY_AND_ASSIGN(CommandDefinition);
};

}  // namespace buffet

#endif  // BUFFET_COMMANDS_COMMAND_DEFINITION_H_
