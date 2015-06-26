// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_COMMANDS_CLOUD_COMMAND_UPDATE_INTERFACE_H_
#define BUFFET_COMMANDS_CLOUD_COMMAND_UPDATE_INTERFACE_H_

#include <string>

#include <base/callback_forward.h>
#include <base/values.h>

namespace buffet {

// An abstract interface to allow for sending command update requests to the
// cloud server.
class CloudCommandUpdateInterface {
 public:
  virtual void UpdateCommand(const std::string& command_id,
                             const base::DictionaryValue& command_patch,
                             const base::Closure& on_success,
                             const base::Closure& on_error) = 0;

 protected:
  virtual ~CloudCommandUpdateInterface() = default;
};

}  // namespace buffet

#endif  // BUFFET_COMMANDS_CLOUD_COMMAND_UPDATE_INTERFACE_H_
