// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <string>
#include <utility>

#include "ml/soda_proto_mojom_conversion.h"

namespace ml {

using speech::soda::chrome::SodaResponse;

TEST(SodaProtoMojomConversionTest, AudioLevelsTest) {
  SodaResponse response;
  response.set_soda_type(SodaResponse::AUDIO_LEVEL);
  response.mutable_audio_level_info()->set_audio_level(0.1);
  response.mutable_audio_level_info()->set_rms(0.3);
  auto actual_audio_mojom = internal::AudioLevelEventFromProto(response);

  auto expected_audio_mojom =
      chromeos::machine_learning::mojom::AudioLevelEvent::New();
  expected_audio_mojom->rms = 0.3;
  expected_audio_mojom->audio_level = 0.1;

  EXPECT_TRUE(actual_audio_mojom.Equals(expected_audio_mojom));

  // now for the full mojom
  auto actual_mojom = SpeechRecognizerEventFromProto(response);
  chromeos::machine_learning::mojom::SpeechRecognizerEventPtr expected_mojom =
      chromeos::machine_learning::mojom::SpeechRecognizerEvent::New();
  expected_mojom->set_audio_event(std::move(expected_audio_mojom));
  EXPECT_TRUE(actual_mojom.Equals(expected_mojom));

  // Let's check the other tests.
  EXPECT_FALSE(IsStopSodaResponse(response));
  EXPECT_FALSE(IsStartSodaResponse(response));
  EXPECT_FALSE(IsShutdownSodaResponse(response));
}

TEST(SodaProtoMojomConversionTest, PartialResultsTest) {
  SodaResponse response;
  response.set_soda_type(SodaResponse::RECOGNITION);
  auto* rec = response.mutable_recognition_result();
  rec->add_hypothesis("first hyp");
  rec->add_hypothesis("second hyp");
  rec->set_result_type(speech::soda::chrome::SodaRecognitionResult::PARTIAL);

  auto expected_rec_mojom =
      chromeos::machine_learning::mojom::PartialResult::New();
  expected_rec_mojom->partial_text.push_back("first hyp");
  expected_rec_mojom->partial_text.push_back("second hyp");
  auto actual_rec_mojom = internal::PartialResultFromProto(response);
  EXPECT_TRUE(actual_rec_mojom.Equals(expected_rec_mojom));

  // now for the full mojom
  auto actual_mojom = SpeechRecognizerEventFromProto(response);
  chromeos::machine_learning::mojom::SpeechRecognizerEventPtr expected_mojom =
      chromeos::machine_learning::mojom::SpeechRecognizerEvent::New();
  expected_mojom->set_partial_result(std::move(actual_rec_mojom));
  EXPECT_TRUE(actual_mojom.Equals(expected_mojom));

  // Let's check the other tests.
  EXPECT_FALSE(IsStopSodaResponse(response));
  EXPECT_FALSE(IsStartSodaResponse(response));
  EXPECT_FALSE(IsShutdownSodaResponse(response));
}

TEST(SodaProtoMojomConversionTest, FinalResultsTest) {
  SodaResponse response;
  response.set_soda_type(SodaResponse::RECOGNITION);
  auto* rec = response.mutable_recognition_result();
  rec->add_hypothesis("first hypo");
  rec->add_hypothesis("second hypo");
  rec->set_result_type(speech::soda::chrome::SodaRecognitionResult::FINAL);

  auto expected_rec_mojom =
      chromeos::machine_learning::mojom::FinalResult::New();
  expected_rec_mojom->final_hypotheses.push_back("first hypo");
  expected_rec_mojom->final_hypotheses.push_back("second hypo");
  auto actual_rec_mojom = internal::FinalResultFromProto(response);
  EXPECT_TRUE(actual_rec_mojom.Equals(expected_rec_mojom));

  // now for the full mojom
  auto actual_mojom = SpeechRecognizerEventFromProto(response);
  chromeos::machine_learning::mojom::SpeechRecognizerEventPtr expected_mojom =
      chromeos::machine_learning::mojom::SpeechRecognizerEvent::New();
  expected_mojom->set_final_result(std::move(actual_rec_mojom));
  EXPECT_TRUE(actual_mojom.Equals(expected_mojom));

  // Let's check the other tests.
  EXPECT_FALSE(IsStopSodaResponse(response));
  EXPECT_FALSE(IsStartSodaResponse(response));
  EXPECT_FALSE(IsShutdownSodaResponse(response));
}

TEST(SodaProtoMojomConversionTest, EndpointTest) {
  SodaResponse response;
  response.set_soda_type(SodaResponse::ENDPOINT);
  auto* end = response.mutable_endpoint_event();
  end->set_endpoint_type(
      speech::soda::chrome::SodaEndpointEvent::END_OF_SPEECH);

  auto expected_end_mojom =
      chromeos::machine_learning::mojom::EndpointerEvent::New();
  expected_end_mojom->endpointer_type =
      chromeos::machine_learning::mojom::EndpointerType::END_OF_SPEECH;
  auto actual_end_mojom = internal::EndpointerEventFromProto(response);
  EXPECT_TRUE(actual_end_mojom.Equals(expected_end_mojom));

  // now for the full mojom
  auto actual_mojom = SpeechRecognizerEventFromProto(response);
  chromeos::machine_learning::mojom::SpeechRecognizerEventPtr expected_mojom =
      chromeos::machine_learning::mojom::SpeechRecognizerEvent::New();
  expected_mojom->set_endpointer_event(std::move(actual_end_mojom));
  EXPECT_TRUE(actual_mojom.Equals(expected_mojom));

  // Let's check the other tests.
  EXPECT_FALSE(IsStopSodaResponse(response));
  EXPECT_FALSE(IsStartSodaResponse(response));
  EXPECT_FALSE(IsShutdownSodaResponse(response));
}

TEST(SodaProtoMojomConversionTest, BooleanFunctionTest) {
  SodaResponse response;

  response.set_soda_type(SodaResponse::STOP);
  EXPECT_TRUE(IsStopSodaResponse(response));
  EXPECT_FALSE(IsStartSodaResponse(response));
  EXPECT_FALSE(IsShutdownSodaResponse(response));

  response.set_soda_type(SodaResponse::START);
  EXPECT_FALSE(IsStopSodaResponse(response));
  EXPECT_TRUE(IsStartSodaResponse(response));
  EXPECT_FALSE(IsShutdownSodaResponse(response));

  response.set_soda_type(SodaResponse::SHUTDOWN);
  EXPECT_FALSE(IsStopSodaResponse(response));
  EXPECT_FALSE(IsStartSodaResponse(response));
  EXPECT_TRUE(IsShutdownSodaResponse(response));
}

}  // namespace ml
