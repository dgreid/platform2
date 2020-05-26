// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/wilco_dtc_supportd/core.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include <base/barrier_closure.h>
#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/threading/thread_task_runner_handle.h>
#include <dbus/object_path.h>
#include <dbus/wilco_dtc_supportd/dbus-constants.h>
#include <mojo/public/cpp/system/message_pipe.h>

#include "diagnostics/wilco_dtc_supportd/json_utils.h"
#include "diagnostics/wilco_dtc_supportd/probe_service_impl.h"

#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

using EcEvent = EcEventService::EcEvent;
using EcEventReason = EcEventService::EcEvent::Reason;
using MojomWilcoDtcSupportdWebRequestStatus =
    chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdWebRequestStatus;
using MojomWilcoDtcSupportdWebRequestHttpMethod =
    chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdWebRequestHttpMethod;

// Converts HTTP method into an appropriate mojom one.
bool ConvertWebRequestHttpMethodToMojom(
    Core::WebRequestHttpMethod http_method,
    MojomWilcoDtcSupportdWebRequestHttpMethod* mojo_http_method_out) {
  DCHECK(mojo_http_method_out);
  switch (http_method) {
    case Core::WebRequestHttpMethod::kGet:
      *mojo_http_method_out = MojomWilcoDtcSupportdWebRequestHttpMethod::kGet;
      return true;
    case Core::WebRequestHttpMethod::kHead:
      *mojo_http_method_out = MojomWilcoDtcSupportdWebRequestHttpMethod::kHead;
      return true;
    case Core::WebRequestHttpMethod::kPost:
      *mojo_http_method_out = MojomWilcoDtcSupportdWebRequestHttpMethod::kPost;
      return true;
    case Core::WebRequestHttpMethod::kPut:
      *mojo_http_method_out = MojomWilcoDtcSupportdWebRequestHttpMethod::kPut;
      return true;
  }
  return false;
}

// Convert the result back from mojom status.
bool ConvertStatusFromMojom(MojomWilcoDtcSupportdWebRequestStatus mojo_status,
                            Core::WebRequestStatus* status_out) {
  DCHECK(status_out);
  switch (mojo_status) {
    case MojomWilcoDtcSupportdWebRequestStatus::kOk:
      *status_out = Core::WebRequestStatus::kOk;
      return true;
    case MojomWilcoDtcSupportdWebRequestStatus::kNetworkError:
      *status_out = Core::WebRequestStatus::kNetworkError;
      return true;
    case MojomWilcoDtcSupportdWebRequestStatus::kHttpError:
      *status_out = Core::WebRequestStatus::kHttpError;
      return true;
  }
  return false;
}

bool ConvertPowerEventToGrpc(
    PowerdEventService::Observer::PowerEventType type,
    grpc_api::HandlePowerNotificationRequest::PowerEvent* type_out) {
  DCHECK(type_out);
  switch (type) {
    case PowerdEventService::Observer::PowerEventType::kAcInsert:
      *type_out = grpc_api::HandlePowerNotificationRequest::AC_INSERT;
      return true;
    case PowerdEventService::Observer::PowerEventType::kAcRemove:
      *type_out = grpc_api::HandlePowerNotificationRequest::AC_REMOVE;
      return true;
    case PowerdEventService::Observer::PowerEventType::kOsSuspend:
      *type_out = grpc_api::HandlePowerNotificationRequest::OS_SUSPEND;
      return true;
    case PowerdEventService::Observer::PowerEventType::kOsResume:
      *type_out = grpc_api::HandlePowerNotificationRequest::OS_RESUME;
      return true;
  }
  return false;
}

}  // namespace

Core::Core(const std::vector<std::string>& grpc_service_uris,
           const std::string& ui_message_receiver_wilco_dtc_grpc_uri,
           const std::vector<std::string>& wilco_dtc_grpc_uris,
           Delegate* delegate)
    : delegate_(delegate),
      grpc_service_uris_(grpc_service_uris),
      ui_message_receiver_wilco_dtc_grpc_uri_(
          ui_message_receiver_wilco_dtc_grpc_uri),
      wilco_dtc_grpc_uris_(wilco_dtc_grpc_uris),
      grpc_server_(base::ThreadTaskRunnerHandle::Get(), grpc_service_uris_) {
  DCHECK(delegate);
  ec_event_service_ = delegate_->CreateEcEventService();
  probe_service_ = delegate->CreateProbeService(this);
  DCHECK(ec_event_service_);
  ec_event_service_->AddObserver(this);
}

Core::~Core() = default;

bool Core::Start() {
  // Associate RPCs of the to-be-exposed gRPC interface with methods of
  // |grpc_service_|.
  grpc_server_.RegisterHandler(
      &grpc_api::WilcoDtcSupportd::AsyncService::RequestSendMessageToUi,
      base::Bind(&GrpcService::SendMessageToUi,
                 base::Unretained(&grpc_service_)));
  grpc_server_.RegisterHandler(
      &grpc_api::WilcoDtcSupportd::AsyncService::RequestGetProcData,
      base::Bind(&GrpcService::GetProcData, base::Unretained(&grpc_service_)));
  grpc_server_.RegisterHandler(
      &grpc_api::WilcoDtcSupportd::AsyncService::RequestGetSysfsData,
      base::Bind(&GrpcService::GetSysfsData, base::Unretained(&grpc_service_)));
  grpc_server_.RegisterHandler(
      &grpc_api::WilcoDtcSupportd::AsyncService::RequestGetEcTelemetry,
      base::Bind(&GrpcService::GetEcTelemetry,
                 base::Unretained(&grpc_service_)));
  grpc_server_.RegisterHandler(
      &grpc_api::WilcoDtcSupportd::AsyncService::RequestPerformWebRequest,
      base::Bind(&GrpcService::PerformWebRequest,
                 base::Unretained(&grpc_service_)));
  grpc_server_.RegisterHandler(
      &grpc_api::WilcoDtcSupportd::AsyncService::RequestGetAvailableRoutines,
      base::Bind(&GrpcService::GetAvailableRoutines,
                 base::Unretained(&grpc_service_)));
  grpc_server_.RegisterHandler(
      &grpc_api::WilcoDtcSupportd::AsyncService::RequestRunRoutine,
      base::Bind(&GrpcService::RunRoutine, base::Unretained(&grpc_service_)));
  grpc_server_.RegisterHandler(
      &grpc_api::WilcoDtcSupportd::AsyncService::RequestGetRoutineUpdate,
      base::Bind(&GrpcService::GetRoutineUpdate,
                 base::Unretained(&grpc_service_)));
  grpc_server_.RegisterHandler(
      &grpc_api::WilcoDtcSupportd::AsyncService::RequestGetOsVersion,
      base::Bind(&GrpcService::GetOsVersion, base::Unretained(&grpc_service_)));
  grpc_server_.RegisterHandler(
      &grpc_api::WilcoDtcSupportd::AsyncService::RequestGetVpdField,
      base::Bind(&GrpcService::GetVpdField, base::Unretained(&grpc_service_)));
  grpc_server_.RegisterHandler(
      &grpc_api::WilcoDtcSupportd::AsyncService::RequestGetConfigurationData,
      base::Bind(&GrpcService::GetConfigurationData,
                 base::Unretained(&grpc_service_)));
  grpc_server_.RegisterHandler(
      &grpc_api::WilcoDtcSupportd::AsyncService::RequestGetDriveSystemData,
      base::Bind(&GrpcService::GetDriveSystemData,
                 base::Unretained(&grpc_service_)));
  grpc_server_.RegisterHandler(
      &grpc_api::WilcoDtcSupportd::AsyncService::
          RequestRequestBluetoothDataNotification,
      base::Bind(&GrpcService::RequestBluetoothDataNotification,
                 base::Unretained(&grpc_service_)));
  grpc_server_.RegisterHandler(
      &grpc_api::WilcoDtcSupportd::AsyncService::
          RequestGetStatefulPartitionAvailableCapacity,
      base::Bind(&GrpcService::GetStatefulPartitionAvailableCapacity,
                 base::Unretained(&grpc_service_)));

  // Start the gRPC server that listens for incoming gRPC requests.
  VLOG(1) << "Starting gRPC server";
  if (!grpc_server_.Start()) {
    LOG(ERROR) << "Failed to start the gRPC server listening on: "
               << base::JoinString(grpc_service_uris_, ", ");
    return false;
  }

  VLOG(0) << "Successfully started gRPC server listening on "
          << base::JoinString(grpc_service_uris_, ",");

  // Start the gRPC clients that talk to the wilco_dtc daemon.
  for (const auto& uri : wilco_dtc_grpc_uris_) {
    wilco_dtc_grpc_clients_.push_back(
        std::make_unique<AsyncGrpcClient<grpc_api::WilcoDtc>>(
            base::ThreadTaskRunnerHandle::Get(), uri));
    VLOG(0) << "Created gRPC wilco_dtc client on " << uri;
  }

  // Start the gRPC client that is allowed to receive UI messages as a normal
  // gRPC client that talks to the wilco_dtc daemon.
  wilco_dtc_grpc_clients_.push_back(
      std::make_unique<AsyncGrpcClient<grpc_api::WilcoDtc>>(
          base::ThreadTaskRunnerHandle::Get(),
          ui_message_receiver_wilco_dtc_grpc_uri_));
  VLOG(0) << "Created gRPC wilco_dtc client on "
          << ui_message_receiver_wilco_dtc_grpc_uri_;
  ui_message_receiver_wilco_dtc_grpc_client_ =
      wilco_dtc_grpc_clients_.back().get();

  // Start EC event service.
  if (!ec_event_service_->Start()) {
    LOG(WARNING)
        << "Failed to start EC event service. EC events will be ignored.";
  }

  return true;
}

void Core::ShutDown(const base::Closure& on_shutdown_callback) {
  VLOG(1) << "Tearing down gRPC server, gRPC wilco_dtc clients, "
             "EC event service and D-Bus server";
  UnsubscribeFromEventServices();
  const base::Closure barrier_closure = base::BarrierClosure(
      wilco_dtc_grpc_clients_.size() + 2, on_shutdown_callback);
  ec_event_service_->ShutDown(barrier_closure);
  grpc_server_.ShutDown(barrier_closure);
  for (const auto& client : wilco_dtc_grpc_clients_) {
    client->ShutDown(barrier_closure);
  }
  ui_message_receiver_wilco_dtc_grpc_client_ = nullptr;

  dbus_object_.reset();
}

void Core::RegisterDBusObjectsAsync(
    const scoped_refptr<dbus::Bus>& bus,
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  DCHECK(bus);
  DCHECK(!dbus_object_);
  dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
      nullptr /* object_manager */, bus,
      dbus::ObjectPath(kWilcoDtcSupportdServicePath));
  brillo::dbus_utils::DBusInterface* dbus_interface =
      dbus_object_->AddOrGetInterface(kWilcoDtcSupportdServiceInterface);
  DCHECK(dbus_interface);
  dbus_interface->AddSimpleMethodHandlerWithError(
      kWilcoDtcSupportdBootstrapMojoConnectionMethod,
      base::Unretained(&dbus_service_), &DBusService::BootstrapMojoConnection);
  dbus_object_->RegisterAsync(sequencer->GetHandler(
      "Failed to register D-Bus object" /* descriptive_message */,
      true /* failure_is_fatal */));

  bluetooth_client_ = delegate_->CreateBluetoothClient(bus);
  DCHECK(bluetooth_client_);

  debugd_adapter_ = delegate_->CreateDebugdAdapter(bus);
  DCHECK(debugd_adapter_);

  powerd_adapter_ = delegate_->CreatePowerdAdapter(bus);
  DCHECK(powerd_adapter_);

  bluetooth_event_service_ =
      delegate_->CreateBluetoothEventService(bluetooth_client_.get());
  DCHECK(bluetooth_event_service_);
  bluetooth_event_service_->AddObserver(this);

  powerd_event_service_ =
      delegate_->CreatePowerdEventService(powerd_adapter_.get());
  DCHECK(powerd_event_service_);
  powerd_event_service_->AddObserver(this);
}

bool Core::StartMojoServiceFactory(base::ScopedFD mojo_pipe_fd,
                                   std::string* error_message) {
  DCHECK(mojo_pipe_fd.is_valid());

  if (mojo_service_bind_attempted_) {
    // This should not normally be triggered, since the other endpoint - the
    // browser process - should bootstrap the Mojo connection only once, and
    // when that process is killed the Mojo shutdown notification should have
    // been received earlier. But handle this case to be on the safe side. After
    // our restart the browser process is expected to invoke the bootstrapping
    // again.
    *error_message = "Mojo connection was already bootstrapped";
    ShutDownDueToMojoError(
        "Repeated Mojo bootstrap request received" /* debug_reason */);
    return false;
  }

  if (!base::SetCloseOnExec(mojo_pipe_fd.get())) {
    PLOG(ERROR) << "Failed to set FD_CLOEXEC on Mojo file descriptor";
    *error_message = "Failed to set FD_CLOEXEC";
    return false;
  }

  mojo_service_bind_attempted_ = true;
  mojo_service_factory_binding_ = delegate_->BindMojoServiceFactory(
      this /* mojo_service_factory */, std::move(mojo_pipe_fd));
  if (!mojo_service_factory_binding_) {
    *error_message = "Failed to bootstrap Mojo";
    ShutDownDueToMojoError("Mojo bootstrap failed" /* debug_reason */);
    return false;
  }
  mojo_service_factory_binding_->set_connection_error_handler(
      base::Bind(&Core::ShutDownDueToMojoError, base::Unretained(this),
                 "Mojo connection error" /* debug_reason */));
  LOG(INFO) << "Successfully bootstrapped Mojo connection";
  return true;
}

bool Core::GetCrosHealthdDiagnosticsService(
    chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsServiceRequest
        service) {
  if (!mojo_service_) {
    LOG(WARNING) << "GetCrosHealthdDiagnosticsService happens before Mojo "
                 << "connection is established.";
    return false;
  }

  mojo_service_->GetCrosHealthdDiagnosticsService(std::move(service));
  return true;
}

bool Core::BindCrosHealthdProbeService(
    chromeos::cros_healthd::mojom::CrosHealthdProbeServiceRequest service) {
  if (!mojo_service_) {
    LOG(WARNING) << "BindCrosHealthdProbeService happens before Mojo "
                 << "connection is established.";
    return false;
  }

  mojo_service_->GetCrosHealthdProbeService(std::move(service));
  return true;
}

void Core::GetService(MojomWilcoDtcSupportdServiceRequest service,
                      MojomWilcoDtcSupportdClientPtr client,
                      GetServiceCallback callback) {
  // Mojo guarantees that these parameters are nun-null (see
  // VALIDATION_ERROR_UNEXPECTED_INVALID_HANDLE).
  DCHECK(service.is_pending());
  DCHECK(client);

  if (mojo_service_) {
    LOG(WARNING) << "GetService Mojo method called multiple times";
    // We should not normally be called more than once, so don't bother with
    // trying to reuse objects from the previous call. However, make sure we
    // don't have duplicate instances of the service at any moment of time.
    mojo_service_.reset();
  }

  // Create an instance of MojoService that will handle incoming
  // Mojo calls. Pass |service| to it to fulfill the remote endpoint's request,
  // allowing it to call into |mojo_service_|. Pass also |client| to allow
  // |mojo_service_| to do calls in the opposite direction.
  mojo_service_ = std::make_unique<MojoService>(
      this /* delegate */, std::move(service), std::move(client));

  std::move(callback).Run();
}

void Core::ShutDownDueToMojoError(const std::string& debug_reason) {
  // Our daemon has to be restarted to be prepared for future Mojo connection
  // bootstraps. We can't do this without a restart since Mojo EDK gives no
  // guarantee to support repeated bootstraps. Therefore tear down and exit from
  // our process and let upstart to restart us again.
  LOG(INFO) << "Shutting down due to: " << debug_reason;
  mojo_service_.reset();
  mojo_service_factory_binding_.reset();
  delegate_->BeginDaemonShutdown();
}

void Core::SendWilcoDtcMessageToUi(const std::string& json_message,
                                   const SendMessageToUiCallback& callback) {
  VLOG(1) << "SendWilcoDtcMessageToUi() json_message=" << json_message;
  if (!mojo_service_) {
    LOG(WARNING) << "GetConfigurationDataFromBrowser happens before Mojo "
                 << "connection is established.";
    callback.Run("");
    return;
  }
  mojo_service_->SendWilcoDtcMessageToUi(json_message, callback);
}

void Core::PerformWebRequestToBrowser(
    WebRequestHttpMethod http_method,
    const std::string& url,
    const std::vector<std::string>& headers,
    const std::string& request_body,
    const PerformWebRequestToBrowserCallback& callback) {
  VLOG(1) << "Core::PerformWebRequestToBrowser";

  if (!mojo_service_) {
    LOG(WARNING) << "PerformWebRequestToBrowser happens before Mojo connection "
                 << "is established.";
    callback.Run(WebRequestStatus::kInternalError, 0 /* http_status */,
                 "" /* response_body */);
    return;
  }

  MojomWilcoDtcSupportdWebRequestHttpMethod mojo_http_method;
  if (!ConvertWebRequestHttpMethodToMojom(http_method, &mojo_http_method)) {
    LOG(ERROR) << "Unknown gRPC http method: " << static_cast<int>(http_method);
    callback.Run(WebRequestStatus::kInternalError, 0 /* http_status */,
                 "" /* response_body */);
    return;
  }

  mojo_service_->PerformWebRequest(
      mojo_http_method, url, headers, request_body,
      base::Bind(
          [](const PerformWebRequestToBrowserCallback& callback,
             MojomWilcoDtcSupportdWebRequestStatus mojo_status, int http_status,
             base::StringPiece response_body) {
            WebRequestStatus status;
            if (!ConvertStatusFromMojom(mojo_status, &status)) {
              LOG(ERROR) << "Unknown mojo web request status: " << mojo_status;
              callback.Run(WebRequestStatus::kInternalError,
                           0 /* http_status */, "" /* response_body */);
              return;
            }
            callback.Run(status, http_status, response_body);
          },
          callback));
}

void Core::GetAvailableRoutinesToService(
    const GetAvailableRoutinesToServiceCallback& callback) {
  routine_service_.GetAvailableRoutines(callback);
}

void Core::RunRoutineToService(const grpc_api::RunRoutineRequest& request,
                               const RunRoutineToServiceCallback& callback) {
  routine_service_.RunRoutine(request, callback);
}

void Core::GetRoutineUpdateRequestToService(
    int uuid,
    grpc_api::GetRoutineUpdateRequest::Command command,
    bool include_output,
    const GetRoutineUpdateRequestToServiceCallback& callback) {
  routine_service_.GetRoutineUpdate(uuid, command, include_output, callback);
}

void Core::GetConfigurationDataFromBrowser(
    const GetConfigurationDataFromBrowserCallback& callback) {
  VLOG(1) << "Core::GetConfigurationDataFromBrowser";

  if (!mojo_service_) {
    LOG(WARNING) << "GetConfigurationDataFromBrowser happens before Mojo "
                 << "connection is established.";
    callback.Run("" /* json_configuration_data */);
    return;
  }

  mojo_service_->GetConfigurationData(callback);
}

void Core::GetDriveSystemData(DriveSystemDataType data_type,
                              const GetDriveSystemDataCallback& callback) {
  if (!debugd_adapter_) {
    LOG(WARNING) << "DebugdAdapter is not yet ready for incoming requests";
    callback.Run("", false /* success */);
    return;
  }

  auto result_callback = base::Bind(
      [](const GetDriveSystemDataCallback& callback, const std::string& result,
         brillo::Error* error) {
        if (error) {
          LOG(WARNING) << "Debugd smartctl failed with error: "
                       << error->GetMessage();
          callback.Run("", false /* success */);
          return;
        }
        callback.Run(result, true /* success */);
      },
      callback);

  switch (data_type) {
    case DriveSystemDataType::kSmartAttributes:
      debugd_adapter_->GetSmartAttributes(result_callback);
      break;
    case DriveSystemDataType::kIdentityAttributes:
      debugd_adapter_->GetNvmeIdentity(result_callback);
      break;
  }
}

void Core::RequestBluetoothDataNotification() {
  VLOG(1) << "WilcoDtcSupportdCore::RequestBluetoothDataNotification";

  if (!bluetooth_event_service_) {
    VLOG(1) << "Bluetooth event service not yet ready";
    return;
  }

  NotifyClientsBluetoothAdapterState(
      bluetooth_event_service_->GetLatestEvent());
}

void Core::ProbeTelemetryInfo(
    std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum> categories,
    ProbeTelemetryInfoCallback callback) {
  VLOG(1) << "Core::ProbeTelemetryInfo";
  probe_service_->ProbeTelemetryInfo(std::move(categories),
                                     std::move(callback));
}

void Core::SendGrpcUiMessageToWilcoDtc(
    base::StringPiece json_message,
    const SendGrpcUiMessageToWilcoDtcCallback& callback) {
  VLOG(1) << "Core::SendGrpcMessageToWilcoDtc";

  if (!ui_message_receiver_wilco_dtc_grpc_client_) {
    VLOG(1) << "The UI message is discarded since the recipient has been shut "
            << "down.";
    callback.Run(std::string() /* response_json_message */);
    return;
  }

  grpc_api::HandleMessageFromUiRequest request;
  request.set_json_message(json_message.data() ? json_message.data() : "",
                           json_message.length());

  ui_message_receiver_wilco_dtc_grpc_client_->CallRpc(
      &grpc_api::WilcoDtc::Stub::AsyncHandleMessageFromUi, request,
      base::Bind(
          [](const SendGrpcUiMessageToWilcoDtcCallback& callback,
             std::unique_ptr<grpc_api::HandleMessageFromUiResponse> response) {
            if (!response) {
              VLOG(1) << "Failed to call HandleMessageFromUiRequest gRPC method"
                         " on wilco_dtc: response message is nullptr";
              callback.Run(std::string() /* response_json_message */);
              return;
            }

            VLOG(1) << "gRPC method HandleMessageFromUiRequest was "
                       "successfully called on wilco_dtc";

            std::string json_error_message;
            if (!IsJsonValid(
                    base::StringPiece(response->response_json_message()),
                    &json_error_message)) {
              LOG(ERROR) << "Invalid JSON error: " << json_error_message;
              callback.Run(std::string() /* response_json_message */);
              return;
            }

            callback.Run(response->response_json_message());
          },
          callback));
}

void Core::NotifyConfigurationDataChangedToWilcoDtc() {
  VLOG(1) << "Core::NotifyConfigurationDataChanged";

  grpc_api::HandleConfigurationDataChangedRequest request;
  for (auto& client : wilco_dtc_grpc_clients_) {
    client->CallRpc(
        &grpc_api::WilcoDtc::Stub::AsyncHandleConfigurationDataChanged, request,
        base::Bind(
            [](std::unique_ptr<grpc_api::HandleConfigurationDataChangedResponse>
                   response) {
              if (!response) {
                VLOG(1) << "Failed to call HandleConfigurationDataChanged gRPC "
                           "method on wilco_dtc: response message is nullptr";
                return;
              }
              VLOG(1) << "gRPC method HandleConfigurationDaraChanged was "
                         "successfully called on wilco_dtc";
            }));
  }
}

void Core::BluetoothAdapterDataChanged(
    const std::vector<BluetoothEventService::AdapterData>& adapters) {
  VLOG(1) << "Core::BluetoothAdapterDataChanged";

  NotifyClientsBluetoothAdapterState(adapters);
}

void Core::OnPowerdEvent(PowerEventType type) {
  VLOG(1) << "Core::OnPowerdEvent: " << static_cast<int>(type);

  grpc_api::HandlePowerNotificationRequest::PowerEvent grpc_type;
  if (!ConvertPowerEventToGrpc(type, &grpc_type)) {
    LOG(ERROR) << "Unable to convert power event to gRPC power event: "
               << static_cast<int>(type);
    return;
  }

  grpc_api::HandlePowerNotificationRequest request;
  request.set_power_event(grpc_type);

  for (auto& client : wilco_dtc_grpc_clients_) {
    client->CallRpc(
        &grpc_api::WilcoDtc::Stub::AsyncHandlePowerNotification, request,
        base::Bind([](std::unique_ptr<grpc_api::HandlePowerNotificationResponse>
                          response) {
          if (!response) {
            VLOG(1) << "Failed to call HandlePowerNotification gRPC "
                       "method on wilco_dtc: response message is nullptr";
            return;
          }
          VLOG(1) << "gRPC method HandlePowerNotification was "
                     "successfully called on wilco_dtc";
        }));
  }
}

void Core::OnEcEvent(const EcEvent& ec_event) {
  VLOG(1) << "Core::OnEcEvent: type=" << static_cast<int>(ec_event.type)
          << " reason=" << static_cast<int>(ec_event.GetReason());

  SendGrpcEcEventToWilcoDtc(ec_event);

  // Parse EcEventReason into a MojoEvent and forward to the delegate.
  // We only will forward certain events. If they aren't relevant, ignore.
  switch (ec_event.GetReason()) {
    case EcEventReason::kNonWilcoCharger:
      SendMojoEcEventToBrowser(MojoEvent::kNonWilcoCharger);
      break;
    case EcEventReason::kLowPowerCharger:
      SendMojoEcEventToBrowser(MojoEvent::kLowPowerCharger);
      break;
    case EcEventReason::kBatteryAuth:
      SendMojoEcEventToBrowser(MojoEvent::kBatteryAuth);
      break;
    case EcEventReason::kDockDisplay:
      SendMojoEcEventToBrowser(MojoEvent::kDockDisplay);
      break;
    case EcEventReason::kDockThunderbolt:
      SendMojoEcEventToBrowser(MojoEvent::kDockThunderbolt);
      break;
    case EcEventReason::kIncompatibleDock:
      SendMojoEcEventToBrowser(MojoEvent::kIncompatibleDock);
      break;
    case EcEventReason::kDockError:
      SendMojoEcEventToBrowser(MojoEvent::kDockError);
      break;
    case EcEventReason::kSysNotification:
      VLOG(2) << "Received EC event that doesn't trigger a mojo event";
      break;
    case EcEventReason::kNonSysNotification:
      VLOG(2) << "Received a non-system notification EC event";
      break;
  }
}

void Core::SendGrpcEcEventToWilcoDtc(const EcEvent& ec_event) {
  VLOG(1) << "Core::SendGrpcEcEventToWilcoDtc";

  size_t payload_size = ec_event.PayloadSizeInBytes();
  if (payload_size > sizeof(ec_event.payload)) {
    VLOG(2) << "Received EC event with invalid payload size: " << payload_size;
    return;
  }

  grpc_api::HandleEcNotificationRequest request;
  request.set_type(ec_event.type);
  request.set_payload(&ec_event.payload, payload_size);

  for (auto& client : wilco_dtc_grpc_clients_) {
    client->CallRpc(
        &grpc_api::WilcoDtc::Stub::AsyncHandleEcNotification, request,
        base::Bind([](std::unique_ptr<grpc_api::HandleEcNotificationResponse>
                          response) {
          if (!response) {
            VLOG(1)
                << "Failed to call HandleEcNotificationRequest gRPC method on "
                   "wilco_dtc: response message is nullptr";
            return;
          }
          VLOG(1) << "gRPC method HandleEcNotificationRequest was successfully"
                     "called on wilco_dtc";
        }));
  }
}

void Core::SendMojoEcEventToBrowser(const MojoEvent& mojo_event) {
  VLOG(1) << "Core::HandleEvent";

  if (!mojo_service_) {
    LOG(WARNING) << "SendMojoEcEventToBrowser happens before Mojo connection "
                    "is established.";
    return;
  }

  mojo_service_->HandleEvent(mojo_event);
}

void Core::NotifyClientsBluetoothAdapterState(
    const std::vector<BluetoothEventService::AdapterData>& adapters) {
  grpc_api::HandleBluetoothDataChangedRequest request;
  for (const auto& adapter : adapters) {
    VLOG(1) << base::StringPrintf(
        "Bluetooth adapter adapter: name=%s addres=%s powered=%d "
        "connected_devices_count=%d",
        adapter.name.c_str(), adapter.address.c_str(), adapter.powered,
        adapter.connected_devices_count);

    auto adapter_data = request.add_adapters();
    adapter_data->set_adapter_name(adapter.name);
    adapter_data->set_adapter_mac_address(adapter.address);
    adapter_data->set_connected_devices_count(adapter.connected_devices_count);
    if (adapter.powered) {
      adapter_data->set_carrier_status(
          grpc_api::HandleBluetoothDataChangedRequest::AdapterData::STATUS_UP);
    } else {
      adapter_data->set_carrier_status(
          grpc_api::HandleBluetoothDataChangedRequest::AdapterData::
              STATUS_DOWN);
    }
  }

  for (auto& client : wilco_dtc_grpc_clients_) {
    client->CallRpc(
        &grpc_api::WilcoDtc::Stub::AsyncHandleBluetoothDataChanged, request,
        base::Bind(
            [](std::unique_ptr<grpc_api::HandleBluetoothDataChangedResponse>
                   response) {
              if (!response) {
                VLOG(1) << "Failed to call HandleBluetoothDataChanged gRPC "
                           "method on wilco_dtc: response message is nullptr";
                return;
              }
              VLOG(1) << "gRPC method HandleBluetoothDataChanged was "
                         "successfully called on wilco_dtc";
            }));
  }
}

void Core::UnsubscribeFromEventServices() {
  if (bluetooth_event_service_) {
    bluetooth_event_service_->RemoveObserver(this);
  }
  if (powerd_event_service_) {
    powerd_event_service_->RemoveObserver(this);
  }
  ec_event_service_->RemoveObserver(this);
}

}  // namespace diagnostics
