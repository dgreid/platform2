// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_USER_PROXIMITY_WATCHER_INTERFACE_H_
#define POWER_MANAGER_POWERD_SYSTEM_USER_PROXIMITY_WATCHER_INTERFACE_H_

#include <base/macros.h>

namespace power_manager {
namespace system {

class UserProximityObserver;

// An interface for querying user proximity interfaces.
class UserProximityWatcherInterface {
 public:
  UserProximityWatcherInterface() = default;
  UserProximityWatcherInterface(const UserProximityWatcherInterface&) = delete;
  UserProximityWatcherInterface& operator=(
      const UserProximityWatcherInterface&) = delete;

  virtual ~UserProximityWatcherInterface() = default;

  // Adds or removes an observer.
  virtual void AddObserver(UserProximityObserver* observer) = 0;
  virtual void RemoveObserver(UserProximityObserver* observer) = 0;

  // TODO(egranata): add querying mechanisms
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_USER_PROXIMITY_WATCHER_INTERFACE_H_
