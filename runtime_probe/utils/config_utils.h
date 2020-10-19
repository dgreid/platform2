// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_UTILS_CONFIG_UTILS_H_
#define RUNTIME_PROBE_UTILS_CONFIG_UTILS_H_

#include <memory>
#include <string>

#include <base/optional.h>
#include <base/values.h>

namespace runtime_probe {

// Data structure to store different data extraced from probe config file.
struct ProbeConfigData {
  base::Value config;
  std::string sha1_hash;
};
// Parse |config_file_path|, the path of file containing probe config in json
base::Optional<ProbeConfigData> ParseProbeConfig(
    const std::string& config_file_path);

// Determine if allowed to load probe config assigned from CLI.
// Return true on success (allowed / load default instead), false for error.
// |probe_config_path| will be the allowed path if success.
bool GetProbeConfigPath(const std::string& probe_config_path_from_cli,
                        std::string* probe_config_path);

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_UTILS_CONFIG_UTILS_H_
