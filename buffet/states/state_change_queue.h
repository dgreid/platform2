// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_STATES_STATE_CHANGE_QUEUE_H_
#define BUFFET_STATES_STATE_CHANGE_QUEUE_H_

#include <map>
#include <vector>

#include <base/macros.h>
#include <base/threading/thread_checker.h>

#include "buffet/states/state_change_queue_interface.h"

namespace buffet {

// An object to record and retrieve device state change notification events.
class StateChangeQueue : public StateChangeQueueInterface {
 public:
  explicit StateChangeQueue(size_t max_queue_size);

  // Overrides from StateChangeQueueInterface.
  bool IsEmpty() const override { return state_changes_.empty(); }
  bool NotifyPropertiesUpdated(
      base::Time timestamp,
      native_types::Object changed_properties) override;
  std::vector<StateChange> GetAndClearRecordedStateChanges() override;
  UpdateID GetLastStateChangeId() const override { return last_change_id_; }
  Token AddOnStateUpdatedCallback(
      const base::Callback<void(UpdateID)>& callback) override;
  void NotifyStateUpdatedOnServer(UpdateID update_id) override;

 private:
  // To make sure we do not call NotifyPropertiesUpdated() and
  // GetAndClearRecordedStateChanges() on different threads, |thread_checker_|
  // is here to help us with verifying the single-threaded operation.
  base::ThreadChecker thread_checker_;

  // Maximum queue size. If it is full, the oldest state update records are
  // merged together until the queue size is within the size limit.
  const size_t max_queue_size_;

  // Accumulated list of device state change notifications.
  std::map<base::Time, native_types::Object> state_changes_;

  // An ID of last state change update. Each NotifyPropertiesUpdated()
  // invocation increments this value by 1.
  UpdateID last_change_id_{0};

  // Callback list for state change queue even sinks.
  base::CallbackList<void(UpdateID)> callbacks_;

  DISALLOW_COPY_AND_ASSIGN(StateChangeQueue);
};

}  // namespace buffet

#endif  // BUFFET_STATES_STATE_CHANGE_QUEUE_H_
