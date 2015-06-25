// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_STATES_STATE_CHANGE_QUEUE_INTERFACE_H_
#define BUFFET_STATES_STATE_CHANGE_QUEUE_INTERFACE_H_

#include <vector>

#include <base/time/time.h>
#include <chromeos/variant_dictionary.h>

#include "buffet/commands/schema_utils.h"

namespace buffet {

// A simple notification record event to track device state changes.
// The |timestamp| records the time of the state change.
// |changed_properties| contains a property set with the new property values
// which were updated at the time the event was recorded.
struct StateChange {
  StateChange(base::Time time, native_types::Object properties)
      : timestamp{time}, changed_properties{std::move(properties)} {}
  base::Time timestamp;
  native_types::Object changed_properties;
};

// An abstract interface to StateChangeQueue to record and retrieve state
// change notification events.
class StateChangeQueueInterface {
 public:
  using UpdateID = uint64_t;

  // Returns true if the state change notification queue is empty.
  virtual bool IsEmpty() const = 0;

  // Called by StateManager when device state properties are updated.
  virtual bool NotifyPropertiesUpdated(
      base::Time timestamp,
      native_types::Object changed_properties) = 0;

  // Returns the recorded state changes since last time this method was called.
  virtual std::vector<StateChange> GetAndClearRecordedStateChanges() = 0;

  // Returns an ID of last state change update. Each NotifyPropertiesUpdated()
  // invocation increments this value by 1.
  virtual UpdateID GetLastStateChangeId() const = 0;

 protected:
  // No one should attempt do destroy the queue through the interface.
  virtual ~StateChangeQueueInterface() {}
};

}  // namespace buffet

#endif  // BUFFET_STATES_STATE_CHANGE_QUEUE_INTERFACE_H_
