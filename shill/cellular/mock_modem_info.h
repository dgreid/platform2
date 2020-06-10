// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_MOCK_MODEM_INFO_H_
#define SHILL_CELLULAR_MOCK_MODEM_INFO_H_

#include <memory>
#include <string>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/cellular/mock_pending_activation_store.h"
#include "shill/cellular/modem_info.h"
#include "shill/mock_control.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"

namespace shill {

class MockModemInfo : public ModemInfo {
 public:
  MockModemInfo();

  // All nullptr parameters are replaced by mock objects.
  MockModemInfo(ControlInterface* control,
                EventDispatcher* dispatcher,
                Metrics* metrics,
                Manager* manager);

  ~MockModemInfo() override;

  // Replaces data members in ModemInfo by mock objects.
  // The following are relaced by mocks if they are nullptr: control_interface,
  // dispatcher, metrics, manager.
  // The following are always replaced by mocks: pending_activation_store.
  void SetMockMembers();

  // Accessors for mock objects
  MockPendingActivationStore* mock_pending_activation_store() const {
    return mock_pending_activation_store_;
  }
  MockControl* mock_control_interface() const { return mock_control_.get(); }
  MockEventDispatcher* mock_dispatcher() const {
    return mock_dispatcher_.get();
  }
  MockMetrics* mock_metrics() const { return mock_metrics_.get(); }
  MockManager* mock_manager() const { return mock_manager_.get(); }

  MOCK_METHOD(void, Start, (), (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(void, OnDeviceInfoAvailable, (const std::string&), (override));

 private:
  std::unique_ptr<MockControl> mock_control_;
  std::unique_ptr<MockEventDispatcher> mock_dispatcher_;
  std::unique_ptr<MockMetrics> mock_metrics_;
  std::unique_ptr<MockManager> mock_manager_;

  // owned by ModemInfo
  MockPendingActivationStore* mock_pending_activation_store_;

  DISALLOW_COPY_AND_ASSIGN(MockModemInfo);
};

}  // namespace shill

#endif  // SHILL_CELLULAR_MOCK_MODEM_INFO_H_
