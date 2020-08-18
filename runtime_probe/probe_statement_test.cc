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
  NAME_PROBE_FUNCTION("mock_function");
  MOCK_METHOD(DataType, Eval, (), (const, override));
};

TEST(ProbeStatementTest, TestEval) {
  ProbeStatement probe_statement;

  // Set up |expect_|
  auto expect_string = R"({
    "expected_field": [true, "str"]
  })";
  auto expect = base::JSONReader::Read(expect_string);
  ASSERT_TRUE(expect.has_value());
  ASSERT_TRUE(expect->is_dict());

  probe_statement.expect_ = ProbeResultChecker::FromValue(*expect);

  // Set up |eval_|
  auto mock_eval = std::make_unique<MockProbeFunction>();

  base::Value good_result(base::Value::Type::DICTIONARY);
  good_result.SetStringKey("expected_field", "expected");
  good_result.SetStringKey("optional_field", "optional");

  auto good_result2 = good_result.Clone();

  // bad_result is empty, which doesn't have expected field
  base::Value bad_result(base::Value::Type::DICTIONARY);
  bad_result.SetStringKey("optional_field", "optional");

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

    auto* str_value = results[0].FindStringKey("expected_field");
    ASSERT_NE(str_value, nullptr);
    ASSERT_EQ(*str_value, "expected");

    str_value = results[0].FindStringKey("optional_field");
    ASSERT_NE(str_value, nullptr);
    ASSERT_EQ(*str_value, "optional");
  }
}

}  // namespace runtime_probe
