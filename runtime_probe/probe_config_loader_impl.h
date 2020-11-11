// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_PROBE_CONFIG_LOADER_IMPL_H_
#define RUNTIME_PROBE_PROBE_CONFIG_LOADER_IMPL_H_

#include "runtime_probe/probe_config_loader.h"

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/optional.h>
#include <base/values.h>
#include <chromeos-config/libcros_config/cros_config.h>
#include <gtest/gtest.h>

#include "runtime_probe/system_property.h"

namespace runtime_probe {

constexpr char kCrosConfigModelNamePath[] = "/";
constexpr char kCrosConfigModelNameKey[] = "name";
constexpr char kRuntimeProbeConfigDir[] = "etc/runtime_probe";
constexpr char kRuntimeProbeConfigName[] = "probe_config.json";

// ProbeConfigLoaderImpl includes all operations and logics related to probe
// config loading.

class ProbeConfigLoaderImpl : public ProbeConfigLoader {
 public:
  ProbeConfigLoaderImpl();

  ProbeConfigLoaderImpl(const ProbeConfigLoaderImpl&) = delete;
  ProbeConfigLoaderImpl& operator=(const ProbeConfigLoaderImpl&) = delete;

  // Loads probe config from the default path.  When cros_debug is disabled, the
  // default path will be:
  //     * `/etc/runtime_probe/<model_name>/probe_config.json`
  //     * `/etc/runtime_probe/probe_config.json`
  // When cros_debug is enabled, the config paths under the stateful partition
  // will also be included:
  //     * `/usr/local/etc/runtime_probe/<model_name>/probe_config.json`
  //     * `/usr/local/etc/runtime_probe/probe_config.json`
  //     * `/etc/runtime_probe/<model_name>/probe_config.json`
  //     * `/etc/runtime_probe/probe_config.json`
  base::Optional<ProbeConfigData> LoadDefault() const override;

  // Loads probe config from the given path.  This method only works when
  // cros_debug is enabled.
  base::Optional<ProbeConfigData> LoadFromFile(
      const base::FilePath& file_path) const override;

  std::vector<base::FilePath> GetDefaultPaths() const;

  void SetCrosConfigForTesting(
      std::unique_ptr<brillo::CrosConfigInterface> cros_config);
  void SetSystemProertyForTesting(
      std::unique_ptr<SystemProperty> system_property);
  void SetRootForTest(const base::FilePath& root);

 private:
  std::unique_ptr<brillo::CrosConfigInterface> cros_config_;
  std::unique_ptr<SystemProperty> system_property_;
  base::FilePath root_;

  int GetCrosDebug() const;
  std::string GetModelName() const;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_PROBE_CONFIG_LOADER_IMPL_H_
