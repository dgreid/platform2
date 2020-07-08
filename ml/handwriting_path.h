// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_HANDWRITING_PATH_H_
#define ML_HANDWRITING_PATH_H_

#include <string>

#include "chrome/knowledge/handwriting/interface.pb.h"
#include "ml/mojom/handwriting_recognizer.mojom.h"

namespace ml {

// Returns model paths based on the `spec`.
base::Optional<chrome_knowledge::HandwritingRecognizerModelPaths> GetModelPaths(
    chromeos::machine_learning::mojom::HandwritingRecognizerSpecPtr spec);

// Returns labeled request path based on the `spec`.
std::string GetLabeledRequestsPathForTesting(
    chromeos::machine_learning::mojom::HandwritingRecognizerSpecPtr spec);

}  // namespace ml

#endif  // ML_HANDWRITING_PATH_H_
