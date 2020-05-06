// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd_event_tool/lid_subscriber.h"

#include <iostream>
#include <string>
#include <utility>

#include <base/logging.h>

namespace diagnostics {

namespace {

constexpr char kHumanReadableOnLidClosedEvent[] = "Lid closed";
constexpr char kHumanReadableOnLidOpenedEvent[] = "Lid opened";

// Enumeration of the different lid event types.
enum class LidEventType {
  kOnLidClosed,
  kOnLidOpened,
};

std::string GetHumanReadableEvent(LidEventType event) {
  switch (event) {
    case LidEventType::kOnLidClosed:
      return kHumanReadableOnLidClosedEvent;
    case LidEventType::kOnLidOpened:
      return kHumanReadableOnLidOpenedEvent;
  }
}

void PrintLidNotification(LidEventType event) {
  std::cout << "Lid event received: " << GetHumanReadableEvent(event)
            << std::endl;
}

}  // namespace

LidSubscriber::LidSubscriber(
    chromeos::cros_healthd::mojom::CrosHealthdLidObserverRequest request)
    : binding_{this /* impl */, std::move(request)} {
  DCHECK(binding_.is_bound());
}

LidSubscriber::~LidSubscriber() = default;

void LidSubscriber::OnLidClosed() {
  PrintLidNotification(LidEventType::kOnLidClosed);
}

void LidSubscriber::OnLidOpened() {
  PrintLidNotification(LidEventType::kOnLidOpened);
}

}  // namespace diagnostics
