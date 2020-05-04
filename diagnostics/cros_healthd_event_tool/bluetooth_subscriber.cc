// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd_event_tool/bluetooth_subscriber.h"

#include <iostream>
#include <utility>

#include <base/logging.h>

namespace diagnostics {

const char kHumanReadableOnAdapterAddedEvent[] = "Adapter added";
const char kHumanReadableOnAdapterRemovedEvent[] = "Adapter removed";
const char kHumanReadableOnAdapterPropertyChangedEvent[] =
    "Adapter property changed";
const char kHumanReadableOnDeviceAddedEvent[] = "Device added";
const char kHumanReadableOnDeviceRemovedEvent[] = "Device removed";
const char kHumanReadableOnDevicePropertyChangedEvent[] =
    "Device property changed";

BluetoothSubscriber::BluetoothSubscriber(
    chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserverRequest request)
    : binding_{this /* impl */, std::move(request)} {
  DCHECK(binding_.is_bound());
}

BluetoothSubscriber::~BluetoothSubscriber() = default;

void BluetoothSubscriber::OnAdapterAdded() {
  PrintBluetoothEvent(BluetoothEventType::kOnAdapterAdded);
}

void BluetoothSubscriber::OnAdapterRemoved() {
  PrintBluetoothEvent(BluetoothEventType::kOnAdapterRemoved);
}

void BluetoothSubscriber::OnAdapterPropertyChanged() {
  PrintBluetoothEvent(BluetoothEventType::kOnAdapterPropertyChanged);
}

void BluetoothSubscriber::OnDeviceAdded() {
  PrintBluetoothEvent(BluetoothEventType::kOnDeviceAdded);
}

void BluetoothSubscriber::OnDeviceRemoved() {
  PrintBluetoothEvent(BluetoothEventType::kOnDeviceRemoved);
}

void BluetoothSubscriber::OnDevicePropertyChanged() {
  PrintBluetoothEvent(BluetoothEventType::kOnDevicePropertyChanged);
}

void BluetoothSubscriber::PrintBluetoothEvent(BluetoothEventType event) {
  auto itr = human_readable_bluetooth_events_.find(event);
  DCHECK(itr != human_readable_bluetooth_events_.end());
  std::cout << "Bluetooth event received: " << itr->second << std::endl;
}

}  // namespace diagnostics
