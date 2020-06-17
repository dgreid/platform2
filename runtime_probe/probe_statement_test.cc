/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string>

#include <base/json/json_reader.h>
#include <base/values.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "runtime_probe/probe_function.h"
#include "runtime_probe/probe_result_checker.h"
#include "runtime_probe/probe_statement.h"

namespace runtime_probe {

class MockProbeFunction : public ProbeFunction {
 public:
  MOCK_METHOD(DataType, Eval, (), (const, override));
};

TEST(ProbeStatementTest, TestEval) {
  ProbeStatement probe_statement;

  // Set up |expect_|
  auto expect_string = R"({
    "expected_field": [true, "str"]
  })";
  auto val = base::JSONReader::Read(expect_string);
  base::DictionaryValue* expect = nullptr;
  val->GetAsDictionary(&expect);

  probe_statement.expect_ = ProbeResultChecker::FromDictionaryValue(*expect);

  // Set up |eval_|
  auto mock_eval = std::make_unique<MockProbeFunction>();

  base::DictionaryValue good_result;
  good_result.SetString("expected_field", "expected");
  good_result.SetString("optional_field", "optional");

  base::DictionaryValue good_result2;  // duplicate due to copy limit
  good_result2.SetString("expected_field", "expected");
  good_result2.SetString("optional_field", "optional");

  // bad_result is empty, which doesn't have expected field
  base::DictionaryValue bad_result;
  bad_result.SetString("optional_field", "optional");

  ProbeFunction::DataType val_a;
  // val_a{std::move(x), std::move(y)} implicitly calls the copy constructor
  // which is not possible.
  val_a.push_back(std::move(good_result));
  val_a.push_back(std::move(bad_result));

  ProbeFunction::DataType val_b;
  val_b.push_back(std::move(good_result2));

  EXPECT_CALL(*mock_eval, Eval())
      .WillOnce(::testing::Return(::testing::ByMove(std::move(val_a))))
      .WillOnce(::testing::Return(::testing::ByMove(std::move(val_b))));

  probe_statement.eval_ = std::move(mock_eval);

  // Test twice, both invocations should only return |good_result|.
  for (auto i = 0; i < 2; i++) {
    auto results = probe_statement.Eval();
    ASSERT_EQ(results.size(), 1);

    std::string str_value;
    ASSERT_TRUE(results[0].GetString("expected_field", &str_value));
    ASSERT_EQ(str_value, "expected");

    ASSERT_TRUE(results[0].GetString("optional_field", &str_value));
    ASSERT_EQ(str_value, "optional");
  }
}

}  // namespace runtime_probe
