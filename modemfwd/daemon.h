// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_DAEMON_H_
#define MODEMFWD_DAEMON_H_

#include <map>
#include <memory>
#include <set>
#include <string>

#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <brillo/daemons/dbus_daemon.h>
#include <dbus/bus.h>

#include "modemfwd/dbus_adaptors/org.chromium.Modemfwd.h"
#include "modemfwd/journal.h"
#include "modemfwd/modem.h"
#include "modemfwd/modem_flasher.h"
#include "modemfwd/modem_helper.h"
#include "modemfwd/modem_tracker.h"

namespace modemfwd {

class Daemon;

class DBusAdaptor : public org::chromium::ModemfwdInterface,
                    public org::chromium::ModemfwdAdaptor {
 public:
  explicit DBusAdaptor(scoped_refptr<dbus::Bus> bus, Daemon* daemon);
  DBusAdaptor(const DBusAdaptor&) = delete;
  DBusAdaptor& operator=(const DBusAdaptor&) = delete;

  void RegisterAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb);

  // org::chromium::ModemfwdInterface overrides.
  void SetDebugMode(bool debug_mode) override;
  bool ForceFlash(const std::string& device_id) override;

 private:
  brillo::dbus_utils::DBusObject dbus_object_;
  Daemon* daemon_;  // weak
};

class Daemon : public brillo::DBusServiceDaemon {
 public:
  // Constructor for Daemon which loads the cellular DLC to get firmware.
  Daemon(const std::string& journal_file, const std::string& helper_directory);
  // Constructor for Daemon which loads from already set-up
  // directories.
  Daemon(const std::string& journal_file,
         const std::string& helper_directory,
         const std::string& firmware_directory);
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

  ~Daemon() override = default;

  bool ForceFlash(const std::string& device_id);

 protected:
  // brillo::Daemon overrides.
  int OnInit() override;
  int OnEventLoopStarted() override;

  // brillo::DBusServiceDaemon overrides.
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

 private:
  // Once we have a path for the firmware directory we can set up the
  // journal and flasher.
  int CompleteInitialization();

  // Called when a modem appears. Generally this means on startup but can
  // also be called in response to e.g. rebooting the modem or SIM hot
  // swapping.
  void OnModemAppeared(
      std::unique_ptr<org::chromium::flimflam::DeviceProxy> modem);

  // Check for wedged modems and force-flash them if necessary.
  void CheckForWedgedModems();
  void ForceFlashIfWedged(const std::string& device_id,
                          ModemHelper* modem_helper);
  void ForceFlashIfNeverAppeared(const std::string& device_id);

  base::FilePath journal_file_path_;
  base::FilePath helper_dir_path_;
  base::FilePath firmware_dir_path_;

  std::unique_ptr<ModemHelperDirectory> helper_directory_;

  std::unique_ptr<ModemTracker> modem_tracker_;
  std::unique_ptr<ModemFlasher> modem_flasher_;

  std::map<std::string, base::Closure> modem_reappear_callbacks_;
  std::set<std::string> device_ids_seen_;

  std::unique_ptr<DBusAdaptor> dbus_adaptor_;

  base::WeakPtrFactory<Daemon> weak_ptr_factory_;
};

}  // namespace modemfwd

#endif  // MODEMFWD_DAEMON_H_
