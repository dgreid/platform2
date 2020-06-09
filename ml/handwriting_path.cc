// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/handwriting_path.h"

namespace ml {
namespace {

using ::chromeos::machine_learning::mojom::HandwritingRecognizerSpecPtr;

// A list of supported language code.
constexpr char kLanguageCodeEn[] = "en";
constexpr char kLanguageCodeGesture[] = "gesture_in_context";

constexpr char kLabeledRequestPathEn[] =
    "/build/share/libhandwriting/handwriting_labeled_requests.pb";
constexpr char kLabeledRequestPathGesture[] =
    "/build/share/libhandwriting/gesture_labeled_requests.pb";
constexpr char kHandwritingModelDir[] =
    "/opt/google/chrome/ml_models/handwriting/";

// Returns model paths for guesture recognition.
chrome_knowledge::HandwritingRecognizerModelPaths GetModelPathsForGesture() {
  chrome_knowledge::HandwritingRecognizerModelPaths paths;
  const std::string model_dir = std::string(kHandwritingModelDir);
  paths.set_reco_model_path(model_dir + "gic.reco_model.tflite");
  paths.set_recospec_path(model_dir + "gic.recospec.pb");
  return paths;
}

// Returns model paths for english recognition.
chrome_knowledge::HandwritingRecognizerModelPaths GetModelPathsForEn() {
  chrome_knowledge::HandwritingRecognizerModelPaths paths;
  const std::string model_dir = std::string(kHandwritingModelDir);
  paths.set_reco_model_path(model_dir + "latin_indy.tflite");
  paths.set_seg_model_path(model_dir + "latin_indy_seg.tflite");
  paths.set_conf_model_path(model_dir + "latin_indy_conf.tflite");
  paths.set_fst_lm_path(model_dir + "latin_indy.compact.fst");
  paths.set_recospec_path(model_dir + "latin_indy.pb");
  return paths;
}

}  // namespace

base::Optional<chrome_knowledge::HandwritingRecognizerModelPaths> GetModelPaths(
    HandwritingRecognizerSpecPtr spec) {
  if (spec->language == kLanguageCodeEn) {
    return GetModelPathsForEn();
  }
  if (spec->language == kLanguageCodeGesture) {
    return GetModelPathsForGesture();
  }
  return base::nullopt;
}

std::string GetLabeledRequestsPathForTesting(
    chromeos::machine_learning::mojom::HandwritingRecognizerSpecPtr spec) {
  if (spec->language == kLanguageCodeEn) {
    return kLabeledRequestPathEn;
  }
  DCHECK_EQ(spec->language, kLanguageCodeGesture);
  return kLabeledRequestPathGesture;
}

}  // namespace ml
