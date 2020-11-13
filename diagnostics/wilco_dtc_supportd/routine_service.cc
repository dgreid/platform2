// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/wilco_dtc_supportd/routine_service.h"

#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "diagnostics/common/mojo_utils.h"
#include "mojo/cros_healthd.mojom.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"
#include "mojo/nullable_primitives.mojom.h"

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {

// Converts from mojo's DiagnosticRoutineStatusEnum to gRPC's
// DiagnosticRoutineStatus.
bool GetGrpcStatusFromMojoStatus(
    chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum mojo_status,
    grpc_api::DiagnosticRoutineStatus* grpc_status_out) {
  DCHECK(grpc_status_out);
  switch (mojo_status) {
    case chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::kReady:
      *grpc_status_out = grpc_api::ROUTINE_STATUS_READY;
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::kRunning:
      *grpc_status_out = grpc_api::ROUTINE_STATUS_RUNNING;
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::kWaiting:
      *grpc_status_out = grpc_api::ROUTINE_STATUS_WAITING;
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::kPassed:
      *grpc_status_out = grpc_api::ROUTINE_STATUS_PASSED;
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::kFailed:
      *grpc_status_out = grpc_api::ROUTINE_STATUS_FAILED;
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::kError:
      *grpc_status_out = grpc_api::ROUTINE_STATUS_ERROR;
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::kCancelled:
      *grpc_status_out = grpc_api::ROUTINE_STATUS_CANCELLED;
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::
        kFailedToStart:
      *grpc_status_out = grpc_api::ROUTINE_STATUS_FAILED_TO_START;
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::kRemoved:
      *grpc_status_out = grpc_api::ROUTINE_STATUS_REMOVED;
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::
        kCancelling:
      *grpc_status_out = grpc_api::ROUTINE_STATUS_CANCELLING;
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::
        kUnsupported:
      *grpc_status_out = grpc_api::ROUTINE_STATUS_ERROR;
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::kNotRun:
      *grpc_status_out = grpc_api::ROUTINE_STATUS_FAILED_TO_START;
      return true;
  }
  LOG(ERROR) << "Unknown mojo routine status: "
             << static_cast<int>(mojo_status);
  return false;
}

// Converts from mojo's DiagnosticRoutineUserMessageEnum to gRPC's
// DiagnosticRoutineUserMessage.
bool GetUserMessageFromMojoEnum(
    chromeos::cros_healthd::mojom::DiagnosticRoutineUserMessageEnum
        mojo_message,
    grpc_api::DiagnosticRoutineUserMessage* grpc_message_out) {
  DCHECK(grpc_message_out);
  switch (mojo_message) {
    case chromeos::cros_healthd::mojom::DiagnosticRoutineUserMessageEnum::
        kUnplugACPower:
      *grpc_message_out = grpc_api::ROUTINE_USER_MESSAGE_UNPLUG_AC_POWER;
      return true;
    default:
      LOG(ERROR) << "Unknown mojo user message: "
                 << static_cast<int>(mojo_message);
      return false;
  }
}

// Converts from mojo's DiagnosticRoutineEnum to gRPC's DiagnosticRoutine.
bool GetGrpcRoutineEnumFromMojoRoutineEnum(
    chromeos::cros_healthd::mojom::DiagnosticRoutineEnum mojo_enum,
    std::vector<grpc_api::DiagnosticRoutine>* grpc_enum_out) {
  DCHECK(grpc_enum_out);
  switch (mojo_enum) {
    case chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::kBatteryCapacity:
      grpc_enum_out->push_back(grpc_api::ROUTINE_BATTERY);
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::kBatteryHealth:
      grpc_enum_out->push_back(grpc_api::ROUTINE_BATTERY_SYSFS);
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::kUrandom:
      grpc_enum_out->push_back(grpc_api::ROUTINE_URANDOM);
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::kSmartctlCheck:
      grpc_enum_out->push_back(grpc_api::ROUTINE_SMARTCTL_CHECK);
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::kCpuCache:
      grpc_enum_out->push_back(grpc_api::ROUTINE_CPU_CACHE);
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::kCpuStress:
      grpc_enum_out->push_back(grpc_api::ROUTINE_CPU_STRESS);
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::
        kFloatingPointAccuracy:
      grpc_enum_out->push_back(grpc_api::ROUTINE_FLOATING_POINT_ACCURACY);
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::kNvmeWearLevel:
      grpc_enum_out->push_back(grpc_api::ROUTINE_NVME_WEAR_LEVEL);
      return true;
    // There is only one mojo enum for self_test(short & extended share same
    // class), but there're 2 gRPC enum for self_test according to requirement.
    case chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::kNvmeSelfTest:
      grpc_enum_out->push_back(grpc_api::ROUTINE_NVME_SHORT_SELF_TEST);
      grpc_enum_out->push_back(grpc_api::ROUTINE_NVME_LONG_SELF_TEST);
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::kDiskRead:
      grpc_enum_out->push_back(grpc_api::ROUTINE_DISK_LINEAR_READ);
      grpc_enum_out->push_back(grpc_api::ROUTINE_DISK_RANDOM_READ);
      return true;
    case chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::kPrimeSearch:
      grpc_enum_out->push_back(grpc_api::ROUTINE_PRIME_SEARCH);
      return true;
    default:
      LOG(ERROR) << "Unknown mojo routine: " << static_cast<int>(mojo_enum);
      return false;
  }
}

// Converts from mojo's RoutineUpdate to gRPC's GetRoutineUpdateResponse.
void SetGrpcUpdateFromMojoUpdate(
    chromeos::cros_healthd::mojom::RoutineUpdatePtr mojo_update,
    grpc_api::GetRoutineUpdateResponse* grpc_update) {
  DCHECK(grpc_update);
  grpc_update->set_progress_percent(mojo_update->progress_percent);
  const auto& update_union = mojo_update->routine_update_union;
  if (update_union->is_interactive_update()) {
    grpc_api::DiagnosticRoutineUserMessage grpc_message;
    chromeos::cros_healthd::mojom::DiagnosticRoutineUserMessageEnum
        mojo_message = update_union->get_interactive_update()->user_message;
    if (!GetUserMessageFromMojoEnum(mojo_message, &grpc_message)) {
      grpc_update->set_status(grpc_api::ROUTINE_STATUS_ERROR);
    } else {
      grpc_update->set_user_message(grpc_message);
    }
  } else {
    grpc_update->set_status_message(
        update_union->get_noninteractive_update()->status_message);
    grpc_api::DiagnosticRoutineStatus grpc_status;
    auto mojo_status = update_union->get_noninteractive_update()->status;
    if (!GetGrpcStatusFromMojoStatus(mojo_status, &grpc_status)) {
      grpc_update->set_status(grpc_api::ROUTINE_STATUS_ERROR);
    } else {
      grpc_update->set_status(grpc_status);
    }
  }

  if (!mojo_update->output.is_valid()) {
    // This isn't necessarily an error, since some requests may not have
    // specified that they wanted output returned, and some routines may never
    // return any extra input. We'll log the event in the case that it was an
    // error.
    VLOG(1) << "No output in mojo update.";
    return;
  }

  auto shm_mapping = GetReadOnlySharedMemoryMappingFromMojoHandle(
      std::move(mojo_update->output));
  if (!shm_mapping.IsValid()) {
    PLOG(ERROR) << "Failed to read data from mojo handle";
    return;
  }
  grpc_update->set_output(std::string(shm_mapping.GetMemoryAs<const char>(),
                                      shm_mapping.mapped_size()));
}

// Converts from gRPC's GetRoutineUpdateRequest::Command to mojo's
// DiagnosticRoutineCommandEnum.
bool GetMojoCommandFromGrpcCommand(
    grpc_api::GetRoutineUpdateRequest::Command grpc_command,
    chromeos::cros_healthd::mojom::DiagnosticRoutineCommandEnum*
        mojo_command_out) {
  DCHECK(mojo_command_out);
  switch (grpc_command) {
    case grpc_api::GetRoutineUpdateRequest::RESUME:
      *mojo_command_out = chromeos::cros_healthd::mojom::
          DiagnosticRoutineCommandEnum::kContinue;
      return true;
    case grpc_api::GetRoutineUpdateRequest::CANCEL:
      *mojo_command_out =
          chromeos::cros_healthd::mojom::DiagnosticRoutineCommandEnum::kCancel;
      return true;
    case grpc_api::GetRoutineUpdateRequest::GET_STATUS:
      *mojo_command_out = chromeos::cros_healthd::mojom::
          DiagnosticRoutineCommandEnum::kGetStatus;
      return true;
    case grpc_api::GetRoutineUpdateRequest::REMOVE:
      *mojo_command_out =
          chromeos::cros_healthd::mojom::DiagnosticRoutineCommandEnum::kRemove;
      return true;
    default:
      LOG(ERROR) << "Unknown gRPC command: " << static_cast<int>(grpc_command);
      return false;
  }
}

}  // namespace

RoutineService::RoutineService(Delegate* delegate) : delegate_(delegate) {
  DCHECK(delegate_);
}

RoutineService::~RoutineService() {
  RunInFlightCallbacks();
}

void RoutineService::GetAvailableRoutines(
    const GetAvailableRoutinesToServiceCallback& callback) {
  if (!BindCrosHealthdDiagnosticsServiceIfNeeded()) {
    LOG(WARNING) << "GetAvailableRoutines called before mojo was bootstrapped.";
    callback.Run(std::vector<grpc_api::DiagnosticRoutine>{},
                 grpc_api::ROUTINE_SERVICE_STATUS_UNAVAILABLE);
    return;
  }

  const size_t callback_key = next_get_available_routines_key_;
  next_get_available_routines_key_++;
  DCHECK_EQ(get_available_routines_callbacks_.count(callback_key), 0);
  get_available_routines_callbacks_.insert({callback_key, std::move(callback)});
  service_ptr_->GetAvailableRoutines(
      base::Bind(&RoutineService::ForwardGetAvailableRoutinesResponse,
                 weak_ptr_factory_.GetWeakPtr(), callback_key));
}

void RoutineService::RunRoutine(const grpc_api::RunRoutineRequest& request,
                                const RunRoutineToServiceCallback& callback) {
  if (!BindCrosHealthdDiagnosticsServiceIfNeeded()) {
    LOG(WARNING) << "RunRoutine called before mojo was bootstrapped.";
    callback.Run(0 /* uuid */, grpc_api::ROUTINE_STATUS_FAILED_TO_START,
                 grpc_api::ROUTINE_SERVICE_STATUS_UNAVAILABLE);
    return;
  }

  const size_t callback_key = next_run_routine_key_;
  next_run_routine_key_++;
  DCHECK_EQ(run_routine_callbacks_.count(callback_key), 0);
  auto it = run_routine_callbacks_.insert({callback_key, std::move(callback)});

  switch (request.routine()) {
    case grpc_api::ROUTINE_BATTERY:
      DCHECK_EQ(request.parameters_case(),
                grpc_api::RunRoutineRequest::kBatteryParams);
      service_ptr_->RunBatteryCapacityRoutine(
          base::Bind(&RoutineService::ForwardRunRoutineResponse,
                     weak_ptr_factory_.GetWeakPtr(), callback_key));
      break;
    case grpc_api::ROUTINE_BATTERY_SYSFS:
      DCHECK_EQ(request.parameters_case(),
                grpc_api::RunRoutineRequest::kBatterySysfsParams);
      service_ptr_->RunBatteryHealthRoutine(
          base::Bind(&RoutineService::ForwardRunRoutineResponse,
                     weak_ptr_factory_.GetWeakPtr(), callback_key));
      break;
    case grpc_api::ROUTINE_URANDOM:
      DCHECK_EQ(request.parameters_case(),
                grpc_api::RunRoutineRequest::kUrandomParams);
      service_ptr_->RunUrandomRoutine(
          chromeos::cros_healthd::mojom::NullableUint32::New(
              request.urandom_params().length_seconds()),
          base::Bind(&RoutineService::ForwardRunRoutineResponse,
                     weak_ptr_factory_.GetWeakPtr(), callback_key));
      break;
    case grpc_api::ROUTINE_SMARTCTL_CHECK:
      DCHECK_EQ(request.parameters_case(),
                grpc_api::RunRoutineRequest::kSmartctlCheckParams);
      service_ptr_->RunSmartctlCheckRoutine(
          base::Bind(&RoutineService::ForwardRunRoutineResponse,
                     weak_ptr_factory_.GetWeakPtr(), callback_key));
      break;
    case grpc_api::ROUTINE_CPU_CACHE:
      DCHECK_EQ(request.parameters_case(),
                grpc_api::RunRoutineRequest::kCpuParams);
      service_ptr_->RunCpuCacheRoutine(
          chromeos::cros_healthd::mojom::NullableUint32::New(
              request.cpu_params().length_seconds()),
          base::Bind(&RoutineService::ForwardRunRoutineResponse,
                     weak_ptr_factory_.GetWeakPtr(), callback_key));
      break;
    case grpc_api::ROUTINE_CPU_STRESS:
      DCHECK_EQ(request.parameters_case(),
                grpc_api::RunRoutineRequest::kCpuParams);
      service_ptr_->RunCpuStressRoutine(
          chromeos::cros_healthd::mojom::NullableUint32::New(
              request.cpu_params().length_seconds()),
          base::Bind(&RoutineService::ForwardRunRoutineResponse,
                     weak_ptr_factory_.GetWeakPtr(), callback_key));
      break;
    case grpc_api::ROUTINE_FLOATING_POINT_ACCURACY:
      DCHECK_EQ(request.parameters_case(),
                grpc_api::RunRoutineRequest::kFloatingPointAccuracyParams);
      service_ptr_->RunFloatingPointAccuracyRoutine(
          chromeos::cros_healthd::mojom::NullableUint32::New(
              request.floating_point_accuracy_params().length_seconds()),
          base::Bind(&RoutineService::ForwardRunRoutineResponse,
                     weak_ptr_factory_.GetWeakPtr(), callback_key));
      break;
    case grpc_api::ROUTINE_NVME_WEAR_LEVEL:
      DCHECK_EQ(request.parameters_case(),
                grpc_api::RunRoutineRequest::kNvmeWearLevelParams);
      service_ptr_->RunNvmeWearLevelRoutine(
          request.nvme_wear_level_params().wear_level_threshold(),
          base::Bind(&RoutineService::ForwardRunRoutineResponse,
                     weak_ptr_factory_.GetWeakPtr(), callback_key));
      break;
    case grpc_api::ROUTINE_NVME_SHORT_SELF_TEST:
      DCHECK_EQ(request.parameters_case(),
                grpc_api::RunRoutineRequest::kNvmeShortSelfTestParams);
      service_ptr_->RunNvmeSelfTestRoutine(
          chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum::kShortSelfTest,
          base::Bind(&RoutineService::ForwardRunRoutineResponse,
                     weak_ptr_factory_.GetWeakPtr(), callback_key));
      break;
    case grpc_api::ROUTINE_NVME_LONG_SELF_TEST:
      DCHECK_EQ(request.parameters_case(),
                grpc_api::RunRoutineRequest::kNvmeLongSelfTestParams);
      service_ptr_->RunNvmeSelfTestRoutine(
          chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum::kLongSelfTest,
          base::Bind(&RoutineService::ForwardRunRoutineResponse,
                     weak_ptr_factory_.GetWeakPtr(), callback_key));
      break;
    case grpc_api::ROUTINE_DISK_LINEAR_READ:
      DCHECK_EQ(request.parameters_case(),
                grpc_api::RunRoutineRequest::kDiskLinearReadParams);
      service_ptr_->RunDiskReadRoutine(
          mojo_ipc::DiskReadRoutineTypeEnum::kLinearRead,
          request.disk_linear_read_params().length_seconds(),
          request.disk_linear_read_params().file_size_mb(),
          base::Bind(&RoutineService::ForwardRunRoutineResponse,
                     weak_ptr_factory_.GetWeakPtr(), callback_key));
      break;
    case grpc_api::ROUTINE_DISK_RANDOM_READ:
      DCHECK_EQ(request.parameters_case(),
                grpc_api::RunRoutineRequest::kDiskRandomReadParams);
      service_ptr_->RunDiskReadRoutine(
          mojo_ipc::DiskReadRoutineTypeEnum::kRandomRead,
          request.disk_random_read_params().length_seconds(),
          request.disk_random_read_params().file_size_mb(),
          base::Bind(&RoutineService::ForwardRunRoutineResponse,
                     weak_ptr_factory_.GetWeakPtr(), callback_key));
      break;
    case grpc_api::ROUTINE_PRIME_SEARCH:
      DCHECK_EQ(request.parameters_case(),
                grpc_api::RunRoutineRequest::kPrimeSearchParams);
      service_ptr_->RunPrimeSearchRoutine(
          chromeos::cros_healthd::mojom::NullableUint32::New(
              request.prime_search_params().length_seconds()),
          base::Bind(&RoutineService::ForwardRunRoutineResponse,
                     weak_ptr_factory_.GetWeakPtr(), callback_key));
      break;
    default:
      LOG(ERROR) << "RunRoutineRequest routine not set or unrecognized.";
      it.first->second.Run(0 /* uuid */, grpc_api::ROUTINE_STATUS_INVALID_FIELD,
                           grpc_api::ROUTINE_SERVICE_STATUS_OK);
      run_routine_callbacks_.erase(it.first);
      break;
  }
}

void RoutineService::GetRoutineUpdate(
    int uuid,
    grpc_api::GetRoutineUpdateRequest::Command command,
    bool include_output,
    const GetRoutineUpdateRequestToServiceCallback& callback) {
  if (!BindCrosHealthdDiagnosticsServiceIfNeeded()) {
    LOG(WARNING) << "GetRoutineUpdate called before mojo was bootstrapped.";
    callback.Run(uuid, grpc_api::ROUTINE_STATUS_ERROR, 0 /* progress_percent */,
                 grpc_api::ROUTINE_USER_MESSAGE_UNSET, "" /* output */,
                 "" /* status_message */,
                 grpc_api::ROUTINE_SERVICE_STATUS_UNAVAILABLE);
    return;
  }

  chromeos::cros_healthd::mojom::DiagnosticRoutineCommandEnum mojo_command;
  if (!GetMojoCommandFromGrpcCommand(command, &mojo_command)) {
    callback.Run(uuid, grpc_api::ROUTINE_STATUS_INVALID_FIELD,
                 0 /* progress_percent */, grpc_api::ROUTINE_USER_MESSAGE_UNSET,
                 "" /* output */, "" /* status_message */,
                 grpc_api::ROUTINE_SERVICE_STATUS_OK);
    return;
  }

  const size_t callback_key = next_get_routine_update_key_;
  next_get_routine_update_key_++;
  DCHECK_EQ(get_routine_update_callbacks_.count(callback_key), 0);
  get_routine_update_callbacks_.insert(
      {callback_key, {uuid, std::move(callback)}});
  service_ptr_->GetRoutineUpdate(
      uuid, mojo_command, include_output,
      base::Bind(&RoutineService::ForwardGetRoutineUpdateResponse,
                 weak_ptr_factory_.GetWeakPtr(), callback_key));
}

void RoutineService::ForwardGetAvailableRoutinesResponse(
    size_t callback_key,
    const std::vector<chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>&
        mojo_routines) {
  auto it = get_available_routines_callbacks_.find(callback_key);
  if (it == get_available_routines_callbacks_.end()) {
    LOG(ERROR) << "Unknown callback_key for received mojo GetAvailableRoutines "
                  "response: "
               << callback_key;
    return;
  }

  std::vector<grpc_api::DiagnosticRoutine> grpc_routines;
  for (auto mojo_routine : mojo_routines) {
    std::vector<grpc_api::DiagnosticRoutine> grpc_mojo_routines;
    if (GetGrpcRoutineEnumFromMojoRoutineEnum(mojo_routine,
                                              &grpc_mojo_routines))
      for (auto grpc_routine : grpc_mojo_routines)
        grpc_routines.push_back(grpc_routine);
  }

  it->second.Run(std::move(grpc_routines), grpc_api::ROUTINE_SERVICE_STATUS_OK);
  get_available_routines_callbacks_.erase(it);
}

void RoutineService::ForwardRunRoutineResponse(
    size_t callback_key,
    chromeos::cros_healthd::mojom::RunRoutineResponsePtr response) {
  auto it = run_routine_callbacks_.find(callback_key);
  if (it == run_routine_callbacks_.end()) {
    LOG(ERROR) << "Unknown callback_key for received mojo GetAvailableRoutines "
                  "response: "
               << callback_key;
    return;
  }

  grpc_api::DiagnosticRoutineStatus grpc_status;
  chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum mojo_status =
      response->status;
  if (!GetGrpcStatusFromMojoStatus(mojo_status, &grpc_status)) {
    it->second.Run(0 /* uuid */, grpc_api::ROUTINE_STATUS_ERROR,
                   grpc_api::ROUTINE_SERVICE_STATUS_OK);
  } else {
    it->second.Run(response->id, grpc_status,
                   grpc_api::ROUTINE_SERVICE_STATUS_OK);
  }
  run_routine_callbacks_.erase(it);
}

void RoutineService::ForwardGetRoutineUpdateResponse(
    size_t callback_key,
    chromeos::cros_healthd::mojom::RoutineUpdatePtr response) {
  auto it = get_routine_update_callbacks_.find(callback_key);
  if (it == get_routine_update_callbacks_.end()) {
    LOG(ERROR) << "Unknown callback_key for received mojo GetAvailableRoutines "
                  "response: "
               << callback_key;
    return;
  }

  grpc_api::GetRoutineUpdateResponse grpc_response;
  SetGrpcUpdateFromMojoUpdate(std::move(response), &grpc_response);
  it->second.second.Run(it->second.first /* uuid */, grpc_response.status(),
                        grpc_response.progress_percent(),
                        grpc_response.user_message(), grpc_response.output(),
                        grpc_response.status_message(),
                        grpc_api::ROUTINE_SERVICE_STATUS_OK);
  get_routine_update_callbacks_.erase(it);
}

bool RoutineService::BindCrosHealthdDiagnosticsServiceIfNeeded() {
  if (service_ptr_.is_bound())
    return true;

  auto request = mojo::MakeRequest(&service_ptr_);

  service_ptr_.set_connection_error_handler(base::Bind(
      &RoutineService::OnDisconnect, weak_ptr_factory_.GetWeakPtr()));

  if (!delegate_->GetCrosHealthdDiagnosticsService(std::move(request)))
    return false;

  return true;
}

void RoutineService::OnDisconnect() {
  VLOG(1) << "cros_healthd Mojo connection closed.";
  RunInFlightCallbacks();
  service_ptr_.reset();
}

void RoutineService::RunInFlightCallbacks() {
  for (auto& it : get_available_routines_callbacks_) {
    it.second.Run(std::vector<grpc_api::DiagnosticRoutine>{},
                  grpc_api::ROUTINE_SERVICE_STATUS_UNAVAILABLE);
  }
  get_available_routines_callbacks_.clear();

  for (auto& it : run_routine_callbacks_) {
    it.second.Run(0 /* uuid */, grpc_api::ROUTINE_STATUS_FAILED_TO_START,
                  grpc_api::ROUTINE_SERVICE_STATUS_UNAVAILABLE);
  }
  run_routine_callbacks_.clear();

  for (auto& it : get_routine_update_callbacks_) {
    it.second.second.Run(
        it.second.first /* uuid */, grpc_api::ROUTINE_STATUS_ERROR,
        0 /* progress_percent */, grpc_api::ROUTINE_USER_MESSAGE_UNSET,
        "" /* output */, "" /* status_message */,
        grpc_api::ROUTINE_SERVICE_STATUS_UNAVAILABLE);
  }
  get_routine_update_callbacks_.clear();
}

}  // namespace diagnostics
