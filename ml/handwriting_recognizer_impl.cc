// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/handwriting_recognizer_impl.h"

#include <utility>
#include <vector>

#include "ml/handwriting_proto_mojom_conversion.h"

namespace ml {
namespace {

using ::chromeos::machine_learning::mojom::HandwritingRecognitionQueryPtr;
using ::chromeos::machine_learning::mojom::HandwritingRecognizerCandidatePtr;
using ::chromeos::machine_learning::mojom::HandwritingRecognizerRequest;
using ::chromeos::machine_learning::mojom::HandwritingRecognizerResult;

// Returns paths of the current HandwritingRecognizerModel.
chrome_knowledge::HandwritingRecognizerModelPaths GetModelPaths() {
  chrome_knowledge::HandwritingRecognizerModelPaths paths;
  paths.set_reco_model_path(
      "/opt/google/chrome/ml_models/handwriting/latin_indy.tflite");
  paths.set_seg_model_path(
      "/opt/google/chrome/ml_models/handwriting/latin_indy_seg.tflite");
  paths.set_conf_model_path(
      "/opt/google/chrome/ml_models/handwriting/latin_indy_conf.tflite");
  paths.set_fst_lm_path(
      "/opt/google/chrome/ml_models/handwriting/latin_indy.compact.fst");
  paths.set_recospec_path(
      "/opt/google/chrome/ml_models/handwriting/latin_indy.pb");
  return paths;
}

}  // namespace

bool HandwritingRecognizerImpl::Create(HandwritingRecognizerRequest request) {
  auto recognizer_impl =
      new HandwritingRecognizerImpl(std::move(request));

  // Set the connection error handler to strongly bind |recognizer_impl| to
  // delete |recognizer_impl| when the connection is gone.
  recognizer_impl->binding_.set_connection_error_handler(base::Bind(
      [](const HandwritingRecognizerImpl* const recognizer_impl) {
        delete recognizer_impl;
      },
      base::Unretained(recognizer_impl)));

  return recognizer_impl->successfully_loaded_;
}

HandwritingRecognizerImpl::HandwritingRecognizerImpl(
    HandwritingRecognizerRequest request)
    : binding_(this, std::move(request)) {
  auto* const hwr_library = ml::HandwritingLibrary::GetInstance();
  DCHECK(hwr_library->GetStatus() == ml::HandwritingLibrary::Status::kOk)
      << "HandwritingRecognizerImpl should be created only if "
         "HandwritingLibrary is initialized successfully.";

  recognizer_ = hwr_library->CreateHandwritingRecognizer();

  successfully_loaded_ = hwr_library->LoadHandwritingRecognizer(
      recognizer_, chrome_knowledge::HandwritingRecognizerOptions(),
      GetModelPaths());
}

HandwritingRecognizerImpl::~HandwritingRecognizerImpl() {
  ml::HandwritingLibrary::GetInstance()->DestroyHandwritingRecognizer(
      recognizer_);
}

void HandwritingRecognizerImpl::Recognize(HandwritingRecognitionQueryPtr query,
                                          RecognizeCallback callback) {
  chrome_knowledge::HandwritingRecognizerResult result_proto;

  if (ml::HandwritingLibrary::GetInstance()->RecognizeHandwriting(
          recognizer_, HandwritingRecognitionQueryToProto(std::move(query)),
          &result_proto)) {
    // Recognition succeeded, run callback on the result.
    std::move(callback).Run(HandwritingRecognizerResultFromProto(result_proto));
  } else {
    // Recognition failed, run callback on empty result and status = ERROR.
    std::move(callback).Run(HandwritingRecognizerResult::New(
        HandwritingRecognizerResult::Status::ERROR,
        std::vector<HandwritingRecognizerCandidatePtr>()));
  }
}

}  // namespace ml
