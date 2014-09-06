// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_POWER_MANAGER_H_
#define SHILL_POWER_MANAGER_H_

// This class instantiates a PowerManagerProxy and distributes power events to
// registered users.  It also provides a means for calling methods on the
// PowerManagerProxy.

#include <map>
#include <string>

#include <base/callback.h>
#include <base/cancelable_callback.h>
#include <base/memory/scoped_ptr.h>

#include "shill/power_manager_proxy_interface.h"

namespace shill {

class DBusManager;
class DBusNameWatcher;
class EventDispatcher;
class ProxyFactory;

// TODO(benchan): The current implementation supports multiple suspend delays,
// which seems unnecessary as shill only registers one. We should simplify the
// implementation (crbug.com/373348).
class PowerManager : public PowerManagerProxyDelegate {
 public:
  // This callback is called prior to a suspend attempt.  When it is OK for the
  // system to suspend, this callback should call ReportSuspendReadiness(),
  // passing it the key passed to AddSuspendDelay() and the suspend ID passed to
  // this callback.
  typedef base::Callback<void(int)> SuspendImminentCallback;

  // This callback is called after the completion of a suspend attempt.  The
  // receiver should undo any pre-suspend work that was done by the
  // SuspendImminentCallback.
  typedef base::Callback<void(int)> SuspendDoneCallback;

  static const int kSuspendTimeoutMilliseconds;

  // |proxy_factory| creates the PowerManagerProxy.  Usually this is
  // ProxyFactory::GetInstance().  Use a fake for testing.
  // Note: |Start| should be called to initialize this object before using it.
  PowerManager(EventDispatcher *dispatcher, ProxyFactory *proxy_factory);
  ~PowerManager() override;

  bool suspending() const { return suspending_; }

  // Requires a |DBusManager| that has been |Start|'ed.
  virtual void Start(DBusManager *dbus_manager);

  // Registers a suspend delay with the power manager under the unique name
  // |key|.  See PowerManagerProxyInterface::RegisterSuspendDelay() for
  // information about |description| and |timeout|.  |imminent_callback| will be
  // invoked when a suspend attempt is commenced and |done_callback| will be
  // invoked when the attempt is completed.  Returns false on failure.
  virtual bool AddSuspendDelay(const std::string &key,
                               const std::string &description,
                               base::TimeDelta timeout,
                               const SuspendImminentCallback &immiment_callback,
                               const SuspendDoneCallback &done_callback);

  // Removes a suspend delay previously created via AddSuspendDelay().  Returns
  // false on failure.
  virtual bool RemoveSuspendDelay(const std::string &key);

  // Reports readiness for suspend attempt |suspend_id| on behalf of the suspend
  // delay described by |key|, the value originally passed to AddSuspendDelay().
  // Returns false on failure.
  virtual bool ReportSuspendReadiness(const std::string &key, int suspend_id);

  // Methods inherited from PowerManagerProxyDelegate.
  virtual void OnSuspendImminent(int suspend_id);
  virtual void OnSuspendDone(int suspend_id);

 private:
  friend class ManagerTest;
  friend class PowerManagerTest;
  friend class ServiceTest;

  // Information about a suspend delay added via AddSuspendDelay().
  struct SuspendDelay {
    std::string key;
    std::string description;
    base::TimeDelta timeout;
    SuspendImminentCallback imminent_callback;
    SuspendDoneCallback done_callback;
    int delay_id;
    bool is_registered;
  };

  // Keyed by the |key| argument to AddSuspendDelay().
  typedef std::map<const std::string, SuspendDelay> SuspendDelayMap;

  // Run by |suspend_timeout_| to manually invoke OnSuspendDone() if the signal
  // never arrives.
  void OnSuspendTimeout(int suspend_id);

  // These functions track the power_manager daemon appearing/vanishing from the
  // DBus connection.
  void OnPowerManagerAppeared(const std::string &name,
                              const std::string &owner);
  void OnPowerManagerVanished(const std::string &name);

  EventDispatcher *dispatcher_;

  // The power manager proxy created by this class.  It dispatches the inherited
  // delegate methods of this object when changes in the power state occur.
  const scoped_ptr<PowerManagerProxyInterface> power_manager_proxy_;
  scoped_ptr<DBusNameWatcher> power_manager_name_watcher_;
  SuspendDelayMap suspend_delays_;
  base::CancelableClosure suspend_timeout_;

  // Set to true by OnSuspendImminent() and to false by OnSuspendDone().
  bool suspending_;

  DISALLOW_COPY_AND_ASSIGN(PowerManager);
};

}  // namespace shill

#endif  // SHILL_POWER_MANAGER_H_
