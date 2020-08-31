// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/handwriting.h"

#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/native_library.h>

namespace ml {
namespace {

using chrome_knowledge::HandwritingRecognizerModelPaths;
using chrome_knowledge::HandwritingRecognizerOptions;
using chromeos::machine_learning::mojom::HandwritingRecognizerSpecPtr;

constexpr char kHandwritingLibraryRelativePath[] = "libhandwriting.so";

// A list of supported language code.
constexpr char kLanguageCodeEn[] = "en";
constexpr char kLanguageCodeGesture[] = "gesture_in_context";

// Returns HandwritingRecognizerModelPaths based on the `spec`.
HandwritingRecognizerModelPaths GetModelPaths(
    HandwritingRecognizerSpecPtr spec, const base::FilePath& model_path) {
  HandwritingRecognizerModelPaths paths;
  if (spec->language == kLanguageCodeEn) {
    paths.set_reco_model_path(model_path.Append("latin_indy.tflite").value());
    paths.set_seg_model_path(
        model_path.Append("latin_indy_seg.tflite").value());
    paths.set_conf_model_path(
        model_path.Append("latin_indy_conf.tflite").value());
    paths.set_fst_lm_path(model_path.Append("latin_indy.compact.fst").value());
    paths.set_recospec_path(model_path.Append("latin_indy.pb").value());
    return paths;
  }

  DCHECK_EQ(spec->language, kLanguageCodeGesture);
  paths.set_reco_model_path(model_path.Append("gic.reco_model.tflite").value());
  paths.set_recospec_path(model_path.Append("gic.recospec.pb").value());
  return paths;
}

}  // namespace

constexpr char HandwritingLibrary::kHandwritingDefaultModelDir[];

HandwritingLibrary::HandwritingLibrary(const std::string& model_path)
    : status_(Status::kUninitialized),
      model_path_(model_path),
      create_handwriting_recognizer_(nullptr),
      load_handwriting_recognizer_(nullptr),
      recognize_handwriting_(nullptr),
      delete_handwriting_result_data_(nullptr),
      destroy_handwriting_recognizer_(nullptr) {
  if (!IsHandwritingLibrarySupported()) {
    status_ = Status::kNotSupported;
    return;
  }
  // Load the library with an option preferring own symbols. Otherwise the
  // library will try to call, e.g., external tflite, which leads to crash.
  base::NativeLibraryOptions native_library_options;
  native_library_options.prefer_own_symbols = true;
  library_.emplace(base::LoadNativeLibraryWithOptions(
      model_path_.Append(kHandwritingLibraryRelativePath),
      native_library_options, nullptr));
  if (!library_->is_valid()) {
    status_ = Status::kLoadLibraryFailed;
    return;
  }

// Helper macro to look up functions from the library, assuming the function
// pointer type is named as (name+"Fn"), which is the case in
// "libhandwriting/handwriting_interface.h".
#define ML_HANDWRITING_LOOKUP_FUNCTION(function_ptr, name)             \
  function_ptr =                                                       \
      reinterpret_cast<name##Fn>(library_->GetFunctionPointer(#name)); \
  if (function_ptr == NULL) {                                          \
    status_ = Status::kFunctionLookupFailed;                           \
    return;                                                            \
  }
  // Look up the function pointers.
  ML_HANDWRITING_LOOKUP_FUNCTION(create_handwriting_recognizer_,
                                 CreateHandwritingRecognizer);
  ML_HANDWRITING_LOOKUP_FUNCTION(load_handwriting_recognizer_,
                                 LoadHandwritingRecognizer);
  ML_HANDWRITING_LOOKUP_FUNCTION(recognize_handwriting_, RecognizeHandwriting);
  ML_HANDWRITING_LOOKUP_FUNCTION(delete_handwriting_result_data_,
                                 DeleteHandwritingResultData);
  ML_HANDWRITING_LOOKUP_FUNCTION(destroy_handwriting_recognizer_,
                                 DestroyHandwritingRecognizer);
#undef ML_HANDWRITING_LOOKUP_FUNCTION

  status_ = Status::kOk;
}

HandwritingLibrary::Status HandwritingLibrary::GetStatus() const {
  return status_;
}

HandwritingLibrary* HandwritingLibrary::GetInstance(
    const std::string& model_path) {
  static base::NoDestructor<HandwritingLibrary> instance(model_path);
  return instance.get();
}

// Proxy functions to the library function pointers.
HandwritingRecognizer HandwritingLibrary::CreateHandwritingRecognizer() const {
  DCHECK(status_ == Status::kOk);
  return (*create_handwriting_recognizer_)();
}

bool HandwritingLibrary::LoadHandwritingRecognizer(
    HandwritingRecognizer const recognizer,
    HandwritingRecognizerSpecPtr spec) const {
  DCHECK(status_ == Status::kOk);

  // options is not used for now.
  const std::string options_pb =
      HandwritingRecognizerOptions().SerializeAsString();

  const std::string paths_pb =
      GetModelPaths(std::move(spec), model_path_).SerializeAsString();
  return (*load_handwriting_recognizer_)(recognizer, options_pb.data(),
                                         options_pb.size(), paths_pb.data(),
                                         paths_pb.size());
}

bool HandwritingLibrary::RecognizeHandwriting(
    HandwritingRecognizer const recognizer,
    const chrome_knowledge::HandwritingRecognizerRequest& request,
    chrome_knowledge::HandwritingRecognizerResult* const result) const {
  DCHECK(status_ == Status::kOk);
  const std::string request_pb = request.SerializeAsString();
  char* result_data = nullptr;
  int result_size = 0;
  const bool recognize_result =
      (*recognize_handwriting_)(recognizer, request_pb.data(),
                                request_pb.size(), &result_data, &result_size);
  if (recognize_result) {
    const bool parse_result_status =
        result->ParseFromArray(result_data, result_size);
    DCHECK(parse_result_status);
    // only need to delete result_data if succeeds.
    (*delete_handwriting_result_data_)(result_data);
  }

  return recognize_result;
}

void HandwritingLibrary::DestroyHandwritingRecognizer(
    HandwritingRecognizer const recognizer) const {
  DCHECK(status_ == Status::kOk);
  (*destroy_handwriting_recognizer_)(recognizer);
}

}  // namespace ml
