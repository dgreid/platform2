// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/wilco_dtc_supportd/mojo_service.h"

#include <memory>
#include <utility>

#include <base/logging.h>
#include <base/memory/shared_memory.h>

#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/wilco_dtc_supportd/json_utils.h"

namespace diagnostics {

using SendUiMessageToWilcoDtcCallback =
    MojoService::SendUiMessageToWilcoDtcCallback;

namespace {

void ForwardMojoJsonResponse(
    SendUiMessageToWilcoDtcCallback mojo_response_callback,
    std::string response_json_message) {
  if (response_json_message.empty()) {
    std::move(mojo_response_callback)
        .Run(mojo::ScopedHandle() /* response_json_message */);
    return;
  }
  mojo::ScopedHandle response_json_message_handle =
      CreateReadOnlySharedMemoryRegionMojoHandle(
          base::StringPiece(response_json_message));
  std::move(mojo_response_callback)
      .Run(std::move(response_json_message_handle));
}

void ForwardMojoSendtoUiResponse(
    const MojoService::MojomSendWilcoDtcMessageToUiCallback& callback,
    mojo::ScopedHandle response_body_handle) {
  auto shm_mapping = GetReadOnlySharedMemoryMappingFromMojoHandle(
      std::move(response_body_handle));
  if (!shm_mapping.IsValid()) {
    LOG(ERROR) << "Failed to read data from mojo handle";
    callback.Run(grpc::Status(grpc::StatusCode::UNKNOWN,
                              "Failed to read data from mojo handle"),
                 base::StringPiece());
    return;
  }
  callback.Run(grpc::Status::OK,
               base::StringPiece(shm_mapping.GetMemoryAs<const char>(),
                                 shm_mapping.mapped_size()));
}

void ForwardMojoWebResponse(
    const MojoService::MojomPerformWebRequestCallback& callback,
    MojoService::MojomWilcoDtcSupportdWebRequestStatus status,
    int http_status,
    mojo::ScopedHandle response_body_handle) {
  if (!response_body_handle.is_valid()) {
    callback.Run(status, http_status, base::StringPiece());
    return;
  }
  auto shm_mapping = GetReadOnlySharedMemoryMappingFromMojoHandle(
      std::move(response_body_handle));
  if (!shm_mapping.IsValid()) {
    LOG(ERROR) << "Failed to read data from mojo handle";
    callback.Run(
        MojoService::MojomWilcoDtcSupportdWebRequestStatus::kNetworkError, 0,
        base::StringPiece());
    return;
  }
  callback.Run(status, http_status,
               base::StringPiece(shm_mapping.GetMemoryAs<const char>(),
                                 shm_mapping.mapped_size()));
}

}  // namespace

MojoService::MojoService(
    MojoGrpcAdapter* grpc_adapter,
    MojomWilcoDtcSupportdServiceRequest self_interface_request,
    MojomWilcoDtcSupportdClientPtr client_ptr)
    : grpc_adapter_(grpc_adapter),
      self_binding_(this /* impl */, std::move(self_interface_request)),
      client_ptr_(std::move(client_ptr)) {
  DCHECK(self_binding_.is_bound());
  DCHECK(grpc_adapter_);
  DCHECK(client_ptr_);
}

MojoService::~MojoService() = default;

void MojoService::SendUiMessageToWilcoDtc(
    mojo::ScopedHandle json_message, SendUiMessageToWilcoDtcCallback callback) {
  auto shm_mapping =
      GetReadOnlySharedMemoryMappingFromMojoHandle(std::move(json_message));
  if (!shm_mapping.IsValid()) {
    LOG(ERROR) << "Failed to read data from mojo handle";
    std::move(callback).Run(mojo::ScopedHandle() /* response_json_message */);
    return;
  }
  base::StringPiece json_message_content(shm_mapping.GetMemoryAs<const char>(),
                                         shm_mapping.mapped_size());

  std::string json_error_message;
  if (!IsJsonValid(json_message_content, &json_error_message)) {
    LOG(ERROR) << "Invalid JSON error: " << json_error_message;
    std::move(callback).Run(mojo::ScopedHandle() /* response_json_message */);
    return;
  }

  grpc_adapter_->SendGrpcUiMessageToWilcoDtc(
      json_message_content,
      base::Bind(&ForwardMojoJsonResponse, base::Passed(std::move(callback))));
}

void MojoService::NotifyConfigurationDataChanged() {
  grpc_adapter_->NotifyConfigurationDataChangedToWilcoDtc();
}

void MojoService::SendWilcoDtcMessageToUi(
    const std::string& json_message,
    const MojomSendWilcoDtcMessageToUiCallback& callback) {
  VLOG(1) << "SendWilcoDtcMessageToUi json_message=" << json_message;
  mojo::ScopedHandle json_message_mojo_handle =
      CreateReadOnlySharedMemoryRegionMojoHandle(json_message);
  if (!json_message_mojo_handle.is_valid()) {
    LOG(ERROR) << "Failed to create a mojo handle.";
    callback.Run(grpc::Status(grpc::StatusCode::UNKNOWN,
                              "Failed to read data from mojo handle"),
                 base::StringPiece());
    return;
  }

  client_ptr_->SendWilcoDtcMessageToUi(
      std::move(json_message_mojo_handle),
      base::Bind(&ForwardMojoSendtoUiResponse, callback));
}

void MojoService::PerformWebRequest(
    MojomWilcoDtcSupportdWebRequestHttpMethod http_method,
    const std::string& url,
    const std::vector<std::string>& headers,
    const std::string& request_body,
    const MojomPerformWebRequestCallback& callback) {
  DCHECK(client_ptr_);
  mojo::ScopedHandle url_handle =
      CreateReadOnlySharedMemoryRegionMojoHandle(url);
  if (!url_handle.is_valid()) {
    LOG(ERROR) << "Failed to create a mojo handle.";
    callback.Run(MojomWilcoDtcSupportdWebRequestStatus::kNetworkError, 0,
                 base::StringPiece());
    return;
  }

  std::vector<mojo::ScopedHandle> header_handles;
  for (const auto& header : headers) {
    header_handles.push_back(
        CreateReadOnlySharedMemoryRegionMojoHandle(header));
    if (!header_handles.back().is_valid()) {
      LOG(ERROR) << "Failed to create a mojo handle.";
      callback.Run(MojomWilcoDtcSupportdWebRequestStatus::kNetworkError, 0,
                   base::StringPiece());
      return;
    }
  }
  mojo::ScopedHandle request_body_handle =
      CreateReadOnlySharedMemoryRegionMojoHandle(request_body);
  // Invalid handle for an empty |request_body| does not cause an error.
  if (!request_body.empty() && !request_body_handle.is_valid()) {
    LOG(ERROR) << "Failed to create a mojo handle.";
    callback.Run(MojomWilcoDtcSupportdWebRequestStatus::kNetworkError, 0,
                 base::StringPiece());
    return;
  }

  client_ptr_->PerformWebRequest(http_method, std::move(url_handle),
                                 std::move(header_handles),
                                 std::move(request_body_handle),
                                 base::Bind(&ForwardMojoWebResponse, callback));
}

void MojoService::GetConfigurationData(
    MojomGetConfigurationDataCallback callback) {
  DCHECK(client_ptr_);
  client_ptr_->GetConfigurationData(std::move(callback));
}

void MojoService::HandleEvent(const MojomWilcoDtcSupportdEvent event) {
  client_ptr_->HandleEvent(event);
}

void MojoService::GetCrosHealthdDiagnosticsService(
    chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsServiceRequest
        service) {
  client_ptr_->GetCrosHealthdDiagnosticsService(std::move(service));
}

void MojoService::GetCrosHealthdProbeService(
    chromeos::cros_healthd::mojom::CrosHealthdProbeServiceRequest service) {
  client_ptr_->GetCrosHealthdProbeService(std::move(service));
}

}  // namespace diagnostics
