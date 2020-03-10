// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cli/command.h"

#include <algorithm>
#include <utility>

#include <base/bind.h>

namespace shill_cli {

namespace {

constexpr char kHelpCommand[] = "help";

int CommonLength(const std::string& a, const std::string& b) {
  size_t max_len = std::min(a.size(), b.size());
  for (size_t len = 0; len < max_len; ++len) {
    if (a[len] != b[len]) {
      return static_cast<int>(len);
    }
  }
  return -1;
}

bool StartsWith(std::string_view prefix, std::string_view word) {
  if (prefix.size() > word.size()) {
    return false;
  }

  return prefix == word.substr(0, prefix.size());
}

bool ArgsFunctionWrapper(Command* cmd, Command::TakeArgsFunction cb) {
  CHECK(cmd);
  const auto& args_begin = cmd->extra_args_begin();
  const auto& args_end = cmd->extra_args_end();
  // Since |cb| is directly handling arguments, first check for a help command
  // to ensure `.. help` always works.
  if (args_begin != args_end && StartsWith(*args_begin, kHelpCommand)) {
    return cmd->Help(std::next(args_begin), args_end);
  }
  return cb.Run(args_begin, args_end);
}

}  // namespace

Command::Command(std::string full_name, std::string short_description)
    : Command(std::move(full_name),
              std::move(short_description),
              base::Bind(&Command::Top, base::Unretained(this))) {}

Command::Command(std::string full_name,
                 std::string short_description,
                 base::Callback<bool()> func)
    : full_name_(full_name),
      short_description_(short_description),
      consume_extra_args_(false),
      top_function_(std::move(func)) {
  // Avoid infinite loop of Command constructors.
  if (full_name != kHelpCommand) {
    AddSubcommand(kHelpCommand, "Help for this command",
                  base::Bind(&Command::Help, base::Unretained(this)));
  }
}

Command::Command(std::string full_name,
                 std::string short_description,
                 TakeArgsFunction top_function)
    : Command(std::move(full_name),
              std::move(short_description),
              base::Bind(&ArgsFunctionWrapper,
                         base::Unretained(this),
                         std::move(top_function))) {
  consume_extra_args_ = true;
}

bool Command::Run(ArgsIterator args_current,
                  ArgsIterator args_end,
                  std::string current_command) {
  if (args_current == args_end || consume_extra_args_) {
    extra_args_begin_ = args_current;
    extra_args_end_ = args_end;
    return top_function_.Run();
  }

  auto match = std::find_if(subcommands_.begin(), subcommands_.end(),
                            [args_current](const auto& pair) {
                              return StartsWith(*args_current, pair.first);
                            });
  if (match == subcommands_.end()) {
    LOG(INFO) << "Unknown command '" << *args_current << "'. Try `"
              << current_command << " " << kHelpCommand << "`";
    return false;
  }

  // Since we're iterating over sorted strings, multiple matches must occur one
  // after the other.
  auto next = std::next(match);
  if (next != subcommands_.end() && StartsWith(*args_current, next->first)) {
    LOG(INFO) << "Ambiguous command '" << *args_current << "'. Try `"
              << current_command << " " << kHelpCommand << "`";
    return false;
  }

  current_command += " " + match->first;
  return match->second->Run(std::next(args_current), args_end,
                            std::move(current_command));
}

bool Command::Help(ArgsIterator /*begin*/, ArgsIterator /*end*/) const {
  LOG(INFO) << short_description_;
  if (subcommands_.empty()) {
    return true;
  }

  LOG(INFO) << "";
  ListSubcommands();
  return true;
}

void Command::AddSubcommand(std::unique_ptr<Command> cmd) {
  CHECK(!cmd->full_name_.empty() && cmd->full_name_[0] != ' ');
  // Ensure every command has a unique prefix.
  for (auto iter = subcommands_.begin(); iter != subcommands_.end(); ++iter) {
    CHECK_NE(CommonLength(cmd->full_name_, iter->first), -1);
  }
  subcommands_.insert({cmd->full_name_, std::move(cmd)});
}

void Command::AddSubcommand(std::string full_name,
                            std::string short_description,
                            base::Callback<bool()> top_function) {
  // Note that std::make_unique does not have access to this Command
  // constructor.
  AddSubcommand(std::unique_ptr<Command>(
      new Command(std::move(full_name), std::move(short_description),
                  std::move(top_function))));
}

void Command::AddSubcommand(std::string full_name,
                            std::string short_description,
                            TakeArgsFunction top_function) {
  // Note that std::make_unique does not have access to this Command
  // constructor.
  AddSubcommand(std::unique_ptr<Command>(
      new Command(std::move(full_name), std::move(short_description),
                  std::move(top_function))));
}

void Command::ListSubcommands() const {
  if (subcommands_.empty()) {
    return;
  }

  auto command_names = GetPrefixedSubcommands();
  int max_length =
      std::max_element(command_names.begin(), command_names.end(),
                       [](auto& a, auto& b) { return a.size() < b.size(); })
          ->size();
  LOG(INFO) << "SUBCOMMANDS";
  auto iter = subcommands_.begin();
  for (auto name : command_names) {
    LOG(INFO) << std::string(4, ' ') << name
              << std::string(max_length + 4 - name.size(), ' ')
              << iter->second->short_description();
    ++iter;
  }
}

std::vector<std::string> Command::GetPrefixedSubcommands() const {
  std::vector<std::string> command_names;
  if (subcommands_.empty()) {
    return command_names;
  }

  command_names.reserve(subcommands_.size() + 1);
  for (auto iter = subcommands_.begin(); iter != subcommands_.end(); ++iter) {
    command_names.push_back(iter->first);
  }
  // Add sentinel to the end to reduce edge cases.
  command_names.push_back(" ");

  // Since |command_names| is sorted, we only need to check consecutive
  // neighbors in order to find the unique prefix for each command.
  int match_length = CommonLength(command_names[0], command_names[1]);
  command_names[0].insert(match_length + 1, "[");
  command_names[0].push_back(']');
  for (int i = 1; i < command_names.size() - 1; ++i) {
    // |match_length| now represents the common length with the element to the
    // left. |new_match_length| represents the common length with the element
    // to the right.
    int new_match_length = CommonLength(command_names[i], command_names[i + 1]);
    command_names[i].insert(std::max(match_length, new_match_length) + 1, "[");
    command_names[i].push_back(']');
    match_length = new_match_length;
  }
  command_names.pop_back();
  return command_names;
}

}  // namespace shill_cli
