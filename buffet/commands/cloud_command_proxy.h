// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_COMMANDS_CLOUD_COMMAND_PROXY_H_
#define BUFFET_COMMANDS_CLOUD_COMMAND_PROXY_H_

#include <bitset>
#include <string>

#include <base/macros.h>
#include <base/memory/weak_ptr.h>

#include "buffet/commands/cloud_command_update_interface.h"
#include "buffet/commands/command_proxy_interface.h"

namespace buffet {

class CommandInstance;

// Command proxy which publishes command updates to the cloud.
class CloudCommandProxy final : public CommandProxyInterface {
 public:
  CloudCommandProxy(CommandInstance* command_instance,
                    CloudCommandUpdateInterface* cloud_command_updater);
  ~CloudCommandProxy() override = default;

  // CommandProxyInterface implementation/overloads.
  void OnResultsChanged() override;
  void OnStatusChanged() override;
  void OnProgressChanged() override;

 private:
  // Flags used to mark the command resource parts that need to be updated on
  // the server.
  using CommandUpdateFlags = std::bitset<3>;

  // Sends an asynchronous request to GCD server to update the command resource.
  void SendCommandUpdate();

  // Retry last failed request.
  void ResendCommandUpdate();

  // Callback invoked by the asynchronous PATCH request to the server.
  // Called both in a case of successfully updating server command resource
  // and in case of an error, indicated by the |success| parameter.
  void OnUpdateCommandFinished(bool success);

  CommandInstance* command_instance_;
  CloudCommandUpdateInterface* cloud_command_updater_;

  // Set to true while a pending PATCH request is in flight to the server.
  bool command_update_in_progress_{false};
  // The flags indicating of new command resource updates since the last req.
  CommandUpdateFlags new_pending_command_updates_;
  // The flags indicating of command updates currently in flight.
  CommandUpdateFlags in_progress_command_updates_;

  base::WeakPtrFactory<CloudCommandProxy> weak_ptr_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(CloudCommandProxy);
};

}  // namespace buffet

#endif  // BUFFET_COMMANDS_CLOUD_COMMAND_PROXY_H_
