// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_MACHINE_LEARNING_SERVICE_IMPL_H_
#define ML_MACHINE_LEARNING_SERVICE_IMPL_H_

#include <map>
#include <memory>
#include <string>

#include <base/callback_forward.h>
#include <base/macros.h>
#include <mojo/public/cpp/bindings/binding.h>
#include <mojo/public/cpp/bindings/binding_set.h>

#include "ml/model_metadata.h"
#include "ml/mojom/machine_learning_service.mojom.h"

namespace ml {

class MachineLearningServiceImpl
    : public chromeos::machine_learning::mojom::MachineLearningService {
 public:
  // Creates an instance bound to `pipe`. The specified
  // `connection_error_handler` will be invoked if the binding encounters a
  // connection error.
  MachineLearningServiceImpl(mojo::ScopedMessagePipeHandle pipe,
                             base::Closure connection_error_handler);

  // A interface to change `text_classifier_model_filename_` for testing. Should
  // not be used outside of tests.
  void SetTextClassifierModelFilenameForTesting(const std::string& filename);

 protected:
  // Testing constructor that allows overriding of the model dir. Should not be
  // used outside of tests.
  MachineLearningServiceImpl(mojo::ScopedMessagePipeHandle pipe,
                             base::Closure connection_error_handler,
                             const std::string& model_dir);

 private:
  // chromeos::machine_learning::mojom::MachineLearningService:
  void Clone(chromeos::machine_learning::mojom::MachineLearningServiceRequest
                 request) override;
  void LoadBuiltinModel(
      chromeos::machine_learning::mojom::BuiltinModelSpecPtr spec,
      chromeos::machine_learning::mojom::ModelRequest request,
      LoadBuiltinModelCallback callback) override;
  void LoadFlatBufferModel(
      chromeos::machine_learning::mojom::FlatBufferModelSpecPtr spec,
      chromeos::machine_learning::mojom::ModelRequest request,
      LoadFlatBufferModelCallback callback) override;
  void LoadTextClassifier(
      chromeos::machine_learning::mojom::TextClassifierRequest request,
      LoadTextClassifierCallback callback) override;
  void LoadHandwritingModel(
      chromeos::machine_learning::mojom::HandwritingRecognizerRequest request,
      LoadHandwritingModelCallback callback) override;
  void LoadHandwritingModelWithSpec(
      chromeos::machine_learning::mojom::HandwritingRecognizerSpecPtr spec,
      chromeos::machine_learning::mojom::HandwritingRecognizerRequest request,
      LoadHandwritingModelCallback callback) override;

  // Init the icu data if it is not initialized yet.
  void InitIcuIfNeeded();

  // Used to hold the icu data read from file.
  char* icu_data_;

  std::string text_classifier_model_filename_;

  // Metadata required to load builtin models. Initialized at construction.
  const std::map<chromeos::machine_learning::mojom::BuiltinModelId,
                 BuiltinModelMetadata>
      builtin_model_metadata_;

  const std::string model_dir_;

  // Primordial binding bootstrapped over D-Bus. Once opened, is never closed.
  mojo::Binding<chromeos::machine_learning::mojom::MachineLearningService>
      binding_;

  // Additional bindings obtained via `Clone`.
  mojo::BindingSet<chromeos::machine_learning::mojom::MachineLearningService>
      clone_bindings_;

  DISALLOW_COPY_AND_ASSIGN(MachineLearningServiceImpl);
};

}  // namespace ml

#endif  // ML_MACHINE_LEARNING_SERVICE_IMPL_H_
