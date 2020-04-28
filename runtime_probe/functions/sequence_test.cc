/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/values.h>
#include <base/json/json_reader.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "runtime_probe/functions/sequence.h"
#include "runtime_probe/functions/shell.h"
#include "runtime_probe/probe_function.h"

namespace runtime_probe {

typedef ProbeFunction::DataType DataType;

class MockProbeFunction : public ProbeFunction {
 public:
  MOCK_METHOD(DataType, Eval, (), (const, override));
};

TEST(SequenceFunctionTest, TestEvalFailTooManyResults) {
  auto mock_probe_function_1 = std::make_unique<MockProbeFunction>();

  base::DictionaryValue a;
  a.SetBoolean("a", true);

  base::DictionaryValue b;
  b.SetBoolean("b", true);

  DataType val;
  // val{std::move(a), std::move(b)} implicitly calls the copy constructor which
  // is not possible.
  val.push_back(std::move(a));
  val.push_back(std::move(b));

  EXPECT_CALL(*mock_probe_function_1, Eval())
      .WillOnce(testing::Return(testing::ByMove(std::move(val))));

  auto mock_probe_function_2 = std::make_unique<MockProbeFunction>();
  // The sequence function should abort after calling mock_probe_function_1,
  // mock_probe_function_2 should never be called.

  SequenceFunction sequence{};
  sequence.functions_.push_back(std::move(mock_probe_function_1));
  sequence.functions_.push_back(std::move(mock_probe_function_2));

  DataType results = sequence.Eval();

  std::stringstream stream;
  for (auto& result : results) {
    stream << result;
  }
  /* The `results` should be empty */
  ASSERT_EQ(results.size(), 0) << "unexpected results: " << stream.str();

  return;
}

TEST(SequenceFunctionTest, TestEvalSuccess) {
  auto mock_probe_function_1 = std::make_unique<MockProbeFunction>();

  base::DictionaryValue a;
  a.SetBoolean("a", true);
  a.SetBoolean("c", false);

  DataType val_a;
  val_a.push_back(std::move(a));

  EXPECT_CALL(*mock_probe_function_1, Eval())
      .WillOnce(testing::Return(testing::ByMove(std::move(val_a))));

  auto mock_probe_function_2 = std::make_unique<MockProbeFunction>();

  base::DictionaryValue b;
  b.SetBoolean("b", true);
  b.SetBoolean("c", true);

  DataType val_b;
  val_b.push_back(std::move(b));

  EXPECT_CALL(*mock_probe_function_2, Eval())
      .WillOnce(testing::Return(testing::ByMove(std::move(val_b))));

  SequenceFunction sequence{};
  sequence.functions_.push_back(std::move(mock_probe_function_1));
  sequence.functions_.push_back(std::move(mock_probe_function_2));

  DataType results = sequence.Eval();

  std::stringstream stream;
  for (auto& result : results) {
    stream << result;
  }
  /* The `results` should be empty */
  ASSERT_EQ(results.size(), 1) << "unexpected results: " << stream.str();

  std::set<std::string> result_keys;
  LOG(ERROR) << results[0];

  for (base::DictionaryValue::Iterator it{results[0]}; !it.IsAtEnd();
       it.Advance()) {
    ASSERT_TRUE(it.value().is_bool()) << "unexpected result: " << results[0];
    ASSERT_TRUE(it.value().GetBool()) << "unexpected result: " << results[0];
    result_keys.insert(it.key());
  }

  ASSERT_THAT(result_keys, ::testing::UnorderedElementsAre("a", "b", "c"));
}

TEST(SequenceFunctionTest, TestParserEmptyList) {
  // TODO(crbug.com/1054279): use base::JSONReader::Read after uprev to r680000.
  auto json_object = base::JSONReader::ReadDeprecated(R"({
    "sequence": {
      "functions": []
    }
  })");

  auto p = ProbeFunction::FromValue(*json_object);
  ASSERT_NE(p, nullptr) << "Failed to load function: " << json_object;

  auto sequence = dynamic_cast<SequenceFunction*>(p.get());
  ASSERT_NE(sequence, nullptr) << "Loaded function is not SequenceFunction";

  ASSERT_EQ(sequence->functions_.size(), 0);
}

TEST(SequenceFunctionTest, TestParseFunctions) {
  // TODO(crbug.com/1054279): use base::JSONReader::Read after uprev to r680000.
  auto json_object = base::JSONReader::ReadDeprecated(R"({
    "sequence": {
      "functions": [
        {
          "sysfs": {
            "dir_path": "/sys/class/cool/device/*",
            "keys": ["1", "2", "3"]
          }
        },
        {
          "sysfs": {
            "dir_path": "/sys/class/some/device/*",
            "keys": ["4", "5", "6"]
          }
        }
      ]
    }
  })");

  auto p = ProbeFunction::FromValue(*json_object);
  ASSERT_NE(p, nullptr) << "Failed to load function: " << json_object;

  auto sequence = dynamic_cast<SequenceFunction*>(p.get());
  ASSERT_NE(sequence, nullptr) << "Loaded function is not SequenceFunction";

  ASSERT_EQ(sequence->functions_.size(), 2);

  for (const auto& func : sequence->functions_) {
    ASSERT_NE(func, nullptr);
  }
}

}  // namespace runtime_probe
