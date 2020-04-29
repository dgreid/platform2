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
#include <mojo/public/cpp/bindings/binding.h>
#include <mojo/public/cpp/system/buffer.h>

#include "mojo/cros_healthd.mojom.h"
#include "mojo/wilco_dtc_supportd.mojom.h"

namespace diagnostics {

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
      base::Callback<void(base::StringPiece)>;
  using MojomPerformWebRequestCallback = base::Callback<void(
      MojomWilcoDtcSupportdWebRequestStatus, int, base::StringPiece)>;
  using MojomGetConfigurationDataCallback =
      base::OnceCallback<void(const std::string&)>;

  class Delegate {
   public:
    using SendGrpcUiMessageToWilcoDtcCallback =
        base::Callback<void(std::string response_json_message)>;

    virtual ~Delegate() = default;

    // Called when wilco_dtc_supportd daemon mojo function
    // |SendUiMessageToWilcoDtc| was called.
    //
    // Calls gRPC HandleMessageFromUiRequest method on wilco_dtc and puts
    // |json_message| to the gRPC |HandleMessageFromUiRequest| request message.
    // Result of the call is returned via |callback|; if the request succeeded,
    // it will receive the message returned by the wilco_dtc.
    virtual void SendGrpcUiMessageToWilcoDtc(
        base::StringPiece json_message,
        const SendGrpcUiMessageToWilcoDtcCallback& callback) = 0;

    // Called when wilco_dtc_supportd daemon mojo function
    // |NotifyConfigurationDataChanged| was called.
    //
    // Calls gRPC HandleConfigurationDataChanged method on wilco_dtc to notify
    // that new JSON configuration data is available and can be retrieved by
    // calling |GetConfigurationData|.
    virtual void NotifyConfigurationDataChangedToWilcoDtc() = 0;
  };

  // |delegate| - Unowned pointer; must outlive this instance.
  // |self_interface_request| - Mojo interface request that will be fulfilled
  // by this instance. In production, this interface request is created by the
  // browser process, and allows the browser to call our methods.
  // |client_ptr| - Mojo interface to the WilcoDtcSupportdServiceClient
  // endpoint. In production, it allows this instance to call browser's methods.
  MojoService(Delegate* delegate,
              MojomWilcoDtcSupportdServiceRequest self_interface_request,
              MojomWilcoDtcSupportdClientPtr client_ptr);
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
  // Unowned. The delegate should outlive this instance.
  Delegate* const delegate_;

  // Mojo binding that connects |this| with the message pipe, allowing the
  // remote end to call our methods.
  const mojo::Binding<MojomWilcoDtcSupportdService> self_binding_;

  // Mojo interface to the WilcoDtcSupportdServiceClient endpoint.
  //
  // In production this interface is implemented in the Chrome browser process.
  MojomWilcoDtcSupportdClientPtr client_ptr_;

  DISALLOW_COPY_AND_ASSIGN(MojoService);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_MOJO_SERVICE_H_
