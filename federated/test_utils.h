// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_TEST_UTILS_H_
#define FEDERATED_TEST_UTILS_H_

#include "federated/mojom/example.mojom.h"

namespace federated {

// Creates a mojom::Example Mojo struct with various nonempty fields.
chromeos::federated::mojom::ExamplePtr CreateExamplePtr();

}  // namespace federated

#endif  // FEDERATED_TEST_UTILS_H_
