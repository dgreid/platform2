// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/events/lid_events_impl.h"

#include <utility>

#include <base/logging.h>

namespace diagnostics {

LidEventsImpl::LidEventsImpl(PowerdAdapter* powerd_adapter)
    : powerd_adapter_(powerd_adapter) {
  DCHECK(powerd_adapter_);
}

LidEventsImpl::~LidEventsImpl() {
  if (is_observing_powerd_)
    powerd_adapter_->RemoveLidObserver(this);
}

void LidEventsImpl::AddObserver(
    chromeos::cros_healthd::mojom::CrosHealthdLidObserverPtr observer) {
  if (!is_observing_powerd_) {
    powerd_adapter_->AddLidObserver(this);
    is_observing_powerd_ = true;
  }
  observers_.AddPtr(std::move(observer));
}

void LidEventsImpl::OnLidClosedSignal() {
  observers_.ForAllPtrs(
      [](chromeos::cros_healthd::mojom::CrosHealthdLidObserver* observer) {
        observer->OnLidClosed();
      });

  StopObservingPowerdIfNecessary();
}

void LidEventsImpl::OnLidOpenedSignal() {
  observers_.ForAllPtrs(
      [](chromeos::cros_healthd::mojom::CrosHealthdLidObserver* observer) {
        observer->OnLidOpened();
      });

  StopObservingPowerdIfNecessary();
}

void LidEventsImpl::StopObservingPowerdIfNecessary() {
  if (!observers_.empty())
    return;

  powerd_adapter_->RemoveLidObserver(this);
  is_observing_powerd_ = false;
}

}  // namespace diagnostics
