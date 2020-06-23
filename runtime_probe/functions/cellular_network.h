/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef RUNTIME_PROBE_FUNCTIONS_CELLULAR_NETWORK_H_
#define RUNTIME_PROBE_FUNCTIONS_CELLULAR_NETWORK_H_

#include <memory>
#include <string>

#include <base/optional.h>

#include "runtime_probe/function_templates/network.h"

namespace runtime_probe {

class CellularNetworkFunction : public NetworkFunction {
 public:
  static constexpr auto function_name = "cellular_network";
  std::string GetFunctionName() const override { return function_name; }

  static std::unique_ptr<ProbeFunction> FromValue(
      const base::Value& dict_value) {
    if (dict_value.DictSize() != 0) {
      LOG(ERROR) << function_name << " dooes not take any arguement";
      return nullptr;
    }
    return std::make_unique<CellularNetworkFunction>();
  }

 protected:
  base::Optional<std::string> GetNetworkType() const override;

 private:
  static ProbeFunction::Register<CellularNetworkFunction> register_;
};

/* Register the CellularNetworkFunction */
REGISTER_PROBE_FUNCTION(CellularNetworkFunction);

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_CELLULAR_NETWORK_H_
