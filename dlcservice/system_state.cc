// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/system_state.h"

#include <limits.h>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "dlcservice/boot/boot_device.h"

namespace dlcservice {

std::unique_ptr<SystemState> SystemState::g_instance_ = nullptr;

SystemState::SystemState(
    std::unique_ptr<org::chromium::ImageLoaderInterfaceProxyInterface>
        image_loader_proxy,
    std::unique_ptr<org::chromium::UpdateEngineInterfaceProxyInterface>
        update_engine_proxy,
    std::unique_ptr<BootSlot> boot_slot,
    const base::FilePath& manifest_dir,
    const base::FilePath& preloaded_content_dir,
    const base::FilePath& content_dir)
    : image_loader_proxy_(std::move(image_loader_proxy)),
      update_engine_proxy_(std::move(update_engine_proxy)),
      manifest_dir_(manifest_dir),
      preloaded_content_dir_(preloaded_content_dir),
      content_dir_(content_dir) {
  std::string boot_disk_name;
  PCHECK(boot_slot->GetCurrentSlot(&boot_disk_name, &active_boot_slot_))
      << "Can not get current boot slot.";
}

// static
void SystemState::Initialize(
    std::unique_ptr<org::chromium::ImageLoaderInterfaceProxyInterface>
        image_loader_proxy,
    std::unique_ptr<org::chromium::UpdateEngineInterfaceProxyInterface>
        update_engine_proxy,
    std::unique_ptr<BootSlot> boot_slot,
    const base::FilePath& manifest_dir,
    const base::FilePath& preloaded_content_dir,
    const base::FilePath& content_dir,
    bool for_test) {
  if (!for_test)
    CHECK(!g_instance_) << "SystemState::Initialize() called already.";
  g_instance_.reset(new SystemState(
      std::move(image_loader_proxy), std::move(update_engine_proxy),
      std::move(boot_slot), manifest_dir, preloaded_content_dir, content_dir));
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

BootSlot::Slot SystemState::active_boot_slot() const {
  return active_boot_slot_;
}

BootSlot::Slot SystemState::inactive_boot_slot() const {
  return active_boot_slot_ == BootSlot::Slot::A ? BootSlot::Slot::B
                                                : BootSlot::Slot::A;
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

}  // namespace dlcservice
