/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef RUNTIME_PROBE_FUNCTIONS_SEQUENCE_H_
#define RUNTIME_PROBE_FUNCTIONS_SEQUENCE_H_

#include <memory>
#include <vector>

#include <base/values.h>
#include <gtest/gtest.h>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

/* Execute multiple probe functions sequentially and merge their outputs.
 *
 * Each subfunction must create one result, it will be merged to previous
 * results.  If there are common keys, the later one will override previous one.
 *
 * For example, function_1 and function_2 outputs the following respectively::
 *   { "a": true, "common": false }
 *   { "b": true, "common": true }
 *
 * The final result will be { "a": true, "b": true, "common": true }
 *
 * If any subfunction returns 0 or more than 1 results, the final result will be
 * empty (vector size will be empty).
 */
class SequenceFunction : public ProbeFunction {
 public:
  static constexpr auto function_name = "sequence";

  static std::unique_ptr<ProbeFunction> FromValue(
      const base::Value& dict_value) {
    auto instance = std::make_unique<SequenceFunction>();

    bool result = true;

    result &= PARSE_ARGUMENT(functions);

    if (result)
      return instance;
    return nullptr;
  }

  DataType Eval() const override;

 private:
  static Register<SequenceFunction> register_;

  std::vector<std::unique_ptr<ProbeFunction>> functions_;

  FRIEND_TEST(SequenceFunctionTest, TestEvalFailTooManyResults);
  FRIEND_TEST(SequenceFunctionTest, TestEvalSuccess);
  FRIEND_TEST(SequenceFunctionTest, TestParserEmptyList);
  FRIEND_TEST(SequenceFunctionTest, TestParseFunctions);
};

REGISTER_PROBE_FUNCTION(SequenceFunction);

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_SEQUENCE_H_
