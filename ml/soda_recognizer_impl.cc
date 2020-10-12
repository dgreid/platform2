// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/soda_recognizer_impl.h"

#include <string>
#include <utility>

#include "chrome/knowledge/soda/extended_soda_api.pb.h"
#include "ml/request_metrics.h"
#include "ml/soda.h"
#include "ml/soda_proto_mojom_conversion.h"

namespace ml {
namespace {

using ::chromeos::machine_learning::mojom::EndpointReason;
using ::chromeos::machine_learning::mojom::FinalResult;
using ::chromeos::machine_learning::mojom::FinalResultPtr;
using ::chromeos::machine_learning::mojom::SodaClient;
using ::chromeos::machine_learning::mojom::SodaConfigPtr;
using ::chromeos::machine_learning::mojom::SodaRecognizer;
using ::chromeos::machine_learning::mojom::SpeechRecognizerEvent;
using ::chromeos::machine_learning::mojom::SpeechRecognizerEventPtr;
using ::speech::soda::chrome::SodaResponse;

constexpr char kSodaDefaultConfigFilePath[] =
    "/opt/google/chrome/ml_models/soda/models/en_us/dictation.ascii_proto";

void SodaCallback(const char* soda_response_str,
                  int size,
                  void* soda_recognizer_impl) {
  SodaResponse response;
  if (!response.ParseFromArray(soda_response_str, size)) {
    LOG(ERROR) << "Parse SODA response failed." << std::endl;
    return;
  }
  // For this initial version, only send the recognition result to Chrome.
  if (response.has_recognition_result() &&
      !response.recognition_result().hypothesis().empty()) {
    reinterpret_cast<SodaRecognizerImpl*>(soda_recognizer_impl)
        ->OnSodaEvent(response.SerializeAsString());
  }
}

}  // namespace

bool SodaRecognizerImpl::Create(
    SodaConfigPtr spec,
    mojo::PendingRemote<SodaClient> soda_client,
    mojo::PendingReceiver<SodaRecognizer> soda_recognizer) {
  auto recognizer_impl = new SodaRecognizerImpl(
      std::move(spec), std::move(soda_client), std::move(soda_recognizer));
  // Set the disconnection handler to strongly bind `recognizer_impl` to delete
  // `recognizer_impl` when the connection is gone.
  recognizer_impl->receiver_.set_disconnect_handler(base::Bind(
      [](const SodaRecognizerImpl* const recognizer_impl) {
        delete recognizer_impl;
      },
      base::Unretained(recognizer_impl)));
  return recognizer_impl->successfully_loaded_;
}

void SodaRecognizerImpl::AddAudio(const std::string& audio) {
  auto* const soda_library = ml::SodaLibrary::GetInstance();
  DCHECK(soda_library->GetStatus() == ml::SodaLibrary::Status::kOk);
  soda_library->ExtendedAddAudio(recognizer_, audio);
}

void SodaRecognizerImpl::Stop() {
  auto* const soda_library = ml::SodaLibrary::GetInstance();
  DCHECK(soda_library->GetStatus() == ml::SodaLibrary::Status::kOk);
  soda_library->ExtendedSodaStop(recognizer_);
}

void SodaRecognizerImpl::Start() {
  auto* const soda_library = ml::SodaLibrary::GetInstance();
  DCHECK(soda_library->GetStatus() == ml::SodaLibrary::Status::kOk);
  soda_library->ExtendedSodaStart(recognizer_);
}

void SodaRecognizerImpl::MarkDone() {
  auto* const soda_library = ml::SodaLibrary::GetInstance();
  DCHECK(soda_library->GetStatus() == ml::SodaLibrary::Status::kOk);
  soda_library->ExtendedSodaMarkDone(recognizer_);
}

void SodaRecognizerImpl::OnSodaEvent(const std::string& response_str) {
  SodaResponse response;
  response.ParseFromString(response_str);
  if (IsStartSodaResponse(response)) {
    client_remote_->OnStart();
  } else if (IsStopSodaResponse(response)) {
    client_remote_->OnStop();
  } else if (IsShutdownSodaResponse(response)) {
    // Shutdowns are ignored for now.
  } else {
    client_remote_->OnSpeechRecognizerEvent(
        SpeechRecognizerEventFromProto(response));
  }
}

SodaRecognizerImpl::SodaRecognizerImpl(
    SodaConfigPtr spec,
    mojo::PendingRemote<SodaClient> soda_client,
    mojo::PendingReceiver<SodaRecognizer> soda_recognizer)
    : receiver_(this, std::move(soda_recognizer)),
      client_remote_(std::move(soda_client)) {
  auto* const soda_library = ml::SodaLibrary::GetInstance();
  DCHECK(soda_library->GetStatus() == ml::SodaLibrary::Status::kOk)
      << "SodaRecognizerImpl should be created only if "
         "SodaLibrary is initialized successfully.";
  speech::soda::chrome::ExtendedSodaConfigMsg cfg_msg;
  cfg_msg.set_channel_count(spec->channel_count);
  cfg_msg.set_sample_rate(spec->sample_rate);
  cfg_msg.set_config_file_location(kSodaDefaultConfigFilePath);
  cfg_msg.set_api_key(spec->api_key);
  std::string serialized = cfg_msg.SerializeAsString();

  ExtendedSodaConfig cfg;
  cfg.soda_config = serialized.c_str();
  cfg.soda_config_size = static_cast<int>(serialized.size());
  cfg.callback = &SodaCallback;
  cfg.callback_handle = this;

  recognizer_ = soda_library->CreateExtendedSodaAsync(cfg);

  successfully_loaded_ = (recognizer_ != nullptr);
}

SodaRecognizerImpl::~SodaRecognizerImpl() {
  ml::SodaLibrary::GetInstance()->DeleteExtendedSodaAsync(recognizer_);
}

}  // namespace ml
