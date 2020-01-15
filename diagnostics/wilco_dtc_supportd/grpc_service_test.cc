// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iterator>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <base/strings/string_piece.h>
#include "base/strings/stringprintf.h"
#include "base/sys_info.h"
#include "base/time/time.h"
#include <gmock/gmock.h>
#include <google/protobuf/repeated_field.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/common/protobuf_test_utils.h"
#include "diagnostics/wilco_dtc_supportd/ec_constants.h"
#include "diagnostics/wilco_dtc_supportd/grpc_service.h"
#include "diagnostics/wilco_dtc_supportd/telemetry/fake_system_files_service.h"
#include "diagnostics/wilco_dtc_supportd/vpd_constants.h"

#include "wilco_dtc_supportd.pb.h"  // NOLINT(build/include)

using testing::_;
using testing::AnyOf;
using testing::ByRef;
using testing::Eq;
using testing::Invoke;
using testing::Return;
using testing::StrEq;
using testing::StrictMock;
using testing::UnorderedElementsAre;
using testing::WithArgs;

namespace diagnostics {

namespace {

using DelegateWebRequestHttpMethod =
    GrpcService::Delegate::WebRequestHttpMethod;
using DelegateWebRequestStatus = GrpcService::Delegate::WebRequestStatus;
using DelegateDriveSystemDataType = GrpcService::Delegate::DriveSystemDataType;

constexpr char kFakeFileContentsChars[] = "\0fake Æ row 1\nfake row 2\n\0\377";

constexpr int kHttpStatusOk = 200;
constexpr char kBadNonHttpsUrl[] = "Http://www.google.com";
constexpr char kCorrectUrl[] = "hTTps://www.google.com";
constexpr char kFakeWebResponseBody[] = "\0Fake WEB\n response body\n\0";
const DelegateWebRequestHttpMethod kDelegateWebRequestHttpMethodGet =
    DelegateWebRequestHttpMethod::kGet;
const DelegateWebRequestHttpMethod kDelegateWebRequestHttpMethodHead =
    DelegateWebRequestHttpMethod::kHead;
const DelegateWebRequestHttpMethod kDelegateWebRequestHttpMethodPost =
    DelegateWebRequestHttpMethod::kPost;
const DelegateWebRequestHttpMethod kDelegateWebRequestHttpMethodPut =
    DelegateWebRequestHttpMethod::kPut;

constexpr grpc_api::DiagnosticRoutine kFakeAvailableRoutines[] = {
    grpc_api::ROUTINE_BATTERY,
    grpc_api::ROUTINE_BATTERY_SYSFS,
    grpc_api::ROUTINE_SMARTCTL_CHECK,
    grpc_api::ROUTINE_URANDOM,
    grpc_api::ROUTINE_FLOATING_POINT_ACCURACY,
    grpc_api::ROUTINE_NVME_WEAR_LEVEL};
constexpr int kFakeUuid = 13;
constexpr grpc_api::DiagnosticRoutineStatus kFakeStatus =
    grpc_api::ROUTINE_STATUS_RUNNING;
constexpr int kFakeProgressPercent = 37;
constexpr grpc_api::DiagnosticRoutineUserMessage kFakeUserMessage =
    grpc_api::ROUTINE_USER_MESSAGE_UNSET;
constexpr char kFakeOutput[] = "Some output.";
constexpr char kFakeStatusMessage[] = "Status message.";
constexpr char kFakeVpdValue[] = "fakeserialnumber";

constexpr char kTestFilePath[] = "file/path";
constexpr char kTestCanonicalFilePath[] = "canonical/path";

std::string FakeFileContents() {
  return std::string(std::begin(kFakeFileContentsChars),
                     std::end(kFakeFileContentsChars));
}

template <class T>
base::Callback<void(std::unique_ptr<T>)> GrpcCallbackResponseSaver(
    std::unique_ptr<T>* response) {
  return base::Bind(
      [](std::unique_ptr<T>* response, std::unique_ptr<T> received_response) {
        *response = std::move(received_response);
        ASSERT_TRUE(*response);
      },
      base::Unretained(response));
}

std::unique_ptr<grpc_api::GetEcTelemetryResponse> MakeGetEcTelemetryResponse(
    grpc_api::GetEcTelemetryResponse::Status status,
    const std::string& payload) {
  auto response = std::make_unique<grpc_api::GetEcTelemetryResponse>();
  response->set_status(status);
  response->set_payload(payload);
  return response;
}

std::unique_ptr<grpc_api::PerformWebRequestResponse>
MakePerformWebRequestResponse(
    grpc_api::PerformWebRequestResponse::Status status,
    const int* http_status,
    const char* response_body) {
  auto response = std::make_unique<grpc_api::PerformWebRequestResponse>();
  response->set_status(status);
  if (http_status)
    response->set_http_status(*http_status);
  if (response_body)
    response->set_response_body(response_body);
  return response;
}

std::unique_ptr<grpc_api::GetAvailableRoutinesResponse>
MakeGetAvailableRoutinesResponse() {
  auto response = std::make_unique<grpc_api::GetAvailableRoutinesResponse>();
  for (auto routine : kFakeAvailableRoutines)
    response->add_routines(routine);
  response->set_service_status(grpc_api::ROUTINE_SERVICE_STATUS_OK);
  return response;
}

std::unique_ptr<grpc_api::RunRoutineResponse> MakeRunRoutineResponse() {
  auto response = std::make_unique<grpc_api::RunRoutineResponse>();
  response->set_uuid(kFakeUuid);
  response->set_status(kFakeStatus);
  response->set_service_status(grpc_api::ROUTINE_SERVICE_STATUS_OK);
  return response;
}

std::unique_ptr<grpc_api::GetRoutineUpdateResponse>
MakeGetRoutineUpdateResponse(int uuid, bool include_output) {
  auto response = std::make_unique<grpc_api::GetRoutineUpdateResponse>();
  response->set_uuid(uuid);
  response->set_status(kFakeStatus);
  response->set_progress_percent(kFakeProgressPercent);
  response->set_user_message(kFakeUserMessage);
  response->set_output(include_output ? kFakeOutput : "");
  response->set_status_message(kFakeStatusMessage);
  response->set_service_status(grpc_api::ROUTINE_SERVICE_STATUS_OK);
  return response;
}

std::unique_ptr<grpc_api::RunRoutineRequest> MakeRunBatteryRoutineRequest() {
  constexpr int kLowmAh = 10;
  constexpr int kHighmAh = 100;
  auto request = std::make_unique<grpc_api::RunRoutineRequest>();
  request->set_routine(grpc_api::ROUTINE_BATTERY);
  request->mutable_battery_params()->set_low_mah(kLowmAh);
  request->mutable_battery_params()->set_high_mah(kHighmAh);
  return request;
}

std::unique_ptr<grpc_api::RunRoutineRequest>
MakeRunBatterySysfsRoutineRequest() {
  constexpr int kMaximumCycleCount = 5;
  constexpr int kPercentBatteryWearAllowed = 10;
  auto request = std::make_unique<grpc_api::RunRoutineRequest>();
  request->set_routine(grpc_api::ROUTINE_BATTERY_SYSFS);
  request->mutable_battery_sysfs_params()->set_maximum_cycle_count(
      kMaximumCycleCount);
  request->mutable_battery_sysfs_params()->set_percent_battery_wear_allowed(
      kPercentBatteryWearAllowed);
  return request;
}

std::unique_ptr<grpc_api::RunRoutineRequest> MakeRunUrandomRoutineRequest() {
  constexpr int kLengthSeconds = 10;
  auto request = std::make_unique<grpc_api::RunRoutineRequest>();
  request->set_routine(grpc_api::ROUTINE_URANDOM);
  request->mutable_urandom_params()->set_length_seconds(kLengthSeconds);
  return request;
}

std::unique_ptr<grpc_api::RunRoutineRequest>
MakeRunFloatingPointAccuracyRoutineRequest() {
  constexpr int kLengthSeconds = 10;
  auto request = std::make_unique<grpc_api::RunRoutineRequest>();
  request->set_routine(grpc_api::ROUTINE_FLOATING_POINT_ACCURACY);
  request->mutable_floating_point_accuracy_params()->set_length_seconds(
      kLengthSeconds);
  return request;
}

std::unique_ptr<grpc_api::RunRoutineRequest>
MakeRunNvmeWearLevelRoutineRequest() {
  constexpr int kWearLevelThreshold = 50;
  auto request = std::make_unique<grpc_api::RunRoutineRequest>();
  request->set_routine(grpc_api::ROUTINE_NVME_WEAR_LEVEL);
  request->mutable_nvme_wear_level_params()->set_wear_level_threshold(
      kWearLevelThreshold);
  return request;
}

MATCHER_P(GrpcDumpsEquivalentWithInternal, expected, "") {
  if (arg.size() != expected.get().size())
    return false;

  for (int i = 0; i < arg.size(); i++) {
    if (arg[i].contents() != expected.get()[i]->contents ||
        arg[i].canonical_path() != expected.get()[i]->canonical_path.value() ||
        arg[i].path() != expected.get()[i]->path.value())
      return false;
  }

  return true;
}

class MockGrpcServiceDelegate : public GrpcService::Delegate {
 public:
  // GrpcService::Delegate overrides:
  MOCK_METHOD(void,
              SendWilcoDtcMessageToUi,
              (const std::string&, const SendMessageToUiCallback&),
              (override));
  MOCK_METHOD(void,
              PerformWebRequestToBrowser,
              (WebRequestHttpMethod,
               const std::string&,
               const std::vector<std::string>&,
               const std::string&,
               const PerformWebRequestToBrowserCallback&),
              (override));
  MOCK_METHOD(void,
              GetAvailableRoutinesToService,
              (const GetAvailableRoutinesToServiceCallback&),
              (override));
  MOCK_METHOD(void,
              RunRoutineToService,
              (const grpc_api::RunRoutineRequest&,
               const RunRoutineToServiceCallback&),
              (override));
  MOCK_METHOD(void,
              GetRoutineUpdateRequestToService,
              (const int,
               const grpc_api::GetRoutineUpdateRequest::Command,
               const bool,
               const GetRoutineUpdateRequestToServiceCallback&),
              (override));
  MOCK_METHOD(void,
              GetConfigurationDataFromBrowser,
              (const GetConfigurationDataFromBrowserCallback&),
              (override));
  MOCK_METHOD(void,
              GetDriveSystemData,
              (DriveSystemDataType, const GetDriveSystemDataCallback&),
              (override));
  MOCK_METHOD(void, RequestBluetoothDataNotification, (), (override));
};

// Tests for the GrpcService class.
class GrpcServiceTest : public testing::Test {
 protected:
  GrpcServiceTest() = default;

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    service_.set_root_dir_for_testing(temp_dir_.GetPath());
  }

  GrpcService* service() { return &service_; }

  StrictMock<MockGrpcServiceDelegate>* delegate() { return &delegate_; }

  void ExecuteSendMessageToUi(
      const std::string& json_message,
      std::unique_ptr<grpc_api::SendMessageToUiResponse>* response) {
    auto request = std::make_unique<grpc_api::SendMessageToUiRequest>();
    request->set_json_message(json_message);
    EXPECT_CALL(delegate_, SendWilcoDtcMessageToUi(json_message, _))
        .WillOnce(WithArgs<1>(Invoke(
            [json_message](
                const base::Callback<void(base::StringPiece)>& callback) {
              callback.Run(json_message);
            })));
    service()->SendMessageToUi(std::move(request),
                               GrpcCallbackResponseSaver(response));
  }

  void ExecuteGetProcData(grpc_api::GetProcDataRequest::Type request_type,
                          std::vector<grpc_api::FileDump>* file_dumps) {
    auto request = std::make_unique<grpc_api::GetProcDataRequest>();
    request->set_type(request_type);
    std::unique_ptr<grpc_api::GetProcDataResponse> response;
    service()->GetProcData(std::move(request),
                           GrpcCallbackResponseSaver(&response));

    // Expect the method to return immediately.
    ASSERT_TRUE(response);
    file_dumps->assign(response->file_dump().begin(),
                       response->file_dump().end());
  }

  void ExecuteGetSysfsData(grpc_api::GetSysfsDataRequest::Type request_type,
                           std::vector<grpc_api::FileDump>* file_dumps) {
    auto request = std::make_unique<grpc_api::GetSysfsDataRequest>();
    request->set_type(request_type);
    std::unique_ptr<grpc_api::GetSysfsDataResponse> response;
    service()->GetSysfsData(std::move(request),
                            GrpcCallbackResponseSaver(&response));

    // Expect the method to return immediately.
    ASSERT_TRUE(response);
    file_dumps->assign(response->file_dump().begin(),
                       response->file_dump().end());
  }

  void ExecuteGetEcTelemetry(
      const std::string request_payload,
      std::unique_ptr<grpc_api::GetEcTelemetryResponse>* response) {
    auto request = std::make_unique<grpc_api::GetEcTelemetryRequest>();
    request->set_payload(request_payload);

    service()->GetEcTelemetry(std::move(request),
                              GrpcCallbackResponseSaver(response));
    ASSERT_TRUE(*response);
  }

  void ExecutePerformWebRequest(
      grpc_api::PerformWebRequestParameter::HttpMethod http_method,
      const std::string& url,
      const std::vector<std::string>& string_headers,
      const std::string& request_body,
      const DelegateWebRequestHttpMethod* delegate_http_method,
      std::unique_ptr<grpc_api::PerformWebRequestResponse>* response) {
    auto request = std::make_unique<grpc_api::PerformWebRequestParameter>();
    request->set_http_method(http_method);
    request->set_url(url);

    google::protobuf::RepeatedPtrField<std::string> headers(
        string_headers.begin(), string_headers.end());
    request->mutable_headers()->Swap(&headers);

    request->set_request_body(request_body);

    base::Callback<void(DelegateWebRequestStatus, int)> callback;
    if (delegate_http_method) {
      EXPECT_CALL(delegate_,
                  PerformWebRequestToBrowser(Eq(*delegate_http_method), url,
                                             string_headers, request_body, _))
          .WillOnce(WithArgs<4>(Invoke(
              [](const base::Callback<void(DelegateWebRequestStatus, int,
                                           base::StringPiece)>& callback) {
                callback.Run(DelegateWebRequestStatus::kOk, kHttpStatusOk,
                             kFakeWebResponseBody);
              })));
    }
    service()->PerformWebRequest(std::move(request),
                                 GrpcCallbackResponseSaver(response));
  }

  void ExecuteGetAvailableRoutines(
      std::unique_ptr<grpc_api::GetAvailableRoutinesResponse>* response) {
    auto request = std::make_unique<grpc_api::GetAvailableRoutinesRequest>();
    EXPECT_CALL(delegate_, GetAvailableRoutinesToService(_))
        .WillOnce(Invoke([](const base::Callback<void(
                                const std::vector<grpc_api::DiagnosticRoutine>&,
                                grpc_api::RoutineServiceStatus)>& callback) {
          callback.Run(std::vector<grpc_api::DiagnosticRoutine>(
                           std::begin(kFakeAvailableRoutines),
                           std::end(kFakeAvailableRoutines)),
                       grpc_api::ROUTINE_SERVICE_STATUS_OK);
        }));
    service()->GetAvailableRoutines(std::move(request),
                                    GrpcCallbackResponseSaver(response));
  }

  void ExecuteRunRoutine(
      std::unique_ptr<grpc_api::RunRoutineRequest> request,
      std::unique_ptr<grpc_api::RunRoutineResponse>* response,
      bool is_valid_request) {
    if (is_valid_request) {
      EXPECT_CALL(delegate_, RunRoutineToService(_, _))
          .WillOnce(WithArgs<1>(
              Invoke([](const base::Callback<void(
                            int, grpc_api::DiagnosticRoutineStatus,
                            grpc_api::RoutineServiceStatus)>& callback) {
                callback.Run(kFakeUuid, kFakeStatus,
                             grpc_api::ROUTINE_SERVICE_STATUS_OK);
              })));
    }
    service()->RunRoutine(std::move(request),
                          GrpcCallbackResponseSaver(response));
  }

  void ExecuteGetRoutineUpdate(
      int uuid,
      grpc_api::GetRoutineUpdateRequest::Command command,
      bool include_output,
      std::unique_ptr<grpc_api::GetRoutineUpdateResponse>* response) {
    if (command != grpc_api::GetRoutineUpdateRequest::COMMAND_UNSET) {
      EXPECT_CALL(delegate_, GetRoutineUpdateRequestToService(
                                 uuid, command, include_output, _))
          .WillOnce(WithArgs<3>(
              Invoke([=](const base::Callback<void(
                             int, grpc_api::DiagnosticRoutineStatus, int,
                             grpc_api::DiagnosticRoutineUserMessage,
                             const std::string&, const std::string&,
                             grpc_api::RoutineServiceStatus)>& callback) {
                callback.Run(
                    uuid, kFakeStatus, kFakeProgressPercent, kFakeUserMessage,
                    include_output ? kFakeOutput : "", kFakeStatusMessage,
                    grpc_api::ROUTINE_SERVICE_STATUS_OK);
              })));
    }
    auto request = std::make_unique<grpc_api::GetRoutineUpdateRequest>();
    request->set_uuid(uuid);
    request->set_command(command);
    request->set_include_output(include_output);
    service()->GetRoutineUpdate(std::move(request),
                                GrpcCallbackResponseSaver(response));
  }

  void ExecuteGetOsVersion(
      std::unique_ptr<grpc_api::GetOsVersionResponse>* response) {
    auto request = std::make_unique<grpc_api::GetOsVersionRequest>();
    service()->GetOsVersion(std::move(request),
                            GrpcCallbackResponseSaver(response));
  }

  void ExecuteGetConfigurationData(
      const std::string& json_configuration_data,
      std::unique_ptr<grpc_api::GetConfigurationDataResponse>* response) {
    auto request = std::make_unique<grpc_api::GetConfigurationDataRequest>();
    EXPECT_CALL(delegate_, GetConfigurationDataFromBrowser(_))
        .WillOnce(WithArgs<0>(Invoke(
            [json_configuration_data](
                const base::Callback<void(const std::string&)>& callback) {
              callback.Run(json_configuration_data);
            })));
    service()->GetConfigurationData(std::move(request),
                                    GrpcCallbackResponseSaver(response));
  }

  void ExecuteGetVpdField(grpc_api::GetVpdFieldRequest::VpdField vpd_field,
                          grpc_api::GetVpdFieldResponse::Status* status,
                          std::string* vpd_field_value) {
    auto request = std::make_unique<grpc_api::GetVpdFieldRequest>();
    request->set_vpd_field(vpd_field);
    std::unique_ptr<grpc_api::GetVpdFieldResponse> response;
    service()->GetVpdField(std::move(request),
                           GrpcCallbackResponseSaver(&response));

    // Expect the method to return immediately.
    ASSERT_TRUE(response);
    *status = response->status();
    *vpd_field_value = response->vpd_field_value();
  }

  grpc_api::FileDump MakeFileDump(
      const base::FilePath& relative_file_path,
      const base::FilePath& canonical_relative_file_path,
      const std::string& file_contents) const {
    grpc_api::FileDump file_dump;
    file_dump.set_path(temp_dir_.GetPath().Append(relative_file_path).value());
    file_dump.set_canonical_path(
        temp_dir_.GetPath().Append(canonical_relative_file_path).value());
    file_dump.set_contents(file_contents);
    return file_dump;
  }

  base::FilePath temp_dir_path() const { return temp_dir_.GetPath(); }

 private:
  base::MessageLoop message_loop_;
  base::ScopedTempDir temp_dir_;
  StrictMock<MockGrpcServiceDelegate> delegate_;
  GrpcService service_{&delegate_};
};

TEST_F(GrpcServiceTest, SendMessageToUi) {
  constexpr char kFakeJsonMessage[] = "Fake Message From Wilco DTC to UI";
  std::unique_ptr<grpc_api::SendMessageToUiResponse> response;
  ExecuteSendMessageToUi(kFakeJsonMessage, &response);
  ASSERT_TRUE(response);
  EXPECT_EQ(response->response_json_message(), kFakeJsonMessage);
}

TEST_F(GrpcServiceTest, GetProcDataUnsetType) {
  std::vector<grpc_api::FileDump> file_dumps;
  ExecuteGetProcData(grpc_api::GetProcDataRequest::TYPE_UNSET, &file_dumps);

  EXPECT_TRUE(file_dumps.empty())
      << "Obtained: "
      << GetProtosRangeDebugString(file_dumps.begin(), file_dumps.end());
}

TEST_F(GrpcServiceTest, GetSysfsDataUnsetType) {
  std::vector<grpc_api::FileDump> file_dumps;
  ExecuteGetSysfsData(grpc_api::GetSysfsDataRequest::TYPE_UNSET, &file_dumps);

  EXPECT_TRUE(file_dumps.empty())
      << "Obtained: "
      << GetProtosRangeDebugString(file_dumps.begin(), file_dumps.end());
}

TEST_F(GrpcServiceTest, RunRoutineUnsetType) {
  auto request = std::make_unique<grpc_api::RunRoutineRequest>();
  request->set_routine(grpc_api::ROUTINE_UNSET);
  auto response = std::make_unique<grpc_api::RunRoutineResponse>();
  ExecuteRunRoutine(std::move(request), &response,
                    false /* is_valid_request */);
  EXPECT_EQ(response->uuid(), 0);
  EXPECT_EQ(response->status(), grpc_api::ROUTINE_STATUS_INVALID_FIELD);
}

TEST_F(GrpcServiceTest, GetRoutineUpdateUnsetType) {
  std::unique_ptr<grpc_api::GetRoutineUpdateResponse> response;
  constexpr bool kIncludeOutput = false;
  ExecuteGetRoutineUpdate(kFakeUuid,
                          grpc_api::GetRoutineUpdateRequest::COMMAND_UNSET,
                          kIncludeOutput, &response);
  ASSERT_TRUE(response);
  EXPECT_EQ(response->uuid(), kFakeUuid);
  EXPECT_EQ(response->status(), grpc_api::ROUTINE_STATUS_INVALID_FIELD);
}

// Test that GetEcTelemetry() response contains expected |status| and |payload|
// field values.
TEST_F(GrpcServiceTest, GetEcTelemetryErrorAccessingDriver) {
  std::unique_ptr<grpc_api::GetEcTelemetryResponse> response;
  ExecuteGetEcTelemetry(FakeFileContents(), &response);
  ASSERT_TRUE(response);
  auto expected_response = MakeGetEcTelemetryResponse(
      grpc_api::GetEcTelemetryResponse::STATUS_ERROR_ACCESSING_DRIVER, "");
  EXPECT_THAT(*response, ProtobufEquals(*expected_response))
      << "Actual response: {" << response->ShortDebugString() << "}";
}

// Test that GetAvailableRoutines returns the expected list of diagnostic
// routines.
TEST_F(GrpcServiceTest, GetAvailableRoutines) {
  std::unique_ptr<grpc_api::GetAvailableRoutinesResponse> response;
  ExecuteGetAvailableRoutines(&response);
  auto expected_response = MakeGetAvailableRoutinesResponse();
  EXPECT_THAT(*response, ProtobufEquals(*expected_response))
      << "Actual response: {" << response->ShortDebugString() << "}";
}

// Test that we can request that the battery routine be run.
TEST_F(GrpcServiceTest, RunBatteryRoutine) {
  std::unique_ptr<grpc_api::RunRoutineResponse> response;
  ExecuteRunRoutine(MakeRunBatteryRoutineRequest(), &response,
                    true /* is_valid_request */);
  auto expected_response = MakeRunRoutineResponse();
  EXPECT_THAT(*response, ProtobufEquals(*expected_response))
      << "Actual response: {" << response->ShortDebugString() << "}";
}

// Test that a battery routine with no parameters will fail.
TEST_F(GrpcServiceTest, RunBatteryRoutineNoParameters) {
  std::unique_ptr<grpc_api::RunRoutineResponse> response;
  auto request = std::make_unique<grpc_api::RunRoutineRequest>();
  request->set_routine(grpc_api::ROUTINE_BATTERY);
  ExecuteRunRoutine(std::move(request), &response,
                    false /* is_valid_request */);
  EXPECT_EQ(response->uuid(), 0);
  EXPECT_EQ(response->status(), grpc_api::ROUTINE_STATUS_INVALID_FIELD);
}

// Test that we can request that the battery_sysfs routine be run.
TEST_F(GrpcServiceTest, RunBatterySysfsRoutine) {
  std::unique_ptr<grpc_api::RunRoutineResponse> response;
  ExecuteRunRoutine(MakeRunBatterySysfsRoutineRequest(), &response,
                    true /* is_valid_request */);
  auto expected_response = MakeRunRoutineResponse();
  EXPECT_THAT(*response, ProtobufEquals(*expected_response))
      << "Actual response: {" << response->ShortDebugString() << "}";
}

// Test that a battery_sysfs routine with no parameters will fail.
TEST_F(GrpcServiceTest, RunBatterySysfsRoutineNoParameters) {
  std::unique_ptr<grpc_api::RunRoutineResponse> response;
  auto request = std::make_unique<grpc_api::RunRoutineRequest>();
  request->set_routine(grpc_api::ROUTINE_BATTERY_SYSFS);
  ExecuteRunRoutine(std::move(request), &response,
                    false /* is_valid_request */);
  EXPECT_EQ(response->uuid(), 0);
  EXPECT_EQ(response->status(), grpc_api::ROUTINE_STATUS_INVALID_FIELD);
}

// Test that we can request that the urandom routine be run.
TEST_F(GrpcServiceTest, RunUrandomRoutine) {
  std::unique_ptr<grpc_api::RunRoutineResponse> response;
  ExecuteRunRoutine(MakeRunUrandomRoutineRequest(), &response,
                    true /* is_valid_request */);
  auto expected_response = MakeRunRoutineResponse();
  EXPECT_THAT(*response, ProtobufEquals(*expected_response))
      << "Actual response: {" << response->ShortDebugString() << "}";
}

// Test that a urandom routine with no parameters will fail.
TEST_F(GrpcServiceTest, RunUrandomRoutineNoParameters) {
  std::unique_ptr<grpc_api::RunRoutineResponse> response;
  auto request = std::make_unique<grpc_api::RunRoutineRequest>();
  request->set_routine(grpc_api::ROUTINE_URANDOM);
  ExecuteRunRoutine(std::move(request), &response,
                    false /* is_valid_request */);
  EXPECT_EQ(response->uuid(), 0);
  EXPECT_EQ(response->status(), grpc_api::ROUTINE_STATUS_INVALID_FIELD);
}

// Test that we can request that the floating_point_accuracy routine be run.
TEST_F(GrpcServiceTest, RunFloatingPointAccuracyRoutine) {
  std::unique_ptr<grpc_api::RunRoutineResponse> response;
  ExecuteRunRoutine(MakeRunFloatingPointAccuracyRoutineRequest(), &response,
                    true /* is_valid_request */);
  auto expected_response = MakeRunRoutineResponse();
  EXPECT_THAT(*response, ProtobufEquals(*expected_response))
      << "Actual response: {" << response->ShortDebugString() << "}";
}

// Test that we can request that the nvme_wear_level routine be run.
TEST_F(GrpcServiceTest, RunNvmeWearLevelRoutine) {
  std::unique_ptr<grpc_api::RunRoutineResponse> response;
  ExecuteRunRoutine(MakeRunNvmeWearLevelRoutineRequest(), &response,
                    true /* is_valid_request */);
  auto expected_response = MakeRunRoutineResponse();
  EXPECT_THAT(*response, ProtobufEquals(*expected_response))
      << "Actual response: {" << response->ShortDebugString() << "}";
}

// Test that a nvme_wear_level routine with no parameters will fail.
TEST_F(GrpcServiceTest, RunNvmeWearLevelRoutineNoParameters) {
  std::unique_ptr<grpc_api::RunRoutineResponse> response;
  auto request = std::make_unique<grpc_api::RunRoutineRequest>();
  request->set_routine(grpc_api::ROUTINE_NVME_WEAR_LEVEL);
  ExecuteRunRoutine(std::move(request), &response,
                    false /* is_valid_request */);
  EXPECT_EQ(response->uuid(), 0);
  EXPECT_EQ(response->status(), grpc_api::ROUTINE_STATUS_INVALID_FIELD);
}

TEST_F(GrpcServiceTest, GetOsVersionUnset) {
  std::unique_ptr<grpc_api::GetOsVersionResponse> response;
  base::SysInfo::SetChromeOSVersionInfoForTest("", base::Time());
  ExecuteGetOsVersion(&response);

  ASSERT_TRUE(response);

  EXPECT_TRUE(response->version().empty());
  EXPECT_EQ(response->milestone(), 0);
}

TEST_F(GrpcServiceTest, GetOsVersion) {
  constexpr char kLsbRelease[] =
      "CHROMEOS_RELEASE_CHROME_MILESTONE=%d\n"
      "CHROMEOS_RELEASE_VERSION=%s";
  constexpr int kMilestone = 75;
  constexpr char kOsVersion[] = "11932.0.2019_03_20_1100";

  std::unique_ptr<grpc_api::GetOsVersionResponse> response;
  base::SysInfo::SetChromeOSVersionInfoForTest(
      base::StringPrintf(kLsbRelease, kMilestone, kOsVersion), base::Time());

  ExecuteGetOsVersion(&response);

  ASSERT_TRUE(response);

  EXPECT_EQ(response->version(), kOsVersion);
  EXPECT_EQ(response->milestone(), kMilestone);
}

// Test that an empty string is a valid result.
TEST_F(GrpcServiceTest, GetConfigurationDataEmpty) {
  std::unique_ptr<grpc_api::GetConfigurationDataResponse> response;
  ExecuteGetConfigurationData("", &response);
  EXPECT_EQ(response->json_configuration_data(), "");
}

TEST_F(GrpcServiceTest, GetConfigurationData) {
  // The JSON configuration data is passed through from the cloud to DTC binary
  // and might not be in JSON format.
  constexpr char kFakeJsonConfigurationData[] = "Fake JSON Configuration Data";
  std::unique_ptr<grpc_api::GetConfigurationDataResponse> response;
  ExecuteGetConfigurationData(kFakeJsonConfigurationData, &response);
  EXPECT_EQ(response->json_configuration_data(), kFakeJsonConfigurationData);
}

TEST_F(GrpcServiceTest, GetVpdFieldUnset) {
  grpc_api::GetVpdFieldResponse::Status status;
  std::string vpd_field_value;
  ASSERT_NO_FATAL_FAILURE(ExecuteGetVpdField(
      grpc_api::GetVpdFieldRequest::FIELD_UNSET, &status, &vpd_field_value));
  EXPECT_EQ(status,
            grpc_api::GetVpdFieldResponse::STATUS_ERROR_VPD_FIELD_UNKNOWN);
  EXPECT_TRUE(vpd_field_value.empty());
}

TEST_F(GrpcServiceTest, GetDriveSystemDataTypeUnknown) {
  auto request = std::make_unique<grpc_api::GetDriveSystemDataRequest>();
  std::unique_ptr<grpc_api::GetDriveSystemDataResponse> response;
  service()->GetDriveSystemData(std::move(request),
                                GrpcCallbackResponseSaver(&response));

  auto expected_response =
      std::make_unique<grpc_api::GetDriveSystemDataResponse>();
  expected_response->set_status(
      grpc_api::GetDriveSystemDataResponse::STATUS_ERROR_REQUEST_TYPE_UNKNOWN);
  EXPECT_THAT(*response, ProtobufEquals(*expected_response))
      << "Actual response: {" << response->ShortDebugString() << "}";
}

TEST_F(GrpcServiceTest, GetDriveSystemDataInternalError) {
  auto request = std::make_unique<grpc_api::GetDriveSystemDataRequest>();
  request->set_type(grpc_api::GetDriveSystemDataRequest::SMART_ATTRIBUTES);
  EXPECT_CALL(*delegate(), GetDriveSystemData(_, _))
      .WillOnce(WithArgs<1>(
          Invoke([](const base::Callback<void(const std::string& payload,
                                              bool success)>& callback) {
            callback.Run("", false /* success */);
          })));

  std::unique_ptr<grpc_api::GetDriveSystemDataResponse> response;
  service()->GetDriveSystemData(std::move(request),
                                GrpcCallbackResponseSaver(&response));

  auto expected_response =
      std::make_unique<grpc_api::GetDriveSystemDataResponse>();
  expected_response->set_status(
      grpc_api::GetDriveSystemDataResponse::STATUS_ERROR_REQUEST_PROCESSING);
  EXPECT_THAT(*response, ProtobufEquals(*expected_response))
      << "Actual response: {" << response->ShortDebugString() << "}";
}

TEST_F(GrpcServiceTest, RequestBluetoothDataNotification) {
  auto request =
      std::make_unique<grpc_api::RequestBluetoothDataNotificationRequest>();

  EXPECT_CALL(*delegate(), RequestBluetoothDataNotification());

  base::RunLoop run_loop;
  service()->RequestBluetoothDataNotification(
      std::move(request),
      base::Bind(
          [](base::Closure callback,
             std::unique_ptr<
                 grpc_api::RequestBluetoothDataNotificationResponse>) {
            callback.Run();
          },
          run_loop.QuitClosure()));

  run_loop.Run();
}

class GrpcServiceWithFakeSystemFilesServiceTest : public GrpcServiceTest {
 public:
  void SetUp() override {
    GrpcServiceTest::SetUp();

    auto fake = std::make_unique<FakeSystemFilesService>();
    system_files_service_fake_ = fake.get();
    service()->set_system_files_service_for_testing(std::move(fake));
  }

 protected:
  // Owned by |service_| from GrpcServiceTest
  FakeSystemFilesService* system_files_service_fake_ = nullptr;
};

TEST_F(GrpcServiceWithFakeSystemFilesServiceTest,
       DirectoryAcpiButtonSingleFile) {
  std::vector<std::unique_ptr<SystemFilesService::FileDump>> directory_dump;
  auto single_dump = std::make_unique<SystemFilesService::FileDump>();
  single_dump->contents = FakeFileContents();
  single_dump->path = base::FilePath(kTestFilePath);
  single_dump->canonical_path = base::FilePath(kTestCanonicalFilePath);
  directory_dump.push_back(std::move(single_dump));

  system_files_service_fake_->set_directory_dump(
      SystemFilesService::Directory::kProcAcpiButton, directory_dump);

  std::vector<grpc_api::FileDump> file_dumps;
  ExecuteGetProcData(grpc_api::GetProcDataRequest::DIRECTORY_ACPI_BUTTON,
                     &file_dumps);

  EXPECT_THAT(file_dumps,
              GrpcDumpsEquivalentWithInternal(ByRef(directory_dump)));

  EXPECT_EQ(system_files_service_fake_->get_dumped_files().size(), 0);

  ASSERT_EQ(system_files_service_fake_->get_dumped_directories().size(), 1);
  EXPECT_EQ(system_files_service_fake_->get_dumped_directories()[0],
            SystemFilesService::Directory::kProcAcpiButton);
}

TEST_F(GrpcServiceWithFakeSystemFilesServiceTest,
       DirectoryAcpiButtonMultiFile) {
  std::vector<std::unique_ptr<SystemFilesService::FileDump>> directory_dump;
  auto first_dump = std::make_unique<SystemFilesService::FileDump>();
  first_dump->contents = FakeFileContents();
  first_dump->path = base::FilePath(kTestFilePath);
  first_dump->canonical_path = base::FilePath(kTestCanonicalFilePath);
  directory_dump.push_back(std::move(first_dump));

  auto second_dump = std::make_unique<SystemFilesService::FileDump>();
  second_dump->contents = FakeFileContents() + "file_2";
  second_dump->path = base::FilePath(kTestFilePath).Append("2");
  second_dump->canonical_path =
      base::FilePath(kTestCanonicalFilePath).Append("3");
  directory_dump.push_back(std::move(second_dump));

  system_files_service_fake_->set_directory_dump(
      SystemFilesService::Directory::kProcAcpiButton, directory_dump);

  std::vector<grpc_api::FileDump> file_dumps;
  ExecuteGetProcData(grpc_api::GetProcDataRequest::DIRECTORY_ACPI_BUTTON,
                     &file_dumps);

  EXPECT_THAT(file_dumps,
              GrpcDumpsEquivalentWithInternal(ByRef(directory_dump)));

  EXPECT_EQ(system_files_service_fake_->get_dumped_files().size(), 0);

  ASSERT_EQ(system_files_service_fake_->get_dumped_directories().size(), 1);
  EXPECT_EQ(system_files_service_fake_->get_dumped_directories()[0],
            SystemFilesService::Directory::kProcAcpiButton);
}

TEST_F(GrpcServiceWithFakeSystemFilesServiceTest, DirectoryAcpiButtonEmpty) {
  system_files_service_fake_->set_directory_dump(
      SystemFilesService::Directory::kProcAcpiButton,
      std::vector<std::unique_ptr<SystemFilesService::FileDump>>());

  std::vector<grpc_api::FileDump> file_dumps;
  ExecuteGetProcData(grpc_api::GetProcDataRequest::DIRECTORY_ACPI_BUTTON,
                     &file_dumps);

  EXPECT_EQ(file_dumps.size(), 0);

  EXPECT_EQ(system_files_service_fake_->get_dumped_files().size(), 0);

  ASSERT_EQ(system_files_service_fake_->get_dumped_directories().size(), 1);
  EXPECT_EQ(system_files_service_fake_->get_dumped_directories()[0],
            SystemFilesService::Directory::kProcAcpiButton);
}

TEST_F(GrpcServiceWithFakeSystemFilesServiceTest, DirectoryAcpiButtonMissing) {
  std::vector<grpc_api::FileDump> file_dumps;
  ExecuteGetProcData(grpc_api::GetProcDataRequest::DIRECTORY_ACPI_BUTTON,
                     &file_dumps);

  EXPECT_EQ(file_dumps.size(), 0);

  EXPECT_EQ(system_files_service_fake_->get_dumped_files().size(), 0);

  ASSERT_EQ(system_files_service_fake_->get_dumped_directories().size(), 1);
  EXPECT_EQ(system_files_service_fake_->get_dumped_directories()[0],
            SystemFilesService::Directory::kProcAcpiButton);
}

// Tests for the GetProcData() method of GrpcServiceTest when a
// single file is requested.
//
// This is a parameterized test with the following parameters:
// * |proc_data_request_type| - type of the GetProcData() request to be executed
//   (see GetProcDataRequest::Type);
// * |expected_location| - SystemFilesService::File that should be requested
class SingleProcFileGrpcServiceTest
    : public GrpcServiceWithFakeSystemFilesServiceTest,
      public testing::WithParamInterface<
          std::tuple<grpc_api::GetProcDataRequest::Type,
                     SystemFilesService::File>> {
 protected:
  // Accessors to individual test parameters from the test parameter tuple
  // returned by gtest's GetParam():

  grpc_api::GetProcDataRequest::Type proc_data_request_type() const {
    return std::get<0>(GetParam());
  }

  SystemFilesService::File expected_location() const {
    return std::get<1>(GetParam());
  }
};

// Test that GetProcData() returns a single item with the requested file data
// when the file exists.
TEST_P(SingleProcFileGrpcServiceTest, Success) {
  SystemFilesService::FileDump file_dump;
  file_dump.contents = FakeFileContents();
  file_dump.path = base::FilePath(kTestFilePath);
  file_dump.canonical_path = base::FilePath(kTestCanonicalFilePath);

  system_files_service_fake_->set_file_dump(expected_location(), file_dump);

  std::vector<grpc_api::FileDump> file_dumps;
  ExecuteGetProcData(proc_data_request_type(), &file_dumps);

  ASSERT_EQ(file_dumps.size(), 1);

  EXPECT_EQ(file_dumps[0].contents(), file_dump.contents);
  EXPECT_EQ(file_dumps[0].path(), file_dump.path.value());
  EXPECT_EQ(file_dumps[0].canonical_path(), file_dump.canonical_path.value());

  ASSERT_EQ(system_files_service_fake_->get_dumped_files().size(), 1);
  EXPECT_EQ(system_files_service_fake_->get_dumped_files()[0],
            expected_location());

  EXPECT_EQ(system_files_service_fake_->get_dumped_directories().size(), 0);
}

// Test that GetProcData() returns empty result when the file doesn't exist.
TEST_P(SingleProcFileGrpcServiceTest, NonExisting) {
  std::vector<grpc_api::FileDump> file_dumps;
  ExecuteGetProcData(proc_data_request_type(), &file_dumps);

  EXPECT_EQ(file_dumps.size(), 0);

  ASSERT_EQ(system_files_service_fake_->get_dumped_files().size(), 1);
  EXPECT_EQ(system_files_service_fake_->get_dumped_files()[0],
            expected_location());

  EXPECT_EQ(system_files_service_fake_->get_dumped_directories().size(), 0);
}

INSTANTIATE_TEST_CASE_P(
    ,
    SingleProcFileGrpcServiceTest,
    testing::Values(
        std::make_tuple(grpc_api::GetProcDataRequest::FILE_UPTIME,
                        SystemFilesService::File::kProcUptime),
        std::make_tuple(grpc_api::GetProcDataRequest::FILE_MEMINFO,
                        SystemFilesService::File::kProcMeminfo),
        std::make_tuple(grpc_api::GetProcDataRequest::FILE_LOADAVG,
                        SystemFilesService::File::kProcLoadavg),
        std::make_tuple(grpc_api::GetProcDataRequest::FILE_STAT,
                        SystemFilesService::File::kProcStat),
        std::make_tuple(grpc_api::GetProcDataRequest::FILE_NET_NETSTAT,
                        SystemFilesService::File::kProcNetNetstat),
        std::make_tuple(grpc_api::GetProcDataRequest::FILE_NET_DEV,
                        SystemFilesService::File::kProcNetDev),
        std::make_tuple(grpc_api::GetProcDataRequest::FILE_DISKSTATS,
                        SystemFilesService::File::kProcDiskstats),
        std::make_tuple(grpc_api::GetProcDataRequest::FILE_CPUINFO,
                        SystemFilesService::File::kProcCpuinfo),
        std::make_tuple(grpc_api::GetProcDataRequest::FILE_VMSTAT,
                        SystemFilesService::File::kProcVmstat)));

// Tests for the GetSysfsData() method of GrpcServiceTest when a
// directory is requested.
//
// This is a parameterized test with the following parameters:
// * |sysfs_data_request_type| - type of the GetSysfsData() request to be
//    executed (see GetSysfsDataRequest::Type);
// * |expected_location| - SystemFilesService::Directory that should be
//    requested
class SysfsDirectoryGrpcServiceTest
    : public GrpcServiceWithFakeSystemFilesServiceTest,
      public testing::WithParamInterface<
          std::tuple<grpc_api::GetSysfsDataRequest::Type,
                     SystemFilesService::Directory>> {
 protected:
  // Accessors to individual test parameters constructed from the test parameter
  // tuple returned by gtest's GetParam():
  grpc_api::GetSysfsDataRequest::Type sysfs_data_request_type() const {
    return std::get<0>(GetParam());
  }

  SystemFilesService::Directory expected_location() const {
    return std::get<1>(GetParam());
  }
};

// Test that GetSysfsData() returns a single file when called on a directory
// containing a single file.
TEST_P(SysfsDirectoryGrpcServiceTest, SingleFile) {
  std::vector<std::unique_ptr<SystemFilesService::FileDump>> directory_dump;
  auto single_dump = std::make_unique<SystemFilesService::FileDump>();
  single_dump->contents = FakeFileContents();
  single_dump->path = base::FilePath(kTestFilePath);
  single_dump->canonical_path = base::FilePath(kTestCanonicalFilePath);
  directory_dump.push_back(std::move(single_dump));

  system_files_service_fake_->set_directory_dump(expected_location(),
                                                 directory_dump);

  std::vector<grpc_api::FileDump> file_dumps;
  ExecuteGetSysfsData(sysfs_data_request_type(), &file_dumps);

  EXPECT_THAT(file_dumps,
              GrpcDumpsEquivalentWithInternal(ByRef(directory_dump)));

  EXPECT_EQ(system_files_service_fake_->get_dumped_files().size(), 0);

  ASSERT_EQ(system_files_service_fake_->get_dumped_directories().size(), 1);
  EXPECT_EQ(system_files_service_fake_->get_dumped_directories()[0],
            expected_location());
}

// Test that GetSysfsData() returns a multiple files when called on a directory
// containing multiple files.
TEST_P(SysfsDirectoryGrpcServiceTest, MultiFile) {
  std::vector<std::unique_ptr<SystemFilesService::FileDump>> directory_dump;
  auto first_dump = std::make_unique<SystemFilesService::FileDump>();
  first_dump->contents = FakeFileContents();
  first_dump->path = base::FilePath(kTestFilePath);
  first_dump->canonical_path = base::FilePath(kTestCanonicalFilePath);
  directory_dump.push_back(std::move(first_dump));

  auto second_dump = std::make_unique<SystemFilesService::FileDump>();
  second_dump->contents = FakeFileContents() + "file_2";
  second_dump->path = base::FilePath(kTestFilePath).Append("2");
  second_dump->canonical_path =
      base::FilePath(kTestCanonicalFilePath).Append("3");
  directory_dump.push_back(std::move(second_dump));

  system_files_service_fake_->set_directory_dump(expected_location(),
                                                 directory_dump);

  std::vector<grpc_api::FileDump> file_dumps;
  ExecuteGetSysfsData(sysfs_data_request_type(), &file_dumps);

  EXPECT_THAT(file_dumps,
              GrpcDumpsEquivalentWithInternal(ByRef(directory_dump)));

  EXPECT_EQ(system_files_service_fake_->get_dumped_files().size(), 0);

  ASSERT_EQ(system_files_service_fake_->get_dumped_directories().size(), 1);
  EXPECT_EQ(system_files_service_fake_->get_dumped_directories()[0],
            expected_location());
}

// Test that GetSysfsData() returns an empty result when the directory doesn't
// exist.
TEST_P(SysfsDirectoryGrpcServiceTest, NonExisting) {
  std::vector<grpc_api::FileDump> file_dumps;
  ExecuteGetSysfsData(sysfs_data_request_type(), &file_dumps);

  EXPECT_EQ(file_dumps.size(), 0);

  EXPECT_EQ(system_files_service_fake_->get_dumped_files().size(), 0);

  ASSERT_EQ(system_files_service_fake_->get_dumped_directories().size(), 1);
  EXPECT_EQ(system_files_service_fake_->get_dumped_directories()[0],
            expected_location());
}

// Test that GetSysfsData() returns an empty result when the directory is
// empty.
TEST_P(SysfsDirectoryGrpcServiceTest, Empty) {
  system_files_service_fake_->set_directory_dump(
      expected_location(),
      std::vector<std::unique_ptr<SystemFilesService::FileDump>>());

  std::vector<grpc_api::FileDump> file_dumps;
  ExecuteGetSysfsData(sysfs_data_request_type(), &file_dumps);

  EXPECT_EQ(file_dumps.size(), 0);

  EXPECT_EQ(system_files_service_fake_->get_dumped_files().size(), 0);

  ASSERT_EQ(system_files_service_fake_->get_dumped_directories().size(), 1);
  EXPECT_EQ(system_files_service_fake_->get_dumped_directories()[0],
            expected_location());
}

INSTANTIATE_TEST_CASE_P(
    ,
    SysfsDirectoryGrpcServiceTest,
    testing::Values(
        std::make_tuple(grpc_api::GetSysfsDataRequest::CLASS_HWMON,
                        SystemFilesService::Directory::kSysClassHwmon),
        std::make_tuple(grpc_api::GetSysfsDataRequest::CLASS_THERMAL,
                        SystemFilesService::Directory::kSysClassThermal),
        std::make_tuple(grpc_api::GetSysfsDataRequest::FIRMWARE_DMI_TABLES,
                        SystemFilesService::Directory::kSysFirmwareDmiTables),
        std::make_tuple(grpc_api::GetSysfsDataRequest::CLASS_POWER_SUPPLY,
                        SystemFilesService::Directory::kSysClassPowerSupply),
        std::make_tuple(grpc_api::GetSysfsDataRequest::CLASS_BACKLIGHT,
                        SystemFilesService::Directory::kSysClassBacklight),
        std::make_tuple(grpc_api::GetSysfsDataRequest::CLASS_NETWORK,
                        SystemFilesService::Directory::kSysClassNetwork),
        std::make_tuple(grpc_api::GetSysfsDataRequest::DEVICES_SYSTEM_CPU,
                        SystemFilesService::Directory::kSysDevicesSystemCpu)));

// Tests for the GetEcTelemetry() method of GrpcServiceTest.
//
// This is a parameterized test with the following parameters:
// * |request_payload| - payload of the GetEcTelemetry() request;
// * |expected_response_status| - expected GetEcTelemetry() response status;
// * |expected_response_payload| - expected GetEcTelemetry() response payload.
class GetEcTelemetryGrpcServiceTest
    : public GrpcServiceTest,
      public testing::WithParamInterface<
          std::tuple<std::string /* request_payload */,
                     grpc_api::GetEcTelemetryResponse::
                         Status /* expected_response_status */,
                     std::string /* expected_response_payload */>> {
 protected:
  std::string request_payload() const { return std::get<0>(GetParam()); }

  grpc_api::GetEcTelemetryResponse::Status expected_response_status() const {
    return std::get<1>(GetParam());
  }

  std::string expected_response_payload() const {
    return std::get<2>(GetParam());
  }

  base::FilePath devfs_telemetry_file() const {
    return temp_dir_path().Append(kEcGetTelemetryFilePath);
  }
};

// Test that GetEcTelemetry() response contains expected |status| and |payload|
// field values.
TEST_P(GetEcTelemetryGrpcServiceTest, Base) {
  // Write request and response payload because EC telemetry char device is
  // non-seekable.
  EXPECT_TRUE(WriteFileAndCreateParentDirs(
      devfs_telemetry_file(), request_payload() + expected_response_payload()));
  std::unique_ptr<grpc_api::GetEcTelemetryResponse> response;
  ExecuteGetEcTelemetry(request_payload(), &response);
  ASSERT_TRUE(response);
  auto expected_response = MakeGetEcTelemetryResponse(
      expected_response_status(), expected_response_payload());
  EXPECT_THAT(*response, ProtobufEquals(*expected_response))
      << "Actual response: {" << response->ShortDebugString() << "}";
}

INSTANTIATE_TEST_CASE_P(
    ,
    GetEcTelemetryGrpcServiceTest,
    testing::Values(
        std::make_tuple(FakeFileContents(),
                        grpc_api::GetEcTelemetryResponse::STATUS_OK,
                        FakeFileContents()),
        std::make_tuple(std::string("A", kEcGetTelemetryPayloadMaxSize),
                        grpc_api::GetEcTelemetryResponse::STATUS_OK,
                        std::string("B", kEcGetTelemetryPayloadMaxSize)),
        std::make_tuple(
            "",
            grpc_api::GetEcTelemetryResponse::STATUS_ERROR_INPUT_PAYLOAD_EMPTY,
            ""),
        std::make_tuple(std::string("A", kEcGetTelemetryPayloadMaxSize + 1),
                        grpc_api::GetEcTelemetryResponse::
                            STATUS_ERROR_INPUT_PAYLOAD_MAX_SIZE_EXCEEDED,
                        "")));

// Tests for the PerformWebRequest() method of GrpcService.
//
// This is a parameterized test with the following parameters:
//
// The input arguments to create a PerformWebRequestParameter:
// * |http_method| - gRPC PerformWebRequest HTTP method.
// * |url| - gRPC PerformWebRequest URL.
// * |headers| - gRPC PerformWebRequest headers list.
// * |request_body| - gRPC PerformWebRequest request body.
//
// The intermediate parameters to verify by the test:
// * |delegate_http_method| - this is an optional value, a nullptr if the
//                            intermediate verification is not needed.
//                            GrpcService's Delegate's HTTP
//                            method to verify the mapping between gRPC and
//                            Delegate's HTTP method names.
//
// The expected response values to verify PerformWebRequestResponse:
// * |status| - gRPC PerformWebRequestResponse status.
// * |http_status| - this is an optional value. gRPC PerformWebRequestResponse
//                   HTTP status. If there is no HTTP status needed for
//                   the passed |status|, pass a nullptr.
// * |response_body| - this is an optional value. gRPC PerformWebRequestResponse
//                     body. If not set, pass a nullptr.
class PerformWebRequestGrpcServiceTest
    : public GrpcServiceTest,
      public testing::WithParamInterface<
          std::tuple<grpc_api::PerformWebRequestParameter::HttpMethod,
                     std::string /* URL */,
                     std::vector<std::string> /* headers */,
                     std::string /* request body */,
                     const DelegateWebRequestHttpMethod*,
                     grpc_api::PerformWebRequestResponse::Status /* status */,
                     const int* /* HTTP status */,
                     const char* /* response body */>> {
 protected:
  grpc_api::PerformWebRequestParameter::HttpMethod http_method() {
    return std::get<0>(GetParam());
  }
  std::string url() const { return std::get<1>(GetParam()); }
  std::vector<std::string> headers() const { return std::get<2>(GetParam()); }
  std::string request_body() const { return std::get<3>(GetParam()); }
  const DelegateWebRequestHttpMethod* delegate_http_method() const {
    return std::get<4>(GetParam());
  }
  grpc_api::PerformWebRequestResponse::Status status() const {
    return std::get<5>(GetParam());
  }
  const int* http_status() const { return std::get<6>(GetParam()); }
  const char* response_body() const { return std::get<7>(GetParam()); }
};

// Tests that PerformWebRequest() returns an appropriate status and HTTP status
// code.
TEST_P(PerformWebRequestGrpcServiceTest, PerformWebRequest) {
  std::unique_ptr<grpc_api::PerformWebRequestResponse> response;
  ExecutePerformWebRequest(http_method(), url(), headers(), request_body(),
                           delegate_http_method(), &response);
  ASSERT_TRUE(response);

  auto expected_response =
      MakePerformWebRequestResponse(status(), http_status(), response_body());
  EXPECT_THAT(*response, ProtobufEquals(*expected_response))
      << "Actual response: {" << response->ShortDebugString() << "}";
}

// Test cases to run a PerformWebRequest test.
// Make sure that the delegate_http_header is not set if the flow does not
// involve the calls to GrpcService::Delegate.
INSTANTIATE_TEST_CASE_P(
    ,
    PerformWebRequestGrpcServiceTest,
    testing::Values(
        // Tests an incorrect HTTP method.
        std::make_tuple(grpc_api::PerformWebRequestParameter::HTTP_METHOD_UNSET,
                        kCorrectUrl,
                        std::vector<std::string>() /* headers */,
                        "" /* request_body */,
                        nullptr /* delegate_http_method */,
                        grpc_api::PerformWebRequestResponse ::
                            STATUS_ERROR_REQUIRED_FIELD_MISSING,
                        nullptr /* http_status */,
                        nullptr /* response_body */),
        // Tests an empty URL.
        std::make_tuple(
            grpc_api::PerformWebRequestParameter::HTTP_METHOD_GET,
            "" /* url */,
            std::vector<std::string>() /* headers */,
            "" /* request_body */,
            nullptr /* delegate_http_method */,
            grpc_api::PerformWebRequestResponse ::STATUS_ERROR_INVALID_URL,
            nullptr /* http_status */,
            nullptr /* response_body */),
        // Tests a non-HTTPS URL.
        std::make_tuple(
            grpc_api::PerformWebRequestParameter::HTTP_METHOD_PUT,
            kBadNonHttpsUrl,
            std::vector<std::string>() /* headers */,
            "" /* request_body */,
            nullptr /* delegate_http_method */,
            grpc_api::PerformWebRequestResponse::STATUS_ERROR_INVALID_URL,
            nullptr /* http_status */,
            nullptr /* response_body */),
        // Tests the maximum allowed number of headers with HTTP method GET.
        std::make_tuple(grpc_api::PerformWebRequestParameter::HTTP_METHOD_GET,
                        kCorrectUrl,
                        std::vector<std::string>(
                            kMaxNumberOfHeadersInPerformWebRequestParameter,
                            ""),
                        "" /* request_body */,
                        &kDelegateWebRequestHttpMethodGet,
                        grpc_api::PerformWebRequestResponse::STATUS_OK,
                        &kHttpStatusOk,
                        kFakeWebResponseBody),
        // The HTTP method is HEAD.
        std::make_tuple(grpc_api::PerformWebRequestParameter::HTTP_METHOD_HEAD,
                        kCorrectUrl,
                        std::vector<std::string>(
                            kMaxNumberOfHeadersInPerformWebRequestParameter,
                            ""),
                        "" /* request_body */,
                        &kDelegateWebRequestHttpMethodHead,
                        grpc_api::PerformWebRequestResponse::STATUS_OK,
                        &kHttpStatusOk,
                        kFakeWebResponseBody),
        // The HTTP method is POST.
        std::make_tuple(grpc_api::PerformWebRequestParameter::HTTP_METHOD_POST,
                        kCorrectUrl,
                        std::vector<std::string>() /* headers */,
                        "" /* request_body */,
                        &kDelegateWebRequestHttpMethodPost,
                        grpc_api::PerformWebRequestResponse::STATUS_OK,
                        &kHttpStatusOk,
                        kFakeWebResponseBody),
        // Tests the minimum not allowed number of headers.
        std::make_tuple(
            grpc_api::PerformWebRequestParameter::HTTP_METHOD_GET,
            kCorrectUrl,
            std::vector<std::string>(
                kMaxNumberOfHeadersInPerformWebRequestParameter + 1, ""),
            "" /* request_body */,
            nullptr /* delegate_http_method */,
            grpc_api::PerformWebRequestResponse::STATUS_ERROR_MAX_SIZE_EXCEEDED,
            nullptr /* http_status */,
            nullptr /* response_body */),
        // Tests the total size of "string" and "byte" fields of
        // PerformWebRequestParameter = 1Mb, the HTTP method is PUT.
        std::make_tuple(grpc_api::PerformWebRequestParameter::HTTP_METHOD_PUT,
                        kCorrectUrl,
                        std::vector<std::string>() /* headers */,
                        std::string(kMaxPerformWebRequestParameterSizeInBytes -
                                        strlen(kCorrectUrl),
                                    'A'),
                        &kDelegateWebRequestHttpMethodPut,
                        grpc_api::PerformWebRequestResponse::STATUS_OK,
                        &kHttpStatusOk,
                        kFakeWebResponseBody),
        // Tests the total size of "string" and "byte" fields of
        // PerformWebRequestParameter > 1Mb.
        std::make_tuple(
            grpc_api::PerformWebRequestParameter::HTTP_METHOD_GET,
            kCorrectUrl,
            std::vector<std::string>() /* headers */,
            std::string(kMaxPerformWebRequestParameterSizeInBytes, 'A'),
            nullptr /* delegate_http_method */,
            grpc_api::PerformWebRequestResponse::STATUS_ERROR_MAX_SIZE_EXCEEDED,
            nullptr /* http_status */,
            nullptr /* response_body */)));

// Tests for the GetRoutineUpdate() method of GrpcService.
//
// This is a parameterized test with the following parameters:
//
// The input arguments to create a GetRoutineUpdateRequest:
// * |command| - gRPC GetRoutineUpdateRequest command.
class GetRoutineUpdateRequestGrpcServiceTest
    : public GrpcServiceTest,
      public testing::WithParamInterface<
          grpc_api::GetRoutineUpdateRequest::Command /* command */> {
 protected:
  grpc_api::GetRoutineUpdateRequest::Command command() const {
    return GetParam();
  }
};

// Tests that GetRoutineUpdate() returns an appropriate uuid, status, progress
// percent, user message and output.
TEST_P(GetRoutineUpdateRequestGrpcServiceTest,
       GetRoutineUpdateRequestWithOutput) {
  std::unique_ptr<grpc_api::GetRoutineUpdateResponse> response;
  constexpr bool kIncludeOutput = true;
  ExecuteGetRoutineUpdate(kFakeUuid, command(), kIncludeOutput, &response);
  ASSERT_TRUE(response);

  auto expected_response =
      MakeGetRoutineUpdateResponse(kFakeUuid, kIncludeOutput);
  EXPECT_THAT(*response, ProtobufEquals(*expected_response))
      << "Actual response: {" << response->ShortDebugString() << "}";
}

// Tests that GetRoutineUpdate() does not return output when include_output is
// false.
TEST_P(GetRoutineUpdateRequestGrpcServiceTest,
       GetRoutineUpdateRequestNoOutput) {
  std::unique_ptr<grpc_api::GetRoutineUpdateResponse> response;
  constexpr bool kIncludeOutput = false;
  ExecuteGetRoutineUpdate(kFakeUuid, command(), kIncludeOutput, &response);
  ASSERT_TRUE(response);

  auto expected_response =
      MakeGetRoutineUpdateResponse(kFakeUuid, kIncludeOutput);
  EXPECT_THAT(*response, ProtobufEquals(*expected_response))
      << "Actual response: {" << response->ShortDebugString() << "}";
}

// Test cases to run a GetRoutineUpdateRequest test.
INSTANTIATE_TEST_CASE_P(,
                        GetRoutineUpdateRequestGrpcServiceTest,
                        testing::Values(
                            // Test each possible command value.
                            grpc_api::GetRoutineUpdateRequest::RESUME,
                            grpc_api::GetRoutineUpdateRequest::CANCEL,
                            grpc_api::GetRoutineUpdateRequest::GET_STATUS));

// Test for the GetVpdField() method of GrpcService.
//
// This is a parametrized test with the following parameters:
// * |vpd_field| - the requested VPD field.
// * |file_path| - the corresponding file path.
class GetVpdFieldGrpcServiceTest
    : public GrpcServiceTest,
      public testing::WithParamInterface<
          std::tuple<grpc_api::GetVpdFieldRequest::VpdField /* vpd_field */,
                     std::string /* file_path */>> {
 protected:
  grpc_api::GetVpdFieldRequest::VpdField vpd_field() const {
    return std::get<0>(GetParam());
  }
  base::FilePath file_path() const {
    return temp_dir_path().Append(std::get<1>(GetParam()));
  }
};

// Test that GetVpdField() is read properly.
TEST_P(GetVpdFieldGrpcServiceTest, GetVpdField) {
  EXPECT_TRUE(WriteFileAndCreateParentDirs(file_path(), kFakeVpdValue));
  grpc_api::GetVpdFieldResponse::Status status;
  std::string vpd_field_value;
  ASSERT_NO_FATAL_FAILURE(
      ExecuteGetVpdField(vpd_field(), &status, &vpd_field_value));

  EXPECT_EQ(status, grpc_api::GetVpdFieldResponse::STATUS_OK);
  EXPECT_EQ(vpd_field_value, kFakeVpdValue);
}

TEST_F(GetVpdFieldGrpcServiceTest, GetVpdFieldWithWhiteSpaces) {
  EXPECT_TRUE(WriteFileAndCreateParentDirs(
      temp_dir_path().Append(kVpdFieldSerialNumberFilePath),
      base::StringPrintf("%s\n\t", kFakeVpdValue)));
  grpc_api::GetVpdFieldResponse::Status status;
  std::string vpd_field_value;
  ASSERT_NO_FATAL_FAILURE(
      ExecuteGetVpdField(grpc_api::GetVpdFieldRequest::FIELD_SERIAL_NUMBER,
                         &status, &vpd_field_value));
  EXPECT_EQ(status, grpc_api::GetVpdFieldResponse::STATUS_OK);
  EXPECT_EQ(vpd_field_value, kFakeVpdValue);
}

INSTANTIATE_TEST_CASE_P(
    ,
    GetVpdFieldGrpcServiceTest,
    testing::Values(
        std::make_tuple(grpc_api::GetVpdFieldRequest::FIELD_SERIAL_NUMBER,
                        kVpdFieldSerialNumberFilePath),
        std::make_tuple(grpc_api::GetVpdFieldRequest::FIELD_MODEL_NAME,
                        kVpdFieldModelNameFilePath),
        std::make_tuple(grpc_api::GetVpdFieldRequest::FIELD_ASSET_ID,
                        kVpdFieldAssetIdFilePath),
        std::make_tuple(grpc_api::GetVpdFieldRequest::FIELD_SKU_NUMBER,
                        kVpdFieldSkuNumberFilePath),
        std::make_tuple(grpc_api::GetVpdFieldRequest::FIELD_UUID_ID,
                        kVpdFieldUuidFilePath),
        std::make_tuple(grpc_api::GetVpdFieldRequest::FIELD_MANUFACTURE_DATE,
                        kVpdFieldMfgDateFilePath),
        std::make_tuple(grpc_api::GetVpdFieldRequest::FIELD_ACTIVATE_DATE,
                        kVpdFieldActivateDateFilePath),
        std::make_tuple(grpc_api::GetVpdFieldRequest::FIELD_SYSTEM_ID,
                        kVpdFieldSystemIdFilePath)));

class GetVpdFieldFailGrpcServiceTest
    : public GrpcServiceTest,
      public testing::WithParamInterface<
          std::tuple<std::string /* file_content */>> {
 protected:
  std::string file_content() const { return std::get<0>(GetParam()); }
};

TEST_P(GetVpdFieldFailGrpcServiceTest, GetVpdFieldFail) {
  EXPECT_TRUE(WriteFileAndCreateParentDirs(
      temp_dir_path().Append(kVpdFieldSerialNumberFilePath), file_content()));
  grpc_api::GetVpdFieldResponse::Status status;
  std::string vpd_field_value;
  ASSERT_NO_FATAL_FAILURE(
      ExecuteGetVpdField(grpc_api::GetVpdFieldRequest::FIELD_SERIAL_NUMBER,
                         &status, &vpd_field_value));
  EXPECT_EQ(status, grpc_api::GetVpdFieldResponse::STATUS_ERROR_INTERNAL);
  EXPECT_TRUE(vpd_field_value.empty());
}

INSTANTIATE_TEST_CASE_P(
    ,
    GetVpdFieldFailGrpcServiceTest,
    testing::Values(
        // Valid empty file test case.
        std::make_tuple(base::StringPrintf("")),
        // Valid whitespace characters only test case.
        std::make_tuple(base::StringPrintf("\t\n")),
        // Valid non ASCII characters test case.
        std::make_tuple(base::StringPrintf("%s不", kFakeVpdValue))));

// Test for the GetDriveSystemData() method of GrpcService.
//
// This is a parametrized test with the following parameters:
// * |vpd_field| - the requested drive system data type
// * |expected_data_type| - the expected internal drive system data type value.
class GetDriveSystemDataGrpcServiceTest
    : public GrpcServiceTest,
      public testing::WithParamInterface<
          std::tuple<grpc_api::GetDriveSystemDataRequest::Type /* data_type */,
                     DelegateDriveSystemDataType /* expected_data_type */>> {
 protected:
  grpc_api::GetDriveSystemDataRequest::Type data_type() const {
    return std::get<0>(GetParam());
  }
  DelegateDriveSystemDataType expected_data_type() const {
    return std::get<1>(GetParam());
  }
};

// Test that GetDriveSystemData() parses gRPC message and calls delegate
// function with appropriate data type.
TEST_P(GetDriveSystemDataGrpcServiceTest, GetDriveSystem) {
  constexpr char kFakeDriveSystemData[] = "Fake DriveSystem data";

  auto request = std::make_unique<grpc_api::GetDriveSystemDataRequest>();
  request->set_type(data_type());
  EXPECT_CALL(*delegate(), GetDriveSystemData(expected_data_type(), _))
      .WillOnce(WithArgs<1>(
          Invoke([kFakeDriveSystemData](
                     const base::Callback<void(const std::string& payload,
                                               bool success)>& callback) {
            callback.Run(kFakeDriveSystemData, true /* success */);
          })));

  std::unique_ptr<grpc_api::GetDriveSystemDataResponse> response;
  service()->GetDriveSystemData(std::move(request),
                                GrpcCallbackResponseSaver(&response));

  auto expected_response =
      std::make_unique<grpc_api::GetDriveSystemDataResponse>();
  expected_response->set_status(
      grpc_api::GetDriveSystemDataResponse::STATUS_OK);
  expected_response->set_payload(kFakeDriveSystemData);
  EXPECT_THAT(*response, ProtobufEquals(*expected_response))
      << "Actual response: {" << response->ShortDebugString() << "}";
}

INSTANTIATE_TEST_CASE_P(
    ,
    GetDriveSystemDataGrpcServiceTest,
    testing::Values(
        std::make_tuple(grpc_api::GetDriveSystemDataRequest::SMART_ATTRIBUTES,
                        DelegateDriveSystemDataType::kSmartAttributes),
        std::make_tuple(
            grpc_api::GetDriveSystemDataRequest::IDENTITY_ATTRIBUTES,
            DelegateDriveSystemDataType::kIdentityAttributes)));

}  // namespace

}  // namespace diagnostics
