// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_STATES_MOCK_STATE_CHANGE_QUEUE_INTERFACE_H_
#define BUFFET_STATES_MOCK_STATE_CHANGE_QUEUE_INTERFACE_H_

#include <vector>

#include <gmock/gmock.h>

#include "buffet/states/state_change_queue_interface.h"

namespace buffet {

class MockStateChangeQueueInterface : public StateChangeQueueInterface {
 public:
  MOCK_CONST_METHOD0(IsEmpty, bool());
  MOCK_METHOD2(NotifyPropertiesUpdated,
               bool(base::Time timestamp,
                    native_types::Object changed_properties));
  MOCK_METHOD0(GetAndClearRecordedStateChanges, std::vector<StateChange>());
  MOCK_CONST_METHOD0(GetLastStateChangeId, UpdateID());
  MOCK_METHOD1(MockAddOnStateUpdatedCallback,
               base::CallbackList<void(UpdateID)>::Subscription*(
                  const base::Callback<void(UpdateID)>&));
  MOCK_METHOD1(NotifyStateUpdatedOnServer, void(UpdateID));

 private:
  Token AddOnStateUpdatedCallback(
      const base::Callback<void(UpdateID)>& callback) override {
    return Token{MockAddOnStateUpdatedCallback(callback)};
  }
};

}  // namespace buffet

#endif  // BUFFET_STATES_MOCK_STATE_CHANGE_QUEUE_INTERFACE_H_
