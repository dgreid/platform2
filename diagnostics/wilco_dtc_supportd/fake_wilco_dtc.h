// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_WILCO_DTC_SUPPORTD_FAKE_WILCO_DTC_H_
#define DIAGNOSTICS_WILCO_DTC_SUPPORTD_FAKE_WILCO_DTC_H_

#include <memory>
#include <string>
#include <utility>

#include <base/callback.h>
#include <base/macros.h>
#include <base/optional.h>

#include <brillo/grpc/async_grpc_client.h>
#include <brillo/grpc/async_grpc_server.h>

#include "wilco_dtc.grpc.pb.h"           // NOLINT(build/include)
#include "wilco_dtc_supportd.grpc.pb.h"  // NOLINT(build/include)

namespace diagnostics {

// Helper class that allows to test gRPC communication between wilco_dtc and
// support daemon.
//
// This class runs a "WilcoDtc" gRPC server on the given |grpc_server_uri| URI,
// and a gRPC client to the "WilcoDtcSupportd" gRPC service on the
// |wilco_dtc_supportd_grpc_uri| gRPC URI.
class FakeWilcoDtc final {
 public:
  using SendMessageToUiCallback = base::Callback<void(
      grpc::Status status,
      std::unique_ptr<grpc_api::SendMessageToUiResponse> response)>;
  using GetProcDataCallback = base::Callback<void(
      grpc::Status status, std::unique_ptr<grpc_api::GetProcDataResponse>)>;
  using GetEcTelemetryCallback = base::Callback<void(
      grpc::Status status, std::unique_ptr<grpc_api::GetEcTelemetryResponse>)>;
  using HandleMessageFromUiCallback = base::Callback<void(
      grpc::Status status,
      std::unique_ptr<grpc_api::HandleMessageFromUiResponse>)>;
  using HandleEcNotificationCallback = base::Callback<void(
      grpc::Status status,
      std::unique_ptr<grpc_api::HandleEcNotificationResponse>)>;
  using HandlePowerNotificationCallback = base::Callback<void(
      grpc::Status status,
      std::unique_ptr<grpc_api::HandlePowerNotificationResponse>)>;
  using PerformWebRequestResponseCallback = base::Callback<void(
      grpc::Status status,
      std::unique_ptr<grpc_api::PerformWebRequestResponse>)>;
  using GetConfigurationDataCallback = base::Callback<void(
      grpc::Status status,
      std::unique_ptr<grpc_api::GetConfigurationDataResponse>)>;
  using GetDriveSystemDataCallback = base::Callback<void(
      grpc::Status status,
      std::unique_ptr<grpc_api::GetDriveSystemDataResponse>)>;
  using RequestBluetoothDataNotificationCallback = base::Callback<void(
      grpc::Status status,
      std::unique_ptr<grpc_api::RequestBluetoothDataNotificationResponse>)>;
  using GetStatefulPartitionAvailableCapacityCallback = base::Callback<void(
      grpc::Status status,
      std::unique_ptr<
          grpc_api::GetStatefulPartitionAvailableCapacityResponse>)>;
  using HandleConfigurationDataChangedCallback = base::Callback<void(
      grpc::Status status,
      std::unique_ptr<grpc_api::HandleConfigurationDataChangedResponse>)>;
  using HandleBluetoothDataChangedCallback = base::Callback<void(
      grpc::Status status,
      std::unique_ptr<grpc_api::HandleBluetoothDataChangedResponse>)>;
  using GetAvailableRoutinesCallback = base::Callback<void(
      grpc::Status status,
      std::unique_ptr<grpc_api::GetAvailableRoutinesResponse>)>;

  using HandleEcNotificationRequestCallback =
      base::RepeatingCallback<void(int32_t, const std::string&)>;
  using HandlePowerNotificationRequestCallback = base::RepeatingCallback<void(
      grpc_api::HandlePowerNotificationRequest::PowerEvent)>;
  using HandleBluetoothDataChangedRequestCallback =
      base::RepeatingCallback<void(
          const grpc_api::HandleBluetoothDataChangedRequest&)>;

  FakeWilcoDtc(const std::string& grpc_server_uri,
               const std::string& wilco_dtc_supportd_grpc_uri);
  FakeWilcoDtc(const FakeWilcoDtc&) = delete;
  FakeWilcoDtc& operator=(const FakeWilcoDtc&) = delete;

  ~FakeWilcoDtc();

  // Methods that correspond to the "WilcoDtcSupportd" gRPC interface and allow
  // to perform actual gRPC requests as if the wilco_dtc daemon would do them:
  void SendMessageToUi(const grpc_api::SendMessageToUiRequest& request,
                       SendMessageToUiCallback callback);
  void GetProcData(const grpc_api::GetProcDataRequest& request,
                   GetProcDataCallback callback);
  void GetEcTelemetry(const grpc_api::GetEcTelemetryRequest& request,
                      GetEcTelemetryCallback callback);
  void PerformWebRequest(const grpc_api::PerformWebRequestParameter& parameter,
                         const PerformWebRequestResponseCallback& callback);
  void GetConfigurationData(
      const grpc_api::GetConfigurationDataRequest& request,
      const GetConfigurationDataCallback& callback);
  void GetDriveSystemData(const grpc_api::GetDriveSystemDataRequest& request,
                          const GetDriveSystemDataCallback& callback);
  void RequestBluetoothDataNotification(
      const grpc_api::RequestBluetoothDataNotificationRequest& request,
      const RequestBluetoothDataNotificationCallback& callback);
  void GetStatefulPartitionAvailableCapacity(
      const grpc_api::GetStatefulPartitionAvailableCapacityRequest& request,
      const GetStatefulPartitionAvailableCapacityCallback& callback);
  void GetAvailableRoutines(const GetAvailableRoutinesCallback& callback);

  // Sets up the passed callback to be used for subsequent
  // |HandleMessageFromUi| gRPC calls.
  void set_handle_message_from_ui_callback(
      base::Closure handle_message_from_ui_callback) {
    handle_message_from_ui_callback_.emplace(
        std::move(handle_message_from_ui_callback));
  }

  // Sets up the passed json message to be used as a response for subsequent
  // |HandleMessageFromUi| gRPC calls.
  void set_handle_message_from_ui_json_message_response(
      const std::string& json_message_response) {
    handle_message_from_ui_json_message_response_.emplace(
        json_message_response);
  }

  // Sets up the passed callback to be used for subsequent
  // |HandleEcNotification| gRPC calls.
  void set_handle_ec_event_request_callback(
      HandleEcNotificationRequestCallback handle_ec_event_request_callback) {
    handle_ec_event_request_callback_ = handle_ec_event_request_callback;
  }

  // Sets up the passed callback to be used for subsequent
  // |HandlePowerNotification| gRPC calls.
  void set_handle_power_event_request_callback(
      HandlePowerNotificationRequestCallback
          handle_powerd_event_request_callback) {
    handle_power_event_request_callback_ = handle_powerd_event_request_callback;
  }

  const base::Optional<std::string>&
  handle_message_from_ui_actual_json_message() const {
    return handle_message_from_ui_actual_json_message_;
  }

  // Sets up the passed callback to be used for subsequent
  // |HandleConfigurationDataChanged| gRPC calls.
  void set_configuration_data_changed_callback(
      base::RepeatingClosure callback) {
    configuration_data_changed_callback_.emplace(std::move(callback));
  }

  // Sets up the passed callback to be used for subsequent
  // |HandleBluetoothDataChanged| gRPC calls.
  void set_bluetooth_data_changed_callback(
      HandleBluetoothDataChangedRequestCallback callback) {
    bluetooth_data_changed_request_callback_.emplace(std::move(callback));
  }

 private:
  using AsyncGrpcWilcoDtcServer =
      brillo::AsyncGrpcServer<grpc_api::WilcoDtc::AsyncService>;
  using AsyncGrpcWilcoDtcSupportdClient =
      brillo::AsyncGrpcClient<grpc_api::WilcoDtcSupportd>;

  // Receives gRPC request and saves json message from request in
  // |handle_message_from_ui_actual_json_message_|.
  // Calls the callback |handle_message_from_ui_callback_| after all.
  void HandleMessageFromUi(
      std::unique_ptr<grpc_api::HandleMessageFromUiRequest> request,
      const HandleMessageFromUiCallback& callback);

  // Receives gRPC request and invokes the given |callback| with gRPC response.
  // Calls the callback |handle_ec_event_request_callback_| after all with the
  // request type and payload.
  void HandleEcNotification(
      std::unique_ptr<grpc_api::HandleEcNotificationRequest> request,
      const HandleEcNotificationCallback& callback);

  // Receives gRPC request and invokes the given |callback| with gRPC response.
  // Calls the callback |handle_power_event_request_callback_| after all with
  // the request type and payload.
  void HandlePowerNotification(
      std::unique_ptr<grpc_api::HandlePowerNotificationRequest> request,
      const HandlePowerNotificationCallback& callback);

  // Receives gRPC request and invokes the given |callback| with gRPC response.
  // Calls the callback |configuration_data_changed_callback_| after all.
  void HandleConfigurationDataChanged(
      std::unique_ptr<grpc_api::HandleConfigurationDataChangedRequest> request,
      const HandleConfigurationDataChangedCallback& callback);

  // Receives gRPC request and invokes the given |callback| with gRPC response.
  // Calls the callback |bluetooth_data_changed_callback_| after all.
  void HandleBluetoothDataChanged(
      std::unique_ptr<grpc_api::HandleBluetoothDataChangedRequest> request,
      const HandleBluetoothDataChangedCallback& callback);

  AsyncGrpcWilcoDtcServer grpc_server_;
  AsyncGrpcWilcoDtcSupportdClient wilco_dtc_supportd_grp_client_;

  base::Optional<base::Closure> handle_message_from_ui_callback_;
  base::Optional<std::string> handle_message_from_ui_actual_json_message_;
  base::Optional<std::string> handle_message_from_ui_json_message_response_;

  base::Optional<HandleEcNotificationRequestCallback>
      handle_ec_event_request_callback_;

  base::Optional<HandlePowerNotificationRequestCallback>
      handle_power_event_request_callback_;

  base::Optional<base::RepeatingClosure> configuration_data_changed_callback_;

  base::Optional<HandleBluetoothDataChangedRequestCallback>
      bluetooth_data_changed_request_callback_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_FAKE_WILCO_DTC_H_
