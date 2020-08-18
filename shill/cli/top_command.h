// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CLI_TOP_COMMAND_H_
#define SHILL_CLI_TOP_COMMAND_H_

#include <memory>

#include <base/bind.h>

#include "shill/cli/command.h"

namespace shill_cli {

class TopCommand : public Command {
 public:
  TopCommand() : Command("shillcli", "Shill Command Line Interface") {
    // TODO(crbug.com/1024079) Replace these with actual functional commands
    // (most of these will likely have their own child class). Having this is
    // useful for testing.
    AddSubcommand("device", "Interact with Devices", base::Bind([]() {
                    LOG(INFO) << "Device was called";
                    return true;
                  }));
    AddSubcommand("service", "Interact with Services", base::Bind([]() {
                    LOG(INFO) << "Service was called";
                    return true;
                  }));
    AddSubcommand("log", "Testing log", base::Bind([]() {
                    LOG(INFO) << "Log was called";
                    return true;
                  }));
    AddSubcommand("list", "Testing list", base::Bind([]() {
                    LOG(INFO) << "List was called";
                    return true;
                  }));
  }

  bool Top() override {
    LOG(INFO) << "{Insert top-level shill status here}";
    LOG(INFO) << "";
    LOG(INFO) << "See `" << full_name() << " help` for more commands.";
    return true;
  }
};

}  // namespace shill_cli

#endif  // SHILL_CLI_TOP_COMMAND_H_
