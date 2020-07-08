// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementations of specializations of TensorView<> for all supported tensor
// data types

#include "ml/tensor_view.h"

namespace ml {

using ::chromeos::machine_learning::mojom::FloatList;
using ::chromeos::machine_learning::mojom::Int64List;
using ::chromeos::machine_learning::mojom::ValueList;

template <>
std::vector<int64_t>& TensorView<int64_t>::GetValues() {
  return tensor_->data->get_int64_list()->value;
}

template <>
bool TensorView<int64_t>::IsValidType() const {
  return tensor_->data->which() == ValueList::Tag::INT64_LIST;
}

template <>
void TensorView<int64_t>::AllocateValues() {
  tensor_->data->set_int64_list(Int64List::New());
  // TODO(hidehiko): assigning std::vector<>() to `value` is unneeded
  // on libmojo uprev. Remove them after the uprev.
  tensor_->data->get_int64_list()->value = std::vector<int64_t>();
}

template <>
std::vector<double>& TensorView<double>::GetValues() {
  return tensor_->data->get_float_list()->value;
}

template <>
bool TensorView<double>::IsValidType() const {
  return tensor_->data->which() == ValueList::Tag::FLOAT_LIST;
}

template <>
void TensorView<double>::AllocateValues() {
  tensor_->data->set_float_list(FloatList::New());
  // TODO(hidehiko): assigning std::vector<>() to `value` is unneeded
  // on libmojo uprev. Remove them after the uprev.
  tensor_->data->get_float_list()->value = std::vector<double>();
}

}  // namespace ml
