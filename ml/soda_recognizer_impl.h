// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_SODA_RECOGNIZER_IMPL_H_
#define ML_SODA_RECOGNIZER_IMPL_H_

#include <string>

#include <base/callback_forward.h>
#include <base/macros.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/shared_remote.h>

#include "ml/mojom/soda.mojom.h"

namespace ml {

// The implementation of SodaSpeechRecognizer.
class SodaRecognizerImpl
    : public chromeos::machine_learning::mojom::SodaRecognizer {
 public:
  // Constructs a SodaRecognizerImpl; and set disconnect handler so
  // that the SodaRecognizerImpl will be deleted when the mojom
  // connection is destroyed.
  // Returns whether the object is create successfully.
  static bool Create(
      chromeos::machine_learning::mojom::SodaConfigPtr spec,
      mojo::PendingRemote<chromeos::machine_learning::mojom::SodaClient>
          soda_client,
      mojo::PendingReceiver<chromeos::machine_learning::mojom::SodaRecognizer>
          soda_recognizer);

  // Called when mojom connection is destroyed.
  ~SodaRecognizerImpl();

  // mojom::SodaRecognizer::AddAudio:
  void AddAudio(const std::string& audio) override;

  // mojom::SodaRecognizer::Stop:
  void Stop() override;

  // mojom::SodaRecognizer::Start:
  void Start() override;

  // mojom::SodaRecognizer::MarkDone:
  void MarkDone() override;

  // Used to send the event to the client. For the initial version, only accepts
  // a string, which is in an unspecified format.
  void OnSodaEvent(const std::string& soda_response);

 private:
  // Creates a SodaRecognizer and Binds to `receiver` inside so that
  // Recognize can be called on the other side for a particular soda
  // reconition query.
  SodaRecognizerImpl(
      chromeos::machine_learning::mojom::SodaConfigPtr spec,
      mojo::PendingRemote<chromeos::machine_learning::mojom::SodaClient>
          soda_client,
      mojo::PendingReceiver<chromeos::machine_learning::mojom::SodaRecognizer>
          receiver);

  bool successfully_loaded_;

  // Pointer handle to the internal implementation of SodaRecognizer inside
  // the SodaLibrary.
  void* recognizer_;

  mojo::Receiver<chromeos::machine_learning::mojom::SodaRecognizer> receiver_;

  mojo::SharedRemote<chromeos::machine_learning::mojom::SodaClient>
      client_remote_;

  DISALLOW_COPY_AND_ASSIGN(SodaRecognizerImpl);
};

}  // namespace ml

#endif  // ML_SODA_RECOGNIZER_IMPL_H_
