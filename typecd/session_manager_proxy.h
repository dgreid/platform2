// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_SESSION_MANAGER_PROXY_H_
#define TYPECD_SESSION_MANAGER_PROXY_H_

#include <string>

#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <base/observer_list.h>
#include <dbus/bus.h>
#include <session_manager/dbus-proxies.h>

#include "typecd/session_manager_observer_interface.h"

namespace typecd {

// A proxy class that listens to DBus signals from the session manager and
// notifies a list of registered observers for events.
class SessionManagerProxy {
 public:
  explicit SessionManagerProxy(scoped_refptr<dbus::Bus> bus);

  ~SessionManagerProxy() = default;

  void AddObserver(SessionManagerObserverInterface* observer);

 private:
  // Handles the ScreenIsLocked DBus signal.
  void OnScreenIsLocked();

  // Handles the ScreenIsUnlocked DBus signal.
  void OnScreenIsUnlocked();

  // Handles the SessionStateChanged DBus signal.
  void OnSessionStateChanged(const std::string& state);

  org::chromium::SessionManagerInterfaceProxy proxy_;
  base::ObserverList<SessionManagerObserverInterface> observer_list_;
  base::WeakPtrFactory<SessionManagerProxy> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(SessionManagerProxy);
};

}  // namespace typecd

#endif  // TYPECD_SESSION_MANAGER_PROXY_H_
