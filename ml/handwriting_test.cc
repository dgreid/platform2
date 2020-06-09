// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <gtest/gtest.h>

#include "chrome/knowledge/handwriting/validate.pb.h"
#include "ml/handwriting.h"
#include "ml/handwriting_path.h"

namespace ml {

using chromeos::machine_learning::mojom::HandwritingRecognizerSpec;
using chromeos::machine_learning::mojom::HandwritingRecognizerSpecPtr;

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

// Tests each supported language against a file of labeled requests.
TEST(HandwritingLibraryTest, ExampleRequest) {
  auto* const instance = ml::HandwritingLibrary::GetInstance();
  // Nothing to test on an unsupported platform.
  if (instance->GetStatus() == ml::HandwritingLibrary::Status::kNotSupported) {
    return;
  }

  ASSERT_EQ(instance->GetStatus(), ml::HandwritingLibrary::Status::kOk);

  const std::vector<std::string> languages = {"en", "gesture_in_context"};

  for (const auto& language : languages) {
    HandwritingRecognizerSpecPtr spec =
        HandwritingRecognizerSpec::New(language);

    HandwritingRecognizer const recognizer =
        instance->CreateHandwritingRecognizer();
    const chrome_knowledge::HandwritingRecognizerModelPaths paths =
        GetModelPaths(spec.Clone()).value();
    chrome_knowledge::HandwritingRecognizerOptions options;
    ASSERT_TRUE(
        instance->LoadHandwritingRecognizer(recognizer, options, paths));

    chrome_knowledge::HandwritingRecognizerLabeledRequests test_data;
    std::string buf;
    ASSERT_TRUE(base::ReadFileToString(
        base::FilePath(GetLabeledRequestsPathForTesting(spec.Clone())), &buf));
    ASSERT_TRUE(test_data.ParseFromString(buf));
    ASSERT_GT(test_data.labeled_requests().size(), 0);
    for (auto const& request : test_data.labeled_requests()) {
      chrome_knowledge::HandwritingRecognizerResult result;
      ASSERT_TRUE(instance->RecognizeHandwriting(recognizer, request.request(),
                                                 &result));
      ASSERT_GT(result.candidates().size(), 0);
      EXPECT_EQ(result.candidates(0).text(), request.label());
    }
    instance->DestroyHandwritingRecognizer(recognizer);
  }
}

}  // namespace ml
