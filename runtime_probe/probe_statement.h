// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_PROBE_STATEMENT_H_
#define RUNTIME_PROBE_PROBE_STATEMENT_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/values.h>
#include <gtest/gtest.h>

#include "runtime_probe/probe_function.h"
#include "runtime_probe/probe_result_checker.h"

namespace runtime_probe {

class ProbeStatement {
  // Holds a probe statement with following JSON schema::
  //   {
  //     "eval": <function_name:string> |
  //             <func:ProbeFunction> |
  //             [<func:ProbeFunction>],
  //     "keys": [<key:string>],
  //     "expect": <see |ProbeResultChecker|>,
  //     "information": <info:DictValue>,
  //   }
  //
  // For "eval", the case "[<func:ProbeFunction>]" will be transformed into::
  //   (ProbeFunction) {
  //     "function_name": "sequence",
  //     "args": {
  //       "functions": [<func:ProbeFunction>]
  //     }
  //   }
  //
  // For "expect", the dictionary value should represent a ProbeResultChecker
  // object.  See ProbeResultChecker for more details.
  //
  // When evaluating a ProbeStatement, the ProbeFunction defined by "eval" will
  // be called.  The results will be filtered / processed by "keys" and "expect"
  // rules.  See ProbeStatement::Eval() for more details.
 public:
  static std::unique_ptr<ProbeStatement> FromValue(std::string component_name,
                                                   const base::Value& dv);

  // Evaluate the probe statement.
  //
  // The process can be break into following steps:
  // - Call probe function |eval_|
  // - Filter results by |key_|  (if |key_| is not empty)
  // - Transform and check results by |expect_|  (if |expect_| is not empty)
  // - Return final results that passed |expect_| check.
  ProbeFunction::DataType Eval() const;

  base::Optional<base::Value> GetInformation() const {
    if (information_)
      return information_->Clone();
    return base::nullopt;
  }

 private:
  ProbeStatement() = default;

  std::string component_name_;
  std::unique_ptr<ProbeFunction> eval_;
  std::set<std::string> key_;
  std::unique_ptr<ProbeResultChecker> expect_;
  base::Optional<base::Value> information_;

  FRIEND_TEST(ProbeConfigTest, LoadConfig);
  FRIEND_TEST(ProbeStatementTest, TestEval);
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_PROBE_STATEMENT_H_
