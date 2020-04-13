// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/mock_modem_info.h"

namespace shill {

MockModemInfo::MockModemInfo()
    : ModemInfo(nullptr, nullptr, nullptr, nullptr),
      mock_pending_activation_store_(nullptr) {}

MockModemInfo::MockModemInfo(ControlInterface* control,
                             EventDispatcher* dispatcher,
                             Metrics* metrics,
                             Manager* manager)
    : ModemInfo(control, dispatcher, metrics, manager),
      mock_pending_activation_store_(nullptr) {
  SetMockMembers();
}

MockModemInfo::~MockModemInfo() = default;

void MockModemInfo::SetMockMembers() {
  // These are always replaced by mocks.
  // Assumes ownership.
  pending_activation_store_ = std::make_unique<MockPendingActivationStore>();
  mock_pending_activation_store_ =
      static_cast<MockPendingActivationStore*>(pending_activation_store_.get());
  // These are replaced by mocks only if current unset in ModemInfo.
  if (!control_interface_) {
    mock_control_.reset(new MockControl());
    control_interface_ = mock_control_.get();
  }
  if (!dispatcher_) {
    mock_dispatcher_.reset(new MockEventDispatcher());
    dispatcher_ = mock_dispatcher_.get();
  }
  if (!metrics_) {
    mock_metrics_.reset(new MockMetrics());
    metrics_ = mock_metrics_.get();
  }
  if (!manager_) {
    mock_manager_.reset(
        new MockManager(control_interface(), dispatcher(), metrics()));
    manager_ = mock_manager_.get();
  }
}

}  // namespace shill
