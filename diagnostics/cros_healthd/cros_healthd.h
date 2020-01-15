// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_H_
#define DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_H_

#include <memory>
#include <string>

#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <base/memory/scoped_refptr.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/dbus/dbus_connection.h>
#include <brillo/dbus/dbus_object.h>
#include <chromeos/chromeos-config/libcros_config/cros_config.h>
#include <dbus/bus.h>
#include <dbus/object_proxy.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/binding_set.h>

#include "debugd/dbus-proxies.h"
#include "diagnostics/common/system/debugd_adapter_impl.h"
#include "diagnostics/cros_healthd/cros_healthd_mojo_service.h"
#include "diagnostics/cros_healthd/cros_healthd_routine_factory_impl.h"
#include "diagnostics/cros_healthd/cros_healthd_routine_service.h"
#include "diagnostics/cros_healthd/utils/backlight_utils.h"
#include "diagnostics/cros_healthd/utils/battery_utils.h"
#include "diagnostics/cros_healthd/utils/vpd_utils.h"
#include "mojo/cros_healthd.mojom.h"

namespace diagnostics {

// Daemon class for cros_healthd.
class CrosHealthd final
    : public brillo::DBusServiceDaemon,
      public chromeos::cros_healthd::mojom::CrosHealthdServiceFactory {
 public:
  CrosHealthd();
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

  // This should be the only connection to D-Bus. Use |connection_| to get the
  // |dbus_bus_|.
  brillo::DBusConnection connection_;
  // Single |dbus_bus_| object used by cros_healthd to initiate the
  // |debugd_proxy_| and |power_manager_proxy_|.
  scoped_refptr<dbus::Bus> dbus_bus_;
  // Use the |debugd_proxy_| to make calls to debugd. Example: cros_healthd
  // calls out to debugd when it needs to collect smart battery metrics like
  // manufacture_date_smart and temperature_smart.
  std::unique_ptr<org::chromium::debugdProxy> debugd_proxy_;
  // Use the |debugd_adapter_| to make calls to debugd. Example:
  // cros_healthd calls out to debugd with async callbacks when it
  // needs to trigger nvme self-test or collect data like progress info.
  std::unique_ptr<DebugdAdapterImpl> debugd_adapter_;
  // Use the |power_manager_proxy_| (owned by |dbus_bus_|) to make calls to
  // power_manager. Example: cros_healthd calls out to power_manager when it
  // needs to collect battery metrics like cycle count.
  dbus::ObjectProxy* power_manager_proxy_;
  // Use |cros_config_| to determine which metrics a device supports.
  std::unique_ptr<brillo::CrosConfig> cros_config_;
  // |backlight_fetcher_| is responsible for collecting metrics related to
  // the device's backlights. It uses |cros_config_| to determine whether or not
  // the device has a backlight.
  std::unique_ptr<BacklightFetcher> backlight_fetcher_;
  // |battery_fetcher_| is responsible for collecting all battery metrics (smart
  // and regular) by using the available D-Bus proxies. It also uses
  // |cros_config_| to determine which of those metrics a device supports.
  std::unique_ptr<BatteryFetcher> battery_fetcher_;
  // |cached_vpd_fetcher_| is responsible for collecting cached VPD metrics and
  // uses |cros_config_| to determine which of those metrics a device supports.
  std::unique_ptr<CachedVpdFetcher> cached_vpd_fetcher_;

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

  DISALLOW_COPY_AND_ASSIGN(CrosHealthd);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_H_
