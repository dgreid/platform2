// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_NETWORK_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_NETWORK_FETCHER_H_

#include <base/callback_forward.h>

#include "diagnostics/cros_healthd/system/context.h"
#include "mojo/cros_healthd_probe.mojom-forward.h"

namespace diagnostics {

// Responsible for gathering network information that is reported by
// cros_healthd.
class NetworkFetcher final {
 public:
  using FetchNetworkInfoCallback =
      base::OnceCallback<void(chromeos::cros_healthd::mojom::NetworkResultPtr)>;

  explicit NetworkFetcher(Context* context);
  NetworkFetcher(const NetworkFetcher&) = delete;
  NetworkFetcher& operator=(const NetworkFetcher&) = delete;
  ~NetworkFetcher();

  void FetchNetworkInfo(FetchNetworkInfoCallback callback);

 private:
  // Unowned pointer that outlives this NetworkFetcher instance.
  Context* const context_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_NETWORK_FETCHER_H_
