// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/mock_modem_info.h"

using testing::NiceMock;

namespace shill {

MockModemInfo::MockModemInfo(ControlInterface* control,
                             EventDispatcher* dispatcher,
                             Metrics* metrics,
                             Manager* manager)
    : ModemInfo(control, dispatcher, metrics, manager),
      mock_pending_activation_store_(nullptr) {
  pending_activation_store_ = std::make_unique<MockPendingActivationStore>();
  mock_pending_activation_store_ =
      static_cast<MockPendingActivationStore*>(pending_activation_store_.get());
}

MockModemInfo::~MockModemInfo() = default;

}  // namespace shill
