// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_COMMANDS_CLOUD_COMMAND_PROXY_H_
#define BUFFET_COMMANDS_CLOUD_COMMAND_PROXY_H_

#include <deque>
#include <memory>
#include <string>
#include <utility>

#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <base/task_runner.h>
#include <chromeos/backoff_entry.h>

#include "buffet/commands/cloud_command_update_interface.h"
#include "buffet/commands/command_proxy_interface.h"
#include "buffet/states/state_change_queue_interface.h"

namespace buffet {

class CommandInstance;

// Command proxy which publishes command updates to the cloud.
class CloudCommandProxy final : public CommandProxyInterface {
 public:
  CloudCommandProxy(CommandInstance* command_instance,
                    CloudCommandUpdateInterface* cloud_command_updater,
                    StateChangeQueueInterface* state_change_queue,
                    std::unique_ptr<chromeos::BackoffEntry> backoff_entry,
                    const scoped_refptr<base::TaskRunner>& task_runner);
  ~CloudCommandProxy() override = default;

  // CommandProxyInterface implementation/overloads.
  void OnResultsChanged() override;
  void OnStatusChanged() override;
  void OnProgressChanged() override;

 private:
  using UpdateID = StateChangeQueueInterface::UpdateID;
  using UpdateQueueEntry =
      std::pair<UpdateID, std::unique_ptr<base::DictionaryValue>>;

  // Puts a command update data into the update queue, and optionally sends an
  // asynchronous request to GCD server to update the command resource, if there
  // are no pending device status updates.
  void QueueCommandUpdate(std::unique_ptr<base::DictionaryValue> patch);

  // Sends an asynchronous request to GCD server to update the command resource,
  // if there are no pending device status updates.
  void SendCommandUpdate();

  // Retry the last failed command update request to the server.
  void ResendCommandUpdate();

  // Callback invoked by the asynchronous PATCH request to the server.
  // Called both in a case of successfully updating server command resource
  // and in case of an error, indicated by the |success| parameter.
  void OnUpdateCommandFinished(bool success);

  // Callback invoked by the device state change queue to notify of the
  // successful device state update. |update_id| is the ID of the state that
  // has been updated on the server.
  void OnDeviceStateUpdated(UpdateID update_id);

  CommandInstance* command_instance_;
  CloudCommandUpdateInterface* cloud_command_updater_;
  StateChangeQueueInterface* state_change_queue_;
  scoped_refptr<base::TaskRunner> task_runner_;

  // Backoff for SendCommandUpdate() method.
  std::unique_ptr<chromeos::BackoffEntry> cloud_backoff_entry_;

  // Set to true while a pending PATCH request is in flight to the server.
  bool command_update_in_progress_{false};
  // Update queue with all the command update requests ready to be sent to
  // the server.
  std::deque<UpdateQueueEntry> update_queue_;

  // Callback token from the state change queue for OnDeviceStateUpdated()
  // callback for ask the device state change queue to call when the state
  // is updated on the server.
  StateChangeQueueInterface::Token callback_token_;

  // Last device state update ID that has been sent out to the server
  // successfully.
  UpdateID last_state_update_id_{0};

  base::WeakPtrFactory<CloudCommandProxy> backoff_weak_ptr_factory_{this};
  base::WeakPtrFactory<CloudCommandProxy> weak_ptr_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(CloudCommandProxy);
};

}  // namespace buffet

#endif  // BUFFET_COMMANDS_CLOUD_COMMAND_PROXY_H_
