// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/manager.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <brillo/errors/error_codes.h>
#include <google-lpa/lpa/core/lpa.h>

namespace hermes {

Manager::Manager()
    : context_(Context::Get()),
      dbus_adaptor_(context_->adaptor_factory()->CreateManagerAdaptor(this)) {
}

void Manager::SetTestMode(bool /*is_test_mode*/) {
  NOTIMPLEMENTED();
}

void Manager::OnEuiccUpdated(uint8_t physical_slot, EuiccSlotInfo slot_info) {
  auto iter = available_euiccs_.find(physical_slot);
  if (iter == available_euiccs_.end()) {
    available_euiccs_[physical_slot] =
        std::make_unique<Euicc>(physical_slot, std::move(slot_info));
    UpdateAvailableEuiccsProperty();
    return;
  }

  iter->second->UpdateSlotInfo(std::move(slot_info));
}

void Manager::OnEuiccRemoved(uint8_t physical_slot) {
  auto iter = available_euiccs_.find(physical_slot);
  if (iter == available_euiccs_.end()) {
    return;
  }
  available_euiccs_.erase(iter);
  UpdateAvailableEuiccsProperty();
}

void Manager::UpdateAvailableEuiccsProperty() {
  std::vector<dbus::ObjectPath> euicc_paths;
  for (const auto& euicc : available_euiccs_) {
    euicc_paths.push_back(euicc.second->object_path());
  }
  dbus_adaptor_->SetAvailableEuiccs(euicc_paths);
}

}  // namespace hermes
