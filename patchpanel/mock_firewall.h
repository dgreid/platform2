// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MOCK_FIREWALL_H_
#define PATCHPANEL_MOCK_FIREWALL_H_

#include <string>
#include <vector>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "patchpanel/firewall.h"

namespace patchpanel {

class MockFirewall : public Firewall {
 public:
  MockFirewall() = default;
  MockFirewall(const MockFirewall&) = delete;
  MockFirewall& operator=(const MockFirewall&) = delete;

  ~MockFirewall() = default;

  int SetRunInMinijailFailCriterion(const std::vector<std::string>& keywords,
                                    bool repeat,
                                    bool omit_failure);
  int GetRunInMinijailCriterionMatchCount(int id);
  bool CheckCommandsUndone();
  int CountActiveCommands();
  void ResetStoredCommands();

  // Check if the current command matches a failure rule,
  // if the failure rule is not a repeat rule, remove it
  // from the match criteria.
  bool MatchAndUpdate(const std::vector<std::string>& argv);

  // Returns all commands issued with RunInMinijail during the test.
  std::vector<std::string> GetAllCommands();

 private:
  struct Criterion {
    std::vector<std::string> keywords;
    // If false, remove the criterion after it's matched.
    bool repeat;
    // If false, treat matching commands as failures, otherwise,
    // omit failing.
    bool omit_failure;
    // Count the number of times the criterion is matched.
    int match_count;

    // Don't take into account match_count.
    bool operator==(const Criterion& c) const {
      return (
          std::equal(keywords.begin(), keywords.end(), c.keywords.begin()) &&
          (repeat == c.repeat) && (c.omit_failure == omit_failure));
    }
  };

  // A list of collections of keywords that a command
  // must have in order for it to fail.
  std::vector<Criterion> match_criteria_;

  // A log of commands issued with RunInMinijail during the test.
  std::vector<std::vector<std::string>> commands_;

  // The mock's implementation simply logs the command issued if
  // successful.
  int RunInMinijail(const std::vector<std::string>& argv) override;

  // Given an ip/iptables command, return the command that undoes its effect.
  std::vector<std::string> GetInverseCommand(
      const std::vector<std::string>& command);
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MOCK_FIREWALL_H_
