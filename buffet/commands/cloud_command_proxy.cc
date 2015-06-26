// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/commands/cloud_command_proxy.h"

#include <base/message_loop/message_loop.h>

#include "buffet/commands/command_instance.h"
#include "buffet/commands/prop_constraints.h"
#include "buffet/commands/prop_types.h"
#include "buffet/commands/schema_constants.h"
#include "buffet/device_registration_info.h"

namespace buffet {

namespace {
// Bits used in CommandUpdateFlags for various command resource parts.
enum {
  kFlagResults,
  kFlagState,
  kFlagProgress
};

// Retry timeout for re-sending failed command update request.
static const int64_t kCommandUpdateRetryTimeoutSeconds = 5;

}  // anonymous namespace

CloudCommandProxy::CloudCommandProxy(
    CommandInstance* command_instance,
    CloudCommandUpdateInterface* cloud_command_updater)
    : command_instance_{command_instance},
      cloud_command_updater_{cloud_command_updater} {
}

void CloudCommandProxy::OnResultsChanged() {
  new_pending_command_updates_.set(kFlagResults);
  SendCommandUpdate();
}

void CloudCommandProxy::OnStatusChanged() {
  new_pending_command_updates_.set(kFlagState);
  SendCommandUpdate();
}

void CloudCommandProxy::OnProgressChanged() {
  new_pending_command_updates_.set(kFlagProgress);
  SendCommandUpdate();
}

void CloudCommandProxy::SendCommandUpdate() {
  if (command_update_in_progress_ || new_pending_command_updates_.none())
    return;

  base::DictionaryValue patch;
  if (new_pending_command_updates_.test(kFlagResults)) {
    auto json = TypedValueToJson(command_instance_->GetResults(), nullptr);
    patch.Set(commands::attributes::kCommand_Results, json.release());
  }

  if (new_pending_command_updates_.test(kFlagState)) {
    patch.SetString(commands::attributes::kCommand_State,
                    command_instance_->GetStatus());
  }

  if (new_pending_command_updates_.test(kFlagProgress)) {
    auto json = TypedValueToJson(command_instance_->GetProgress(), nullptr);
    patch.Set(commands::attributes::kCommand_Progress, json.release());
  }
  command_update_in_progress_ = true;
  in_progress_command_updates_ = new_pending_command_updates_;
  new_pending_command_updates_.reset();
  cloud_command_updater_->UpdateCommand(
      command_instance_->GetID(), patch,
      base::Bind(&CloudCommandProxy::OnUpdateCommandFinished,
                 weak_ptr_factory_.GetWeakPtr(), true),
      base::Bind(&CloudCommandProxy::OnUpdateCommandFinished,
                 weak_ptr_factory_.GetWeakPtr(), false));
}

void CloudCommandProxy::ResendCommandUpdate() {
  command_update_in_progress_ = false;
  SendCommandUpdate();
}

void CloudCommandProxy::OnUpdateCommandFinished(bool success) {
  if (success) {
    command_update_in_progress_ = false;
    // If previous update was successful, and we have new pending updates,
    // send a new request to the server immediately.
    SendCommandUpdate();
  } else {
    // If previous request failed, re-send the old data as well.
    new_pending_command_updates_ |= in_progress_command_updates_;
    auto message_loop = base::MessageLoop::current();
    if (message_loop == nullptr) {
      // Assume we are in unit tests, resend the request immediately...
      ResendCommandUpdate();
    } else {
      // Resend the request to the server after a pre-set delay...
      message_loop->PostDelayedTask(
          FROM_HERE,
          base::Bind(&CloudCommandProxy::ResendCommandUpdate,
                     weak_ptr_factory_.GetWeakPtr()),
          base::TimeDelta::FromSeconds(kCommandUpdateRetryTimeoutSeconds));
    }
  }
}

}  // namespace buffet
