/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <cstring>

#include <base/json/json_reader.h>
#include <base/values.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "runtime_probe/functions/sysfs.h"

namespace runtime_probe {

void SysfsFunction::MockSysfsPathForTesting(base::FilePath sysfs_path) {
  CHECK(!sysfs_path.empty());
  // Can only override once.
  CHECK(sysfs_path_for_testing_.empty());
  sysfs_path_for_testing_ = sysfs_path;
}

TEST(SysfsFunctionTest, TestRead) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  auto dir_a = temp_dir.GetPath().Append("Da");
  base::CreateDirectory(dir_a);
  base::WriteFile(dir_a.Append("1"), "a1", strlen("a1"));

  auto dir_b = temp_dir.GetPath().Append("Db");
  base::CreateDirectory(dir_b);
  base::WriteFile(dir_b.Append("1"), "b1", strlen("b1"));
  base::WriteFile(dir_b.Append("2"), "b2", strlen("b2"));

  auto dir_c = temp_dir.GetPath().Append("Dc");
  base::CreateDirectory(dir_c);
  base::WriteFile(dir_c.Append("2"), "c2", strlen("c2"));

  base::DictionaryValue* dict_value = nullptr;
  auto json_val = base::JSONReader::Read(R"({
      "keys": ["1"],
      "optional_keys": ["2"]
  })");
  json_val->GetAsDictionary(&dict_value);

  dict_value->SetString("dir_path", temp_dir.GetPath().Append("D*").value());
  auto p = SysfsFunction::FromDictionaryValue(*dict_value);
  ASSERT_TRUE(p) << "Failed to create SysfsFunction: " << dict_value;

  auto f = dynamic_cast<SysfsFunction*>(p.get());
  ASSERT_TRUE(f) << "Loaded function is not a SysfsFunction";
  f->MockSysfsPathForTesting(temp_dir.GetPath());

  auto results = f->Eval();
  ASSERT_EQ(results.size(), 2);

  for (auto& result : results) {
    ASSERT_TRUE(result.HasKey("1")) << "result: " << result;

    std::string value_1;
    result.GetString("1", &value_1);

    ASSERT_EQ(value_1.at(1), '1') << "result: " << result;

    switch (value_1.at(0)) {
      case 'a':
        break;
      case 'b': {
        ASSERT_TRUE(result.HasKey("2")) << "result: " << result;
        std::string value_2;
        result.GetString("2", &value_2);
        ASSERT_EQ(value_2, "b2") << "result: " << result;
      } break;
      default:
        ASSERT_TRUE(false) << "result: " << result;
        break;
    }
  }
}

}  // namespace runtime_probe
