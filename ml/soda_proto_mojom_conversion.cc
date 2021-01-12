// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

#include "ml/soda_proto_mojom_conversion.h"

using chromeos::machine_learning::mojom::EndpointerType;
using speech::soda::chrome::SodaEndpointEvent;
using speech::soda::chrome::SodaRecognitionResult;
using speech::soda::chrome::SodaResponse;

namespace ml {

chromeos::machine_learning::mojom::SpeechRecognizerEventPtr
SpeechRecognizerEventFromProto(const SodaResponse& soda_response) {
  auto speech_recognizer_event =
      chromeos::machine_learning::mojom::SpeechRecognizerEvent::New();
  if (soda_response.soda_type() == SodaResponse::AUDIO_LEVEL) {
    auto audio_level_event = internal::AudioLevelEventFromProto(soda_response);
    speech_recognizer_event->set_audio_event(std::move(audio_level_event));
  } else if (soda_response.soda_type() == SodaResponse::RECOGNITION) {
    const auto& rec_result = soda_response.recognition_result();
    if (rec_result.result_type() == SodaRecognitionResult::PARTIAL) {
      speech_recognizer_event->set_partial_result(
          internal::PartialResultFromProto(soda_response));
    } else if (rec_result.result_type() == SodaRecognitionResult::FINAL) {
      speech_recognizer_event->set_final_result(
          internal::FinalResultFromProto(soda_response));
    } else if (rec_result.result_type() == SodaRecognitionResult::PREFETCH) {
      speech_recognizer_event->set_partial_result(
          internal::PartialResultFromPrefetchProto(soda_response));
    } else {
      LOG(ERROR) << "Only partial/prefetch/final results are supported, not "
                 << speech::soda::chrome::SodaRecognitionResult_ResultType_Name(
                        rec_result.result_type());
    }
  } else if (soda_response.soda_type() == SodaResponse::ENDPOINT) {
    speech_recognizer_event->set_endpointer_event(
        internal::EndpointerEventFromProto(soda_response));
  } else {
    LOG(DFATAL) << "Unexpected type of soda type to convert: "
                << speech::soda::chrome::SodaResponse_SodaMessageType_Name(
                       soda_response.soda_type());
  }
  return speech_recognizer_event;
}

bool IsStopSodaResponse(const SodaResponse& soda_response) {
  return soda_response.soda_type() == SodaResponse::STOP;
}
bool IsStartSodaResponse(const SodaResponse& soda_response) {
  return soda_response.soda_type() == SodaResponse::START;
}

bool IsShutdownSodaResponse(const SodaResponse& soda_response) {
  return soda_response.soda_type() == SodaResponse::SHUTDOWN;
}

namespace internal {
chromeos::machine_learning::mojom::AudioLevelEventPtr AudioLevelEventFromProto(
    const SodaResponse& soda_response) {
  auto audio_level_event =
      chromeos::machine_learning::mojom::AudioLevelEvent::New();
  if (!soda_response.has_audio_level_info()) {
    LOG(DFATAL) << "Should only call this method if audio level info is set.";
    return audio_level_event;
  }
  const auto& audio_level_info = soda_response.audio_level_info();
  audio_level_event->rms = audio_level_info.rms();
  audio_level_event->audio_level = audio_level_info.audio_level();

  // TODO(robsc): add support for time here.
  return audio_level_event;
}

chromeos::machine_learning::mojom::PartialResultPtr
PartialResultFromPrefetchProto(
    const speech::soda::chrome::SodaResponse& soda_response) {
  auto partial_result = chromeos::machine_learning::mojom::PartialResult::New();
  if (!soda_response.has_recognition_result() ||
      soda_response.soda_type() != SodaResponse::RECOGNITION ||
      soda_response.recognition_result().result_type() !=
          SodaRecognitionResult::PREFETCH) {
    LOG(DFATAL) << "Should only be called when there's a prefetch result.";
  }
  for (const std::string& hyp :
       soda_response.recognition_result().hypothesis()) {
    partial_result->partial_text.push_back(hyp);
  }
  return partial_result;
}

chromeos::machine_learning::mojom::PartialResultPtr PartialResultFromProto(
    const SodaResponse& soda_response) {
  auto partial_result = chromeos::machine_learning::mojom::PartialResult::New();
  if (!soda_response.has_recognition_result() ||
      soda_response.soda_type() != SodaResponse::RECOGNITION ||
      soda_response.recognition_result().result_type() !=
          SodaRecognitionResult::PARTIAL) {
    LOG(DFATAL)
        << "Should only call when there's a partial recognition result.";
    return partial_result;
  }
  for (const std::string& hyp :
       soda_response.recognition_result().hypothesis()) {
    partial_result->partial_text.push_back(hyp);
  }
  return partial_result;
}

chromeos::machine_learning::mojom::FinalResultPtr FinalResultFromProto(
    const SodaResponse& soda_response) {
  auto final_result = chromeos::machine_learning::mojom::FinalResult::New();
  if (!soda_response.has_recognition_result() ||
      soda_response.soda_type() != SodaResponse::RECOGNITION ||
      soda_response.recognition_result().result_type() !=
          SodaRecognitionResult::FINAL) {
    LOG(DFATAL) << "Should only call when there's a final recognition result.";
    return final_result;
  }
  for (const std::string& hyp :
       soda_response.recognition_result().hypothesis()) {
    final_result->final_hypotheses.push_back(hyp);
  }
  // TODO(robsc): Add endpoint reason when available from
  final_result->endpoint_reason =
      chromeos::machine_learning::mojom::EndpointReason::ENDPOINT_UNKNOWN;
  return final_result;
}

chromeos::machine_learning::mojom::EndpointerEventPtr EndpointerEventFromProto(
    const SodaResponse& soda_response) {
  auto endpointer_event =
      chromeos::machine_learning::mojom::EndpointerEvent::New();
  if (!soda_response.has_endpoint_event() ||
      soda_response.soda_type() != SodaResponse::ENDPOINT) {
    LOG(DFATAL) << "Shouldn't have been called without an endpoint event.";
    return endpointer_event;
  }
  const auto& soda_endpoint_event = soda_response.endpoint_event();
  // Set the type, we don't have the timing right here.
  switch (soda_endpoint_event.endpoint_type()) {
    case SodaEndpointEvent::START_OF_SPEECH:
      endpointer_event->endpointer_type = EndpointerType::START_OF_SPEECH;
      break;
    case SodaEndpointEvent::END_OF_SPEECH:
      endpointer_event->endpointer_type = EndpointerType::END_OF_SPEECH;
      break;
    case SodaEndpointEvent::END_OF_AUDIO:
      endpointer_event->endpointer_type = EndpointerType::END_OF_AUDIO;
      break;
    case SodaEndpointEvent::END_OF_UTTERANCE:
      endpointer_event->endpointer_type = EndpointerType::END_OF_UTTERANCE;
      break;
    default:
      LOG(DFATAL) << "Unknown endpointer type.";
      endpointer_event->endpointer_type = EndpointerType::END_OF_UTTERANCE;
      break;
  }
  return endpointer_event;
}

}  // namespace internal
}  // namespace ml
