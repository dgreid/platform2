// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_WILCO_DTC_SUPPORTD_GRPC_SERVICE_H_
#define DIAGNOSTICS_WILCO_DTC_SUPPORTD_GRPC_SERVICE_H_

#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/strings/string_piece.h>
#include <base/macros.h>
#include <google/protobuf/repeated_field.h>
#include <grpcpp/grpcpp.h>

#include "diagnostics/wilco_dtc_supportd/telemetry/ec_service.h"
#include "diagnostics/wilco_dtc_supportd/telemetry/system_files_service.h"
#include "diagnostics/wilco_dtc_supportd/telemetry/system_info_service.h"

#include "mojo/cros_healthd_probe.mojom.h"
#include "wilco_dtc_supportd.pb.h"  // NOLINT(build/include)

namespace diagnostics {

// The total size of all "string" and "byte" fileds in a single
// PerformWebRequestParameter message must not exceed this size.
extern const int kMaxPerformWebRequestParameterSizeInBytes;

// Max number of headers in PerformWebRequestParameter.
extern const int kMaxNumberOfHeadersInPerformWebRequestParameter;

// Implements the "WilcoDtcSupportd" gRPC interface exposed by the
// wilco_dtc_supportd daemon (see the API definition at
// grpc/wilco_dtc_supportd.proto)
class GrpcService final {
 public:
  class Delegate {
   public:
    // Status of a Web Request performed by |PerformWebRequestToBrowser|.
    enum class WebRequestStatus {
      kOk,
      kNetworkError,
      kHttpError,
      kInternalError,
    };

    // HTTP method to be performed by |PerformWebRequestToBrowser|.
    enum class WebRequestHttpMethod {
      kGet,
      kHead,
      kPost,
      kPut,
      kPatch,
    };

    // Drive system data type to be retrieved by |GetDriveSystemData|.
    enum class DriveSystemDataType {
      kSmartAttributes,
      kIdentityAttributes,
    };

    using SendMessageToUiCallback = base::Callback<void(
        grpc::Status, base::StringPiece response_json_message)>;
    using PerformWebRequestToBrowserCallback =
        base::Callback<void(WebRequestStatus status,
                            int http_status,
                            base::StringPiece response_body)>;
    using GetAvailableRoutinesToServiceCallback = base::Callback<void(
        const std::vector<grpc_api::DiagnosticRoutine>& routines,
        grpc_api::RoutineServiceStatus service_status)>;
    using RunRoutineToServiceCallback =
        base::Callback<void(int uuid,
                            grpc_api::DiagnosticRoutineStatus status,
                            grpc_api::RoutineServiceStatus service_status)>;
    using GetRoutineUpdateRequestToServiceCallback =
        base::Callback<void(int uuid,
                            grpc_api::DiagnosticRoutineStatus status,
                            int progress_percent,
                            grpc_api::DiagnosticRoutineUserMessage user_message,
                            const std::string& output,
                            const std::string& status_message,
                            grpc_api::RoutineServiceStatus service_status)>;
    using GetConfigurationDataFromBrowserCallback =
        base::Callback<void(const std::string& json_configuration_data)>;
    using GetDriveSystemDataCallback =
        base::Callback<void(const std::string& payload, bool success)>;
    using ProbeTelemetryInfoCallback = base::OnceCallback<void(
        chromeos::cros_healthd::mojom::TelemetryInfoPtr)>;

    virtual ~Delegate() = default;

    // Called when gRPC |SendMessageToUi| was called.
    //
    // Calls wilco_dtc_supportd daemon mojo function |SendWilcoDtcMessageToUi|
    // method and passes all fields of |SendMessageToUiRequest| to
    // send a message to the diagnostics UI extension. The result
    // of the call is returned via |callback|.
    virtual void SendWilcoDtcMessageToUi(
        const std::string& json_message,
        const SendMessageToUiCallback& callback) = 0;
    // Called when gRPC |PerformWebRequest| was called.
    //
    // Calls wilco_dtc_supportd daemon mojo function
    // |PerformWebRequestToBrowser| method and passes all fields of
    // |PerformWebRequestParameter| to perform a web request.
    // The result of the call is returned via |callback|.
    virtual void PerformWebRequestToBrowser(
        WebRequestHttpMethod httpMethod,
        const std::string& url,
        const std::vector<std::string>& headers,
        const std::string& request_body,
        const PerformWebRequestToBrowserCallback& callback) = 0;
    // Called when gRPC |GetAvailableRoutines| was called.
    //
    // Calls wilco_dtc_supportd daemon routine function |GetAvailableRoutines|
    // method and passes all fields of |GetAvailableRoutinesRequest| to
    // determine which routines are available on the platform. The result
    // of the call is returned via |callback|.
    virtual void GetAvailableRoutinesToService(
        const GetAvailableRoutinesToServiceCallback& callback) = 0;
    // Called when gRPC |RunRoutine| was called.
    //
    // Calls wilco_dtc_supportd daemon routine function |RunRoutine| method and
    // passes all fields of |RunRoutineRequest| to ask the platform to run a
    // diagnostic routine. The result of the call is returned via |callback|.
    virtual void RunRoutineToService(
        const grpc_api::RunRoutineRequest& request,
        const RunRoutineToServiceCallback& callback) = 0;
    // Called when gRPC |GetRoutineUpdate| was called.
    //
    // Calls wilco_dtc_supportd daemon routine function |GetRoutineUpdate|
    // method and passes all fields of |GetRoutineUpdateRequest| to
    // control or get the status of an existing diagnostic routine. The result
    // of the call is returned via |callback|.
    virtual void GetRoutineUpdateRequestToService(
        int uuid,
        grpc_api::GetRoutineUpdateRequest::Command command,
        bool include_output,
        const GetRoutineUpdateRequestToServiceCallback& callback) = 0;

    // Called when gRPC |GetConfigurationData| was called.
    //
    // Calls wilco_dtc_supportd daemon mojo function
    // |GetConfigurationDataFromBrowser| method.
    // The result of the call is returned via |callback|.
    virtual void GetConfigurationDataFromBrowser(
        const GetConfigurationDataFromBrowserCallback& callback) = 0;

    // Called when gRPC |GetDriveSystemData| was called.
    //
    // Calls wilco_dtc_supportd daemon |GetDriveSystemData| method. The result
    // of the call is returned via |callback|.
    virtual void GetDriveSystemData(
        DriveSystemDataType data_type,
        const GetDriveSystemDataCallback& callback) = 0;

    // Called when gRPC |RequestBluetoothDataNotification| was called.
    //
    // Calls wilco_dtc_supportd daemon |RequestBluetoothDataNotification|
    // method.
    virtual void RequestBluetoothDataNotification() = 0;

    // Called when gRPC |GetStatefulPartitionAvailableCapacty| was called.
    //
    // Calls cros_healthd's probe service.
    virtual void ProbeTelemetryInfo(
        std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum>
            categories,
        ProbeTelemetryInfoCallback callback) = 0;

    // Gets a pointer to the EcService.
    virtual EcService* GetEcService() = 0;
  };

  using SendMessageToUiCallback = base::Callback<void(
      grpc::Status, std::unique_ptr<grpc_api::SendMessageToUiResponse>)>;
  using GetProcDataCallback = base::Callback<void(
      grpc::Status, std::unique_ptr<grpc_api::GetProcDataResponse>)>;
  using GetSysfsDataCallback = base::Callback<void(
      grpc::Status, std::unique_ptr<grpc_api::GetSysfsDataResponse>)>;
  using GetEcTelemetryCallback = base::Callback<void(
      grpc::Status, std::unique_ptr<grpc_api::GetEcTelemetryResponse>)>;
  using PerformWebRequestResponseCallback = base::Callback<void(
      grpc::Status, std::unique_ptr<grpc_api::PerformWebRequestResponse>)>;
  using GetAvailableRoutinesCallback = base::Callback<void(
      grpc::Status, std::unique_ptr<grpc_api::GetAvailableRoutinesResponse>)>;
  using RunRoutineCallback = base::Callback<void(
      grpc::Status, std::unique_ptr<grpc_api::RunRoutineResponse>)>;
  using GetRoutineUpdateCallback = base::Callback<void(
      grpc::Status, std::unique_ptr<grpc_api::GetRoutineUpdateResponse>)>;
  using GetOsVersionCallback = base::Callback<void(
      grpc::Status, std::unique_ptr<grpc_api::GetOsVersionResponse>)>;
  using GetConfigurationDataCallback = base::Callback<void(
      grpc::Status, std::unique_ptr<grpc_api::GetConfigurationDataResponse>)>;
  using GetVpdFieldCallback = base::Callback<void(
      grpc::Status, std::unique_ptr<grpc_api::GetVpdFieldResponse>)>;
  using GetDriveSystemDataCallback = base::Callback<void(
      grpc::Status, std::unique_ptr<grpc_api::GetDriveSystemDataResponse>)>;
  using RequestBluetoothDataNotificationCallback = base::Callback<void(
      grpc::Status,
      std::unique_ptr<grpc_api::RequestBluetoothDataNotificationResponse>)>;
  using GetStatefulPartitionAvailableCapacityCallback = base::Callback<void(
      grpc::Status,
      std::unique_ptr<
          grpc_api::GetStatefulPartitionAvailableCapacityResponse>)>;

  explicit GrpcService(Delegate* delegate);
  GrpcService(const GrpcService&) = delete;
  GrpcService& operator=(const GrpcService&) = delete;

  ~GrpcService();

  // Overrides the file system root directory for file operations in tests.
  void set_root_dir_for_testing(const base::FilePath& root_dir);

  // Overrides the system files service for operations in tests.
  void set_system_files_service_for_testing(
      std::unique_ptr<SystemFilesService> service);

  // Overrides the system info service for operations in tests.
  void set_system_info_service_for_testing(
      std::unique_ptr<SystemInfoService> service);

  // Implementation of the "WilcoDtcSupportd" gRPC interface:
  void SendMessageToUi(
      std::unique_ptr<grpc_api::SendMessageToUiRequest> request,
      const SendMessageToUiCallback& callback);
  void GetProcData(std::unique_ptr<grpc_api::GetProcDataRequest> request,
                   const GetProcDataCallback& callback);
  void GetSysfsData(std::unique_ptr<grpc_api::GetSysfsDataRequest> request,
                    const GetSysfsDataCallback& callback);
  void GetEcTelemetry(std::unique_ptr<grpc_api::GetEcTelemetryRequest> request,
                      const GetEcTelemetryCallback& callback);
  void PerformWebRequest(
      std::unique_ptr<grpc_api::PerformWebRequestParameter> parameter,
      const PerformWebRequestResponseCallback& callback);
  void GetAvailableRoutines(
      std::unique_ptr<grpc_api::GetAvailableRoutinesRequest> request,
      const GetAvailableRoutinesCallback& callback);
  void RunRoutine(std::unique_ptr<grpc_api::RunRoutineRequest> request,
                  const RunRoutineCallback& callback);
  void GetRoutineUpdate(
      std::unique_ptr<grpc_api::GetRoutineUpdateRequest> request,
      const GetRoutineUpdateCallback& callback);
  void GetOsVersion(std::unique_ptr<grpc_api::GetOsVersionRequest> request,
                    const GetOsVersionCallback& callback);
  void GetConfigurationData(
      std::unique_ptr<grpc_api::GetConfigurationDataRequest> request,
      const GetConfigurationDataCallback& callback);
  void GetVpdField(std::unique_ptr<grpc_api::GetVpdFieldRequest> request,
                   const GetVpdFieldCallback& callback);
  void GetDriveSystemData(
      std::unique_ptr<grpc_api::GetDriveSystemDataRequest> request,
      const GetDriveSystemDataCallback& callback);
  void RequestBluetoothDataNotification(
      std::unique_ptr<grpc_api::RequestBluetoothDataNotificationRequest>
          request,
      const RequestBluetoothDataNotificationCallback& callback);
  void GetStatefulPartitionAvailableCapacity(
      std::unique_ptr<grpc_api::GetStatefulPartitionAvailableCapacityRequest>
          request,
      const GetStatefulPartitionAvailableCapacityCallback& callback);

 private:
  void AddFileDump(
      SystemFilesService::File location,
      google::protobuf::RepeatedPtrField<grpc_api::FileDump>* file_dumps);
  void AddDirectoryDump(
      SystemFilesService::Directory location,
      google::protobuf::RepeatedPtrField<grpc_api::FileDump>* file_dumps);

  // Unowned. The delegate should outlive this instance.
  Delegate* const delegate_;

  std::unique_ptr<SystemFilesService> system_files_service_;

  std::unique_ptr<SystemInfoService> system_info_service_;

  // The file system root directory. Can be overridden in tests.
  base::FilePath root_dir_{"/"};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_GRPC_SERVICE_H_
