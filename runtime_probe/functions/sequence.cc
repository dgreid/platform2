// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/sequence.h"

#include <utility>

namespace runtime_probe {

SequenceFunction::DataType SequenceFunction::Eval() const {
  base::Value result(base::Value::Type::DICTIONARY);

  for (const auto& func : functions_) {
    const auto& probe_results = func->Eval();

    if (probe_results.size() == 0)
      return {};

    if (probe_results.size() > 1) {
      LOG(ERROR) << "Subfunction call generates more than one results.";
      return {};
    }

    result.MergeDictionary(&probe_results[0]);
  }

  DataType results;
  results.push_back(std::move(result));
  return results;
}

}  // namespace runtime_probe
