// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/webauthn_handler.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind_helpers.h>
#include <base/time/time.h>
#include <chromeos/cbor/values.h>
#include <chromeos/cbor/writer.h>
#include <chromeos/dbus/service_constants.h>
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
  DCHECK_EQ(credential_public_key.size(), sizeof(struct u2f_ec_point));
  cbor::Value::MapValue cbor_map;
  cbor_map[cbor::Value(kCoseKeyKtyLabel)] = cbor::Value(kCoseKeyKtyEC2);
  cbor_map[cbor::Value(kCoseKeyAlgLabel)] = cbor::Value(kCoseKeyAlgES256);
  cbor_map[cbor::Value(kCoseECKeyCrvLabel)] = cbor::Value(1);
  cbor_map[cbor::Value(kCoseECKeyXLabel)] = cbor::Value(base::make_span(
      credential_public_key.data() + offsetof(struct u2f_ec_point, x),
      U2F_EC_KEY_SIZE));
  cbor_map[cbor::Value(kCoseECKeyYLabel)] = cbor::Value(base::make_span(
      credential_public_key.data() + offsetof(struct u2f_ec_point, y),
      U2F_EC_KEY_SIZE));
  return *cbor::Writer::Write(cbor::Value(std::move(cbor_map)));
}

}  // namespace

WebAuthnHandler::WebAuthnHandler()
    : tpm_proxy_(nullptr), user_state_(nullptr) {}

void WebAuthnHandler::Initialize(dbus::Bus* bus,
                                 TpmVendorCommandProxy* tpm_proxy,
                                 UserState* user_state,
                                 std::function<void()> request_presence) {
  tpm_proxy_ = tpm_proxy;
  user_state_ = user_state;
  request_presence_ = request_presence;
  bus_ = bus;
  auth_dialog_dbus_proxy_ = bus_->GetObjectProxy(
      chromeos::kUserAuthenticationServiceName,
      dbus::ObjectPath(chromeos::kUserAuthenticationServicePath));
  DCHECK(auth_dialog_dbus_proxy_);
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

  if (request.verification_type() == VerificationType::VERIFICATION_UNKNOWN) {
    response.set_status(MakeCredentialResponse::VERIFICATION_FAILED);
    method_response->Return(response);
    return;
  }

  struct MakeCredentialSession session = {
      static_cast<uint64_t>(base::Time::Now().ToTimeT()), request,
      std::move(method_response)};

  if (request.verification_type() ==
      VerificationType::VERIFICATION_USER_VERIFICATION) {
    dbus::MethodCall call(
        chromeos::kUserAuthenticationServiceInterface,
        chromeos::kUserAuthenticationServiceShowAuthDialogMethod);
    dbus::MessageWriter writer(&call);
    writer.AppendString(request.rp_id());
    writer.AppendInt32(request.verification_type());
    writer.AppendUint64(request.request_id());

    auth_dialog_dbus_proxy_->CallMethod(
        &call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&WebAuthnHandler::HandleUVFlowResultMakeCredential,
                   base::Unretained(this), base::Passed(std::move(session))));
    return;
  }

  DoMakeCredential(std::move(session), PresenceRequirement::kPowerButton);
}

void WebAuthnHandler::HandleUVFlowResultMakeCredential(
    struct MakeCredentialSession session, dbus::Response* flow_response) {
  MakeCredentialResponse response;

  if (!flow_response) {
    LOG(ERROR) << "User auth flow had no response.";
    response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
    session.response_->Return(response);
    return;
  }

  dbus::MessageReader response_reader(flow_response);
  bool success;
  if (!response_reader.PopBool(&success)) {
    LOG(ERROR) << "Failed to parse user auth flow result.";
    response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
    session.response_->Return(response);
    return;
  }

  if (!success) {
    LOG(ERROR) << "User auth flow failed. Aborting MakeCredential.";
    response.set_status(MakeCredentialResponse::VERIFICATION_FAILED);
    session.response_->Return(response);
    return;
  }

  DoMakeCredential(std::move(session), PresenceRequirement::kNone);
}

void WebAuthnHandler::HandleUVFlowResultGetAssertion(
    struct GetAssertionSession session, dbus::Response* flow_response) {
  GetAssertionResponse response;

  if (!flow_response) {
    LOG(ERROR) << "User auth flow had no response.";
    response.set_status(GetAssertionResponse::INTERNAL_ERROR);
    session.response_->Return(response);
    return;
  }

  dbus::MessageReader response_reader(flow_response);
  bool success;
  if (!response_reader.PopBool(&success)) {
    LOG(ERROR) << "Failed to parse user auth flow result.";
    response.set_status(GetAssertionResponse::INTERNAL_ERROR);
    session.response_->Return(response);
    return;
  }

  if (!success) {
    LOG(ERROR) << "User auth flow failed. Aborting GetAssertion.";
    response.set_status(GetAssertionResponse::VERIFICATION_FAILED);
    session.response_->Return(response);
    return;
  }

  DoGetAssertion(std::move(session), PresenceRequirement::kAuthorizationSecret);
}

void WebAuthnHandler::DoMakeCredential(
    struct MakeCredentialSession session,
    PresenceRequirement presence_requirement) {
  MakeCredentialResponse response;
  const std::vector<uint8_t> rp_id_hash =
      util::Sha256(session.request_.rp_id());
  std::vector<uint8_t> credential_id;
  std::vector<uint8_t> credential_public_key;

  // TODO(yichengli): Make this a parameter of MakeCredential once we support
  // UP-only (non-consumer) credentials in WebAuthnHandler.
  // UV-compatible means the credential works with power button, fingerprint or
  // PIN.
  bool uv_compatible = true;

  MakeCredentialResponse::MakeCredentialStatus generate_status =
      DoU2fGenerate(rp_id_hash, presence_requirement, uv_compatible,
                    &credential_id, &credential_public_key);

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

  auto ret = HasExcludedCredentials(session.request_);
  if (ret == HasCredentialsResponse::INTERNAL_ERROR) {
    response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
    session.response_->Return(response);
  } else if (ret == HasCredentialsResponse::SUCCESS) {
    response.set_status(MakeCredentialResponse::EXCLUDED_CREDENTIAL_ID);
    session.response_->Return(response);
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
    bool uv_compatible,
    std::vector<uint8_t>* credential_id,
    std::vector<uint8_t>* credential_public_key) {
  DCHECK(rp_id_hash.size() == SHA256_DIGEST_LENGTH);
  base::Optional<brillo::SecureBlob> user_secret = user_state_->GetUserSecret();
  if (!user_secret.has_value()) {
    return MakeCredentialResponse::INTERNAL_ERROR;
  }

  struct u2f_generate_req generate_req = {};
  util::VectorToObject(rp_id_hash, generate_req.appId);
  util::VectorToObject(*user_secret, generate_req.userSecret);

  if (uv_compatible) {
    generate_req.flags |= U2F_UV_ENABLED_KH;
    struct u2f_generate_versioned_resp generate_resp = {};

    if (presence_requirement != PresenceRequirement::kPowerButton) {
      uint32_t generate_status =
          tpm_proxy_->SendU2fGenerate(generate_req, &generate_resp);
      if (generate_status != 0)
        return MakeCredentialResponse::INTERNAL_ERROR;

      util::AppendToVector(generate_resp.pubKey, credential_public_key);
      util::AppendToVector(generate_resp.keyHandle, credential_id);
      return MakeCredentialResponse::SUCCESS;
    }

    // Require user presence, consume.
    generate_req.flags |= U2F_AUTH_ENFORCE;
    return SendU2fGenerateWaitForPresence(&generate_req, &generate_resp,
                                          credential_id, credential_public_key);
  } else {
    // Non-versioned KH must be signed with power button press.
    if (presence_requirement != PresenceRequirement::kPowerButton)
      return MakeCredentialResponse::INTERNAL_ERROR;
    // Require user presence, consume.
    generate_req.flags |= U2F_AUTH_ENFORCE;
    struct u2f_generate_resp generate_resp = {};
    return SendU2fGenerateWaitForPresence(&generate_req, &generate_resp,
                                          credential_id, credential_public_key);
  }
}

template <typename Response>
MakeCredentialResponse::MakeCredentialStatus
WebAuthnHandler::SendU2fGenerateWaitForPresence(
    struct u2f_generate_req* generate_req,
    Response* generate_resp,
    std::vector<uint8_t>* credential_id,
    std::vector<uint8_t>* credential_public_key) {
  uint32_t generate_status = -1;
  base::AutoLock(tpm_proxy_->GetLock());
  CallAndWaitForPresence(
      [this, generate_req, generate_resp]() {
        return tpm_proxy_->SendU2fGenerate(*generate_req, generate_resp);
      },
      &generate_status);
  brillo::SecureMemset(&generate_req->userSecret, 0,
                       sizeof(generate_req->userSecret));

  if (generate_status == 0) {
    util::AppendToVector(generate_resp->pubKey, credential_public_key);
    util::AppendToVector(generate_resp->keyHandle, credential_id);
    return MakeCredentialResponse::SUCCESS;
  }

  return MakeCredentialResponse::VERIFICATION_FAILED;
}

HasCredentialsResponse::HasCredentialsStatus
WebAuthnHandler::HasExcludedCredentials(const MakeCredentialRequest& request) {
  std::vector<uint8_t> rp_id_hash = util::Sha256(request.rp_id());
  for (auto credential : request.excluded_credential_id()) {
    auto ret = DoU2fSignCheckOnly(rp_id_hash, util::ToVector(credential));
    if (ret == HasCredentialsResponse::SUCCESS)
      return ret;
    if (ret == HasCredentialsResponse::INTERNAL_ERROR)
      return ret;
  }
  return HasCredentialsResponse::UNKNOWN_CREDENTIAL_ID;
}

void WebAuthnHandler::GetAssertion(
    std::unique_ptr<GetAssertionMethodResponse> method_response,
    const GetAssertionRequest& request) {
  GetAssertionResponse response;

  if (!Initialized()) {
    response.set_status(GetAssertionResponse::INTERNAL_ERROR);
    method_response->Return(response);
    return;
  }

  if (request.rp_id().empty() ||
      request.client_data_hash().size() != SHA256_DIGEST_LENGTH) {
    response.set_status(GetAssertionResponse::INVALID_REQUEST);
    method_response->Return(response);
    return;
  }

  if (request.verification_type() == VerificationType::VERIFICATION_UNKNOWN) {
    response.set_status(GetAssertionResponse::VERIFICATION_FAILED);
    method_response->Return(response);
    return;
  }

  // TODO(louiscollard): Support resident credentials.

  const std::vector<uint8_t> rp_id_hash = util::Sha256(request.rp_id());
  int matched_index = -1;

  for (int index = 0; index < request.allowed_credential_id_size(); index++) {
    const HasCredentialsResponse::HasCredentialsStatus ret = DoU2fSignCheckOnly(
        rp_id_hash, util::ToVector(request.allowed_credential_id(index)));

    if (ret == HasCredentialsResponse::INTERNAL_ERROR) {
      // If there's an internal error then the remaining credentials won't
      // succeed.
      response.set_status(GetAssertionResponse::INTERNAL_ERROR);
      method_response->Return(response);
      return;
    }
    if (ret != HasCredentialsResponse::UNKNOWN_CREDENTIAL_ID) {
      matched_index = index;
      break;
    }
  }

  if (matched_index == -1) {
    // No credential_id matched.
    response.set_status(GetAssertionResponse::UNKNOWN_CREDENTIAL_ID);
    method_response->Return(response);
    return;
  }

  struct GetAssertionSession session = {
      static_cast<uint64_t>(base::Time::Now().ToTimeT()), request,
      request.allowed_credential_id(matched_index), std::move(method_response)};

  if (request.verification_type() ==
      VerificationType::VERIFICATION_USER_VERIFICATION) {
    dbus::MethodCall call(
        chromeos::kUserAuthenticationServiceInterface,
        chromeos::kUserAuthenticationServiceShowAuthDialogMethod);
    dbus::MessageWriter writer(&call);
    writer.AppendString(request.rp_id());
    writer.AppendInt32(request.verification_type());
    writer.AppendUint64(request.request_id());

    auth_dialog_dbus_proxy_->CallMethod(
        &call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&WebAuthnHandler::HandleUVFlowResultGetAssertion,
                   base::Unretained(this), base::Passed(std::move(session))));
    return;
  }

  DoGetAssertion(std::move(session), PresenceRequirement::kPowerButton);
}

// If already seeing failure, then no need to get user secret. This means
// in the fingerprint case, this signal should ideally come from UI instead of
// biod because only UI knows about retry.
void WebAuthnHandler::DoGetAssertion(struct GetAssertionSession session,
                                     PresenceRequirement presence_requirement) {
  GetAssertionResponse response;
  const std::vector<uint8_t> rp_id_hash =
      util::Sha256(session.request_.rp_id());
  std::vector<uint8_t> authenticator_data = MakeAuthenticatorData(
      rp_id_hash, std::vector<uint8_t>(), std::vector<uint8_t>(),
      session.request_.verification_type() ==
          VerificationType::VERIFICATION_USER_VERIFICATION,
      false);
  std::vector<uint8_t> data_to_sign(authenticator_data);
  util::AppendToVector(session.request_.client_data_hash(), &data_to_sign);
  std::vector<uint8_t> hash_to_sign = util::Sha256(data_to_sign);

  std::vector<uint8_t> signature;
  GetAssertionResponse::GetAssertionStatus sign_status =
      DoU2fSign(rp_id_hash, hash_to_sign, util::ToVector(session.credential_id),
                presence_requirement, &signature);
  response.set_status(sign_status);
  if (sign_status == GetAssertionResponse::SUCCESS) {
    auto* assertion = response.add_assertion();
    assertion->set_credential_id(
        session.request_.allowed_credential_id().Get(0));
    AppendToString(authenticator_data, assertion->mutable_authenticator_data());
    AppendToString(signature, assertion->mutable_signature());
  }

  session.response_->Return(response);
}

GetAssertionResponse::GetAssertionStatus WebAuthnHandler::DoU2fSign(
    const std::vector<uint8_t>& rp_id_hash,
    const std::vector<uint8_t>& hash_to_sign,
    const std::vector<uint8_t>& credential_id,
    PresenceRequirement presence_requirement,
    std::vector<uint8_t>* signature) {
  DCHECK(rp_id_hash.size() == SHA256_DIGEST_LENGTH);
  base::Optional<brillo::SecureBlob> user_secret = user_state_->GetUserSecret();
  if (!user_secret.has_value()) {
    return GetAssertionResponse::INTERNAL_ERROR;
  }

  if (credential_id.size() == sizeof(u2f_versioned_key_handle)) {
    // Allow waiving presence if sign_req.authTimeSecret is correct.
    struct u2f_sign_versioned_req sign_req = {};
    util::VectorToObject(rp_id_hash, sign_req.appId);
    util::VectorToObject(*user_secret, sign_req.userSecret);
    util::VectorToObject(credential_id, &sign_req.keyHandle);
    util::VectorToObject(hash_to_sign, sign_req.hash);
    struct u2f_sign_resp sign_resp = {};

    if (presence_requirement != PresenceRequirement::kPowerButton) {
      uint32_t sign_status = tpm_proxy_->SendU2fSign(sign_req, &sign_resp);
      if (sign_status != 0)
        return GetAssertionResponse::INTERNAL_ERROR;

      base::Optional<std::vector<uint8_t>> opt_signature =
          util::SignatureToDerBytes(sign_resp.sig_r, sign_resp.sig_s);
      if (!opt_signature.has_value()) {
        return GetAssertionResponse::INTERNAL_ERROR;
      }
      *signature = *opt_signature;
      return GetAssertionResponse::SUCCESS;
    }

    // Require user presence, consume.
    sign_req.flags |= U2F_AUTH_ENFORCE;
    return SendU2fSignWaitForPresence(&sign_req, &sign_resp, signature);
  } else {
    // Non-versioned KH must be signed with power button press.
    if (presence_requirement != PresenceRequirement::kPowerButton)
      return GetAssertionResponse::INTERNAL_ERROR;

    struct u2f_sign_req sign_req = {
        .flags = U2F_AUTH_ENFORCE  // Require user presence, consume.
    };
    util::VectorToObject(rp_id_hash, sign_req.appId);
    util::VectorToObject(*user_secret, sign_req.userSecret);
    util::VectorToObject(credential_id, &sign_req.keyHandle);
    util::VectorToObject(hash_to_sign, sign_req.hash);

    struct u2f_sign_resp sign_resp = {};
    return SendU2fSignWaitForPresence(&sign_req, &sign_resp, signature);
  }
}

template <typename Request>
GetAssertionResponse::GetAssertionStatus
WebAuthnHandler::SendU2fSignWaitForPresence(Request* sign_req,
                                            struct u2f_sign_resp* sign_resp,
                                            std::vector<uint8_t>* signature) {
  uint32_t sign_status = -1;
  base::AutoLock(tpm_proxy_->GetLock());
  CallAndWaitForPresence(
      [this, sign_req, sign_resp]() {
        return tpm_proxy_->SendU2fSign(*sign_req, sign_resp);
      },
      &sign_status);
  brillo::SecureMemset(&sign_req->userSecret, 0, sizeof(sign_req->userSecret));

  if (sign_status == 0) {
    base::Optional<std::vector<uint8_t>> opt_signature =
        util::SignatureToDerBytes(sign_resp->sig_r, sign_resp->sig_s);
    if (!opt_signature.has_value()) {
      return GetAssertionResponse::INTERNAL_ERROR;
    }
    *signature = *opt_signature;
    return GetAssertionResponse::SUCCESS;
  }

  return GetAssertionResponse::VERIFICATION_FAILED;
}

HasCredentialsResponse WebAuthnHandler::HasCredentials(
    const HasCredentialsRequest& request) {
  HasCredentialsResponse response;

  if (!Initialized()) {
    response.set_status(HasCredentialsResponse::INTERNAL_ERROR);
    return response;
  }

  if (request.rp_id().empty() || request.credential_id().empty()) {
    response.set_status(HasCredentialsResponse::INVALID_REQUEST);
    return response;
  }

  std::vector<uint8_t> rp_id_hash = util::Sha256(request.rp_id());
  for (const auto& credential_id : request.credential_id()) {
    auto ret = DoU2fSignCheckOnly(rp_id_hash, util::ToVector(credential_id));
    if (ret == HasCredentialsResponse::INTERNAL_ERROR) {
      response.set_status(ret);
      return response;
    } else if (ret == HasCredentialsResponse::SUCCESS) {
      *response.add_credential_id() = credential_id;
    }
  }

  response.set_status((response.credential_id_size() > 0)
                          ? HasCredentialsResponse::SUCCESS
                          : HasCredentialsResponse::UNKNOWN_CREDENTIAL_ID);
  return response;
}

HasCredentialsResponse::HasCredentialsStatus
WebAuthnHandler::DoU2fSignCheckOnly(const std::vector<uint8_t>& rp_id_hash,
                                    const std::vector<uint8_t>& credential_id) {
  base::Optional<brillo::SecureBlob> user_secret = user_state_->GetUserSecret();
  if (!user_secret.has_value()) {
    return HasCredentialsResponse::INTERNAL_ERROR;
  }

  uint32_t sign_status;

  if (credential_id.size() == sizeof(u2f_versioned_key_handle)) {
    struct u2f_sign_versioned_req sign_req = {.flags = U2F_AUTH_CHECK_ONLY};
    util::VectorToObject(rp_id_hash, sign_req.appId);
    util::VectorToObject(*user_secret, sign_req.userSecret);
    util::VectorToObject(credential_id, &sign_req.keyHandle);

    struct u2f_sign_resp sign_resp;
    base::AutoLock(tpm_proxy_->GetLock());
    sign_status = tpm_proxy_->SendU2fSign(sign_req, &sign_resp);
    brillo::SecureMemset(&sign_req.userSecret, 0, sizeof(sign_req.userSecret));
  } else {
    struct u2f_sign_req sign_req = {.flags = U2F_AUTH_CHECK_ONLY};
    util::VectorToObject(rp_id_hash, sign_req.appId);
    util::VectorToObject(*user_secret, sign_req.userSecret);
    util::VectorToObject(credential_id, &sign_req.keyHandle);

    struct u2f_sign_resp sign_resp;
    base::AutoLock(tpm_proxy_->GetLock());
    sign_status = tpm_proxy_->SendU2fSign(sign_req, &sign_resp);
    brillo::SecureMemset(&sign_req.userSecret, 0, sizeof(sign_req.userSecret));
  }

  // Return status of 0 indicates the credential is valid.
  return (sign_status == 0) ? HasCredentialsResponse::SUCCESS
                            : HasCredentialsResponse::UNKNOWN_CREDENTIAL_ID;
}

}  // namespace u2f
