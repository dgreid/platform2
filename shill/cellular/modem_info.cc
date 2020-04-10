// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/modem_info.h"

#include <memory>
#include <utility>

#include <chromeos/dbus/service_constants.h>

#include "shill/cellular/modem_manager.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/pending_activation_store.h"

namespace shill {

ModemInfo::ModemInfo(ControlInterface* control_interface,
                     EventDispatcher* dispatcher,
                     Metrics* metrics,
                     Manager* manager)
    : control_interface_(control_interface),
      dispatcher_(dispatcher),
      metrics_(metrics),
      manager_(manager) {}

ModemInfo::~ModemInfo() {
  Stop();
}

void ModemInfo::Start() {
  pending_activation_store_.reset(new PendingActivationStore());
  pending_activation_store_->InitStorage(manager_->storage_path());
  modem_manager_ = std::make_unique<ModemManager>(
      modemmanager::kModemManager1ServiceName,
      RpcIdentifier(modemmanager::kModemManager1ServicePath), this);
  modem_manager_->Start();
}

void ModemInfo::Stop() {
  pending_activation_store_.reset();
  modem_manager_ = nullptr;
}

void ModemInfo::OnDeviceInfoAvailable(const std::string& link_name) {
  modem_manager_->OnDeviceInfoAvailable(link_name);
}

void ModemInfo::set_pending_activation_store(
    PendingActivationStore* pending_activation_store) {
  pending_activation_store_.reset(pending_activation_store);
}

}  // namespace shill
