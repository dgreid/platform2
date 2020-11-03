// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PCIGUARD_EVENT_HANDLER_H_
#define PCIGUARD_EVENT_HANDLER_H_

#include "pciguard/authorizer.h"

#include <memory>

namespace pciguard {

// A class for handling all events.
class EventHandler {
 public:
  EventHandler();
  EventHandler(const EventHandler&) = delete;
  EventHandler& operator=(const EventHandler&) = delete;
  ~EventHandler() = default;

  void OnUserLogin();

  void OnUserLogout();

  void OnScreenLocked();

  void OnScreenUnlocked();

  void OnUserPermissionChanged();

  void OnNewThunderboltDev(base::FilePath path);

 private:
  enum {
    NO_USER_LOGGED_IN,
    USER_LOGGED_IN_BUT_SCREEN_LOCKED,
    USER_LOGGED_IN_SCREEN_UNLOCKED,
  } state_;

  std::unique_ptr<Authorizer> authorizer_;

  // Protects concurrent access to state_ and authorizer_
  std::mutex lock_;

  // Checks for the User Permission from chrome browser
  bool UserPermissionOK();

  // Logs the event
  void LogEvent(const char ev[]);
};

}  // namespace pciguard

#endif  // PCIGUARD_EVENT_HANDLER_H_
