// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/json/json_reader.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <stddef.h>
#include <stdint.h>

#include <string>

#include "runtime_probe/probe_function.h"
#include "runtime_probe/probe_result_checker.h"
#include "runtime_probe/runtime_probe_fuzzer_helper.h"

using std::string;

namespace runtime_probe {

struct Environment {
  Environment() {
    logging::SetMinLogLevel(logging::LOG_FATAL);  // Disable logging.
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  string ops[6] = {"!eq ", "!ne ", "!gt ", "!ge ", "!lt ", "!le "};
  FuzzedDataProvider fuzz_data(data, size);

  string rule;
  if (fuzz_data.ConsumeBool()) {
    int8_t op = fuzz_data.ConsumeIntegralInRange<int8_t>(0, 5);
    string str = fuzz_data.ConsumeRandomLengthString(10) + "?";
    rule = R"(, ")" + ops[op] + JsonSafe(str) + R"(" )";
  }

  const auto json_string = R"({
    "str": [true, "str")" + rule +
                           R"(],
    "int": [true, "int"],
    "double": [true, "double"],
    "hex": [true, "hex"]
  })";

  string str[4] = {fuzz_data.ConsumeRandomLengthString(30),
                   fuzz_data.ConsumeRandomLengthString(30),
                   fuzz_data.ConsumeRandomLengthString(30),
                   fuzz_data.ConsumeRandomLengthString(30)};

  const auto probe_result_string = R"({
    "str": ")" + JsonSafe(str[0]) + R"(",
    "int": ")" + JsonSafe(str[1]) + R"(",
    "double": ")" + JsonSafe(str[2]) +
                                   R"(",
    "hex": ")" + JsonSafe(str[3]) + R"("
  })";

  auto probe_result_val = base::JSONReader::Read(probe_result_string);
  base::DictionaryValue* probe_result = nullptr;
  probe_result_val->GetAsDictionary(&probe_result);

  auto checker_val = base::JSONReader::Read(json_string);
  base::DictionaryValue* checker_dict = nullptr;
  checker_val->GetAsDictionary(&checker_dict);
  auto checker = ProbeResultChecker::FromDictionaryValue(*checker_dict);

  if (checker != nullptr && probe_result != nullptr)
    checker->Apply(probe_result);

  return 0;
}

}  // namespace runtime_probe
