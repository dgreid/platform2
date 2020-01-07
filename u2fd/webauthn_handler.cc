// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/webauthn_handler.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/time/time.h>
#include <chromeos/cbor/values.h>
#include <chromeos/cbor/writer.h>
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
// \xa0 is empty map in CBOR
constexpr char kAttestationStatementNone = '\xa0';

// AAGUID should be empty for none-attestation.
const std::vector<uint8_t> kAaguid(16);

// AuthenticatorData flags are defined in
// https://www.w3.org/TR/webauthn-2/#sctn-authenticator-data
enum class AuthenticatorDataFlag : uint8_t {
  kTestOfUserPresence = 1u << 0,
  kTestOfUserVerification = 1u << 2,
  kAttestedCredentialData = 1u << 6,
  kExtensionDataIncluded = 1u << 7,
};

// COSE key parameters.
// https://tools.ietf.org/html/rfc8152#section-7.1
const int kCoseKeyKtyLabel = 1;
const int kCoseKeyKtyEC2 = 2;
const int kCoseKeyAlgLabel = 3;
const int kCoseKeyAlgES256 = -7;

// Double coordinate curve parameters.
// https://tools.ietf.org/html/rfc8152#section-13.1.1
const int kCoseECKeyCrvLabel = -1;
const int kCoseECKeyXLabel = -2;
const int kCoseECKeyYLabel = -3;

std::vector<uint8_t> Uint16ToByteVector(uint16_t value) {
  return std::vector<uint8_t>({static_cast<uint8_t>((value >> 8) & 0xff),
                               static_cast<uint8_t>(value & 0xff)});
}

void AppendToString(const std::vector<uint8_t>& vect, std::string* str) {
  str->append(reinterpret_cast<const char*>(vect.data()), vect.size());
}

void AppendAttestedCredential(const std::vector<uint8_t>& credential_id,
                              const std::vector<uint8_t>& credential_public_key,
                              std::vector<uint8_t>* authenticator_data) {
  util::AppendToVector(credential_id, authenticator_data);
  util::AppendToVector(credential_public_key, authenticator_data);
}

// Returns the current time in seconds since epoch as a privacy-preserving
// signature counter. Because of the conversion to a 32-bit unsigned integer,
// the counter will overflow in the year 2108.
std::vector<uint8_t> GetTimestampSignatureCounter() {
  uint32_t sign_counter = static_cast<uint32_t>(base::Time::Now().ToDoubleT());
  return std::vector<uint8_t>{
      static_cast<uint8_t>((sign_counter >> 24) & 0xff),
      static_cast<uint8_t>((sign_counter >> 16) & 0xff),
      static_cast<uint8_t>((sign_counter >> 8) & 0xff),
      static_cast<uint8_t>(sign_counter & 0xff),
  };
}

std::vector<uint8_t> EncodeCredentialPublicKeyInCBOR(
    const std::vector<uint8_t>& credential_public_key) {
  DCHECK_EQ(credential_public_key.size(), sizeof(U2F_EC_POINT));
  cbor::Value::MapValue cbor_map;
  cbor_map[cbor::Value(kCoseKeyKtyLabel)] = cbor::Value(kCoseKeyKtyEC2);
  cbor_map[cbor::Value(kCoseKeyAlgLabel)] = cbor::Value(kCoseKeyAlgES256);
  cbor_map[cbor::Value(kCoseECKeyCrvLabel)] = cbor::Value(1);
  cbor_map[cbor::Value(kCoseECKeyXLabel)] =
      cbor::Value(base::make_span<const uint8_t>(
          credential_public_key.data() + offsetof(U2F_EC_POINT, x),
          U2F_EC_KEY_SIZE));
  cbor_map[cbor::Value(kCoseECKeyYLabel)] =
      cbor::Value(base::make_span<const uint8_t>(
          credential_public_key.data() + offsetof(U2F_EC_POINT, y),
          U2F_EC_KEY_SIZE));
  return *cbor::Writer::Write(cbor::Value(std::move(cbor_map)));
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
  const std::vector<uint8_t> rp_id_hash =
      util::Sha256(session.request_.rp_id());
  std::vector<uint8_t> credential_id;
  std::vector<uint8_t> credential_public_key;

  MakeCredentialResponse::MakeCredentialStatus generate_status = DoU2fGenerate(
      rp_id_hash, presence_requirement, &credential_id, &credential_public_key);

  if (generate_status != MakeCredentialResponse::SUCCESS) {
    response.set_status(generate_status);
    session.response_->Return(response);
    return;
  }

  if (credential_id.empty() || credential_public_key.empty()) {
    response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
    session.response_->Return(response);
    return;
  }

  AppendToString(MakeAuthenticatorData(
                     rp_id_hash, credential_id,
                     EncodeCredentialPublicKeyInCBOR(credential_public_key),
                     session.request_.verification_type() ==
                         VerificationType::VERIFICATION_USER_VERIFICATION,
                     true),
                 response.mutable_authenticator_data());
  AppendNoneAttestation(&response);

  response.set_status(MakeCredentialResponse::SUCCESS);
  session.response_->Return(response);
}

// AuthenticatorData layout:
// (See https://www.w3.org/TR/webauthn-2/#table-authData)
// -----------------------------------------------------------------------
// | RP ID hash:       32 bytes
// | Flags:             1 byte
// | Signature counter: 4 bytes
// |                           -------------------------------------------
// |                           | AAGUID:                  16 bytes
// | Attested Credential Data: | Credential ID length (L): 2 bytes
// | (if present)              | Credential ID:            L bytes
// |                           | Credential public key:    variable length
std::vector<uint8_t> WebAuthnHandler::MakeAuthenticatorData(
    const std::vector<uint8_t>& rp_id_hash,
    const std::vector<uint8_t>& credential_id,
    const std::vector<uint8_t>& credential_public_key,
    bool user_verified,
    bool include_attested_credential_data) {
  std::vector<uint8_t> authenticator_data(rp_id_hash);
  uint8_t flags =
      static_cast<uint8_t>(AuthenticatorDataFlag::kTestOfUserPresence);
  if (user_verified)
    flags |=
        static_cast<uint8_t>(AuthenticatorDataFlag::kTestOfUserVerification);
  if (include_attested_credential_data)
    flags |=
        static_cast<uint8_t>(AuthenticatorDataFlag::kAttestedCredentialData);
  authenticator_data.emplace_back(flags);
  util::AppendToVector(GetTimestampSignatureCounter(), &authenticator_data);

  if (include_attested_credential_data) {
    util::AppendToVector(kAaguid, &authenticator_data);
    uint16_t length = credential_id.size();
    util::AppendToVector(Uint16ToByteVector(length), &authenticator_data);

    AppendAttestedCredential(credential_id, credential_public_key,
                             &authenticator_data);
  }

  return authenticator_data;
}

void WebAuthnHandler::AppendNoneAttestation(MakeCredentialResponse* response) {
  response->set_attestation_format(kAttestationFormatNone);
  response->mutable_attestation_statement()->push_back(
      kAttestationStatementNone);
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
