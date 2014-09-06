// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/power_manager.h"

#include <map>
#include <string>

#include <base/bind.h>
#include <base/stl_util.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/dbus_manager.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/power_manager_proxy_interface.h"
#include "shill/proxy_factory.h"

using base::Bind;
using base::TimeDelta;
using base::Unretained;
using std::map;
using std::string;

namespace shill {

const int PowerManager::kSuspendTimeoutMilliseconds = 15 * 1000;

PowerManager::PowerManager(EventDispatcher *dispatcher,
                           ProxyFactory *proxy_factory)
    : dispatcher_(dispatcher),
      power_manager_proxy_(proxy_factory->CreatePowerManagerProxy(this)),
      suspending_(false) {}

PowerManager::~PowerManager() {
  for (const auto &delay_entry : suspend_delays_) {
    power_manager_proxy_->UnregisterSuspendDelay(
        delay_entry.second.delay_id);
  }
}

void PowerManager::Start(DBusManager *dbus_manager) {
  power_manager_name_watcher_.reset(
      dbus_manager->CreateNameWatcher(
          power_manager::kPowerManagerServiceName,
          Bind(&PowerManager::OnPowerManagerAppeared, Unretained(this)),
          Bind(&PowerManager::OnPowerManagerVanished, Unretained(this))));
}

bool PowerManager::AddSuspendDelay(
    const string &key,
    const string &description,
    TimeDelta timeout,
    const SuspendImminentCallback &imminent_callback,
    const SuspendDoneCallback &done_callback) {
  CHECK(!imminent_callback.is_null());
  CHECK(!done_callback.is_null());

  if (ContainsKey(suspend_delays_, key)) {
    LOG(ERROR) << "Ignoring request to insert duplicate key " << key;
    return false;
  }

  int delay_id = 0;
  bool is_registered = power_manager_proxy_->RegisterSuspendDelay(
      timeout, description, &delay_id);
  if (!is_registered) {
    LOG(INFO) << "Initial suspend delay registration for " << description
              << " failed, but is queued.";
  }

  SuspendDelay delay;
  delay.key = key;
  delay.description = description;
  delay.timeout = timeout;
  delay.imminent_callback = imminent_callback;
  delay.done_callback = done_callback;
  delay.delay_id = delay_id;
  delay.is_registered = is_registered;
  suspend_delays_[key] = delay;
  return is_registered;
}

bool PowerManager::RemoveSuspendDelay(const string &key) {
  SuspendDelayMap::const_iterator it = suspend_delays_.find(key);
  if (it == suspend_delays_.end()) {
    LOG(ERROR) << "Ignoring unknown key " << key;
    return false;
  }

  // We may attempt to unregister with a stale |delay_id| if powerd has
  // reappeared behind our back. It is safe to do so.
  if (it->second.is_registered &&
      !power_manager_proxy_->UnregisterSuspendDelay(it->second.delay_id)) {
    return false;
  }

  suspend_delays_.erase(it);
  return true;
}

bool PowerManager::ReportSuspendReadiness(const string &key,
                                          int suspend_id) {
  SuspendDelayMap::const_iterator it = suspend_delays_.find(key);
  if (it == suspend_delays_.end()) {
    LOG(ERROR) << "Ignoring unknown key " << key;
    return false;
  }

  if (!it->second.is_registered) {
    LOG(ERROR) << "Ignoring unregistered key " << key;
    return false;
  }

  return power_manager_proxy_->ReportSuspendReadiness(
      it->second.delay_id, suspend_id);
}

void PowerManager::OnSuspendImminent(int suspend_id) {
  LOG(INFO) << __func__ << "(" << suspend_id << ")";
  // Change the power state to suspending as soon as this signal is received so
  // that the manager can suppress auto-connect, for example. Schedules a
  // suspend timeout in case the suspend attempt failed or got interrupted, and
  // there's no proper notification from the power manager.
  suspending_ = true;
  suspend_timeout_.Reset(
      base::Bind(&PowerManager::OnSuspendTimeout, base::Unretained(this),
                 suspend_id));
  dispatcher_->PostDelayedTask(suspend_timeout_.callback(),
                               kSuspendTimeoutMilliseconds);

  // Forward the message to all in |suspend_delays_|, whether or not they are
  // registered.
  for (const auto &delay : suspend_delays_) {
    SuspendImminentCallback callback = delay.second.imminent_callback;
    CHECK(!callback.is_null());
    callback.Run(suspend_id);
  }
}

void PowerManager::OnSuspendDone(int suspend_id) {
  LOG(INFO) << __func__ << "(" << suspend_id << ")";
  suspend_timeout_.Cancel();
  suspending_ = false;
  // Forward the message to all in |suspend_delays_|, whether or not they are
  // registered.
  for (const auto &delay : suspend_delays_) {
    SuspendDoneCallback callback = delay.second.done_callback;
    CHECK(!callback.is_null());
    callback.Run(suspend_id);
  }
}

void PowerManager::OnSuspendTimeout(int suspend_id) {
  LOG(ERROR) << "Suspend timed out -- assuming power-on state.";
  OnSuspendDone(suspend_id);
}

void PowerManager::OnPowerManagerAppeared(const string &/*name*/,
                                          const string &/*owner*/) {
  for (auto &delay_entry : suspend_delays_) {
    SuspendDelay &delay = delay_entry.second;
    // Attempt to unregister a stale suspend delay. This guards against a race
    // where |AddSuspendDelay| managed to register a suspend delay with the
    // newly appeared powerd instance before this function had a chance to
    // run.
    if (delay.is_registered) {
      power_manager_proxy_->UnregisterSuspendDelay(delay.delay_id);
    }

    int delay_id = delay.delay_id;
    if (!power_manager_proxy_->RegisterSuspendDelay(
            delay.timeout, delay.description, &delay_id)) {
      // In case of failure, we leave |delay| unchanged.
      LOG(INFO) << "Re-registration of suspend delay for " << delay.description
                << " failed.  Will retry when " << __func__
                << "is called again.";
      // We will retry re-registering this delay whenever
      // |OnPowerManagerAppeared| is called next. In the meantime, this |delay|
      // will be handled as if we had not received a notification for powerd's
      // restart.
      continue;
    } else if (!delay.is_registered) {
      LOG(INFO) << "Delayed suspend delay registration for "
                << delay.description << " succeeded.";
      delay.is_registered = true;
    }
    delay.delay_id = delay_id;
  }
}

void PowerManager::OnPowerManagerVanished(const string &/*name*/) {
  LOG(INFO) << "powerd vanished.";
}

}  // namespace shill
