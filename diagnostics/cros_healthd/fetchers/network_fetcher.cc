// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/network_fetcher.h"

#include <utility>

#include <base/callback.h>
#include <base/optional.h>

#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "mojo/cros_healthd_probe.mojom.h"
#include "mojo/network_health.mojom.h"

namespace diagnostics {

namespace {

namespace cros_healthd_ipc = ::chromeos::cros_healthd::mojom;
namespace network_health_ipc = ::chromeos::network_health::mojom;

// Forwards the response from Chrome's NetworkHealthService to the caller.
void HandleNetworkInfoResponse(
    base::OnceCallback<void(cros_healthd_ipc::NetworkResultPtr)> callback,
    base::Optional<network_health_ipc::NetworkHealthStatePtr> result) {
  if (result == base::nullopt) {
    std::move(callback).Run(cros_healthd_ipc::NetworkResult::NewError(
        CreateAndLogProbeError(cros_healthd_ipc::ErrorType::kServiceUnavailable,
                               "Network Health Service unavailable")));
    return;
  }

  auto info = cros_healthd_ipc::NetworkResult::New();
  info->set_network_health(std::move(result.value()));
  std::move(callback).Run(std::move(info));
}

}  // namespace

NetworkFetcher::NetworkFetcher(Context* context) : context_(context) {
  DCHECK(context_);
}

NetworkFetcher::~NetworkFetcher() = default;

void NetworkFetcher::FetchNetworkInfo(FetchNetworkInfoCallback callback) {
  context_->network_health_adapter()->GetNetworkHealthState(
      base::BindOnce(&HandleNetworkInfoResponse, std::move(callback)));
}

}  // namespace diagnostics
