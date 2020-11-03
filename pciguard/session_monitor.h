// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PCIGUARD_SESSION_MONITOR_H_
#define PCIGUARD_SESSION_MONITOR_H_

#include "pciguard/event_handler.h"

#include <session_manager/dbus-proxies.h>
#include <memory>
#include <string>

namespace pciguard {

// A class for monitoring events from the session manager. It is a dumb class
// and contains the boiler plate code to forward the session events to the
// event_handler class.
class SessionMonitor {
 public:
  explicit SessionMonitor(scoped_refptr<dbus::Bus> bus,
                          std::shared_ptr<EventHandler> ev_handler);
  SessionMonitor(const SessionMonitor&) = delete;
  SessionMonitor& operator=(const SessionMonitor&) = delete;
  ~SessionMonitor() = default;

 private:
  // Handles the ScreenIsLocked DBus signal.
  void OnScreenIsLocked();

  // Handles the ScreenIsUnlocked DBus signal.
  void OnScreenIsUnlocked();

  // Handles the SessionStateChanged DBus signal.
  void OnSessionStateChanged(const std::string& state);

  org::chromium::SessionManagerInterfaceProxy proxy_;
  std::shared_ptr<EventHandler> event_handler_;
  base::WeakPtrFactory<SessionMonitor> weak_ptr_factory_;
};

}  // namespace pciguard

#endif  // PCIGUARD_SESSION_MONITOR_H_
