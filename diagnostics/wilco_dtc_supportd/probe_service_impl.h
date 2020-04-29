// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_WILCO_DTC_SUPPORTD_PROBE_SERVICE_IMPL_H_
#define DIAGNOSTICS_WILCO_DTC_SUPPORTD_PROBE_SERVICE_IMPL_H_

#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/memory/weak_ptr.h>

#include "diagnostics/wilco_dtc_supportd/probe_service.h"
#include "mojo/cros_healthd.mojom.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

class ProbeServiceImpl final : public ProbeService {
 public:
  // |delegate| - Unowned pointer; must outlive this instance.
  explicit ProbeServiceImpl(Delegate* delegate);

  ProbeServiceImpl(const ProbeServiceImpl&) = delete;
  ProbeServiceImpl& operator=(const ProbeServiceImpl&) = delete;

  ~ProbeServiceImpl() override;

  // ProbeService overrides:
  void ProbeTelemetryInfo(
      std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum> categories,
      ProbeTelemetryInfoCallback callback) override;

 private:
  // Forwards and wraps the result of a ProbeTelemetryInfo call into a real
  // callback.
  void ForwardProbeTelemetryInfoResponse(
      size_t callback_key,
      chromeos::cros_healthd::mojom::TelemetryInfoPtr telemetry_info);

  // Binds |service_ptr_| to an implementation of CrosHealthdProbeService,
  // if it is not already bound. Returns false if wilco_dtc_supportd's mojo
  // service is not yet running and the binding cannot be attempted.
  bool BindCrosHealthdProbeServiceIfNeeded();
  // Disconnect handler called if the mojo connection to cros_healthd is lost.
  void OnDisconnect();
  // Runs all in flight callbacks.
  void RunInFlightCallbacks();

  // Unowned. Should outlive this instance.
  Delegate* delegate_ = nullptr;

  // Mojo interface to the CrosHealthdProbeService endpoint.
  //
  // In production this interface is implemented by the cros_healthd process.
  chromeos::cros_healthd::mojom::CrosHealthdProbeServicePtr service_ptr_;

  // The following map holds in flight callbacks to |service_ptr_|.
  // In case the remote mojo endpoint closes while there are any in flight
  // callbacks, the disconnect handler will call those callbacks with nullptr
  // response. This allows wilco_dtc_supportd to remain responsive if
  // cros_healthd dies.
  std::unordered_map<size_t, ProbeTelemetryInfoCallback> callbacks_;

  // Generator for the key used in the in flight callback map. Note that our
  // generation is very simple - just increment the generator when a call is
  // dispatched to cros_healthd. Since the map is only tracking callbacks which
  // are in flight, we don't anticipate having very many stored at a time, and
  // there should never be collisions if size_t wraps back around to zero.
  size_t next_callback_key_ = 0;

  // Must be the last class member.
  base::WeakPtrFactory<ProbeServiceImpl> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_PROBE_SERVICE_IMPL_H_
