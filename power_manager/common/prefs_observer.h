// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_COMMON_PREFS_OBSERVER_H_
#define POWER_MANAGER_COMMON_PREFS_OBSERVER_H_

#include <string>

#include <base/observer_list_types.h>

namespace power_manager {

// Interface for classes that want to be notified when preferences changed.
class PrefsObserver : public base::CheckedObserver {
 public:
  virtual ~PrefsObserver() {}

  // Called when |pref_name|'s value has changed.
  virtual void OnPrefChanged(const std::string& pref_name) = 0;
};

}  // namespace power_manager

#endif  // POWER_MANAGER_COMMON_PREFS_OBSERVER_H_
