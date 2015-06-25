// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/states/state_change_queue.h"

#include <base/logging.h>

namespace buffet {

StateChangeQueue::StateChangeQueue(size_t max_queue_size)
    : max_queue_size_(max_queue_size) {
  CHECK_GT(max_queue_size_, 0U) << "Max queue size must not be zero";
}

bool StateChangeQueue::NotifyPropertiesUpdated(
    base::Time timestamp,
    native_types::Object changed_properties) {
  DCHECK(thread_checker_.CalledOnValidThread());
  auto it = state_changes_.lower_bound(timestamp);
  if (it == state_changes_.end() || it->first != timestamp) {
    // This timestamp doesn't exist. Insert a new element.
    state_changes_.emplace_hint(it, timestamp, std::move(changed_properties));
  } else {
    // Merge the old property set and the new one.
    // For properties that exist in both old and new property sets, keep the
    // new values.
    changed_properties.insert(it->second.begin(), it->second.end());
    it->second = std::move(changed_properties);
  }
  while (state_changes_.size() > max_queue_size_) {
    // Queue is full.
    // Merge the two oldest records into one. The merge strategy is:
    //  - Move non-existent properties from element [old] to [new].
    //  - If both [old] and [new] specify the same property,
    //    keep the value of [new].
    //  - Keep the timestamp of [new].
    auto element_old = state_changes_.begin();
    auto element_new = std::next(element_old);
    // This will skip elements that exist in both [old] and [new].
    element_new->second.insert(element_old->second.begin(),
                               element_old->second.end());
    state_changes_.erase(element_old);
  }
  ++last_change_id_;
  return true;
}

std::vector<StateChange> StateChangeQueue::GetAndClearRecordedStateChanges() {
  DCHECK(thread_checker_.CalledOnValidThread());
  std::vector<StateChange> changes;
  changes.reserve(state_changes_.size());
  for (const auto& pair : state_changes_) {
    changes.emplace_back(pair.first, std::move(pair.second));
  }
  state_changes_.clear();
  return changes;
}

}  // namespace buffet
