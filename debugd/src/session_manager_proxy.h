// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_SESSION_MANAGER_PROXY_H_
#define DEBUGD_SRC_SESSION_MANAGER_PROXY_H_

#include <string>

#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <dbus/bus.h>

namespace dbus {
class ObjectProxy;
class Signal;
}  // namespace dbus

namespace debugd {

// Talks to Session Manager DBus interface.  It also exposes
// convenience method to enable Chrome remote debugging and listens to Session
// Manager signal to ensure Chrome remote debugging is on when it is supposed
// to be.
class SessionManagerProxy {
 public:
  explicit SessionManagerProxy(scoped_refptr<dbus::Bus> bus);
  SessionManagerProxy(const SessionManagerProxy&) = delete;
  SessionManagerProxy& operator=(const SessionManagerProxy&) = delete;

  ~SessionManagerProxy() = default;
  // Sets up the proxy for Chrome remote debugging and tries to enable it.
  void EnableChromeRemoteDebugging();

  // Handler for LoginPromptVisible signal.
  void OnLoginPromptVisible(dbus::Signal*);

 private:
  // Tries to enable Chrome remote debugging.
  void EnableChromeRemoteDebuggingInternal();

  scoped_refptr<dbus::Bus> bus_;
  dbus::ObjectProxy* proxy_;  // weak, owned by |bus_|

  // Should the proxy try to enable Chrome remote debugging.
  bool should_enable_chrome_remote_debugging_ = false;
  // Whether Chrome remote debugging is already successfully enabled.
  bool is_chrome_remote_debugging_enabled_ = false;

  base::WeakPtrFactory<SessionManagerProxy> weak_ptr_factory_;
};

}  // namespace debugd

#endif  // DEBUGD_SRC_SESSION_MANAGER_PROXY_H_
