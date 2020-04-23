// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EVENT_TOOL_POWER_SUBSCRIBER_H_
#define DIAGNOSTICS_CROS_HEALTHD_EVENT_TOOL_POWER_SUBSCRIBER_H_

#include <map>
#include <string>

#include <mojo/public/cpp/bindings/binding.h>

#include "mojo/cros_healthd_events.mojom.h"

namespace diagnostics {

extern const char kHumanReadableOnAcInsertedEvent[];
extern const char kHumanReadableOnAcRemovedEvent[];
extern const char kHumanReadableOnOsSuspendEvent[];
extern const char kHumanReadableOnOsResumeEvent[];

// This class subscribes to cros_healthd's power notifications and outputs any
// notifications received to stdout.
class PowerSubscriber final
    : public chromeos::cros_healthd::mojom::CrosHealthdPowerObserver {
 public:
  explicit PowerSubscriber(
      chromeos::cros_healthd::mojom::CrosHealthdPowerObserverRequest request);
  PowerSubscriber(const PowerSubscriber&) = delete;
  PowerSubscriber& operator=(const PowerSubscriber&) = delete;
  ~PowerSubscriber();

  // chromeos::cros_healthd::mojom::CrosHealthdPowerObserver overrides:
  void OnAcInserted() override;
  void OnAcRemoved() override;
  void OnOsSuspend() override;
  void OnOsResume() override;

 private:
  // Enumeration of the different power event types.
  enum class PowerEventType {
    kOnAcInserted,
    kOnAcRemoved,
    kOnOsSuspend,
    kOnOsResume,
  };

  // Prints the received power event to stdout.
  void PrintPowerNotification(PowerEventType event);

  // Contains the human-readable strings corresponding to each PowerEventType.
  const std::map<PowerEventType, std::string> human_readable_power_events_ = {
      {PowerEventType::kOnAcInserted, kHumanReadableOnAcInsertedEvent},
      {PowerEventType::kOnAcRemoved, kHumanReadableOnAcRemovedEvent},
      {PowerEventType::kOnOsSuspend, kHumanReadableOnOsSuspendEvent},
      {PowerEventType::kOnOsResume, kHumanReadableOnOsResumeEvent}};

  // Allows the remote cros_healthd to call PowerSubscriber's
  // CrosHealthdPowerObserver methods.
  const mojo::Binding<chromeos::cros_healthd::mojom::CrosHealthdPowerObserver>
      binding_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EVENT_TOOL_POWER_SUBSCRIBER_H_
