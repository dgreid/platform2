// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_WILCO_DTC_SUPPORTD_MOJO_SERVICE_H_
#define DIAGNOSTICS_WILCO_DTC_SUPPORTD_MOJO_SERVICE_H_

#include <string>
#include <vector>

#include <base/callback.h>
#include <base/macros.h>
#include <base/optional.h>
#include <base/strings/string_piece.h>
#include <grpcpp/grpcpp.h>
#include <mojo/public/cpp/bindings/binding.h>
#include <mojo/public/cpp/system/buffer.h>

#include "diagnostics/wilco_dtc_supportd/mojo_grpc_adapter.h"
#include "mojo/cros_healthd.mojom.h"
#include "mojo/wilco_dtc_supportd.mojom.h"

namespace diagnostics {

class MojoGrpcAdapter;

// Implements the "WilcoDtcSupportdService" Mojo interface exposed by the
// wilco_dtc_supportd daemon (see the API definition at
// mojo/wilco_dtc_supportd.mojom)
class MojoService final
    : public chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdService {
 public:
  using MojomWilcoDtcSupportdClientPtr =
      chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdClientPtr;
  using MojomWilcoDtcSupportdService =
      chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdService;
  using MojomWilcoDtcSupportdServiceRequest =
      chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdServiceRequest;
  using MojomWilcoDtcSupportdWebRequestHttpMethod =
      chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdWebRequestHttpMethod;
  using MojomWilcoDtcSupportdWebRequestStatus =
      chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdWebRequestStatus;
  using MojomWilcoDtcSupportdEvent =
      chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdEvent;
  using MojomSendWilcoDtcMessageToUiCallback =
      base::Callback<void(grpc::Status, base::StringPiece)>;
  using MojomPerformWebRequestCallback = base::Callback<void(
      MojomWilcoDtcSupportdWebRequestStatus, int, base::StringPiece)>;
  using MojomGetConfigurationDataCallback =
      base::OnceCallback<void(const std::string&)>;

  // |grpc_adapter| - used to forward calls to wilco gRPC clients.
  // |self_interface_request| - Mojo interface request that will be fulfilled
  // by this instance. In production, this interface request is created by the
  // browser process, and allows the browser to call our methods.
  // |client_ptr| - Mojo interface to the WilcoDtcSupportdServiceClient
  // endpoint. In production, it allows this instance to call browser's methods.
  MojoService(MojoGrpcAdapter* grpc_adapter,
              MojomWilcoDtcSupportdServiceRequest self_interface_request,
              MojomWilcoDtcSupportdClientPtr client_ptr);
  MojoService(const MojoService&) = delete;
  MojoService& operator=(const MojoService&) = delete;

  ~MojoService() override;

  // chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdService overrides:
  void SendUiMessageToWilcoDtc(
      mojo::ScopedHandle json_message,
      SendUiMessageToWilcoDtcCallback callback) override;
  void NotifyConfigurationDataChanged() override;

  // Calls to WilcoDtcSupportdClient.
  void SendWilcoDtcMessageToUi(
      const std::string& json,
      const MojomSendWilcoDtcMessageToUiCallback& callback);
  void PerformWebRequest(MojomWilcoDtcSupportdWebRequestHttpMethod http_method,
                         const std::string& url,
                         const std::vector<std::string>& headers,
                         const std::string& request_body,
                         const MojomPerformWebRequestCallback& callback);
  void GetConfigurationData(MojomGetConfigurationDataCallback callback);
  void HandleEvent(const MojomWilcoDtcSupportdEvent event);
  void GetCrosHealthdDiagnosticsService(
      chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsServiceRequest
          service);
  void GetCrosHealthdProbeService(
      chromeos::cros_healthd::mojom::CrosHealthdProbeServiceRequest service);

 private:
  // Unowned. Adapter to connect to Wilco gRPC clients.
  MojoGrpcAdapter* const grpc_adapter_;

  // Mojo binding that connects |this| with the message pipe, allowing the
  // remote end to call our methods.
  const mojo::Binding<MojomWilcoDtcSupportdService> self_binding_;

  // Mojo interface to the WilcoDtcSupportdServiceClient endpoint.
  //
  // In production this interface is implemented in the Chrome browser process.
  MojomWilcoDtcSupportdClientPtr client_ptr_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_MOJO_SERVICE_H_
