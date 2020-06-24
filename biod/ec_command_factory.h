// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_EC_COMMAND_FACTORY_H_
#define BIOD_EC_COMMAND_FACTORY_H_

#include <memory>
#include <string>

#include "biod/cros_fp_device_interface.h"
#include "biod/fp_context_command_factory.h"
#include "biod/fp_info_command.h"

namespace biod {

class EcCommandFactoryInterface {
 public:
  virtual ~EcCommandFactoryInterface() = default;

  virtual std::unique_ptr<EcCommandInterface> FpContextCommand(
      CrosFpDeviceInterface* cros_fp, const std::string& user_id) = 0;

  virtual std::unique_ptr<biod::FpInfoCommand> FpInfoCommand() = 0;
  static_assert(std::is_base_of<EcCommandInterface, biod::FpInfoCommand>::value,
                "All commands created by this class should derive from "
                "EcCommandInterface");

  // TODO(b/144956297): Add factory methods for all of the EC
  // commands we use so that we can easily mock them for testing.
};

class EcCommandFactory : public EcCommandFactoryInterface {
 public:
  EcCommandFactory() = default;
  ~EcCommandFactory() override = default;
  // Disallow copies
  EcCommandFactory(const EcCommandFactory&) = delete;
  EcCommandFactory& operator=(const EcCommandFactory&) = delete;

  std::unique_ptr<EcCommandInterface> FpContextCommand(
      CrosFpDeviceInterface* cros_fp, const std::string& user_id) override;

  std::unique_ptr<biod::FpInfoCommand> FpInfoCommand() override;
};

}  // namespace biod

#endif  // BIOD_EC_COMMAND_FACTORY_H_
