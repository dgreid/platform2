// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/handwriting.h"

#include <string>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/native_library.h>

namespace ml {

namespace {
constexpr char kHandwritingLibraryPath[] =
    "/opt/google/chrome/ml_models/handwriting/lib64/libhandwriting.so";

// Returns whether HandwritingLibrary is supported.
constexpr bool IsHandwritingLibrarySupported() {
#ifdef ML_SUPPORT_HANDWRITING
  // Currently HandwritingLibrary is supported only when the "sanitizer" is not
  // enabled (see https://crbug.com/1082632).
  #if __has_feature(address_sanitizer)
    return false;
  #else
    return true;
  #endif
#else
  return false;
#endif
}

}  // namespace

HandwritingLibrary::HandwritingLibrary()
    : status_(Status::kUninitialized),
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
      base::FilePath(kHandwritingLibraryPath), native_library_options,
      nullptr));
  if (!library_->is_valid()) {
    status_ = Status::kLoadLibraryFailed;
    return;
  }

// Helper macro to look up functions from the library, assuming the function
// pointer type is named as (name+"Fn"), which is the case in
// "libhandwriting/interface.h".
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

HandwritingLibrary* HandwritingLibrary::GetInstance() {
  static base::NoDestructor<HandwritingLibrary> instance;
  return instance.get();
}

// Proxy functions to the library function pointers.
HandwritingRecognizer HandwritingLibrary::CreateHandwritingRecognizer() const {
  DCHECK(status_ == Status::kOk);
  return (*create_handwriting_recognizer_)();
}

bool HandwritingLibrary::LoadHandwritingRecognizer(
    HandwritingRecognizer const recognizer,
    const chrome_knowledge::HandwritingRecognizerOptions& options,
    const chrome_knowledge::HandwritingRecognizerModelPaths& model_path) const {
  DCHECK(status_ == Status::kOk);
  const std::string options_pb = options.SerializeAsString();
  const std::string paths_pb = model_path.SerializeAsString();
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
