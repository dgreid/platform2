// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/cicerone/virtual_machine.h"

#include <arpa/inet.h>
#include <inttypes.h>

#include <algorithm>
#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/guid.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/stringprintf.h>
#include <google/protobuf/repeated_field.h>
#include <grpcpp/grpcpp.h>
#include <vm_cicerone/proto_bindings/cicerone_service.pb.h>
#include <vm_protos/proto_bindings/container_guest.grpc.pb.h>
#include <chromeos/constants/vm_tools.h>

using std::string;

namespace vm_tools {
namespace cicerone {
namespace {

// Default name to use for a container.
constexpr char kDefaultContainerName[] = "penguin";

// How long to wait before timing out on regular RPCs.
constexpr int64_t kDefaultTimeoutSeconds = 60;

// How long to wait while doing more complex operations like starting or
// creating a container.
constexpr int64_t kLongOperationTimeoutSeconds = 120;

}  // namespace

VirtualMachine::VirtualMachine(uint32_t cid, pid_t pid, std::string vm_token)
    : vsock_cid_(cid),
      pid_(pid),
      vm_token_(std::move(vm_token)),
      using_mock_tremplin_stub_(false),
      weak_ptr_factory_(this) {
  DCHECK((vsock_cid_ == 0) ^ vm_token_.empty());
  if (IsPluginVm()) {
    // This is a containerless VM, so create one container for this VM that uses
    // the same token as the VM itself.
    pending_containers_[vm_token_] = std::make_unique<Container>(
        kDefaultContainerName, vm_token_, weak_ptr_factory_.GetWeakPtr());
  }
}

VirtualMachine::~VirtualMachine() = default;

bool VirtualMachine::ConnectTremplin() {
  if (IsPluginVm())
    return false;
  if (!using_mock_tremplin_stub_) {
    std::string tremplin_address =
        base::StringPrintf("vsock:%u:%u", vsock_cid_, kTremplinPort);
    tremplin_stub_ = std::make_unique<vm_tools::tremplin::Tremplin::Stub>(
        grpc::CreateChannel(tremplin_address,
                            grpc::InsecureChannelCredentials()));
  }
  return tremplin_stub_ != nullptr;
}

void VirtualMachine::SetTremplinStubForTesting(
    std::unique_ptr<vm_tools::tremplin::Tremplin::StubInterface>
        mock_tremplin_stub) {
  CHECK(using_mock_tremplin_stub_ || !tremplin_stub_)
      << "Calling SetTremplinStubForTesting too late";
  using_mock_tremplin_stub_ = true;
  tremplin_stub_ = std::move(mock_tremplin_stub);
}

bool VirtualMachine::SetTimezone(
    const std::string& timezone_name,
    const std::string& posix_tz_string,
    const std::vector<std::string>& container_names,
    VirtualMachine::SetTimezoneResults* out_results,
    std::string* out_error) {
  DCHECK(out_results);
  DCHECK(out_error);
  if (!tremplin_stub_) {
    *out_error = "tremplin is not connected";
    return false;
  }
  LOG(INFO) << "Setting timezone to: " << timezone_name;

  vm_tools::tremplin::SetTimezoneRequest request;
  vm_tools::tremplin::SetTimezoneResponse response;

  request.set_timezone_name(timezone_name);
  request.set_posix_tz_string(posix_tz_string);
  for (const std::string& name : container_names)
    request.add_container_names(name);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = tremplin_stub_->SetTimezone(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "SetTimezone RPC failed: " << status.error_message();
    out_error->assign(status.error_message());
    return false;
  }

  int failure_count = response.failure_reasons_size();
  if (failure_count != 0) {
    LOG(ERROR) << "Failed to set timezone for " << failure_count
               << " containers";
  }
  out_results->failure_reasons.clear();
  for (int i = 0; i < failure_count; i++) {
    out_results->failure_reasons.push_back(response.failure_reasons(i));
  }

  out_results->successes = response.successes();
  return true;
}

bool VirtualMachine::RegisterContainer(const std::string& container_token,
                                       const uint32_t garcon_vsock_port,
                                       const std::string& container_ip) {
  // The token will be in the pending map if this is the first start of the
  // container. It will be in the main map if this is from a crash/restart of
  // the garcon process in the container.
  auto pending_iter = pending_containers_.find(container_token);
  if (pending_iter != pending_containers_.end()) {
    containers_[pending_iter->first] = std::move(pending_iter->second);
    pending_containers_.erase(pending_iter);
  } else {
    string container_name = GetContainerNameForToken(container_token);
    if (container_name.empty()) {
      return false;
    }
  }

  auto iter = containers_.find(container_token);
  std::string garcon_addr;
  if (IsPluginVm()) {
    garcon_addr = base::StringPrintf("unix:///run/vm_cicerone/client/%s.sock",
                                     container_token.c_str());
  } else if (garcon_vsock_port != 0) {
    garcon_addr = base::StringPrintf("vsock:%" PRIu32 ":%" PRIu32, vsock_cid_,
                                     garcon_vsock_port);
  } else {
    garcon_addr = base::StringPrintf("%s:%d", container_ip.c_str(),
                                     vm_tools::kGarconPort);
  }

  iter->second->ConnectToGarcon(garcon_addr);

  return true;
}

bool VirtualMachine::UnregisterContainer(const std::string& container_token) {
  auto iter = containers_.find(container_token);
  if (iter == containers_.end()) {
    return false;
  }
  containers_.erase(iter);
  return true;
}

std::string VirtualMachine::GenerateContainerToken(
    const std::string& container_name) {
  std::string token = base::GenerateGUID();
  pending_containers_[token] = std::make_unique<Container>(
      container_name, token, weak_ptr_factory_.GetWeakPtr());
  return token;
}

void VirtualMachine::CreateContainerWithTokenForTesting(
    const std::string& container_name, const std::string& container_token) {
  pending_containers_[container_token] = std::make_unique<Container>(
      container_name, container_token, weak_ptr_factory_.GetWeakPtr());
}

std::string VirtualMachine::GetContainerNameForToken(
    const std::string& container_token) {
  auto iter = containers_.find(container_token);
  if (iter == containers_.end()) {
    return "";
  }
  return iter->second->name();
}

Container* VirtualMachine::GetContainerForToken(
    const std::string& container_token) {
  auto iter = containers_.find(container_token);
  if (iter == containers_.end()) {
    return nullptr;
  }

  return iter->second.get();
}

Container* VirtualMachine::GetPendingContainerForToken(
    const std::string& container_token) {
  auto iter = pending_containers_.find(container_token);
  if (iter == pending_containers_.end()) {
    return nullptr;
  }

  return iter->second.get();
}

Container* VirtualMachine::GetContainerForName(
    const std::string& container_name) {
  for (auto iter = containers_.begin(); iter != containers_.end(); ++iter) {
    if (iter->second->name() == container_name) {
      return iter->second.get();
    }
  }
  return nullptr;
}

const OsRelease* VirtualMachine::GetOsReleaseForContainer(
    const std::string& container_name) const {
  auto iter = container_os_releases_.find(container_name);
  if (iter == container_os_releases_.end()) {
    return nullptr;
  }

  return &iter->second;
}

void VirtualMachine::SetOsReleaseForTesting(const std::string& container_name,
                                            const OsRelease& os_release) {
  container_os_releases_[container_name] = os_release;
}

std::vector<std::string> VirtualMachine::GetContainerNames() {
  std::vector<std::string> retval;
  for (auto& container_entry : containers_) {
    retval.emplace_back(container_entry.second->name());
  }
  return retval;
}

VirtualMachine::CreateLxdContainerStatus VirtualMachine::CreateLxdContainer(
    const std::string& container_name,
    const std::string& image_server,
    const std::string& image_alias,
    const std::string& rootfs_path,
    const std::string& metadata_path,
    std::string* out_error) {
  DCHECK(out_error);
  if (!tremplin_stub_) {
    *out_error = "tremplin is not connected";
    return VirtualMachine::CreateLxdContainerStatus::FAILED;
  }

  vm_tools::tremplin::CreateContainerRequest request;
  vm_tools::tremplin::CreateContainerResponse response;

  request.set_container_name(container_name);
  request.set_image_server(image_server);
  request.set_image_alias(image_alias);
  request.set_rootfs_path(rootfs_path);
  request.set_metadata_path(metadata_path);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kLongOperationTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status =
      tremplin_stub_->CreateContainer(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "CreateContainer RPC failed: " << status.error_message();
    out_error->assign(status.error_message());
    return VirtualMachine::CreateLxdContainerStatus::FAILED;
  }

  if (response.status() != tremplin::CreateContainerResponse::CREATING &&
      response.status() != tremplin::CreateContainerResponse::EXISTS) {
    LOG(ERROR) << "Failed to create LXD container: "
               << response.failure_reason();
    out_error->assign(response.failure_reason());
    return VirtualMachine::CreateLxdContainerStatus::FAILED;
  }

  if (response.status() == tremplin::CreateContainerResponse::EXISTS) {
    return VirtualMachine::CreateLxdContainerStatus::EXISTS;
  }

  return VirtualMachine::CreateLxdContainerStatus::CREATING;
}

VirtualMachine::DeleteLxdContainerStatus VirtualMachine::DeleteLxdContainer(
    const std::string& container_name, std::string* out_error) {
  DCHECK(out_error);
  if (!tremplin_stub_) {
    *out_error = "tremplin is not connected";
    return VirtualMachine::DeleteLxdContainerStatus::FAILED;
  }

  vm_tools::tremplin::DeleteContainerRequest request;
  vm_tools::tremplin::DeleteContainerResponse response;

  request.set_container_name(container_name);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status =
      tremplin_stub_->DeleteContainer(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "DeleteContainer RPC failed: " << status.error_message();
    out_error->assign(status.error_message());
    return VirtualMachine::DeleteLxdContainerStatus::FAILED;
  }

  switch (response.status()) {
    case tremplin::DeleteContainerResponse::DELETING:
      return VirtualMachine::DeleteLxdContainerStatus::DELETING;
    case tremplin::DeleteContainerResponse::DOES_NOT_EXIST:
      return VirtualMachine::DeleteLxdContainerStatus::DOES_NOT_EXIST;

    case tremplin::DeleteContainerResponse::UNKNOWN:
    case tremplin::DeleteContainerResponse::FAILED:
      LOG(ERROR) << "Failed to delete LXD container: "
                 << response.failure_reason();
      out_error->assign(response.failure_reason());
      return VirtualMachine::DeleteLxdContainerStatus::FAILED;

    default:
      LOG(ERROR) << "Unknown response received: " << response.status() << " "
                 << response.failure_reason();
      out_error->assign(response.failure_reason());
      return VirtualMachine::DeleteLxdContainerStatus::UNKNOWN;
  }
}

VirtualMachine::StartLxdContainerStatus VirtualMachine::StartLxdContainer(
    const std::string& container_name,
    const std::string& container_private_key,
    const std::string& host_public_key,
    const std::string& token,
    tremplin::StartContainerRequest::PrivilegeLevel privilege_level,
    std::string* out_error) {
  DCHECK(out_error);
  if (!tremplin_stub_) {
    *out_error = "tremplin is not connected";
    return VirtualMachine::StartLxdContainerStatus::FAILED;
  }

  vm_tools::tremplin::StartContainerRequest request;
  vm_tools::tremplin::StartContainerResponse response;

  request.set_container_name(container_name);
  request.set_container_private_key(container_private_key);
  request.set_host_public_key(host_public_key);
  request.set_token(token);
  request.set_privilege_level(privilege_level);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kLongOperationTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status =
      tremplin_stub_->StartContainer(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "StartContainer RPC failed: " << status.error_message();
    out_error->assign(status.error_message());
    return VirtualMachine::StartLxdContainerStatus::FAILED;
  }

  if (response.status() != tremplin::StartContainerResponse::RUNNING &&
      response.status() != tremplin::StartContainerResponse::FAILED) {
    // Set the os_release on the container. This information is known by
    // tremplin even if the container hasn't started fully. Note also that
    // Tremplin's version of OsRelease is not the same type as cicerone's, even
    // though they currently have the same fields.
    OsRelease os_release;
    os_release.set_pretty_name(response.os_release().pretty_name());
    os_release.set_name(response.os_release().name());
    os_release.set_version(response.os_release().version());
    os_release.set_version_id(response.os_release().version_id());
    os_release.set_id(response.os_release().id());
    // We don't use emplace() here because it doesn't replace existing values.
    container_os_releases_[container_name] = os_release;
  }

  switch (response.status()) {
    case tremplin::StartContainerResponse::STARTING:
      return VirtualMachine::StartLxdContainerStatus::STARTING;
    case tremplin::StartContainerResponse::STARTED:
      return VirtualMachine::StartLxdContainerStatus::STARTED;
    case tremplin::StartContainerResponse::REMAPPING:
      return VirtualMachine::StartLxdContainerStatus::REMAPPING;
    case tremplin::StartContainerResponse::RUNNING:
      return VirtualMachine::StartLxdContainerStatus::RUNNING;
    case tremplin::StartContainerResponse::FAILED:
      LOG(ERROR) << "Failed to start LXD container: "
                 << response.failure_reason();
      out_error->assign(response.failure_reason());
      return VirtualMachine::StartLxdContainerStatus::FAILED;
    default:
      return VirtualMachine::StartLxdContainerStatus::UNKNOWN;
  }
}

VirtualMachine::GetLxdContainerUsernameStatus
VirtualMachine::GetLxdContainerUsername(const std::string& container_name,
                                        std::string* out_username,
                                        std::string* out_homedir,
                                        std::string* out_error) {
  DCHECK(out_username);
  DCHECK(out_homedir);
  DCHECK(out_error);
  if (!tremplin_stub_) {
    *out_error = "tremplin is not connected";
    return VirtualMachine::GetLxdContainerUsernameStatus::FAILED;
  }

  vm_tools::tremplin::GetContainerUsernameRequest request;
  vm_tools::tremplin::GetContainerUsernameResponse response;

  request.set_container_name(container_name);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status =
      tremplin_stub_->GetContainerUsername(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "GetContainerUsername RPC failed: " << status.error_message();
    out_error->assign(status.error_message());
    return VirtualMachine::GetLxdContainerUsernameStatus::FAILED;
  }

  out_error->assign(response.failure_reason());
  out_username->assign(response.username());
  out_homedir->assign(response.homedir());

  switch (response.status()) {
    case tremplin::GetContainerUsernameResponse::UNKNOWN:
      return VirtualMachine::GetLxdContainerUsernameStatus::UNKNOWN;
    case tremplin::GetContainerUsernameResponse::SUCCESS:
      return VirtualMachine::GetLxdContainerUsernameStatus::SUCCESS;
    case tremplin::GetContainerUsernameResponse::CONTAINER_NOT_FOUND:
      return VirtualMachine::GetLxdContainerUsernameStatus::CONTAINER_NOT_FOUND;
    case tremplin::GetContainerUsernameResponse::CONTAINER_NOT_RUNNING:
      return VirtualMachine::GetLxdContainerUsernameStatus::
          CONTAINER_NOT_RUNNING;
    case tremplin::GetContainerUsernameResponse::USER_NOT_FOUND:
      return VirtualMachine::GetLxdContainerUsernameStatus::USER_NOT_FOUND;
    case tremplin::GetContainerUsernameResponse::FAILED:
      return VirtualMachine::GetLxdContainerUsernameStatus::FAILED;
    default:
      return VirtualMachine::GetLxdContainerUsernameStatus::UNKNOWN;
  }
}

VirtualMachine::SetUpLxdContainerUserStatus
VirtualMachine::SetUpLxdContainerUser(const std::string& container_name,
                                      const std::string& container_username,
                                      std::string* out_username,
                                      std::string* out_error) {
  DCHECK(out_username);
  DCHECK(out_error);
  if (!tremplin_stub_) {
    *out_error = "tremplin is not connected";
    return VirtualMachine::SetUpLxdContainerUserStatus::FAILED;
  }

  vm_tools::tremplin::SetUpUserRequest request;
  vm_tools::tremplin::SetUpUserResponse response;

  request.set_container_name(container_name);
  request.set_container_username(container_username);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = tremplin_stub_->SetUpUser(&ctx, request, &response);
  out_username->assign(response.username());
  if (!status.ok()) {
    LOG(ERROR) << "SetUpUser RPC failed: " << status.error_message();
    out_error->assign(status.error_message());
    return VirtualMachine::SetUpLxdContainerUserStatus::FAILED;
  }

  if (response.status() != tremplin::SetUpUserResponse::EXISTS &&
      response.status() != tremplin::SetUpUserResponse::SUCCESS) {
    LOG(ERROR) << "Failed to set up user: " << response.failure_reason();
    out_error->assign(response.failure_reason());
    return VirtualMachine::SetUpLxdContainerUserStatus::FAILED;
  }

  if (response.status() == tremplin::SetUpUserResponse::EXISTS) {
    return VirtualMachine::SetUpLxdContainerUserStatus::EXISTS;
  }

  return VirtualMachine::SetUpLxdContainerUserStatus::SUCCESS;
}

VirtualMachine::GetLxdContainerInfoStatus VirtualMachine::GetLxdContainerInfo(
    const std::string& container_name,
    VirtualMachine::LxdContainerInfo* out_info,
    std::string* out_error) {
  DCHECK(out_info);
  DCHECK(out_error);
  if (!tremplin_stub_) {
    *out_error = "tremplin is not connected";
    return VirtualMachine::GetLxdContainerInfoStatus::FAILED;
  }

  vm_tools::tremplin::GetContainerInfoRequest request;
  vm_tools::tremplin::GetContainerInfoResponse response;

  request.set_container_name(container_name);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status =
      tremplin_stub_->GetContainerInfo(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "GetContainerInfo RPC failed: " << status.error_message();
    out_error->assign(status.error_message());
    return VirtualMachine::GetLxdContainerInfoStatus::FAILED;
  }

  out_info->ipv4_address = response.ipv4_address();
  out_error->assign(response.failure_reason());

  switch (response.status()) {
    case tremplin::GetContainerInfoResponse::RUNNING:
      return VirtualMachine::GetLxdContainerInfoStatus::RUNNING;
    case tremplin::GetContainerInfoResponse::STOPPED:
      return VirtualMachine::GetLxdContainerInfoStatus::STOPPED;
    case tremplin::GetContainerInfoResponse::NOT_FOUND:
      return VirtualMachine::GetLxdContainerInfoStatus::NOT_FOUND;
    default:
      return VirtualMachine::GetLxdContainerInfoStatus::UNKNOWN;
  }
}

VirtualMachine::ExportLxdContainerStatus VirtualMachine::ExportLxdContainer(
    const std::string& container_name,
    const std::string& export_path,
    std::string* out_error) {
  DCHECK(out_error);
  if (!tremplin_stub_) {
    *out_error = "tremplin is not connected";
    return VirtualMachine::ExportLxdContainerStatus::FAILED;
  }

  vm_tools::tremplin::ExportContainerRequest request;
  vm_tools::tremplin::ExportContainerResponse response;

  request.set_container_name(container_name);
  request.set_export_path(export_path);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status =
      tremplin_stub_->ExportContainer(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "ExportLxdContainer RPC failed: " << status.error_message()
               << " " << status.error_code();
    out_error->assign(status.error_message());
    return VirtualMachine::ExportLxdContainerStatus::FAILED;
  }

  switch (response.status()) {
    case tremplin::ExportContainerResponse::EXPORTING:
      return VirtualMachine::ExportLxdContainerStatus::EXPORTING;
    case tremplin::ExportContainerResponse::FAILED:
      return VirtualMachine::ExportLxdContainerStatus::FAILED;
    default:
      return VirtualMachine::ExportLxdContainerStatus::UNKNOWN;
  }
}

VirtualMachine::CancelExportLxdContainerStatus
VirtualMachine::CancelExportLxdContainer(
    const std::string& in_progress_container_name, std::string* out_error) {
  DCHECK(out_error);
  if (!tremplin_stub_) {
    *out_error = "tremplin is not connected";
    return VirtualMachine::CancelExportLxdContainerStatus::FAILED;
  }

  vm_tools::tremplin::CancelExportContainerRequest request;
  vm_tools::tremplin::CancelExportContainerResponse response;

  request.set_in_progress_container_name(in_progress_container_name);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status =
      tremplin_stub_->CancelExportContainer(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "CancelExportLxdContainer RPC failed: "
               << status.error_message() << " " << status.error_code();
    out_error->assign(status.error_message());
    return VirtualMachine::CancelExportLxdContainerStatus::FAILED;
  }

  switch (response.status()) {
    case tremplin::CancelExportContainerResponse::CANCEL_QUEUED:
      return VirtualMachine::CancelExportLxdContainerStatus::CANCEL_QUEUED;
    case tremplin::CancelExportContainerResponse::OPERATION_NOT_FOUND:
      return VirtualMachine::CancelExportLxdContainerStatus::
          OPERATION_NOT_FOUND;
    default:
      return VirtualMachine::CancelExportLxdContainerStatus::UNKNOWN;
  }
}

VirtualMachine::ImportLxdContainerStatus VirtualMachine::ImportLxdContainer(
    const std::string& container_name,
    const std::string& import_path,
    uint64_t available_disk_space,
    std::string* out_error) {
  DCHECK(out_error);
  if (!tremplin_stub_) {
    *out_error = "tremplin is not connected";
    return VirtualMachine::ImportLxdContainerStatus::FAILED;
  }

  vm_tools::tremplin::ImportContainerRequest request;
  vm_tools::tremplin::ImportContainerResponse response;

  request.set_container_name(container_name);
  request.set_import_path(import_path);
  request.set_available_disk_space(available_disk_space);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status =
      tremplin_stub_->ImportContainer(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "ImportLxdContainer RPC failed: " << status.error_message()
               << " " << status.error_code();
    out_error->assign(status.error_message());
    return VirtualMachine::ImportLxdContainerStatus::FAILED;
  }

  switch (response.status()) {
    case tremplin::ImportContainerResponse::IMPORTING:
      return VirtualMachine::ImportLxdContainerStatus::IMPORTING;
    case tremplin::ImportContainerResponse::FAILED:
      return VirtualMachine::ImportLxdContainerStatus::FAILED;
    default:
      return VirtualMachine::ImportLxdContainerStatus::UNKNOWN;
  }
}

VirtualMachine::CancelImportLxdContainerStatus
VirtualMachine::CancelImportLxdContainer(
    const std::string& in_progress_container_name, std::string* out_error) {
  DCHECK(out_error);
  if (!tremplin_stub_) {
    *out_error = "tremplin is not connected";
    return VirtualMachine::CancelImportLxdContainerStatus::FAILED;
  }

  vm_tools::tremplin::CancelImportContainerRequest request;
  vm_tools::tremplin::CancelImportContainerResponse response;

  request.set_in_progress_container_name(in_progress_container_name);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status =
      tremplin_stub_->CancelImportContainer(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "CancelImportLxdContainer RPC failed: "
               << status.error_message() << " " << status.error_code();
    out_error->assign(status.error_message());
    return VirtualMachine::CancelImportLxdContainerStatus::FAILED;
  }

  switch (response.status()) {
    case tremplin::CancelImportContainerResponse::CANCEL_QUEUED:
      return VirtualMachine::CancelImportLxdContainerStatus::CANCEL_QUEUED;
    case tremplin::CancelImportContainerResponse::OPERATION_NOT_FOUND:
      return VirtualMachine::CancelImportLxdContainerStatus::
          OPERATION_NOT_FOUND;
    default:
      return VirtualMachine::CancelImportLxdContainerStatus::UNKNOWN;
  }
}

namespace {
UpgradeContainerRequest::Version VersionFromOsRelease(
    const OsRelease* os_release) {
  if (os_release->id() != "debian") {
    return UpgradeContainerRequest::UNKNOWN;
  }
  if (os_release->version_id() == "9") {
    return UpgradeContainerRequest::DEBIAN_STRETCH;
  }
  if (os_release->version_id() == "10") {
    return UpgradeContainerRequest::DEBIAN_BUSTER;
  }
  return UpgradeContainerRequest::UNKNOWN;
}

vm_tools::tremplin::UpgradeContainerRequest::Version ConvertVersion(
    UpgradeContainerRequest::Version version) {
  switch (version) {
    case UpgradeContainerRequest::UNKNOWN:
      return vm_tools::tremplin::UpgradeContainerRequest::UNKNOWN;
    case UpgradeContainerRequest::DEBIAN_STRETCH:
      return vm_tools::tremplin::UpgradeContainerRequest::DEBIAN_STRETCH;
    case UpgradeContainerRequest::DEBIAN_BUSTER:
      return vm_tools::tremplin::UpgradeContainerRequest::DEBIAN_BUSTER;
    default:
      return vm_tools::tremplin::UpgradeContainerRequest::UNKNOWN;
  }
}
}  // namespace

VirtualMachine::UpgradeContainerStatus VirtualMachine::UpgradeContainer(
    const Container* container,
    const UpgradeContainerRequest::Version& source_version,
    const UpgradeContainerRequest::Version& target_version,
    std::string* out_error) {
  DCHECK(container);
  DCHECK(out_error);
  const OsRelease* os_release = GetOsReleaseForContainer(container->name());
  if (!os_release) {
    out_error->assign("No OsRelease data found for container. Can't upgrade.");
    LOG(ERROR) << *out_error;
    return VirtualMachine::UpgradeContainerStatus::FAILED;
  }
  auto current_version = VersionFromOsRelease(os_release);
  if (current_version == UpgradeContainerRequest::UNKNOWN) {
    LOG(ERROR) << "Unknown OsRelease. Can't upgrade.";
    return VirtualMachine::UpgradeContainerStatus::NOT_SUPPORTED;
  }
  if (current_version == target_version) {
    return VirtualMachine::UpgradeContainerStatus::ALREADY_UPGRADED;
  }

  vm_tools::tremplin::UpgradeContainerRequest request;
  vm_tools::tremplin::UpgradeContainerResponse response;

  request.set_container_name(container->name());
  request.set_source_version(ConvertVersion(source_version));
  request.set_target_version(ConvertVersion(target_version));

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status =
      tremplin_stub_->UpgradeContainer(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "UpgradeLxdContainer RPC failed: " << status.error_message()
               << " " << status.error_code();
    out_error->assign(status.error_message());
    return VirtualMachine::UpgradeContainerStatus::FAILED;
  }
  out_error->assign(response.failure_reason());
  switch (response.status()) {
    case tremplin::UpgradeContainerResponse::UNKNOWN:
      return VirtualMachine::UpgradeContainerStatus::UNKNOWN;
    case tremplin::UpgradeContainerResponse::STARTED:
      return VirtualMachine::UpgradeContainerStatus::STARTED;
    case tremplin::UpgradeContainerResponse::ALREADY_RUNNING:
      return VirtualMachine::UpgradeContainerStatus::ALREADY_RUNNING;
    case tremplin::UpgradeContainerResponse::NOT_SUPPORTED:
      return VirtualMachine::UpgradeContainerStatus::NOT_SUPPORTED;
    case tremplin::UpgradeContainerResponse::ALREADY_UPGRADED:
      return VirtualMachine::UpgradeContainerStatus::ALREADY_UPGRADED;
    case tremplin::UpgradeContainerResponse::FAILED:
      return VirtualMachine::UpgradeContainerStatus::FAILED;
    default:
      return VirtualMachine::UpgradeContainerStatus::UNKNOWN;
  }
}

VirtualMachine::CancelUpgradeContainerStatus
VirtualMachine::CancelUpgradeContainer(Container* container,
                                       std::string* out_error) {
  DCHECK(container);
  DCHECK(out_error);
  vm_tools::tremplin::CancelUpgradeContainerRequest request;
  vm_tools::tremplin::CancelUpgradeContainerResponse response;

  request.set_container_name(container->name());

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status =
      tremplin_stub_->CancelUpgradeContainer(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "CancelUpgradeLxdContainer RPC failed: "
               << status.error_message() << " " << status.error_code();
    out_error->assign(status.error_message());
    return VirtualMachine::CancelUpgradeContainerStatus::FAILED;
  }
  out_error->assign(response.failure_reason());
  switch (response.status()) {
    case tremplin::CancelUpgradeContainerResponse::UNKNOWN:
      return VirtualMachine::CancelUpgradeContainerStatus::UNKNOWN;
    case tremplin::CancelUpgradeContainerResponse::NOT_RUNNING:
      return VirtualMachine::CancelUpgradeContainerStatus::NOT_RUNNING;
    case tremplin::CancelUpgradeContainerResponse::CANCELLED:
      return VirtualMachine::CancelUpgradeContainerStatus::CANCELLED;
    case tremplin::CancelUpgradeContainerResponse::FAILED:
      return VirtualMachine::CancelUpgradeContainerStatus::FAILED;
    default:
      return VirtualMachine::CancelUpgradeContainerStatus::UNKNOWN;
  }
}

VirtualMachine::StartLxdStatus VirtualMachine::StartLxd(
    bool reset_lxd_db, std::string* out_error) {
  DCHECK(out_error);
  vm_tools::tremplin::StartLxdRequest request;
  vm_tools::tremplin::StartLxdResponse response;

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  request.set_reset_lxd_db(reset_lxd_db);

  grpc::Status status = tremplin_stub_->StartLxd(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "StartLxd RPC failed: " << status.error_message() << " "
               << status.error_code();
    out_error->assign(status.error_message());
    return VirtualMachine::StartLxdStatus::FAILED;
  }
  out_error->assign(response.failure_reason());
  switch (response.status()) {
    case tremplin::StartLxdResponse::UNKNOWN:
      return VirtualMachine::StartLxdStatus::UNKNOWN;
    case tremplin::StartLxdResponse::STARTING:
      return VirtualMachine::StartLxdStatus::STARTING;
    case tremplin::StartLxdResponse::ALREADY_RUNNING:
      return VirtualMachine::StartLxdStatus::ALREADY_RUNNING;
    case tremplin::StartLxdResponse::FAILED:
      return VirtualMachine::StartLxdStatus::FAILED;
    default:
      LOG(ERROR) << "Received unrecognised StartLxdStatus";
      return VirtualMachine::StartLxdStatus::UNKNOWN;
  }
}

void VirtualMachine::HostNetworkChanged() {
  if (!tremplin_stub_) {
    return;
  }

  vm_tools::tremplin::HostNetworkChangedRequest request;
  vm_tools::tremplin::HostNetworkChangedResponse response;

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status =
      tremplin_stub_->HostNetworkChanged(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "HostNetworkChanged RPC failed: " << status.error_message();
    return;
  }
}

bool VirtualMachine::GetTremplinDebugInfo(std::string* out) {
  if (!tremplin_stub_) {
    out->assign("Tremplin is not connected");
    return false;
  }

  vm_tools::tremplin::GetDebugInfoRequest request;
  vm_tools::tremplin::GetDebugInfoResponse response;

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kLongOperationTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = tremplin_stub_->GetDebugInfo(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "GetDebugInfo RPC failed: " << status.error_message();
    out->assign(status.error_message());
    return false;
  }
  out->assign(response.debug_information());
  return true;
}

}  // namespace cicerone
}  // namespace vm_tools
