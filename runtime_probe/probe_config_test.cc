/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <utility>

#include <base/json/json_reader.h>
#include <base/values.h>
#include <brillo/map_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "runtime_probe/functions/sysfs.h"
#include "runtime_probe/probe_config.h"

namespace runtime_probe {

TEST(ProbeConfigTest, LoadConfig) {
  const char* config_content = R"({
    "sysfs_battery": {
      "generic": {
        "eval": {
          "sysfs": {
            "dir_path": "/sys/class/power_supply/BAT0",
            "keys": ["model_name", "charge_full_design", "cycle_count"]
          }
        },
        "keys": [],
        "expect": {},
        "information": {}
      }
    }
  })";
  auto dict_value = base::JSONReader::Read(config_content);

  EXPECT_TRUE(dict_value.has_value());

  auto probe_config = ProbeConfig::FromValue(*dict_value);

  EXPECT_NE(probe_config, nullptr);

  EXPECT_THAT(brillo::GetMapKeys(probe_config->category_),
              ::testing::UnorderedElementsAre("sysfs_battery"));

  const auto& category = probe_config->category_["sysfs_battery"];

  EXPECT_EQ(category->category_name_, "sysfs_battery");
  EXPECT_THAT(brillo::GetMapKeys(category->component_),
              ::testing::UnorderedElementsAre("generic"));

  const auto& probe_statement = category->component_["generic"];

  EXPECT_EQ(probe_statement->component_name_, "generic");
  EXPECT_EQ(probe_statement->key_.size(), 0);
  EXPECT_NE(probe_statement->expect_, nullptr);
  EXPECT_EQ(probe_statement->information_->DictSize(), 0);
  EXPECT_NE(probe_statement->eval_, nullptr);

  const SysfsFunction* probe_function =
      dynamic_cast<SysfsFunction*>(probe_statement->eval_.get());

  EXPECT_NE(probe_function, nullptr);
}

}  // namespace runtime_probe
