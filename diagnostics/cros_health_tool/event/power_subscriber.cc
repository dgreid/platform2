// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/event/power_subscriber.h"

#include <iostream>
#include <utility>

#include <base/logging.h>

namespace diagnostics {

const char kHumanReadableOnAcInsertedEvent[] = "AC inserted";
const char kHumanReadableOnAcRemovedEvent[] = "AC removed";
const char kHumanReadableOnOsSuspendEvent[] = "OS suspend";
const char kHumanReadableOnOsResumeEvent[] = "OS resume";

PowerSubscriber::PowerSubscriber(
    chromeos::cros_healthd::mojom::CrosHealthdPowerObserverRequest request)
    : binding_{this /* impl */, std::move(request)} {
  DCHECK(binding_.is_bound());
}

PowerSubscriber::~PowerSubscriber() = default;

void PowerSubscriber::OnAcInserted() {
  PrintPowerNotification(PowerEventType::kOnAcInserted);
}

void PowerSubscriber::OnAcRemoved() {
  PrintPowerNotification(PowerEventType::kOnAcRemoved);
}

void PowerSubscriber::OnOsSuspend() {
  PrintPowerNotification(PowerEventType::kOnOsSuspend);
}

void PowerSubscriber::OnOsResume() {
  PrintPowerNotification(PowerEventType::kOnOsResume);
}

void PowerSubscriber::PrintPowerNotification(PowerEventType event) {
  auto itr = human_readable_power_events_.find(event);
  DCHECK(itr != human_readable_power_events_.end());
  std::cout << "Power event received: " << itr->second << std::endl;
}

}  // namespace diagnostics
