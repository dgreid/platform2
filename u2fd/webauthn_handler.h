// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_WEBAUTHN_HANDLER_H_
#define U2FD_WEBAUTHN_HANDLER_H_

#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include <base/optional.h>
#include <brillo/dbus/dbus_method_response.h>

#include <u2f/proto_bindings/u2f_interface.pb.h>

#include "u2fd/tpm_vendor_cmd.h"
#include "u2fd/user_state.h"
#include "u2fd/webauthn_storage.h"

namespace u2f {

using MakeCredentialMethodResponse =
    brillo::dbus_utils::DBusMethodResponse<MakeCredentialResponse>;
using GetAssertionMethodResponse =
    brillo::dbus_utils::DBusMethodResponse<GetAssertionResponse>;
using IsUvpaaMethodResponse =
    brillo::dbus_utils::DBusMethodResponse<IsUvpaaResponse>;

struct MakeCredentialSession {
  bool empty() { return !response; }
  uint64_t session_id;
  MakeCredentialRequest request;
  std::unique_ptr<MakeCredentialMethodResponse> response;
};

struct GetAssertionSession {
  bool empty() { return !response; }
  uint64_t session_id;
  GetAssertionRequest request;
  // The credential_id to send to the TPM. May be a resident credential.
  std::string credential_id;
  std::unique_ptr<GetAssertionMethodResponse> response;
};

enum class PresenceRequirement {
  kNone,  // Does not require presence. Used only after user-verification in
          // MakeCredential.
  kPowerButton,  // Requires a power button press as indication of presence.
  kFingerprint,  // Requires the GPIO line from fingerprint MCU to be active.
  kAuthorizationSecret,  // Requires the correct authorization secret.
};

// Implementation of the WebAuthn DBus API.
// More detailed documentation is available in u2f_interface.proto
class WebAuthnHandler {
 public:
  WebAuthnHandler();

  // Initializes WebAuthnHandler.
  // |bus| - DBus pointer.
  // |tpm_proxy| - proxy to send commands to TPM. Owned by U2fDaemon and should
  // outlive WebAuthnHandler.
  // |user_state| - pointer to a UserState instance, for requesting user secret.
  // Owned by U2fDaemon and should outlive WebAuthnHandler.
  // |request_presence| - callback for performing other platform tasks when
  // expecting the user to press the power button.
  void Initialize(dbus::Bus* bus,
                  TpmVendorCommandProxy* tpm_proxy,
                  UserState* user_state,
                  std::function<void()> request_presence);

  // Called when session state changed. Loads/clears state for primary user.
  void OnSessionStarted(const std::string& account_id);
  void OnSessionStopped();

  // Generates a new credential.
  void MakeCredential(
      std::unique_ptr<MakeCredentialMethodResponse> method_response,
      const MakeCredentialRequest& request);

  // Signs a challenge from the relaying party.
  void GetAssertion(std::unique_ptr<GetAssertionMethodResponse> method_response,
                    const GetAssertionRequest& request);

  // Tests validity and/or presence of specified credentials.
  HasCredentialsResponse HasCredentials(const HasCredentialsRequest& request);

  // Dismiss user verification UI and abort the operation. This is expected to
  // be called by the browser only in UV operations, because UP operations
  // themselves will timeout after ~5 seconds.
  CancelWebAuthnFlowResponse Cancel(const CancelWebAuthnFlowRequest& request);

  // Check whether user-verifying platform authenticator is available.
  void IsUvpaa(std::unique_ptr<IsUvpaaMethodResponse> method_response,
               const IsUvpaaRequest& request);

  void SetWebAuthnStorageForTesting(std::unique_ptr<WebAuthnStorage> storage);

 private:
  friend class WebAuthnHandlerTest;

  bool Initialized();

  // Fetch auth-time WebAuthn secret and keep the hash of it.
  void GetWebAuthnSecret(const std::string& account_id);

  // Callbacks invoked when UI completes user verification flow.
  void HandleUVFlowResultMakeCredential(dbus::Response* flow_response);
  void HandleUVFlowResultGetAssertion(dbus::Response* flow_response);

  // Proceeds to cr50 for the current MakeCredential request, and responds to
  // the request with authenticator data.
  // Called directly if the request is user-presence only.
  // Called on user verification success if the request is user-verification.
  void DoMakeCredential(struct MakeCredentialSession session,
                        PresenceRequirement presence_requirement);

  // Proceeds to cr50 for the current GetAssertion request, and responds to
  // the request with assertions.
  // Called directly if the request is user-presence only.
  // Called on user verification success if the request is user-verification.
  void DoGetAssertion(struct GetAssertionSession session,
                      PresenceRequirement presence_requirement);

  // Runs a U2F_GENERATE command to create a new key handle, and stores the key
  // handle in |credential_id| and the public key in |credential_public_key|.
  // The flag in the U2F_GENERATE command is set according to
  // |presence_requirement|.
  // |rp_id_hash| must be exactly 32 bytes.
  MakeCredentialResponse::MakeCredentialStatus DoU2fGenerate(
      const std::vector<uint8_t>& rp_id_hash,
      const brillo::SecureBlob& credential_secret,
      PresenceRequirement presence_requirement,
      bool uv_compatible,
      std::vector<uint8_t>* credential_id,
      std::vector<uint8_t>* credential_public_key);

  // Repeatedly send u2f_generate request to the TPM if there's no presence.
  template <typename Response>
  MakeCredentialResponse::MakeCredentialStatus SendU2fGenerateWaitForPresence(
      struct u2f_generate_req* generate_req,
      Response* generate_resp,
      std::vector<uint8_t>* credential_id,
      std::vector<uint8_t>* credential_public_key);

  // Runs a U2F_SIGN command to check that credential_id is valid, and if so,
  // sign |hash_to_sign| and store the signature in |signature|.
  // The flag in the U2F_SIGN command is set according to
  // |presence_requirement|.
  // |rp_id_hash| must be exactly 32 bytes.
  GetAssertionResponse::GetAssertionStatus DoU2fSign(
      const std::vector<uint8_t>& rp_id_hash,
      const std::vector<uint8_t>& hash_to_sign,
      const std::vector<uint8_t>& credential_id,
      const brillo::SecureBlob& credential_secret,
      PresenceRequirement presence_requirement,
      std::vector<uint8_t>* signature);

  // Repeatedly send u2f_sign request to the TPM if there's no presence.
  template <typename Request>
  GetAssertionResponse::GetAssertionStatus SendU2fSignWaitForPresence(
      Request* sign_req,
      struct u2f_sign_resp* sign_resp,
      std::vector<uint8_t>* signature);

  // Runs a U2F_SIGN command with "check only" flag to check whether
  // |credential_id| is a key handle owned by this device tied to |rp_id_hash|.
  HasCredentialsResponse::HasCredentialsStatus DoU2fSignCheckOnly(
      const std::vector<uint8_t>& rp_id_hash,
      const std::vector<uint8_t>& credential_id,
      const brillo::SecureBlob& credential_secret);

  // Prompts the user for presence through |request_presence_| and calls |fn|
  // repeatedly until success or timeout.
  void CallAndWaitForPresence(std::function<uint32_t()> fn, uint32_t* status);

  // Creates and returns authenticator data. |include_attested_credential_data|
  // should be set to true for MakeCredential, false for GetAssertion.
  std::vector<uint8_t> MakeAuthenticatorData(
      const std::vector<uint8_t>& rp_id_hash,
      const std::vector<uint8_t>& credential_id,
      const std::vector<uint8_t>& credential_public_key,
      bool user_verified,
      bool include_attested_credential_data);

  // Appends a none attestation to |response|. Only used in MakeCredential.
  void AppendNoneAttestation(MakeCredentialResponse* response);

  // Runs U2F_SIGN command with "check only" flag on each excluded credential
  // id. Returns true if one of them belongs to this device.
  HasCredentialsResponse::HasCredentialsStatus HasExcludedCredentials(
      const MakeCredentialRequest& request);

  TpmVendorCommandProxy* tpm_proxy_ = nullptr;
  UserState* user_state_ = nullptr;
  std::function<void()> request_presence_;
  dbus::Bus* bus_ = nullptr;
  // Proxy to user authentication dialog in Ash. Used only in UV requests.
  dbus::ObjectProxy* auth_dialog_dbus_proxy_ = nullptr;

  // The MakeCredential session that's waiting on UI. There can only be one
  // such session. UP sessions should not use this since there can be multiple.
  base::Optional<MakeCredentialSession> pending_uv_make_credential_session_;

  // The GetAssertion session that's waiting on UI. There can only be one
  // such session. UP sessions should not use this since there can be multiple.
  base::Optional<GetAssertionSession> pending_uv_get_assertion_session_;

  // Hash of the per-user auth-time secret for WebAuthn.
  std::unique_ptr<brillo::Blob> auth_time_secret_hash_;

  // Storage for WebAuthn credential records.
  std::unique_ptr<WebAuthnStorage> webauthn_storage_;
};

}  // namespace u2f

#endif  // U2FD_WEBAUTHN_HANDLER_H_
