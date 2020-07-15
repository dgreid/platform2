// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_H_
#define DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_H_

#include <memory>
#include <string>

#include <base/files/scoped_file.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/dbus/dbus_object.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/binding_set.h>

#include "diagnostics/cros_healthd/cros_healthd_mojo_service.h"
#include "diagnostics/cros_healthd/cros_healthd_routine_factory_impl.h"
#include "diagnostics/cros_healthd/cros_healthd_routine_service.h"
#include "diagnostics/cros_healthd/events/bluetooth_events.h"
#include "diagnostics/cros_healthd/events/lid_events.h"
#include "diagnostics/cros_healthd/events/power_events.h"
#include "diagnostics/cros_healthd/fetchers/backlight_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/battery_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/bluetooth_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/cached_vpd_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/cpu_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/disk_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/fan_fetcher.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "mojo/cros_healthd.mojom.h"

namespace diagnostics {

// Daemon class for cros_healthd.
class CrosHealthd final
    : public brillo::DBusServiceDaemon,
      public chromeos::cros_healthd::mojom::CrosHealthdServiceFactory {
 public:
  explicit CrosHealthd(Context* context);
  CrosHealthd(const CrosHealthd&) = delete;
  CrosHealthd& operator=(const CrosHealthd&) = delete;
  ~CrosHealthd() override;

 private:
  // brillo::DBusServiceDaemon overrides:
  int OnInit() override;
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

  // chromeos::cros_healthd::mojom::CrosHealthdServiceFactory overrides:
  void GetProbeService(
      chromeos::cros_healthd::mojom::CrosHealthdProbeServiceRequest service)
      override;
  void GetDiagnosticsService(
      chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsServiceRequest
          service) override;
  void GetEventService(
      chromeos::cros_healthd::mojom::CrosHealthdEventServiceRequest service)
      override;

  // Implementation of the "org.chromium.CrosHealthdInterface" D-Bus interface
  // exposed by the cros_healthd daemon (see constants for the API methods at
  // src/platform2/system_api/dbus/cros_healthd/dbus-constants.h). When
  // |is_chrome| = false, this method will return a unique token that can be
  // used to connect to cros_healthd via mojo. When |is_chrome| = true, the
  // returned string has no meaning.
  std::string BootstrapMojoConnection(const base::ScopedFD& mojo_fd,
                                      bool is_chrome);

  void ShutDownDueToMojoError(const std::string& debug_reason);

  // Disconnect handler for |binding_set_|.
  void OnDisconnect();

  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;

  // Provides access to helper objects. Used by various telemetry fetchers,
  // event implementations and diagnostic routines.
  Context* const context_ = nullptr;

  // |backlight_fetcher_| is responsible for collecting metrics related to
  // the device's backlights. It uses |cros_config_| to determine whether or not
  // the device has a backlight.
  std::unique_ptr<BacklightFetcher> backlight_fetcher_;
  // |battery_fetcher_| is responsible for collecting all battery metrics (smart
  // and regular) by using the available D-Bus proxies. It also uses
  // |cros_config_| to determine which of those metrics a device supports.
  std::unique_ptr<BatteryFetcher> battery_fetcher_;
  // |bluetooth_fetcher_| is responsible for collecting Bluetooth information.
  std::unique_ptr<BluetoothFetcher> bluetooth_fetcher_;
  // |cached_vpd_fetcher_| is responsible for collecting cached VPD metrics and
  // uses |cros_config_| to determine which of those metrics a device supports.
  std::unique_ptr<CachedVpdFetcher> cached_vpd_fetcher_;
  // |cpu_fetcher_| is responsible for collecting CPU information.
  std::unique_ptr<CpuFetcher> cpu_fetcher_;
  // |disk_fetcher_| is responsible for collecting disk information.
  std::unique_ptr<DiskFetcher> disk_fetcher_;
  // |fan_fetcher_| is responsible for collecting fan information using
  // |debugd_proxy_|.
  std::unique_ptr<FanFetcher> fan_fetcher_;

  // Provides support for Bluetooth-related events.
  std::unique_ptr<BluetoothEvents> bluetooth_events_;
  // Provides support for lid-related events.
  std::unique_ptr<LidEvents> lid_events_;
  // Provides support for power-related events.
  std::unique_ptr<PowerEvents> power_events_;

  // Production implementation of the CrosHealthdRoutineFactory interface. Will
  // be injected into |routine_service_|.
  CrosHealthdRoutineFactoryImpl routine_factory_impl_;
  // Creates new diagnostic routines and controls existing diagnostic routines.
  std::unique_ptr<CrosHealthdRoutineService> routine_service_;
  // Maintains the Mojo connection with cros_healthd clients.
  std::unique_ptr<CrosHealthdMojoService> mojo_service_;
  // Binding set that connects this instance (which is an implementation of
  // chromeos::cros_healthd::mojom::CrosHealthdServiceFactory) with
  // any message pipes set up on top of received file descriptors. A new binding
  // is added whenever the BootstrapMojoConnection D-Bus method is called.
  mojo::BindingSet<chromeos::cros_healthd::mojom::CrosHealthdServiceFactory,
                   bool>
      binding_set_;
  // Whether binding of the Mojo service was attempted. This flag is needed for
  // detecting repeated Mojo bootstrapping attempts.
  bool mojo_service_bind_attempted_ = false;

  // Connects BootstrapMojoConnection with the methods of the D-Bus object
  // exposed by the cros_healthd daemon.
  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_H_
