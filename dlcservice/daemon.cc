// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/daemon.h"

#include <string>
#include <utility>

#include <chromeos/constants/imageloader.h>
#include <chromeos/dbus/dlcservice/dbus-constants.h>
#include <sysexits.h>

#include "dlcservice/boot/boot_device.h"
#include "dlcservice/boot/boot_slot.h"
#include "dlcservice/dlc_service.h"
#include "dlcservice/system_state.h"

namespace dlcservice {

namespace {

constexpr char kDlcPreloadedImageRootpath[] =
    "/mnt/stateful_partition/var_overlay/cache/dlc-images";

}  // namespace

// kDlcServiceServiceName is defined in
// chromeos/dbus/dlcservice/dbus-constants.h
Daemon::Daemon() : DBusServiceDaemon(kDlcServiceServiceName) {}

int Daemon::OnInit() {
  int return_code = brillo::DBusServiceDaemon::OnInit();
  if (return_code != EX_OK)
    return return_code;

  dlc_service_->LoadDlcModuleImages();
  return EX_OK;
}

void Daemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
      nullptr, bus_,
      org::chromium::DlcServiceInterfaceAdaptor::GetObjectPath());

  bus_for_proxies_ = dbus_connection_for_proxies_.Connect();
  CHECK(bus_for_proxies_);
  // |SystemState| must be:
  //  - Initialized after |bus_for_proxies_|.
  //  - Initialized before |DlcService|.
  SystemState::Initialize(
      std::make_unique<org::chromium::ImageLoaderInterfaceProxy>(
          bus_for_proxies_),
      std::make_unique<org::chromium::UpdateEngineInterfaceProxy>(
          bus_for_proxies_),
      std::make_unique<BootSlot>(std::make_unique<BootDevice>()),
      base::FilePath(imageloader::kDlcManifestRootpath),
      base::FilePath(kDlcPreloadedImageRootpath),
      base::FilePath(imageloader::kDlcImageRootpath));
  CHECK(SystemState::Get());
  dlc_service_ = std::make_unique<DlcService>();

  auto dbus_service = std::make_unique<DBusService>(dlc_service_.get());
  dbus_adaptor_ = std::make_unique<DBusAdaptor>(std::move(dbus_service));
  dlc_service_->AddObserver(dbus_adaptor_.get());

  dbus_adaptor_->RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed.", true));
}

}  // namespace dlcservice
