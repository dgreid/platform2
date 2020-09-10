// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_SYSTEM_STATE_H_
#define DLCSERVICE_SYSTEM_STATE_H_

#include <memory>

#include <base/files/file_path.h>
#include <base/time/time.h>
#include <base/time/clock.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <imageloader/dbus-proxies.h>
#include <session_manager/dbus-proxies.h>
#include <update_engine/proto_bindings/update_engine.pb.h>
#include <update_engine/dbus-proxies.h>

#include "dlcservice/boot/boot_slot.h"
#include "dlcservice/dlc_manager.h"
#include "dlcservice/metrics.h"
#include "dlcservice/state_change_reporter_interface.h"
#include "dlcservice/system_properties.h"

namespace dlcservice {

class SystemState {
 public:
  virtual ~SystemState() = default;

  // Creates a singleton |SystemState| that can be accessible globally by
  // calling |SystemState::Get()|. Subsequent calls will fail and is an error.
  // But if |for_test| is true, repeated calls can be made during testing to
  // reset |SystemState|. Note: Should only be used during tests.
  static void Initialize(
      std::unique_ptr<org::chromium::ImageLoaderInterfaceProxyInterface>
          image_loader_proxy,
      std::unique_ptr<org::chromium::UpdateEngineInterfaceProxyInterface>
          update_engine_proxy,
      std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
          session_manage_proxy,
      StateChangeReporterInterface* state_change_reporter,
      std::unique_ptr<BootSlot> boot_slot,
      std::unique_ptr<Metrics> metrics,
      std::unique_ptr<SystemProperties> system_properties,
      const base::FilePath& manifest_dir,
      const base::FilePath& preloaded_content_dir,
      const base::FilePath& content_dir,
      const base::FilePath& prefs_dir,
      const base::FilePath& users_dir,
      base::Clock* clock,
      bool for_test = false);

  // Gets the pointer to the current |SystemState|.
  static SystemState* Get();

  // Getters for states that |SystemState| holds.
  org::chromium::ImageLoaderInterfaceProxyInterface* image_loader() const;
  org::chromium::UpdateEngineInterfaceProxyInterface* update_engine() const;
  org::chromium::SessionManagerInterfaceProxyInterface* session_manager() const;
  Metrics* metrics() const;
  StateChangeReporterInterface* state_change_reporter() const;
  SystemProperties* system_properties() const;
  const base::FilePath& manifest_dir() const;
  const base::FilePath& preloaded_content_dir() const;
  const base::FilePath& content_dir() const;
  const base::FilePath& prefs_dir() const;
  base::FilePath dlc_prefs_dir() const;
  const base::FilePath& users_dir() const;

  // Getting active and inactive boot slots easily.
  BootSlot::Slot active_boot_slot() const;
  BootSlot::Slot inactive_boot_slot() const;

  // Return true if the device is removable.
  bool IsDeviceRemovable() const;

  // Returns the clock object.
  base::Clock* clock() const;

  void set_update_engine_status(const update_engine::StatusResult& status);
  const update_engine::StatusResult& update_engine_status();
  const base::Time& update_engine_status_timestamp();

 protected:
  SystemState(
      std::unique_ptr<org::chromium::ImageLoaderInterfaceProxyInterface>
          image_loader_proxy,
      std::unique_ptr<org::chromium::UpdateEngineInterfaceProxyInterface>
          update_engine_proxy,
      std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
          session_manager_proxy,
      StateChangeReporterInterface* state_change_reporter,
      std::unique_ptr<BootSlot> boot_slot,
      std::unique_ptr<Metrics> metrics,
      std::unique_ptr<SystemProperties> system_properties,
      const base::FilePath& manifest_dir,
      const base::FilePath& preloaded_content_dir,
      const base::FilePath& content_dir,
      const base::FilePath& prefs_dir,
      const base::FilePath& users_dir,
      base::Clock* clock);

 private:
  std::unique_ptr<org::chromium::ImageLoaderInterfaceProxyInterface>
      image_loader_proxy_;
  std::unique_ptr<org::chromium::UpdateEngineInterfaceProxyInterface>
      update_engine_proxy_;
  std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
      session_manager_proxy_;
  StateChangeReporterInterface* state_change_reporter_;

  std::unique_ptr<Metrics> metrics_;
  std::unique_ptr<SystemProperties> system_properties_;
  base::FilePath manifest_dir_;
  base::FilePath preloaded_content_dir_;
  base::FilePath content_dir_;
  base::FilePath prefs_dir_;
  BootSlot::Slot active_boot_slot_;
  base::FilePath users_dir_;
  base::Clock* clock_;
  bool is_device_removable_;

  // Keep the last status result we saw.
  update_engine::StatusResult last_update_engine_status_;
  base::Time last_update_engine_status_timestamp_;

  static std::unique_ptr<SystemState> g_instance_;

  SystemState(const SystemState&) = delete;
  SystemState& operator=(const SystemState&) = delete;
};

}  // namespace dlcservice

#endif  // DLCSERVICE_SYSTEM_STATE_H_
