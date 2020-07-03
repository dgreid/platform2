// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/event/event_subscriber.h"

#include <utility>

#include <mojo/public/cpp/bindings/interface_request.h>

#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"
#include "mojo/cros_healthd_events.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

}  // namespace

EventSubscriber::EventSubscriber()
    : mojo_adapter_(CrosHealthdMojoAdapter::Create()) {
  DCHECK(mojo_adapter_);
}

EventSubscriber::~EventSubscriber() = default;

void EventSubscriber::SubscribeToBluetoothEvents() {
  mojo_ipc::CrosHealthdBluetoothObserverPtr observer_ptr;
  mojo_ipc::CrosHealthdBluetoothObserverRequest observer_request(
      mojo::MakeRequest(&observer_ptr));
  bluetooth_subscriber_ =
      std::make_unique<BluetoothSubscriber>(std::move(observer_request));
  mojo_adapter_->AddBluetoothObserver(std::move(observer_ptr));
}

void EventSubscriber::SubscribeToLidEvents() {
  mojo_ipc::CrosHealthdLidObserverPtr observer_ptr;
  mojo_ipc::CrosHealthdLidObserverRequest observer_request(
      mojo::MakeRequest(&observer_ptr));
  lid_subscriber_ =
      std::make_unique<LidSubscriber>(std::move(observer_request));
  mojo_adapter_->AddLidObserver(std::move(observer_ptr));
}

void EventSubscriber::SubscribeToPowerEvents() {
  mojo_ipc::CrosHealthdPowerObserverPtr observer_ptr;
  mojo_ipc::CrosHealthdPowerObserverRequest observer_request(
      mojo::MakeRequest(&observer_ptr));
  power_subscriber_ =
      std::make_unique<PowerSubscriber>(std::move(observer_request));
  mojo_adapter_->AddPowerObserver(std::move(observer_ptr));
}

}  // namespace diagnostics
