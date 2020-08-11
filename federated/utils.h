// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_UTILS_H_
#define FEDERATED_UTILS_H_

#include "chrome/knowledge/federated/example.pb.h"
#include "chrome/knowledge/federated/feature.pb.h"
#include "federated/mojom/example.mojom.h"

namespace federated {

// Converts the mojom Example struct to a TensorFlow Example proto.
tensorflow::Example ConvertToTensorFlowExampleProto(
    const chromeos::federated::mojom::ExamplePtr& example);

}  // namespace federated

#endif  // FEDERATED_UTILS_H_
