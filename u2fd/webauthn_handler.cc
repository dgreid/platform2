// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/webauthn_handler.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/time/time.h>
#include <u2f/proto_bindings/u2f_interface.pb.h>

#include "u2fd/util.h"

namespace u2f {

namespace {

constexpr int kVerificationTimeoutMs = 10000;
constexpr int kVerificationRetryDelayUs = 500 * 1000;

// Cr50 Response codes.
// TODO(louiscollard): Don't duplicate these.
constexpr uint32_t kCr50StatusNotAllowed = 0x507;

constexpr char kAttestationFormatNone[] = "none";
constexpr char kAttestationStatementNone = '\xa0';

void AppendToString(const std::vector<uint8_t>& vect, std::string* str) {
  str->append(reinterpret_cast<const char*>(vect.data()), vect.size());
}

std::vector<uint8_t> GetAuthenticatorData() {
  // TODO(louiscollard): Implement.
  return {0, 1, 2, 3, 4, 5};
}

void AppendAttestedCredential(const std::vector<uint8_t>& credential_id,
                              const std::vector<uint8_t>& credential_public_key,
                              std::vector<uint8_t>* authenticator_data) {
  // TODO(louiscollard): Format this data correctly (eg include credential_id
  // length).
  util::AppendToVector(credential_id, authenticator_data);
  util::AppendToVector(credential_public_key, authenticator_data);
}

}  // namespace

WebAuthnHandler::WebAuthnHandler()
    : tpm_proxy_(nullptr), user_state_(nullptr) {}

void WebAuthnHandler::Initialize(TpmVendorCommandProxy* tpm_proxy,
                                 UserState* user_state,
                                 std::function<void()> request_presence) {
  tpm_proxy_ = tpm_proxy;
  user_state_ = user_state;
  request_presence_ = request_presence;
}

bool WebAuthnHandler::Initialized() {
  return tpm_proxy_ != nullptr && user_state_ != nullptr;
}

void WebAuthnHandler::MakeCredential(
    std::unique_ptr<MakeCredentialMethodResponse> method_response,
    const MakeCredentialRequest& request) {
  MakeCredentialResponse response;

  if (!Initialized()) {
    response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
    method_response->Return(response);
    return;
  }

  if (request.rp_id().empty()) {
    response.set_status(MakeCredentialResponse::INVALID_REQUEST);
    method_response->Return(response);
    return;
  }

  if (request.verification_type() !=
      VerificationType::VERIFICATION_USER_PRESENCE) {
    // TODO(yichengli): Add support for VERIFICATION_USER_VERIFICATION
    response.set_status(MakeCredentialResponse::VERIFICATION_FAILED);
    method_response->Return(response);
    return;
  }

  struct MakeCredentialSession session = {
      static_cast<uint64_t>(base::Time::Now().ToTimeT()), request,
      std::move(method_response)};
  DoMakeCredential(std::move(session), PresenceRequirement::kPowerButton);
}

void WebAuthnHandler::DoMakeCredential(
    struct MakeCredentialSession session,
    PresenceRequirement presence_requirement) {
  MakeCredentialResponse response;
  std::vector<uint8_t> credential_id;
  std::vector<uint8_t> credential_public_key;

  MakeCredentialResponse::MakeCredentialStatus generate_status = DoU2fGenerate(
      util::Sha256(session.request_.rp_id()), presence_requirement,
      &credential_id, &credential_public_key);

  if (generate_status != MakeCredentialResponse::SUCCESS) {
    response.set_status(generate_status);
    session.response_->Return(response);
    return;
  }

  response.set_status(MakeCredentialResponse::SUCCESS);

  std::vector<uint8_t> authenticator_data = GetAuthenticatorData();
  AppendAttestedCredential(credential_id, credential_public_key,
                           &authenticator_data);
  AppendToString(authenticator_data, response.mutable_authenticator_data());

  response.set_attestation_format(kAttestationFormatNone);
  response.mutable_attestation_statement()->push_back(
      kAttestationStatementNone);

  session.response_->Return(response);
}

void WebAuthnHandler::CallAndWaitForPresence(std::function<uint32_t()> fn,
                                             uint32_t* status) {
  *status = fn();
  base::TimeTicks verification_start = base::TimeTicks::Now();
  while (*status == kCr50StatusNotAllowed &&
         base::TimeTicks::Now() - verification_start <
             base::TimeDelta::FromMilliseconds(kVerificationTimeoutMs)) {
    // We need user presence. Show a notification requesting it, and try again.
    request_presence_();
    usleep(kVerificationRetryDelayUs);
    *status = fn();
  }
}

MakeCredentialResponse::MakeCredentialStatus WebAuthnHandler::DoU2fGenerate(
    const std::vector<uint8_t>& rp_id_hash,
    PresenceRequirement presence_requirement,
    std::vector<uint8_t>* credential_id,
    std::vector<uint8_t>* credential_public_key) {
  DCHECK(rp_id_hash.size() == SHA256_DIGEST_LENGTH);
  base::Optional<brillo::SecureBlob> user_secret = user_state_->GetUserSecret();
  if (!user_secret.has_value()) {
    return MakeCredentialResponse::INTERNAL_ERROR;
  }

  if (presence_requirement != PresenceRequirement::kPowerButton) {
    // TODO(yichengli): Add support for requiring fingerprint GPIO active.
    return MakeCredentialResponse::INTERNAL_ERROR;
  }

  U2F_GENERATE_REQ generate_req = {
      .flags = U2F_AUTH_ENFORCE  // Require user presence, consume.
  };
  util::VectorToObject(rp_id_hash, generate_req.appId);
  util::VectorToObject(*user_secret, generate_req.userSecret);

  U2F_GENERATE_RESP generate_resp = {};

  uint32_t generate_status = -1;
  base::AutoLock(tpm_proxy_->GetLock());
  CallAndWaitForPresence(
      [this, &generate_req, &generate_resp]() {
        return tpm_proxy_->SendU2fGenerate(generate_req, &generate_resp);
      },
      &generate_status);

  if (generate_status == 0) {
    util::AppendToVector(generate_resp.pubKey, credential_public_key);
    util::AppendToVector(generate_resp.keyHandle, credential_id);
    return MakeCredentialResponse::SUCCESS;
  }

  return MakeCredentialResponse::VERIFICATION_FAILED;
}

void WebAuthnHandler::GetAssertion(
    std::unique_ptr<GetAssertionMethodResponse> method_response,
    const GetAssertionRequest& request) {
  // TODO(louiscollard): Implement.
}

HasCredentialsResponse WebAuthnHandler::HasCredentials(
    const HasCredentialsRequest& request) {
  // TODO(louiscollard): Implement.
  return HasCredentialsResponse();
}

}  // namespace u2f
