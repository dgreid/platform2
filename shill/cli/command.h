// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CLI_COMMAND_H_
#define SHILL_CLI_COMMAND_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>

namespace shill_cli {

// Abstraction of a CLI command, providing such functionality as containing
// subcommands, prefix-matching user input to perform the appropriate action,
// providing a helpful message when unexpected input is provided, etc.
//
// This class may either be inherited from--setting up subcommands in the
// constructor and overwriting virtual methods as needed--or may be used
// directly through the public constructors. This is done so that "leaf"
// commands that don't contain any subcommands can avoid needing to create a
// whole child class.
class Command {
 public:
  using ArgsIterator = std::vector<std::string>::const_iterator;
  using TakeArgsFunction =
      base::Callback<bool(ArgsIterator args_begin, ArgsIterator args_end)>;

  virtual ~Command() = default;

  bool Run(std::vector<std::string>::const_iterator args_current,
           std::vector<std::string>::const_iterator args_end,
           std::string current_command);

  // Function run when the command is provided with no other arguments. Child
  // classes can override this, while leaf commands that don't warrant
  // creating a whole child class can replace this using the |top_function|
  // constructor parameter.
  virtual bool Top() { return false; }
  // The help subcommand for this command.
  virtual bool Help(ArgsIterator args_begin, ArgsIterator args_end) const;

  const std::string& full_name() const { return full_name_; }
  const std::string& short_description() const { return short_description_; }
  ArgsIterator extra_args_begin() const { return extra_args_begin_; }
  ArgsIterator extra_args_end() const { return extra_args_end_; }

 protected:
  // Constructor used for child classes, which can override Top() to change
  // the behavior of the top function.
  Command(std::string full_name, std::string short_description);

  void AddSubcommand(std::unique_ptr<Command> cmd);
  // Add leaf subcommand whose top function takes no arguments.
  void AddSubcommand(std::string full_name,
                     std::string short_description,
                     base::Callback<bool()> top_function);
  // Add leaf subcommand whose top function takes all remaining arguments.
  void AddSubcommand(std::string full_name,
                     std::string short_description,
                     TakeArgsFunction top_function);

  void ListSubcommands() const;

 private:
  friend class CommandTest;

  Command(std::string full_name,
          std::string short_description,
          base::Callback<bool()> top_function);
  Command(std::string full_name,
          std::string short_description,
          TakeArgsFunction top_function);

  // Returns sorted vector of subcommands in the form
  // "$UNIQUE_PREFIX[$REMAINDER]".
  std::vector<std::string> GetPrefixedSubcommands() const;

  // Full name of the command. Users may match any prefix of this name as long
  // as it is not ambiguous.
  std::string full_name_;
  // Description of the command that can fit in one line.
  std::string short_description_;

  // Leaf commands have the option of consuming any remaining command-line
  // arguments, whether to ignore them or to parse and use them.
  bool consume_extra_args_;
  ArgsIterator extra_args_begin_;
  ArgsIterator extra_args_end_;

  base::Callback<bool()> top_function_;
  std::map<std::string, std::unique_ptr<Command>> subcommands_;
};

}  // namespace shill_cli

#endif  // SHILL_CLI_COMMAND_H_
