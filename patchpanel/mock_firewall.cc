// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/mock_firewall.h"

#include <algorithm>

namespace patchpanel {

int MockFirewall::SetRunInMinijailFailCriterion(
    const std::vector<std::string>& keywords, bool repeat, bool omit_failure) {
  match_criteria_.push_back(Criterion{keywords, repeat, omit_failure, 0});
  return match_criteria_.size() - 1;
}

int MockFirewall::GetRunInMinijailCriterionMatchCount(int id) {
  if (id < 0 || id >= static_cast<int>(match_criteria_.size()))
    return -1;
  return match_criteria_[id].match_count;
}

bool MockFirewall::MatchAndUpdate(const std::vector<std::string>& argv) {
  // Make a copy of the container so that we can delete elements while
  // iterating.
  auto criteria = match_criteria_;
  for (auto& criterion : criteria) {
    bool match = true;
    // Empty criterion is a catch all -- fail on any RunInMinijail.
    for (const std::string& keyword : criterion.keywords) {
      match =
          match && (std::find(argv.begin(), argv.end(), keyword) != argv.end());
    }
    if (match) {
      criterion.match_count++;
      if (!criterion.repeat) {
        match_criteria_.erase(std::remove(match_criteria_.begin(),
                                          match_criteria_.end(), criterion),
                              match_criteria_.end());
      }
      // If not negating, treat as fail criterion,
      // otherwise ignore (return false).
      return !criterion.omit_failure;
    }
  }
  return false;
}

std::vector<std::string> MockFirewall::GetAllCommands() {
  std::vector<std::string> commands;
  commands.reserve(commands_.size());
  for (const auto& argv : commands_) {
    std::string joined_argv = "";
    std::string separator = "";
    for (const auto& arg : argv) {
      joined_argv += separator + arg;
      separator = " ";
    }
    commands.push_back(joined_argv);
  }
  return commands;
}

int MockFirewall::RunInMinijail(const std::vector<std::string>& argv) {
  commands_.push_back(argv);
  return MatchAndUpdate(argv) ? 1 : 0;
}

// Returns the inverse of a given command. For an insert or append returns a
// command to delete that rule, for a deletion returns a command to insert it at
// the start. Assumes that the inverse of -D is always -I and that -I|--insert
// is used without index arguments. This holds as of 2019-11-20 but if you start
// using them you'll need to update this method.
std::vector<std::string> MockFirewall::GetInverseCommand(
    const std::vector<std::string>& argv) {
  std::vector<std::string> inverse;
  if (argv.size() == 0) {
    return inverse;
  }

  bool isIpTablesCommand = argv[0] == kIpTablesPath;
  for (const auto& arg : argv) {
    if (isIpTablesCommand && (arg == "-I" || arg == "-A" || arg == "--insert" ||
                              arg == "--append")) {
      inverse.push_back("-D");
    } else if (isIpTablesCommand && arg == "-D") {
      inverse.push_back("-I");
    } else {
      inverse.push_back(arg);
    }
  }
  return inverse;
}

// This check does not enforce ordering. It only checks
// that for each command that adds a rule/mark with
// ip/iptables, there is a later match command that
// deletes that same rule/mark.
bool MockFirewall::CheckCommandsUndone() {
  return CountActiveCommands() == 0;
}

// For each command, if it's an insert or an append it checks if there's a
// corresponding delete later on, then it returns a count of all rules without
// deletes. Will skip any rule that's not an append or insert e.g. delete
// without an insert first will return 0 not -1.
int MockFirewall::CountActiveCommands() {
  int count = 0;
  for (std::vector<std::vector<std::string>>::iterator it = commands_.begin();
       it != commands_.end(); it++) {
    // For each command that adds a rule, check that its inverse
    // exists later in the log of commands.
    if ((std::find(it->begin(), it->end(), "-A") != it->end()) ||
        (std::find(it->begin(), it->end(), "--append") != it->end()) ||
        (std::find(it->begin(), it->end(), "-I") != it->end()) ||
        (std::find(it->begin(), it->end(), "--insert") != it->end())) {
      bool found = false;
      std::vector<std::string> inverse = GetInverseCommand(*it);
      // If GetInverseCommand returns the same command, then it was
      // not an ip/iptables command that added/removed a rule/mark.
      if (std::equal(it->begin(), it->end(), inverse.begin()))
        continue;
      for (auto it_next = it + 1; it_next != commands_.end(); it_next++) {
        if (*it_next == inverse) {
          found = true;
          break;
        }
      }
      if (!found)
        count++;
    }
  }
  return count;
}

void MockFirewall::ResetStoredCommands() {
  commands_.clear();
}

}  // namespace patchpanel
