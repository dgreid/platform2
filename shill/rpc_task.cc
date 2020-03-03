// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/rpc_task.h"

#include <base/strings/string_number_conversions.h>

#include "shill/adaptor_interfaces.h"
#include "shill/control_interface.h"
#include "shill/logging.h"

using std::map;
using std::string;

namespace shill {

// static
unsigned int RpcTask::serial_number_ = 0;

RpcTask::RpcTask(ControlInterface* control_interface, RpcTaskDelegate* delegate)
    : delegate_(delegate),
      unique_name_(base::NumberToString(serial_number_++)),
      adaptor_(control_interface->CreateRpcTaskAdaptor(this)) {
  CHECK(delegate);
  LOG(INFO) << "RpcTask " + unique_name_ + " created.";
}

RpcTask::~RpcTask() {
  LOG(INFO) << "RpcTask " + unique_name_ + " destroyed.";
}

void RpcTask::GetLogin(string* user, string* password) const {
  delegate_->GetLogin(user, password);
}

void RpcTask::Notify(const string& reason, const map<string, string>& dict) {
  delegate_->Notify(reason, dict);
}

map<string, string> RpcTask::GetEnvironment() const {
  map<string, string> env;
  env.emplace(kRpcTaskServiceVariable,
              adaptor_->GetRpcConnectionIdentifier().value());
  env.emplace(kRpcTaskPathVariable, adaptor_->GetRpcIdentifier().value());
  return env;
}

// TODO(quiche): remove after moving OpenVPNDriver over to ExternalTask.
const RpcIdentifier& RpcTask::GetRpcIdentifier() const {
  return adaptor_->GetRpcIdentifier();
}

// TODO(quiche): remove after moving OpenVPNDriver over to ExternalTask.
const RpcIdentifier& RpcTask::GetRpcConnectionIdentifier() const {
  return adaptor_->GetRpcConnectionIdentifier();
}

}  // namespace shill
