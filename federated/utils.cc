// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/utils.h"

#include <string>
#include <vector>

namespace federated {

namespace {
using chromeos::federated::mojom::ExamplePtr;
using chromeos::federated::mojom::FloatList;
using chromeos::federated::mojom::Int64List;
using chromeos::federated::mojom::ValueList;
}  // namespace

tensorflow::Example ConvertToTensorFlowExampleProto(const ExamplePtr& example) {
  tensorflow::Example tf_example;
  auto& feature = *tf_example.mutable_features()->mutable_feature();

  for (const auto& iter : example->features->feature) {
    if (iter.second->which() == ValueList::Tag::INT64_LIST) {
      std::vector<int64_t>& value_list = iter.second->get_int64_list()->value;
      *feature[iter.first].mutable_int64_list()->mutable_value() = {
          value_list.begin(), value_list.end()};
    } else if (iter.second->which() == ValueList::Tag::FLOAT_LIST) {
      std::vector<double>& value_list = iter.second->get_float_list()->value;
      *feature[iter.first].mutable_float_list()->mutable_value() = {
          value_list.begin(), value_list.end()};
    } else if (iter.second->which() == ValueList::Tag::STRING_LIST) {
      std::vector<std::string>& value_list =
          iter.second->get_string_list()->value;
      *feature[iter.first].mutable_bytes_list()->mutable_value() = {
          value_list.begin(), value_list.end()};
    }
  }
  return tf_example;
}

}  // namespace federated
