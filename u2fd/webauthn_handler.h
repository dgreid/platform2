// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_WEBAUTHN_HANDLER_H_
#define U2FD_WEBAUTHN_HANDLER_H_

#include <functional>
#include <memory>
#include <queue>
#include <vector>

#include <brillo/dbus/dbus_method_response.h>

#include <u2f/proto_bindings/u2f_interface.pb.h>

#include "u2fd/tpm_vendor_cmd.h"
#include "u2fd/user_state.h"

namespace u2f {

using MakeCredentialMethodResponse =
    brillo::dbus_utils::DBusMethodResponse<MakeCredentialResponse>;
using GetAssertionMethodResponse =
    brillo::dbus_utils::DBusMethodResponse<GetAssertionResponse>;

struct MakeCredentialSession {
  bool empty() { return !response_; }
  uint64_t session_id;
  MakeCredentialRequest request_;
  std::unique_ptr<MakeCredentialMethodResponse> response_;
};

struct GetAssertionSession {
  bool empty() { return !response_; }
  uint64_t session_id;
  GetAssertionRequest request_;
  std::unique_ptr<GetAssertionMethodResponse> response_;
};

enum class PresenceRequirement {
  kPowerButton,  // Requires a power button press as indication of presence.
  kFingerprint,  // Requires the GPIO line from fingerprint MCU to be active.
};

// Implementation of the WebAuthn DBus API.
// More detailed documentation is available in u2f_interface.proto
class WebAuthnHandler {
 public:
  WebAuthnHandler();

  // Initializes WebAuthnHandler.
  // |tpm_proxy| - proxy to send commands to TPM. Owned by U2fDaemon and should
  // outlive WebAuthnHandler.
  // |user_state| - pointer to a UserState instance, for requesting user secret.
  // Owned by U2fDaemon and should outlive WebAuthnHandler.
  // |request_presence| - callback for performing other platform tasks when
  // expecting the user to press the power button.
  void Initialize(TpmVendorCommandProxy* tpm_proxy,
                  UserState* user_state,
                  std::function<void()> request_presence);

  // Generates a new credential.
  void MakeCredential(
      std::unique_ptr<MakeCredentialMethodResponse> method_response,
      const MakeCredentialRequest& request);

  // Signs a challenge from the relaying party.
  void GetAssertion(std::unique_ptr<GetAssertionMethodResponse> method_response,
                    const GetAssertionRequest& request);

  // Tests validity and/or presence of specified credentials.
  HasCredentialsResponse HasCredentials(const HasCredentialsRequest& request);

 private:
  friend class WebAuthnHandlerTest;

  bool Initialized();

  // Proceeds to cr50 for the current MakeCredential request, and responds to
  // the request with authenticator data.
  // Called directly if the request is user-presence only.
  // Called on user verification success if the request is user-verification.
  void DoMakeCredential(struct MakeCredentialSession session,
                        PresenceRequirement presence_requirement);

  // Runs a U2F_GENERATE command to create a new key handle, and stores the key
  // handle in |credential_id| and the public key in |credential_public_key|.
  // The flag in the U2F_GENERATE command is set according to
  // |presence_requirement|.
  // |rp_id_hash| must be exactly 32 bytes.
  MakeCredentialResponse::MakeCredentialStatus DoU2fGenerate(
      const std::vector<uint8_t>& rp_id_hash,
      PresenceRequirement presence_requirement,
      std::vector<uint8_t>* credential_id,
      std::vector<uint8_t>* credential_public_key);

  // Prompts the user for presence through |request_presence_| and calls |fn|
  // repeatedly until success or timeout.
  void CallAndWaitForPresence(std::function<uint32_t()> fn, uint32_t* status);

  TpmVendorCommandProxy* tpm_proxy_;
  UserState* user_state_;
  std::function<void()> request_presence_;
};

}  // namespace u2f

#endif  // U2FD_WEBAUTHN_HANDLER_H_
