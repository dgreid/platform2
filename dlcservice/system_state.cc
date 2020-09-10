// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/system_state.h"

#include <climits>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "dlcservice/boot/boot_device.h"
#include "dlcservice/state_change_reporter_interface.h"

namespace dlcservice {

std::unique_ptr<SystemState> SystemState::g_instance_ = nullptr;

SystemState::SystemState(
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
    base::Clock* clock)
    : image_loader_proxy_(std::move(image_loader_proxy)),
      update_engine_proxy_(std::move(update_engine_proxy)),
      session_manager_proxy_(std::move(session_manager_proxy)),
      state_change_reporter_(state_change_reporter),
      metrics_(std::move(metrics)),
      system_properties_(std::move(system_properties)),
      manifest_dir_(manifest_dir),
      preloaded_content_dir_(preloaded_content_dir),
      content_dir_(content_dir),
      prefs_dir_(prefs_dir),
      users_dir_(users_dir),
      clock_(clock),
      is_device_removable_(false) {
  std::string boot_disk_name;
  PCHECK(boot_slot->GetCurrentSlot(&boot_disk_name, &active_boot_slot_,
                                   &is_device_removable_))
      << "Can not get current boot slot.";
}

// static
void SystemState::Initialize(
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
    base::Clock* clock,
    bool for_test) {
  if (!for_test)
    CHECK(!g_instance_) << "SystemState::Initialize() called already.";
  g_instance_.reset(new SystemState(
      std::move(image_loader_proxy), std::move(update_engine_proxy),
      std::move(session_manager_proxy), state_change_reporter,
      std::move(boot_slot), std::move(metrics), std::move(system_properties),
      manifest_dir, preloaded_content_dir, content_dir, prefs_dir, users_dir,
      clock));
}

// static
SystemState* SystemState::Get() {
  CHECK(g_instance_);
  return g_instance_.get();
}

org::chromium::ImageLoaderInterfaceProxyInterface* SystemState::image_loader()
    const {
  return image_loader_proxy_.get();
}

org::chromium::UpdateEngineInterfaceProxyInterface* SystemState::update_engine()
    const {
  return update_engine_proxy_.get();
}

org::chromium::SessionManagerInterfaceProxyInterface*
SystemState::session_manager() const {
  return session_manager_proxy_.get();
}

Metrics* SystemState::metrics() const {
  return metrics_.get();
}

SystemProperties* SystemState::system_properties() const {
  return system_properties_.get();
}

StateChangeReporterInterface* SystemState::state_change_reporter() const {
  return state_change_reporter_;
}

BootSlot::Slot SystemState::active_boot_slot() const {
  return active_boot_slot_;
}

BootSlot::Slot SystemState::inactive_boot_slot() const {
  return active_boot_slot_ == BootSlot::Slot::A ? BootSlot::Slot::B
                                                : BootSlot::Slot::A;
}

bool SystemState::IsDeviceRemovable() const {
  return is_device_removable_;
}

const base::FilePath& SystemState::manifest_dir() const {
  return manifest_dir_;
}

const base::FilePath& SystemState::preloaded_content_dir() const {
  return preloaded_content_dir_;
}

const base::FilePath& SystemState::content_dir() const {
  return content_dir_;
}

const base::FilePath& SystemState::prefs_dir() const {
  return prefs_dir_;
}

base::FilePath SystemState::dlc_prefs_dir() const {
  return prefs_dir_.Append("dlc");
}

const base::FilePath& SystemState::users_dir() const {
  return users_dir_;
}

base::Clock* SystemState::clock() const {
  return clock_;
}

void SystemState::set_update_engine_status(
    const update_engine::StatusResult& status) {
  last_update_engine_status_ = status;
  last_update_engine_status_timestamp_ = clock_->Now();
}

const update_engine::StatusResult& SystemState::update_engine_status() {
  return last_update_engine_status_;
}

const base::Time& SystemState::update_engine_status_timestamp() {
  return last_update_engine_status_timestamp_;
}

}  // namespace dlcservice
