// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_TEXT_CLASSIFIER_IMPL_H_
#define ML_TEXT_CLASSIFIER_IMPL_H_

#include <list>
#include <map>
#include <memory>
#include <string>

#include <annotator/annotator.h>
#include <base/macros.h>
#include <mojo/public/cpp/bindings/binding.h>
#include <tensorflow/lite/model.h>
#include <utils/memory/mmap.h>

#include "ml/graph_executor_impl.h"
#include "ml/mojom/model.mojom.h"
#include "ml/mojom/text_classifier.mojom.h"

namespace ml {

class TextClassifierImpl
    : public chromeos::machine_learning::mojom::TextClassifier {
 public:
  // Interface to create new `TextClassifierImpl` object. This function will
  // automatically achieve strong binding. The model object will be deleted when
  // the corresponding mojo connection meets error.
  // Will return false if it fails to create the annotator object, otherwise
  // return true.
  static bool Create(
      std::unique_ptr<libtextclassifier3::ScopedMmap>* annotator_model_mmap,
      const std::string& langid_model_path,
      chromeos::machine_learning::mojom::TextClassifierRequest request);

 private:
  // A private constructor, call `TextClassifierImpl::Create` to create new
  // objects.
  explicit TextClassifierImpl(
      std::unique_ptr<libtextclassifier3::ScopedMmap>* annotator_model_mmap,
      const std::string& langid_model_path,
      chromeos::machine_learning::mojom::TextClassifierRequest request);

  void SetConnectionErrorHandler(base::Closure connection_error_handler);

  // chromeos::machine_learning::mojom::TextClassifier:
  void Annotate(
      chromeos::machine_learning::mojom::TextAnnotationRequestPtr request,
      AnnotateCallback callback) override;

  // chromeos::machine_learning::mojom::TextClassifier:
  void SuggestSelection(
      chromeos::machine_learning::mojom::TextSuggestSelectionRequestPtr request,
      SuggestSelectionCallback callback) override;

  // chromeos::machine_learning::mojom::TextClassifier:
  void FindLanguages(const std::string& text,
                     FindLanguagesCallback callback) override;

  std::unique_ptr<libtextclassifier3::Annotator> annotator_;

  std::unique_ptr<libtextclassifier3::mobile::lang_id::LangId>
      language_identifier_;

  mojo::Binding<chromeos::machine_learning::mojom::TextClassifier> binding_;

  DISALLOW_COPY_AND_ASSIGN(TextClassifierImpl);
};

}  // namespace ml

#endif  // ML_TEXT_CLASSIFIER_IMPL_H_
