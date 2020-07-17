// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_DAEMON_H_
#define LORGNETTE_DAEMON_H_

#include <memory>

#include <base/cancelable_callback.h>
#include <base/memory/weak_ptr.h>
#include <brillo/daemons/dbus_daemon.h>

#include "lorgnette/manager.h"

namespace lorgnette {

class Daemon : public brillo::DBusServiceDaemon {
 public:
  // User and group to run the lorgnette process.
  static const char kScanGroupName[];
  static const char kScanUserName[];

  explicit Daemon(const base::Closure& startup_callback);
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;
  ~Daemon() = default;

 protected:
  int OnInit() override;
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;
  void OnShutdown(int* return_code) override;

 private:
  friend class DaemonTest;

  // Daemon will automatically shutdown after this length of idle time.
  static const int kShutdownTimeoutMilliseconds;

  // Restarts a timer for the termination of the daemon process.
  void PostponeShutdown();

  std::unique_ptr<Manager> manager_;
  base::Closure startup_callback_;
  base::CancelableClosure shutdown_callback_;

  // Keep as the last member variable.
  base::WeakPtrFactory<Daemon> weak_factory_{this};
};

}  // namespace lorgnette

#endif  // LORGNETTE_DAEMON_H_
