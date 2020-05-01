// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_WILCO_DTC_SUPPORTD_CORE_H_
#define DIAGNOSTICS_WILCO_DTC_SUPPORTD_CORE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/strings/string_piece.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <brillo/dbus/dbus_object.h>
#include <dbus/bus.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "diagnostics/common/system/bluetooth_client.h"
#include "diagnostics/common/system/debugd_adapter.h"
#include "diagnostics/common/system/powerd_adapter.h"
#include "diagnostics/grpc_async_adapter/async_grpc_client.h"
#include "diagnostics/grpc_async_adapter/async_grpc_server.h"
#include "diagnostics/wilco_dtc_supportd/dbus_service.h"
#include "diagnostics/wilco_dtc_supportd/grpc_service.h"
#include "diagnostics/wilco_dtc_supportd/mojo_service.h"
#include "diagnostics/wilco_dtc_supportd/routine_service.h"
#include "diagnostics/wilco_dtc_supportd/telemetry/bluetooth_event_service.h"
#include "diagnostics/wilco_dtc_supportd/telemetry/ec_event_service.h"
#include "diagnostics/wilco_dtc_supportd/telemetry/powerd_event_service.h"

#include "mojo/cros_healthd.mojom.h"
#include "mojo/wilco_dtc_supportd.mojom.h"
#include "wilco_dtc.grpc.pb.h"           // NOLINT(build/include)
#include "wilco_dtc_supportd.grpc.pb.h"  // NOLINT(build/include)

namespace diagnostics {

// Integrates together all pieces which implement separate IPC services exposed
// by the wilco_dtc_supportd daemon and IPC clients.
class Core final : public DBusService::Delegate,
                   public GrpcService::Delegate,
                   public MojoService::Delegate,
                   public RoutineService::Delegate,
                   public chromeos::wilco_dtc_supportd::mojom::
                       WilcoDtcSupportdServiceFactory,
                   public BluetoothEventService::Observer,
                   public EcEventService::Observer,
                   public PowerdEventService::Observer {
 public:
  class Delegate {
   public:
    using MojomWilcoDtcSupportdServiceFactory =
        chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdServiceFactory;

    virtual ~Delegate() = default;

    // Binds the given |mojo_service_factory| to the Mojo message pipe that
    // works via the given |mojo_pipe_fd|. On success, returns the created Mojo
    // binding, otherwise returns nullptr.
    //
    // In production this method must be called no more than once during the
    // lifetime of the daemon, since Mojo EDK gives no guarantee to support
    // repeated initialization with different parent handles.
    virtual std::unique_ptr<mojo::Binding<MojomWilcoDtcSupportdServiceFactory>>
    BindMojoServiceFactory(
        MojomWilcoDtcSupportdServiceFactory* mojo_service_factory,
        base::ScopedFD mojo_pipe_fd) = 0;

    // Begins the graceful shutdown of the wilco_dtc_supportd daemon.
    virtual void BeginDaemonShutdown() = 0;

    // Creates BluetoothClient. For performance reason, must be called no more
    // than once.
    virtual std::unique_ptr<BluetoothClient> CreateBluetoothClient(
        const scoped_refptr<dbus::Bus>& bus) = 0;

    // Creates DebugdAdapter. For performance reason, must be called no more
    // than once.
    virtual std::unique_ptr<DebugdAdapter> CreateDebugdAdapter(
        const scoped_refptr<dbus::Bus>& bus) = 0;

    // Creates PowerdAdapter. For performance reason, must be called no more
    // than once.
    virtual std::unique_ptr<PowerdAdapter> CreatePowerdAdapter(
        const scoped_refptr<dbus::Bus>& bus) = 0;

    // Creates BluetoothEventService. For performance reason, must be called no
    // more than once.
    virtual std::unique_ptr<BluetoothEventService> CreateBluetoothEventService(
        BluetoothClient* bluetooth_client) = 0;

    // Creates EcEventService. For performance reason, must be called no
    // more than once.
    virtual std::unique_ptr<EcEventService> CreateEcEventService() = 0;

    // Creates PowerdEventService. For performance reason, must be called no
    // more than once.
    virtual std::unique_ptr<PowerdEventService> CreatePowerdEventService(
        PowerdAdapter* powerd_adapter) = 0;
  };

  // |grpc_service_uris| are the URIs on which the gRPC interface exposed by the
  // wilco_dtc_supportd daemon will be listening.
  // |ui_message_receiver_wilco_dtc_grpc_uri| is the URI which is
  // used for making requests to the gRPC interface exposed by the
  // wilco_dtc daemon which is explicitly eligible to receive
  // messages from UI extension (hosted by browser), no other gRPC client
  // recieves messages from UI extension.
  // |wilco_dtc_grpc_uris| is the list of URI's which are used for
  // making requests to the gRPC interface exposed by the wilco_dtc
  // daemons. Should not contain the URI equal to
  // |ui_message_receiver_wilco_dtc_grpc_uri|.
  Core(const std::vector<std::string>& grpc_service_uris,
       const std::string& ui_message_receiver_wilco_dtc_grpc_uri,
       const std::vector<std::string>& wilco_dtc_grpc_uris,
       Delegate* delegate);
  ~Core() override;

  // Overrides the file system root directory for file operations in tests.
  void set_root_dir_for_testing(const base::FilePath& root_dir) {
    ec_event_service_->set_root_dir_for_testing(root_dir);
    grpc_service_.set_root_dir_for_testing(root_dir);
  }

  // Overrides EC event fd events for |poll()| function in |ec_event_service_|
  // service in tests.
  void set_ec_event_service_fd_events_for_testing(int16_t events) {
    ec_event_service_->set_event_fd_events_for_testing(events);
  }

  // Starts gRPC servers, gRPC clients and EC event service.
  bool Start();

  // Performs asynchronous shutdown and cleanup of gRPC servers, gRPC clients
  // and EC event service. Destroys |dbus_object_| object.
  // This must be used before deleting this instance in case Start() was
  // called and returned success - in that case, the instance must be
  // destroyed only after |on_shutdown_callback| has been called.
  void ShutDown(const base::Closure& on_shutdown_callback);

  // Register the D-Bus object that the wilco_dtc_supportd daemon exposes and
  // tie methods exposed by this object with the actual implementation.
  void RegisterDBusObjectsAsync(
      const scoped_refptr<dbus::Bus>& bus,
      brillo::dbus_utils::AsyncEventSequencer* sequencer);

 private:
  using MojoEvent = chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdEvent;
  using MojomWilcoDtcSupportdClientPtr =
      chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdClientPtr;
  using MojomWilcoDtcSupportdServiceRequest =
      chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdServiceRequest;

  // DBusService::Delegate overrides:
  bool StartMojoServiceFactory(base::ScopedFD mojo_pipe_fd,
                               std::string* error_message) override;

  // Shuts down the self instance after a Mojo fatal error happens.
  void ShutDownDueToMojoError(const std::string& debug_reason);

  // WilcoDtcSupportdGrpcService::Delegate overrides:
  void SendWilcoDtcMessageToUi(
      const std::string& json_message,
      const SendMessageToUiCallback& callback) override;
  void PerformWebRequestToBrowser(
      WebRequestHttpMethod http_method,
      const std::string& url,
      const std::vector<std::string>& headers,
      const std::string& request_body,
      const PerformWebRequestToBrowserCallback& callback) override;
  void GetAvailableRoutinesToService(
      const GetAvailableRoutinesToServiceCallback& callback) override;
  void RunRoutineToService(
      const grpc_api::RunRoutineRequest& request,
      const RunRoutineToServiceCallback& callback) override;
  void GetRoutineUpdateRequestToService(
      int uuid,
      grpc_api::GetRoutineUpdateRequest::Command command,
      bool include_output,
      const GetRoutineUpdateRequestToServiceCallback& callback) override;
  void GetConfigurationDataFromBrowser(
      const GetConfigurationDataFromBrowserCallback& callback) override;
  void GetDriveSystemData(DriveSystemDataType data_type,
                          const GetDriveSystemDataCallback& callback) override;
  void RequestBluetoothDataNotification() override;

  // MojoService::Delegate overrides:
  void SendGrpcUiMessageToWilcoDtc(
      base::StringPiece json_message,
      const SendGrpcUiMessageToWilcoDtcCallback& callback) override;
  void NotifyConfigurationDataChangedToWilcoDtc() override;

  // RoutineService::Delegate overrides:
  bool GetCrosHealthdDiagnosticsService(
      chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsServiceRequest
          service) override;

  // chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdServiceFactory
  // overrides:
  void GetService(MojomWilcoDtcSupportdServiceRequest service,
                  MojomWilcoDtcSupportdClientPtr client,
                  GetServiceCallback callback) override;

  // BluetoothEventService::Observer overrides:
  void BluetoothAdapterDataChanged(
      const std::vector<BluetoothEventService::AdapterData>& adapters) override;

  // EcEventService::Observer overrides:
  void OnEcEvent(const EcEventService::EcEvent& ec_event) override;

  // PowerdEventService::Observer overrides:
  void OnPowerdEvent(PowerEventType type) override;

  // OnEcEvent should trigger the following:
  void SendGrpcEcEventToWilcoDtc(const EcEventService::EcEvent& ec_event);
  void SendMojoEcEventToBrowser(const MojoEvent& mojo_event);

  // Called by BluetoothAdapterDataChanged and RequestBluetoothDataNotification.
  void NotifyClientsBluetoothAdapterState(
      const std::vector<BluetoothEventService::AdapterData>& adapters);

  // Unsubscribes Core from observing events in preparation to shut down.
  void UnsubscribeFromEventServices();

  // Unowned. The delegate should outlive this instance.
  Delegate* const delegate_;

  // gRPC-related members:

  // gRPC URIs on which the |grpc_server_| is listening for incoming requests.
  const std::vector<std::string> grpc_service_uris_;
  // gRPC URI which is used by
  // |ui_message_receiver_wilco_dtc_grpc_client_| for sending UI
  // messages and EC notifications over the gRPC interface.
  const std::string ui_message_receiver_wilco_dtc_grpc_uri_;
  // gRPC URIs which are used by |wilco_dtc_grpc_clients_| for
  // accessing the gRPC interface exposed by the wilco_dtc daemons.
  const std::vector<std::string> wilco_dtc_grpc_uris_;
  // Implementation of the gRPC interface exposed by the wilco_dtc_supportd
  // daemon.
  GrpcService grpc_service_{this /* delegate */};
  // Connects |grpc_service_| with the gRPC server that listens for incoming
  // requests.
  AsyncGrpcServer<grpc_api::WilcoDtcSupportd::AsyncService> grpc_server_;
  // Allows to make outgoing requests to the gRPC interfaces exposed by the
  // wilco_dtc daemons.
  std::vector<std::unique_ptr<AsyncGrpcClient<grpc_api::WilcoDtc>>>
      wilco_dtc_grpc_clients_;
  // The pre-defined gRPC client that is allowed to respond to UI messages.
  // Owned by |wilco_dtc_grpc_clients_|.
  AsyncGrpcClient<grpc_api::WilcoDtc>*
      ui_message_receiver_wilco_dtc_grpc_client_;

  // D-Bus-related members:

  // Implementation of the D-Bus interface exposed by the wilco_dtc_supportd
  // daemon.
  DBusService dbus_service_{this /* delegate */};
  // Connects |dbus_service_| with the methods of the D-Bus object exposed by
  // the wilco_dtc_supportd daemon.
  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;

  // Mojo-related members:

  // Binding that connects this instance (which is an implementation of
  // chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdServiceFactory) with
  // the message pipe set up on top of the received file descriptor.
  //
  // Gets created after the BootstrapMojoConnection D-Bus method is called.
  std::unique_ptr<mojo::Binding<WilcoDtcSupportdServiceFactory>>
      mojo_service_factory_binding_;
  // Implementation of the Mojo interface exposed by the wilco_dtc_supportd
  // daemon and a proxy that allows sending outgoing Mojo requests.
  //
  // Gets created after the GetService() Mojo method is called.
  std::unique_ptr<MojoService> mojo_service_;
  // Whether binding of the Mojo service was attempted.
  //
  // This flag is needed for detecting repeated Mojo bootstrapping attempts
  // (alternative ways, like checking |mojo_service_factory_binding_|, are
  // unreliable during shutdown).
  bool mojo_service_bind_attempted_ = false;

  // D-Bus adapters for system daemons.
  std::unique_ptr<BluetoothClient> bluetooth_client_;
  std::unique_ptr<DebugdAdapter> debugd_adapter_;
  std::unique_ptr<PowerdAdapter> powerd_adapter_;

  // Telemetry services:
  std::unique_ptr<BluetoothEventService> bluetooth_event_service_;
  std::unique_ptr<EcEventService> ec_event_service_;
  std::unique_ptr<PowerdEventService> powerd_event_service_;

  // Diagnostic routine-related members:

  // Implementation of the diagnostic routine interface exposed by the
  // wilco_dtc_supportd daemon.
  RoutineService routine_service_{this /* delegate */};

  DISALLOW_COPY_AND_ASSIGN(Core);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_CORE_H_
