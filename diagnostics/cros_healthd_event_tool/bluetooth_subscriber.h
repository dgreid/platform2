// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EVENT_TOOL_BLUETOOTH_SUBSCRIBER_H_
#define DIAGNOSTICS_CROS_HEALTHD_EVENT_TOOL_BLUETOOTH_SUBSCRIBER_H_

#include <map>
#include <string>

#include <mojo/public/cpp/bindings/binding.h>

#include "mojo/cros_healthd_events.mojom.h"

namespace diagnostics {

extern const char kHumanReadableOnAdapterAddedEvent[];
extern const char kHumanReadableOnAdapterRemovedEvent[];
extern const char kHumanReadableOnAdapterPropertyChangedEvent[];
extern const char kHumanReadableOnDeviceAddedEvent[];
extern const char kHumanReadableOnDeviceRemovedEvent[];
extern const char kHumanReadableOnDevicePropertyChangedEvent[];

// This class subscribes to cros_healthd's Bluetooth events and outputs any
// notifications received to stdout.
class BluetoothSubscriber final
    : public chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserver {
 public:
  explicit BluetoothSubscriber(
      chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserverRequest
          request);
  BluetoothSubscriber(const BluetoothSubscriber&) = delete;
  BluetoothSubscriber& operator=(const BluetoothSubscriber&) = delete;
  ~BluetoothSubscriber();

  // chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserver overrides:
  void OnAdapterAdded() override;
  void OnAdapterRemoved() override;
  void OnAdapterPropertyChanged() override;
  void OnDeviceAdded() override;
  void OnDeviceRemoved() override;
  void OnDevicePropertyChanged() override;

 private:
  // Enumeration of the different Bluetooth event types.
  enum class BluetoothEventType {
    kOnAdapterAdded,
    kOnAdapterRemoved,
    kOnAdapterPropertyChanged,
    kOnDeviceAdded,
    kOnDeviceRemoved,
    kOnDevicePropertyChanged,
  };

  // Prints a human-readable string to stdout for the received Bluetooth event.
  void PrintBluetoothEvent(BluetoothEventType event);

  // Maps human-readable strings to each BluetoothEventType.
  const std::map<BluetoothEventType, std::string>
      human_readable_bluetooth_events_ = {
          {BluetoothEventType::kOnAdapterAdded,
           kHumanReadableOnAdapterAddedEvent},
          {BluetoothEventType::kOnAdapterRemoved,
           kHumanReadableOnAdapterRemovedEvent},
          {BluetoothEventType::kOnAdapterPropertyChanged,
           kHumanReadableOnAdapterPropertyChangedEvent},
          {BluetoothEventType::kOnDeviceAdded,
           kHumanReadableOnDeviceAddedEvent},
          {BluetoothEventType::kOnDeviceRemoved,
           kHumanReadableOnDeviceRemovedEvent},
          {BluetoothEventType::kOnDevicePropertyChanged,
           kHumanReadableOnDevicePropertyChangedEvent}};

  // Allows the remote cros_healthd to call BluetoothSubscriber's
  // CrosHealthdBluetoothObserver methods.
  const mojo::Binding<
      chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserver>
      binding_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EVENT_TOOL_BLUETOOTH_SUBSCRIBER_H_
