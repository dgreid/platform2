// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_HANDWRITING_RECOGNIZER_IMPL_H_
#define ML_HANDWRITING_RECOGNIZER_IMPL_H_

#include <base/callback_forward.h>
#include <base/macros.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "chrome/knowledge/handwriting/interface.pb.h"
#include "ml/handwriting.h"
#include "ml/mojom/handwriting_recognizer.mojom.h"

namespace ml {

// The implementation of HandwritingRecognizer.
class HandwritingRecognizerImpl
    : public chromeos::machine_learning::mojom::HandwritingRecognizer {
 public:
  // Constructs a HandwritingRecognizerImpl; and set_connection_error_handler so
  // that the HandwritingRecognizerImpl will be deleted when the mojom
  // connection is destroyed.
  // Returns whether the object is create successfully.
  static bool Create(
      chromeos::machine_learning::mojom::HandwritingRecognizerSpecPtr spec,
      chromeos::machine_learning::mojom::HandwritingRecognizerRequest request);

  // Called when mojom connection is destroyed.
  ~HandwritingRecognizerImpl();

 private:
  // Creates a HandwritingRecognizer and Binds to `request` inside so that
  // Recognize can be called on the other side for a particular handwriting
  // reconition query.
  HandwritingRecognizerImpl(
      chromeos::machine_learning::mojom::HandwritingRecognizerSpecPtr spec,
      chromeos::machine_learning::mojom::HandwritingRecognizerRequest request);

  // mojom::HandwritingRecognizer:
  void Recognize(
      chromeos::machine_learning::mojom::HandwritingRecognitionQueryPtr query,
      RecognizeCallback callback) override;

  bool successfully_loaded_;
  // Pointer to the internal implementation of HandwritingRecognizer inside
  // the HandwritingLibrary.
  ::HandwritingRecognizer recognizer_;

  mojo::Binding<chromeos::machine_learning::mojom::HandwritingRecognizer>
      binding_;

  DISALLOW_COPY_AND_ASSIGN(HandwritingRecognizerImpl);
};

}  // namespace ml

#endif  // ML_HANDWRITING_RECOGNIZER_IMPL_H_
