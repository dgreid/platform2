// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <gtest/gtest.h>

#include "chrome/knowledge/handwriting/validate.pb.h"
#include "ml/handwriting.h"

namespace ml {

TEST(HandwritingLibraryTest, CanLoadLibrary) {
  auto* const instance = ml::HandwritingLibrary::GetInstance();
#ifdef ML_SUPPORT_HANDWRITING
  #if __has_feature(address_sanitizer)
    EXPECT_EQ(instance->GetStatus(),
              ml::HandwritingLibrary::Status::kNotSupported);
  #else
    EXPECT_EQ(instance->GetStatus(), ml::HandwritingLibrary::Status::kOk);
  #endif
#else
  EXPECT_EQ(instance->GetStatus(),
            ml::HandwritingLibrary::Status::kNotSupported);
#endif
}

TEST(HandwritingLibraryTest, ExampleRequest) {
  auto* const instance = ml::HandwritingLibrary::GetInstance();
  // Nothing to test on an unsupported platform.
  if (instance->GetStatus() == ml::HandwritingLibrary::Status::kNotSupported) {
    return;
  }

  ASSERT_EQ(instance->GetStatus(), ml::HandwritingLibrary::Status::kOk);

  HandwritingRecognizer const recognizer =
      instance->CreateHandwritingRecognizer();
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
  chrome_knowledge::HandwritingRecognizerOptions options;
  ASSERT_TRUE(instance->LoadHandwritingRecognizer(recognizer, options, paths));

  chrome_knowledge::HandwritingRecognizerLabeledRequests test_data;
  std::string buf;
  ASSERT_TRUE(base::ReadFileToString(
      base::FilePath("/build/share/libhandwriting/correct_labeled_requests.pb"),
      &buf));
  ASSERT_TRUE(test_data.ParseFromString(buf));
  ASSERT_GT(test_data.labeled_requests().size(), 0);
  for (auto const& request : test_data.labeled_requests()) {
    chrome_knowledge::HandwritingRecognizerResult result;
    ASSERT_TRUE(
        instance->RecognizeHandwriting(recognizer, request.request(), &result));
    ASSERT_GT(result.candidates().size(), 0);
    EXPECT_EQ(result.candidates(0).text(), request.label());
  }
  instance->DestroyHandwritingRecognizer(recognizer);
}

}  // namespace ml
