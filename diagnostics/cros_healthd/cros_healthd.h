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
#include "diagnostics/cros_healthd/cros_healthd_routine_factory.h"
#include "diagnostics/cros_healthd/events/bluetooth_events.h"
#include "diagnostics/cros_healthd/events/lid_events.h"
#include "diagnostics/cros_healthd/events/power_events.h"
#include "diagnostics/cros_healthd/fetch_aggregator.h"
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
  void SendNetworkHealthService(
      mojo::PendingRemote<chromeos::network_health::mojom::NetworkHealthService>
          remote) override;
  void SendNetworkDiagnosticsRoutines(
      mojo::PendingRemote<
          chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines>
          network_diagnostics_routines) override;

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

  // |fetch_aggregator_| is responsible for fulfulling all ProbeTelemetryInfo
  // requests.
  std::unique_ptr<FetchAggregator> fetch_aggregator_;

  // Provides support for Bluetooth-related events.
  std::unique_ptr<BluetoothEvents> bluetooth_events_;
  // Provides support for lid-related events.
  std::unique_ptr<LidEvents> lid_events_;
  // Provides support for power-related events.
  std::unique_ptr<PowerEvents> power_events_;

  // |routine_service_| delegates routine creation to |routine_factory_|.
  std::unique_ptr<CrosHealthdRoutineFactory> routine_factory_;
  // Creates new diagnostic routines and controls existing diagnostic routines.
  std::unique_ptr<chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsService>
      routine_service_;
  // Maintains the Mojo connection with cros_healthd clients.
  std::unique_ptr<CrosHealthdMojoService> mojo_service_;
  // Binding set that connects this instance (which is an implementation of
  // chromeos::cros_healthd::mojom::CrosHealthdServiceFactory) with
  // any message pipes set up on top of received file descriptors. A new binding
  // is added whenever the BootstrapMojoConnection D-Bus method is called.
  mojo::BindingSet<chromeos::cros_healthd::mojom::CrosHealthdServiceFactory,
                   bool>
      service_factory_binding_set_;
  // Mojo binding set that connects |routine_service_| with message pipes,
  // allowing the remote ends to call our methods.
  mojo::BindingSet<chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsService>
      diagnostics_binding_set_;
  // Whether binding of the Mojo service was attempted. This flag is needed for
  // detecting repeated Mojo bootstrapping attempts.
  bool mojo_service_bind_attempted_ = false;

  // Connects BootstrapMojoConnection with the methods of the D-Bus object
  // exposed by the cros_healthd daemon.
  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_H_
