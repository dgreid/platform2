// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/buffet/command_handlers.h"

#include <memory>

#include <base/callback.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <base/threading/thread_task_runner_handle.h>

#include "buffet/dbus-proxies.h"

using org::chromium::Buffet::CommandProxyInterface;
using org::chromium::Buffet::ObjectManagerProxy;

namespace power_manager {
namespace buffet {

namespace {

// This is the number of seconds to wait before rebooting to allow the command
// status to make it to the cloud.
static const int kRebootDelayInSeconds = 3;

// The GCD command name powerd handles.
const char kBaseRebootCommand[] = "base.reboot";

// The command status "done" is ignored, which means this command has already
// been processed.
const char kCommandStatusDone[] = "done";

// A helper class to register callbacks with Buffet to be notified of new
// GCD commands coming in.
class CommandHandler final {
 public:
  CommandHandler() : weak_ptr_factory_(this) {}
  CommandHandler(const CommandHandler&) = delete;
  CommandHandler& operator=(const CommandHandler&) = delete;

  // Initialize the object and start listening for Buffet commands on D-Bus.
  void Start(const scoped_refptr<dbus::Bus>& bus,
             const base::Closure& reboot_callback) {
    reboot_callback_ = reboot_callback;
    object_manager_.reset(new ObjectManagerProxy(bus));
    object_manager_->SetCommandAddedCallback(
        base::Bind(&CommandHandler::OnCommand, weak_ptr_factory_.GetWeakPtr()));
  }

 private:
  // Command handler callback method. Called when a new command is available
  // on D-Bus from Buffet.
  void OnCommand(CommandProxyInterface* command) {
    if (command->state() != kCommandStatusDone &&
        command->name() == kBaseRebootCommand) {
      // Right now powerd handles only 'base.reboot' command and ignores
      // everything else.
      if (command->Complete({}, nullptr)) {
        // Delay the reboot by a small amount to help mitigate a race where the
        // cloud status doesn't get updated and we get into a reboot loop.
        //
        // TODO(kemp): This should be removed once brbug.com/1265 is fixed.
        base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
            FROM_HERE, reboot_callback_,
            base::TimeDelta::FromSeconds(kRebootDelayInSeconds));
      }
    }
  }

  std::unique_ptr<ObjectManagerProxy> object_manager_;
  base::Closure reboot_callback_;

  base::WeakPtrFactory<CommandHandler> weak_ptr_factory_;
};

}  // anonymous namespace

void InitCommandHandlers(const scoped_refptr<dbus::Bus>& bus,
                         const base::Closure& reboot_callback) {
  static CommandHandler command_handler;
  command_handler.Start(bus, reboot_callback);
}

}  // namespace buffet
}  // namespace power_manager
