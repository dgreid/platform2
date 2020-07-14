// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/text_classifier_impl.h"

#include <utility>
#include <vector>

#include <base/logging.h>
#include <lang_id/lang-id-wrapper.h>
#include <utils/utf8/unicodetext.h>

#include "ml/mojom/text_classifier.mojom.h"
#include "ml/request_metrics.h"

namespace ml {

namespace {

using ::chromeos::machine_learning::mojom::CodepointSpan;
using ::chromeos::machine_learning::mojom::FindLanguagesResult;
using ::chromeos::machine_learning::mojom::TextLanguage;
using ::chromeos::machine_learning::mojom::TextLanguagePtr;
using ::chromeos::machine_learning::mojom::SuggestSelectionResult;
using ::chromeos::machine_learning::mojom::TextAnnotation;
using ::chromeos::machine_learning::mojom::TextAnnotationPtr;
using ::chromeos::machine_learning::mojom::TextAnnotationRequestPtr;
using ::chromeos::machine_learning::mojom::TextAnnotationResult;
using ::chromeos::machine_learning::mojom::TextClassifier;
using ::chromeos::machine_learning::mojom::TextEntity;
using ::chromeos::machine_learning::mojom::TextEntityData;
using ::chromeos::machine_learning::mojom::TextEntityPtr;
using ::chromeos::machine_learning::mojom::TextSuggestSelectionRequestPtr;

// To avoid passing a lambda as a base::Closure.
void DeleteTextClassifierImpl(
    const TextClassifierImpl* const text_classifier_impl) {
  delete text_classifier_impl;
}

}  // namespace

bool TextClassifierImpl::Create(
    std::unique_ptr<libtextclassifier3::ScopedMmap>* annotator_model_mmap,
    const std::string& langid_model_path,
    mojo::PendingReceiver<TextClassifier> receiver) {
  auto text_classifier_impl = new TextClassifierImpl(
      annotator_model_mmap, langid_model_path, std::move(receiver));
  if (text_classifier_impl->annotator_ == nullptr ||
      text_classifier_impl->language_identifier_ == nullptr) {
    // Fails to create annotator, return nullptr.
    delete text_classifier_impl;
    return false;
  }

  // Use a disconnection handler to strongly bind `text_classifier_impl` to
  // `receiver`.
  text_classifier_impl->SetDisconnectionHandler(base::Bind(
      &DeleteTextClassifierImpl, base::Unretained(text_classifier_impl)));
  return true;
}

TextClassifierImpl::TextClassifierImpl(
    std::unique_ptr<libtextclassifier3::ScopedMmap>* annotator_model_mmap,
    const std::string& langid_model_path,
    mojo::PendingReceiver<TextClassifier> receiver)
    : annotator_(libtextclassifier3::Annotator::FromScopedMmap(
          annotator_model_mmap, nullptr, nullptr)),
      language_identifier_(
          libtextclassifier3::langid::LoadFromPath(langid_model_path)),
      receiver_(this, std::move(receiver)) {}

void TextClassifierImpl::SetDisconnectionHandler(
    base::Closure disconnect_handler) {
  receiver_.set_disconnect_handler(std::move(disconnect_handler));
}

void TextClassifierImpl::Annotate(TextAnnotationRequestPtr request,
                                  AnnotateCallback callback) {
  RequestMetrics request_metrics("TextClassifier", "Annotate");
  request_metrics.StartRecordingPerformanceMetrics();

  // Parse and set up the options.
  libtextclassifier3::AnnotationOptions option;
  if (request->default_locales) {
    option.locales = request->default_locales.value();
  }
  if (request->reference_time) {
    option.reference_time_ms_utc =
        request->reference_time->ToTimeT() * base::Time::kMillisecondsPerSecond;
  }
  if (request->reference_timezone) {
    option.reference_timezone = request->reference_timezone.value();
  }
  if (request->enabled_entities) {
    option.entity_types.insert(request->enabled_entities.value().begin(),
                               request->enabled_entities.value().end());
  }
  option.detected_text_language_tags =
      request->detected_text_language_tags.value_or("en");
  option.annotation_usecase =
      static_cast<libtextclassifier3::AnnotationUsecase>(
          request->annotation_usecase);

  // Do the annotation.
  const std::vector<libtextclassifier3::AnnotatedSpan> annotated_spans =
      annotator_->Annotate(request->text, option);

  // Parse the result.
  std::vector<TextAnnotationPtr> annotations;
  for (const auto& annotated_result : annotated_spans) {
    DCHECK(annotated_result.span.second >= annotated_result.span.first);
    std::vector<TextEntityPtr> entities;
    for (const auto& classification : annotated_result.classification) {
      // First, get entity data.
      auto entity_data = TextEntityData::New();
      if (classification.collection == "number") {
        entity_data->set_numeric_value(classification.numeric_double_value);
      } else {
        // For the other types, just encode the substring into string_value.
        // TODO(honglinyu): add data extraction for more types when needed
        // and available.
        // Note that the returned indices by annotator is unicode codepoints.
        entity_data->set_string_value(
            libtextclassifier3::UTF8ToUnicodeText(request->text, false)
                .UTF8Substring(annotated_result.span.first,
                               annotated_result.span.second));
      }

      // Second, create the entity.
      entities.emplace_back(TextEntity::New(classification.collection,
                                            classification.score,
                                            std::move(entity_data)));
    }
    annotations.emplace_back(TextAnnotation::New(annotated_result.span.first,
                                                 annotated_result.span.second,
                                                 std::move(entities)));
  }

  std::move(callback).Run(std::move(annotations));

  request_metrics.FinishRecordingPerformanceMetrics();
}

void TextClassifierImpl::SuggestSelection(
    TextSuggestSelectionRequestPtr request, SuggestSelectionCallback callback) {
  RequestMetrics request_metrics("TextClassifier", "SuggestSelection");
  request_metrics.StartRecordingPerformanceMetrics();

  libtextclassifier3::SelectionOptions option;
  if (request->default_locales) {
    option.locales = request->default_locales.value();
  }
  option.detected_text_language_tags =
      request->detected_text_language_tags.value_or("en");
  option.annotation_usecase =
      static_cast<libtextclassifier3::AnnotationUsecase>(
          request->annotation_usecase);

  libtextclassifier3::CodepointSpan user_selection;
  user_selection.first = request->user_selection->start_offset;
  user_selection.second = request->user_selection->end_offset;

  const libtextclassifier3::CodepointSpan suggested_span =
      annotator_->SuggestSelection(request->text, user_selection, option);
  auto result_span = CodepointSpan::New();
  result_span->start_offset = suggested_span.first;
  result_span->end_offset = suggested_span.second;

  std::move(callback).Run(std::move(result_span));

  request_metrics.FinishRecordingPerformanceMetrics();
}

void TextClassifierImpl::FindLanguages(const std::string& text,
                                       FindLanguagesCallback callback) {
  RequestMetrics request_metrics("TextClassifier", "FindLanguages");
  request_metrics.StartRecordingPerformanceMetrics();

  const std::vector<std::pair<std::string, float>> languages =
      libtextclassifier3::langid::GetPredictions(language_identifier_.get(),
                                                 text);

  std::vector<TextLanguagePtr> langid_result;
  for (const auto& lang : languages) {
    langid_result.emplace_back(TextLanguage::New(lang.first, lang.second));
  }

  std::move(callback).Run(std::move(langid_result));

  request_metrics.FinishRecordingPerformanceMetrics();
  request_metrics.RecordRequestEvent(FindLanguagesResult::OK);
}

}  // namespace ml
