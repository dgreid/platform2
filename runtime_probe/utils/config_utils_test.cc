// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>

#include <base/files/file_path.h>
#include <gtest/gtest.h>

#include "runtime_probe/utils/config_utils.h"

namespace runtime_probe {

namespace {
std::string GetTestDataPath(const std::string& file_name) {
  const char* src_dir = getenv("SRC");
  CHECK(src_dir != nullptr);
  return base::FilePath(src_dir)
      .Append("testdata")
      .Append(file_name)
      .MaybeAsASCII();
}
}  // namespace

TEST(ConfigParserTest, ReadFromFile) {
  // Random file path. The returned optional object should evaluate to false.
  EXPECT_FALSE(ParseProbeConfig("/random/file/path"));
  // File is not in JSON format. The returned object should evaluate to false.
  EXPECT_FALSE(ParseProbeConfig(GetTestDataPath("test.txt")));
  // testdata/probe_config.json is a valid JSON file. The returned object should
  // evaluate to true and contains non-empty Value,
  const auto probe_config_data =
      ParseProbeConfig(GetTestDataPath("probe_config.json"));
  EXPECT_TRUE(probe_config_data);
  EXPECT_FALSE(probe_config_data.value().config.DictEmpty());
  // Calculated by sha1sum testdata/probe_config.json
  EXPECT_EQ(probe_config_data.value().sha1_hash,
            "B4B67B8FB7B094783926CC581850C492C5A246A4");
}

}  // namespace runtime_probe
