// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/handwriting_recognizer_impl.h"

#include <utility>
#include <vector>

#include "ml/handwriting_path.h"
#include "ml/handwriting_proto_mojom_conversion.h"

namespace ml {
namespace {

using ::chromeos::machine_learning::mojom::HandwritingRecognitionQueryPtr;
using ::chromeos::machine_learning::mojom::HandwritingRecognizerCandidatePtr;
using ::chromeos::machine_learning::mojom::HandwritingRecognizer;
using ::chromeos::machine_learning::mojom::HandwritingRecognizerResult;
using ::chromeos::machine_learning::mojom::HandwritingRecognizerSpecPtr;

}  // namespace

bool HandwritingRecognizerImpl::Create(
    HandwritingRecognizerSpecPtr spec,
    mojo::PendingReceiver<HandwritingRecognizer> receiver) {
  auto recognizer_impl =
      new HandwritingRecognizerImpl(std::move(spec), std::move(receiver));

  // Set the disconnection handler to strongly bind `recognizer_impl` to delete
  // `recognizer_impl` when the connection is gone.
  recognizer_impl->receiver_.set_disconnect_handler(base::Bind(
      [](const HandwritingRecognizerImpl* const recognizer_impl) {
        delete recognizer_impl;
      },
      base::Unretained(recognizer_impl)));

  return recognizer_impl->successfully_loaded_;
}

HandwritingRecognizerImpl::HandwritingRecognizerImpl(
    HandwritingRecognizerSpecPtr spec,
    mojo::PendingReceiver<HandwritingRecognizer> receiver)
    : receiver_(this, std::move(receiver)) {
  auto* const hwr_library = ml::HandwritingLibrary::GetInstance();
  DCHECK(hwr_library->GetStatus() == ml::HandwritingLibrary::Status::kOk)
      << "HandwritingRecognizerImpl should be created only if "
         "HandwritingLibrary is initialized successfully.";

  const auto model_path = GetModelPaths(std::move(spec));
  if (!model_path.has_value()) {
    successfully_loaded_ = false;
    return;
  }

  recognizer_ = hwr_library->CreateHandwritingRecognizer();

  successfully_loaded_ = hwr_library->LoadHandwritingRecognizer(
      recognizer_, chrome_knowledge::HandwritingRecognizerOptions(),
      model_path.value());
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
