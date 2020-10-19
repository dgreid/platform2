// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_MOCK_EC_COMMAND_FACTORY_H_
#define BIOD_MOCK_EC_COMMAND_FACTORY_H_

#include <memory>
#include <string>

#include <gmock/gmock.h>

#include "biod/ec_command_factory.h"

namespace biod {

class MockEcCommandFactory : public EcCommandFactoryInterface {
 public:
  MockEcCommandFactory() = default;
  ~MockEcCommandFactory() override = default;

  MOCK_METHOD(std::unique_ptr<EcCommandInterface>,
              FpContextCommand,
              (CrosFpDeviceInterface * cros_fp, const std::string& user_id),
              (override));
  MOCK_METHOD(std::unique_ptr<biod::FpFlashProtectCommand>,
              FpFlashProtectCommand,
              (const uint32_t flags, const uint32_t mask),
              (override));
  MOCK_METHOD(std::unique_ptr<biod::FpInfoCommand>,
              FpInfoCommand,
              (),
              (override));
  MOCK_METHOD(std::unique_ptr<biod::FpSeedCommand>,
              FpSeedCommand,
              (const brillo::SecureVector& seed, uint16_t seed_version),
              (override));
  MOCK_METHOD(std::unique_ptr<biod::FpFrameCommand>,
              FpFrameCommand,
              (int index, uint32_t frame_size, ssize_t max_read_size),
              (override));
};

}  // namespace biod

#endif  // BIOD_MOCK_EC_COMMAND_FACTORY_H_
