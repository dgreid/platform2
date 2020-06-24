// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_SENSOR_ID_H_
#define BIOD_SENSOR_ID_H_

#include <cstdint>

struct SensorId {
  SensorId(uint32_t vendor_id,
           uint32_t product_id,
           uint32_t model_id,
           uint32_t version)
      : vendor_id(vendor_id),
        product_id(product_id),
        model_id(model_id),
        version(version) {}

  uint32_t vendor_id = 0;
  uint32_t product_id = 0;
  uint32_t model_id = 0;
  uint32_t version = 0;
};

#endif  // BIOD_SENSOR_ID_H_
