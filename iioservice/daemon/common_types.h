// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_DAEMON_COMMON_TYPES_H_
#define IIOSERVICE_DAEMON_COMMON_TYPES_H_

#include <set>
#include <string>

#include <mojo/public/cpp/bindings/receiver_set.h>

#include <libmems/iio_device.h>

#include "mojo/sensor.mojom.h"

namespace iioservice {

constexpr char kSamplingFrequencyAvailable[] = "sampling_frequency_available";

struct ClientData {
  mojo::ReceiverId id;
  libmems::IioDevice* iio_device;
  std::set<int32_t> enabled_chn_indices;
  double frequency = -1;    // Hz
  uint32_t timeout = 5000;  // millisecond
  mojo::Remote<cros::mojom::SensorDeviceSamplesObserver> observer;
};

}  // namespace iioservice

#endif  // IIOSERVICE_DAEMON_COMMON_TYPES_H_
