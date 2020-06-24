// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_FP_INFO_COMMAND_H_
#define BIOD_FP_INFO_COMMAND_H_

#include <memory>

#include "biod/ec_command.h"
#include "biod/ec_command_async.h"
#include "biod/fp_sensor_errors.h"
#include "biod/sensor_id.h"
#include "biod/sensor_image.h"
#include "biod/template_info.h"

namespace biod {

class FpInfoCommand : public EcCommand<EmptyParam, struct ec_response_fp_info> {
 public:
  static const int kDeadPixelsUnknown = -1;

  FpInfoCommand() : EcCommand(EC_CMD_FP_INFO, kVersionOne) {}
  ~FpInfoCommand() override = default;

  SensorId* sensor_id();
  SensorImage* sensor_image();
  TemplateInfo* template_info();
  int NumDeadPixels();
  FpSensorErrors GetFpSensorErrors();

 private:
  std::unique_ptr<SensorId> sensor_id_;
  std::unique_ptr<SensorImage> sensor_image_;
  std::unique_ptr<TemplateInfo> template_info_;
};

}  // namespace biod

#endif  // BIOD_FP_INFO_COMMAND_H_
