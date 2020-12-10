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
#include <openssl/rand.h>
#include <u2f/proto_bindings/u2f_interface.pb.h>

#include "u2fd/util.h"

namespace u2f {

namespace {

// User a big timeout for cryptohome. See b/172945202.
constexpr base::TimeDelta kCryptohomeTimeout = base::TimeDelta::FromMinutes(2);
constexpr int kVerificationTimeoutMs = 10000;
constexpr int kVerificationRetryDelayUs = 500 * 1000;
constexpr int kCancelUVFlowTimeoutMs = 5000;

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

// Key label in cryptohome.
constexpr char kCryptohomePinLabel[] = "pin";

// Relative DBus object path for fingerprint manager in biod.
const char kCrosFpBiometricsManagerRelativePath[] = "/CrosFpBiometricsManager";

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
    : tpm_proxy_(nullptr),
      user_state_(nullptr),
      webauthn_storage_(std::make_unique<WebAuthnStorage>()) {}

void WebAuthnHandler::Initialize(dbus::Bus* bus,
                                 TpmVendorCommandProxy* tpm_proxy,
                                 UserState* user_state,
                                 std::function<void()> request_presence) {
  if (Initialized()) {
    LOG(INFO) << "WebAuthn handler already initialized, doing nothing.";
    return;
  }

  tpm_proxy_ = tpm_proxy;
  user_state_ = user_state;
  user_state_->SetSessionStartedCallback(
      base::Bind(&WebAuthnHandler::OnSessionStarted, base::Unretained(this)));
  user_state_->SetSessionStoppedCallback(
      base::Bind(&WebAuthnHandler::OnSessionStopped, base::Unretained(this)));
  request_presence_ = request_presence;
  bus_ = bus;
  auth_dialog_dbus_proxy_ = bus_->GetObjectProxy(
      chromeos::kUserAuthenticationServiceName,
      dbus::ObjectPath(chromeos::kUserAuthenticationServicePath));
  // Testing can inject a mock.
  if (!cryptohome_proxy_)
    cryptohome_proxy_ =
        std::make_unique<org::chromium::CryptohomeInterfaceProxy>(bus_);
  DCHECK(auth_dialog_dbus_proxy_);

  if (user_state_->HasUser()) {
    // WebAuthnHandler should normally initialize on boot, before any user has
    // logged in. If there's already a user, then we have crashed during a user
    // session, so catch up on the state.
    base::Optional<std::string> user = user_state_->GetUser();
    DCHECK(user);
    OnSessionStarted(*user);
  }
}

bool WebAuthnHandler::Initialized() {
  return tpm_proxy_ != nullptr && user_state_ != nullptr;
}

void WebAuthnHandler::OnSessionStarted(const std::string& account_id) {
  // Do this first because there's a timeout for reading the secret.
  const bool received_secret_from_cryptohome = GetWebAuthnSecret(account_id);

  webauthn_storage_->set_allow_access(true);
  base::Optional<std::string> sanitized_user = user_state_->GetSanitizedUser();
  DCHECK(sanitized_user);
  webauthn_storage_->set_sanitized_user(*sanitized_user);

  if (!webauthn_storage_->LoadRecords()) {
    LOG(ERROR) << "Did not load all records for user " << *sanitized_user;
    return;
  }

  if (received_secret_from_cryptohome) {
    // Persist to daemon-store in case we crash during a user session.
    webauthn_storage_->PersistAuthTimeSecretHash(*auth_time_secret_hash_);
  } else {
    // If this is login, we should have received the secret from cryptohomed.
    // Since we did not, either we crashed during a user session (in which case
    // cryptohomed would not have the secret when we restarted), or there's an
    // internal error in cryptohome. Either way, read the backup secret hash.
    auth_time_secret_hash_ = webauthn_storage_->LoadAuthTimeSecretHash();
  }
}

void WebAuthnHandler::OnSessionStopped() {
  auth_time_secret_hash_.reset();
  webauthn_storage_->Reset();
}

bool WebAuthnHandler::GetWebAuthnSecret(const std::string& account_id) {
  cryptohome::AccountIdentifier id;
  id.set_account_id(account_id);
  cryptohome::GetWebAuthnSecretRequest req;
  cryptohome::BaseReply reply;
  brillo::ErrorPtr error;

  if (!cryptohome_proxy_->GetWebAuthnSecret(
          id, req, &reply, &error, kCryptohomeTimeout.InMilliseconds())) {
    LOG(ERROR) << "Failed to call GetWebAuthnSecret on cryptohome, error: "
               << error->GetMessage();
    return false;
  }

  if (reply.has_error()) {
    LOG(ERROR) << "GetWebAuthnSecret reply has error " << reply.error();
    return false;
  }

  if (!reply.HasExtension(cryptohome::GetWebAuthnSecretReply::reply)) {
    LOG(ERROR) << "GetWebAuthnSecret reply doesn't have the correct extension.";
    return false;
  }

  brillo::SecureBlob secret(
      reply.GetExtension(cryptohome::GetWebAuthnSecretReply::reply)
          .webauthn_secret());
  if (secret.size() != SHA256_DIGEST_LENGTH) {
    LOG(ERROR) << "WebAuthn auth time secret size is wrong.";
    return false;
  }

  auth_time_secret_hash_ = std::make_unique<brillo::Blob>(util::Sha256(secret));
  return true;
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

  if (pending_uv_make_credential_session_ ||
      pending_uv_get_assertion_session_) {
    response.set_status(MakeCredentialResponse::REQUEST_PENDING);
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

  // Upgrade UP requests to UV.
  session.request.set_verification_type(
      VerificationType::VERIFICATION_USER_VERIFICATION);

  dbus::MethodCall call(
      chromeos::kUserAuthenticationServiceInterface,
      chromeos::kUserAuthenticationServiceShowAuthDialogMethod);
  dbus::MessageWriter writer(&call);
  writer.AppendString(request.rp_id());
  writer.AppendInt32(request.verification_type());
  writer.AppendUint64(request.request_id());

  pending_uv_make_credential_session_ = std::move(session);
  auth_dialog_dbus_proxy_->CallMethod(
      &call, dbus::ObjectProxy::TIMEOUT_INFINITE,
      base::Bind(&WebAuthnHandler::HandleUVFlowResultMakeCredential,
                 base::Unretained(this)));
}

CancelWebAuthnFlowResponse WebAuthnHandler::Cancel(
    const CancelWebAuthnFlowRequest& request) {
  CancelWebAuthnFlowResponse response;
  if (!pending_uv_make_credential_session_ &&
      !pending_uv_get_assertion_session_) {
    LOG(ERROR) << "No pending session to cancel.";
    response.set_canceled(false);
    return response;
  }

  if (pending_uv_make_credential_session_ &&
      pending_uv_make_credential_session_->request.request_id() !=
          request.request_id()) {
    LOG(ERROR)
        << "MakeCredential session has a different request_id, not cancelling.";
    response.set_canceled(false);
    return response;
  }

  if (pending_uv_get_assertion_session_ &&
      pending_uv_get_assertion_session_->request.request_id() !=
          request.request_id()) {
    LOG(ERROR)
        << "GetAssertion session has a different request_id, not cancelling.";
    response.set_canceled(false);
    return response;
  }

  dbus::MethodCall call(chromeos::kUserAuthenticationServiceInterface,
                        chromeos::kUserAuthenticationServiceCancelMethod);
  std::unique_ptr<dbus::Response> cancel_ui_resp =
      auth_dialog_dbus_proxy_->CallMethodAndBlock(&call,
                                                  kCancelUVFlowTimeoutMs);

  if (!cancel_ui_resp) {
    LOG(ERROR) << "Failed to dismiss WebAuthn user verification UI.";
    response.set_canceled(false);
    return response;
  }

  // We do not reset |pending_uv_make_credential_session_| or
  // |pending_uv_get_assertion_session_| here because UI will still respond
  // to the cancelled request through these, though the response will be
  // ignored by Chrome.
  if (pending_uv_make_credential_session_) {
    pending_uv_make_credential_session_->canceled = true;
  } else {
    pending_uv_get_assertion_session_->canceled = true;
  }
  response.set_canceled(true);
  return response;
}

void WebAuthnHandler::HandleUVFlowResultMakeCredential(
    dbus::Response* flow_response) {
  MakeCredentialResponse response;

  DCHECK(pending_uv_make_credential_session_);

  if (!flow_response) {
    LOG(ERROR) << "User auth flow had no response.";
    response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
    pending_uv_make_credential_session_->response->Return(response);
    pending_uv_make_credential_session_.reset();
    return;
  }

  dbus::MessageReader response_reader(flow_response);
  bool success;
  if (!response_reader.PopBool(&success)) {
    LOG(ERROR) << "Failed to parse user auth flow result.";
    response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
    pending_uv_make_credential_session_->response->Return(response);
    pending_uv_make_credential_session_.reset();
    return;
  }

  if (!success) {
    if (pending_uv_make_credential_session_->canceled) {
      LOG(INFO) << "WebAuthn MakeCredential operation canceled.";
      response.set_status(MakeCredentialResponse::CANCELED);
    } else {
      LOG(ERROR) << "User auth flow failed. Aborting MakeCredential.";
      response.set_status(MakeCredentialResponse::VERIFICATION_FAILED);
    }
    pending_uv_make_credential_session_->response->Return(response);
    pending_uv_make_credential_session_.reset();
    return;
  }

  DoMakeCredential(std::move(*pending_uv_make_credential_session_),
                   PresenceRequirement::kNone);
  pending_uv_make_credential_session_.reset();
}

void WebAuthnHandler::HandleUVFlowResultGetAssertion(
    dbus::Response* flow_response) {
  GetAssertionResponse response;

  DCHECK(pending_uv_get_assertion_session_);

  if (!flow_response) {
    LOG(ERROR) << "User auth flow had no response.";
    response.set_status(GetAssertionResponse::INTERNAL_ERROR);
    pending_uv_get_assertion_session_->response->Return(response);
    pending_uv_get_assertion_session_.reset();
    return;
  }

  dbus::MessageReader response_reader(flow_response);
  bool success;
  if (!response_reader.PopBool(&success)) {
    LOG(ERROR) << "Failed to parse user auth flow result.";
    response.set_status(GetAssertionResponse::INTERNAL_ERROR);
    pending_uv_get_assertion_session_->response->Return(response);
    pending_uv_get_assertion_session_.reset();
    return;
  }

  if (!success) {
    if (pending_uv_get_assertion_session_->canceled) {
      LOG(INFO) << "WebAuthn GetAssertion operation canceled.";
      response.set_status(GetAssertionResponse::CANCELED);
    } else {
      LOG(ERROR) << "User auth flow failed. Aborting GetAssertion.";
      response.set_status(GetAssertionResponse::VERIFICATION_FAILED);
    }
    pending_uv_get_assertion_session_->response->Return(response);
    pending_uv_get_assertion_session_.reset();
    return;
  }

  DoGetAssertion(std::move(*pending_uv_get_assertion_session_),
                 PresenceRequirement::kAuthorizationSecret);
  pending_uv_get_assertion_session_.reset();
}

void WebAuthnHandler::DoMakeCredential(
    struct MakeCredentialSession session,
    PresenceRequirement presence_requirement) {
  MakeCredentialResponse response;
  const std::vector<uint8_t> rp_id_hash = util::Sha256(session.request.rp_id());
  std::vector<uint8_t> credential_id;
  std::vector<uint8_t> credential_public_key;

  // TODO(yichengli): Make this a parameter of MakeCredential once we support
  // UP-only (non-consumer) credentials in WebAuthnHandler.
  // UV-compatible means the credential works with power button, fingerprint or
  // PIN.
  bool uv_compatible = true;

  brillo::Blob credential_secret(kCredentialSecretSize);
  if (RAND_bytes(&credential_secret.front(), credential_secret.size()) != 1) {
    LOG(ERROR) << "Failed to generate secret for new credential.";
    response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
    session.response->Return(response);
    return;
  }

  MakeCredentialResponse::MakeCredentialStatus generate_status =
      DoU2fGenerate(rp_id_hash, credential_secret, presence_requirement,
                    uv_compatible, &credential_id, &credential_public_key);

  if (generate_status != MakeCredentialResponse::SUCCESS) {
    response.set_status(generate_status);
    session.response->Return(response);
    return;
  }

  if (credential_id.empty() || credential_public_key.empty()) {
    response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
    session.response->Return(response);
    return;
  }

  if (uv_compatible)
    InsertAuthTimeSecretHashToCredentialId(&credential_id);

  auto ret = HasExcludedCredentials(session.request);
  if (ret == HasCredentialsResponse::INTERNAL_ERROR) {
    response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
    session.response->Return(response);
    return;
  } else if (ret == HasCredentialsResponse::SUCCESS) {
    response.set_status(MakeCredentialResponse::EXCLUDED_CREDENTIAL_ID);
    session.response->Return(response);
    return;
  }

  WebAuthnRecord record;
  AppendToString(credential_id, &record.credential_id);
  record.secret = std::move(credential_secret);
  record.rp_id = session.request.rp_id();
  record.user_id = session.request.user_id();
  record.user_display_name = session.request.user_display_name();
  record.timestamp = base::Time::Now().ToDoubleT();
  if (!webauthn_storage_->WriteRecord(std::move(record))) {
    response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
    session.response->Return(response);
    return;
  }

  AppendToString(MakeAuthenticatorData(
                     rp_id_hash, credential_id,
                     EncodeCredentialPublicKeyInCBOR(credential_public_key),
                     session.request.verification_type() ==
                         VerificationType::VERIFICATION_USER_VERIFICATION,
                     true),
                 response.mutable_authenticator_data());
  AppendNoneAttestation(&response);

  response.set_status(MakeCredentialResponse::SUCCESS);
  session.response->Return(response);
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
    const std::vector<uint8_t>& credential_secret,
    PresenceRequirement presence_requirement,
    bool uv_compatible,
    std::vector<uint8_t>* credential_id,
    std::vector<uint8_t>* credential_public_key) {
  DCHECK(rp_id_hash.size() == SHA256_DIGEST_LENGTH);

  struct u2f_generate_req generate_req = {};
  if (!util::VectorToObject(rp_id_hash, generate_req.appId,
                            sizeof(generate_req.appId))) {
    return MakeCredentialResponse::INVALID_REQUEST;
  }
  if (!util::VectorToObject(credential_secret, generate_req.userSecret,
                            sizeof(generate_req.userSecret))) {
    return MakeCredentialResponse::INVALID_REQUEST;
  }

  if (uv_compatible) {
    if (!auth_time_secret_hash_) {
      LOG(ERROR) << "No auth-time secret hash to use for u2f_generate.";
      return MakeCredentialResponse::INTERNAL_ERROR;
    }
    generate_req.flags |= U2F_UV_ENABLED_KH;
    memcpy(generate_req.authTimeSecretHash, auth_time_secret_hash_->data(),
           auth_time_secret_hash_->size());
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
  brillo::SecureClear(&generate_req->userSecret,
                      sizeof(generate_req->userSecret));

  if (generate_status == 0) {
    util::AppendToVector(generate_resp->pubKey, credential_public_key);
    util::AppendToVector(generate_resp->keyHandle, credential_id);
    return MakeCredentialResponse::SUCCESS;
  }

  return MakeCredentialResponse::VERIFICATION_FAILED;
}

// TODO(b/172971998): Remove this workaround once cr50 handles this.
void WebAuthnHandler::InsertAuthTimeSecretHashToCredentialId(
    std::vector<uint8_t>* input) {
  CHECK(input->size() == sizeof(u2f_versioned_key_handle));
  // The auth time secret hash should be inserted right after the header and
  // the authorization salt, before the authorization hmac.
  input->insert(
      input->cbegin() + offsetof(u2f_versioned_key_handle, authorization_hmac),
      auth_time_secret_hash_->cbegin(), auth_time_secret_hash_->cend());
}

// TODO(b/172971998): Remove this workaround once cr50 handles this.
void WebAuthnHandler::RemoveAuthTimeSecretHashFromCredentialId(
    std::vector<uint8_t>* input) {
  CHECK_EQ(input->size(),
           sizeof(u2f_versioned_key_handle) + SHA256_DIGEST_LENGTH);
  // The auth time secret hash is after the header and the authorization salt,
  // before the authorization hmac. Remove it so that cr50 recognizes the KH.
  const std::vector<uint8_t>::const_iterator remove_begin =
      input->cbegin() + offsetof(u2f_versioned_key_handle, authorization_hmac);
  input->erase(remove_begin, remove_begin + SHA256_DIGEST_LENGTH);
}

HasCredentialsResponse::HasCredentialsStatus
WebAuthnHandler::HasExcludedCredentials(const MakeCredentialRequest& request) {
  std::vector<uint8_t> rp_id_hash = util::Sha256(request.rp_id());
  for (auto credential : request.excluded_credential_id()) {
    base::Optional<std::vector<uint8_t>> credential_secret =
        webauthn_storage_->GetSecretByCredentialId(credential);
    if (!credential_secret)
      continue;

    auto ret = DoU2fSignCheckOnly(rp_id_hash, util::ToVector(credential),
                                  *credential_secret);
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

  if (pending_uv_make_credential_session_ ||
      pending_uv_get_assertion_session_) {
    response.set_status(GetAssertionResponse::REQUEST_PENDING);
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
    base::Optional<std::vector<uint8_t>> credential_secret =
        webauthn_storage_->GetSecretByCredentialId(
            request.allowed_credential_id(index));
    if (!credential_secret)
      continue;

    const HasCredentialsResponse::HasCredentialsStatus ret = DoU2fSignCheckOnly(
        rp_id_hash, util::ToVector(request.allowed_credential_id(index)),
        *credential_secret);

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

  // Upgrade UP requests to UV.
  session.request.set_verification_type(
      VerificationType::VERIFICATION_USER_VERIFICATION);

  dbus::MethodCall call(
      chromeos::kUserAuthenticationServiceInterface,
      chromeos::kUserAuthenticationServiceShowAuthDialogMethod);
  dbus::MessageWriter writer(&call);
  writer.AppendString(request.rp_id());
  writer.AppendInt32(request.verification_type());
  writer.AppendUint64(request.request_id());

  pending_uv_get_assertion_session_ = std::move(session);
  auth_dialog_dbus_proxy_->CallMethod(
      &call, dbus::ObjectProxy::TIMEOUT_INFINITE,
      base::Bind(&WebAuthnHandler::HandleUVFlowResultGetAssertion,
                 base::Unretained(this)));
}

// If already seeing failure, then no need to get user secret. This means
// in the fingerprint case, this signal should ideally come from UI instead of
// biod because only UI knows about retry.
void WebAuthnHandler::DoGetAssertion(struct GetAssertionSession session,
                                     PresenceRequirement presence_requirement) {
  GetAssertionResponse response;
  const std::vector<uint8_t> rp_id_hash = util::Sha256(session.request.rp_id());
  std::vector<uint8_t> authenticator_data = MakeAuthenticatorData(
      rp_id_hash, std::vector<uint8_t>(), std::vector<uint8_t>(),
      session.request.verification_type() ==
          VerificationType::VERIFICATION_USER_VERIFICATION,
      false);
  std::vector<uint8_t> data_to_sign(authenticator_data);
  util::AppendToVector(session.request.client_data_hash(), &data_to_sign);
  std::vector<uint8_t> hash_to_sign = util::Sha256(data_to_sign);

  base::Optional<std::vector<uint8_t>> credential_secret =
      webauthn_storage_->GetSecretByCredentialId(session.credential_id);
  if (!credential_secret) {
    LOG(ERROR) << "No credential secret for credential id "
               << session.credential_id << ", aborting GetAssertion.";
    response.set_status(GetAssertionResponse::UNKNOWN_CREDENTIAL_ID);
    session.response->Return(response);
  }
  std::vector<uint8_t> signature;
  GetAssertionResponse::GetAssertionStatus sign_status =
      DoU2fSign(rp_id_hash, hash_to_sign, util::ToVector(session.credential_id),
                *credential_secret, presence_requirement, &signature);
  response.set_status(sign_status);
  if (sign_status == GetAssertionResponse::SUCCESS) {
    auto* assertion = response.add_assertion();
    assertion->set_credential_id(
        session.request.allowed_credential_id().Get(0));
    AppendToString(authenticator_data, assertion->mutable_authenticator_data());
    AppendToString(signature, assertion->mutable_signature());
  }

  session.response->Return(response);
}

GetAssertionResponse::GetAssertionStatus WebAuthnHandler::DoU2fSign(
    const std::vector<uint8_t>& rp_id_hash,
    const std::vector<uint8_t>& hash_to_sign,
    const std::vector<uint8_t>& credential_id,
    const std::vector<uint8_t>& credential_secret,
    PresenceRequirement presence_requirement,
    std::vector<uint8_t>* signature) {
  DCHECK(rp_id_hash.size() == SHA256_DIGEST_LENGTH);

  if (credential_id.size() ==
      sizeof(u2f_versioned_key_handle) + SHA256_DIGEST_SIZE) {
    // Allow waiving presence if sign_req.authTimeSecret is correct.
    struct u2f_sign_versioned_req sign_req = {};
    if (!util::VectorToObject(rp_id_hash, sign_req.appId,
                              sizeof(sign_req.appId))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(credential_secret, sign_req.userSecret,
                              sizeof(sign_req.userSecret))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    std::vector<uint8_t> key_handle(credential_id);
    RemoveAuthTimeSecretHashFromCredentialId(&key_handle);
    if (!util::VectorToObject(key_handle, &sign_req.keyHandle,
                              sizeof(sign_req.keyHandle))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(hash_to_sign, sign_req.hash,
                              sizeof(sign_req.hash))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
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
  } else if (credential_id.size() == sizeof(u2f_key_handle)) {
    // Non-versioned KH must be signed with power button press.
    if (presence_requirement != PresenceRequirement::kPowerButton)
      return GetAssertionResponse::INTERNAL_ERROR;

    struct u2f_sign_req sign_req = {
        .flags = U2F_AUTH_ENFORCE  // Require user presence, consume.
    };
    if (!util::VectorToObject(rp_id_hash, sign_req.appId,
                              sizeof(sign_req.appId))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(credential_secret, sign_req.userSecret,
                              sizeof(sign_req.userSecret))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(credential_id, &sign_req.keyHandle,
                              sizeof(sign_req.keyHandle))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(hash_to_sign, sign_req.hash,
                              sizeof(sign_req.hash))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }

    struct u2f_sign_resp sign_resp = {};
    return SendU2fSignWaitForPresence(&sign_req, &sign_resp, signature);
  } else {
    return GetAssertionResponse::INVALID_REQUEST;
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
  brillo::SecureClear(&sign_req->userSecret, sizeof(sign_req->userSecret));

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
    base::Optional<std::vector<uint8_t>> credential_secret =
        webauthn_storage_->GetSecretByCredentialId(credential_id);
    if (!credential_secret)
      continue;

    auto ret = DoU2fSignCheckOnly(rp_id_hash, util::ToVector(credential_id),
                                  *credential_secret);
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
WebAuthnHandler::DoU2fSignCheckOnly(
    const std::vector<uint8_t>& rp_id_hash,
    const std::vector<uint8_t>& credential_id,
    const std::vector<uint8_t>& credential_secret) {
  uint32_t sign_status;

  if (credential_id.size() ==
      sizeof(u2f_versioned_key_handle) + SHA256_DIGEST_SIZE) {
    struct u2f_sign_versioned_req sign_req = {.flags = U2F_AUTH_CHECK_ONLY};
    if (!util::VectorToObject(rp_id_hash, sign_req.appId,
                              sizeof(sign_req.appId))) {
      return HasCredentialsResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(credential_secret, sign_req.userSecret,
                              sizeof(sign_req.userSecret))) {
      return HasCredentialsResponse::INVALID_REQUEST;
    }
    std::vector<uint8_t> key_handle(credential_id);
    RemoveAuthTimeSecretHashFromCredentialId(&key_handle);
    if (!util::VectorToObject(key_handle, &sign_req.keyHandle,
                              sizeof(sign_req.keyHandle))) {
      return HasCredentialsResponse::INVALID_REQUEST;
    }

    struct u2f_sign_resp sign_resp;
    base::AutoLock(tpm_proxy_->GetLock());
    sign_status = tpm_proxy_->SendU2fSign(sign_req, &sign_resp);
    brillo::SecureClear(&sign_req.userSecret, sizeof(sign_req.userSecret));
  } else if (credential_id.size() == sizeof(u2f_key_handle)) {
    struct u2f_sign_req sign_req = {.flags = U2F_AUTH_CHECK_ONLY};
    if (!util::VectorToObject(rp_id_hash, sign_req.appId,
                              sizeof(sign_req.appId))) {
      return HasCredentialsResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(credential_secret, sign_req.userSecret,
                              sizeof(sign_req.userSecret))) {
      return HasCredentialsResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(credential_id, &sign_req.keyHandle,
                              sizeof(sign_req.keyHandle))) {
      return HasCredentialsResponse::INVALID_REQUEST;
    }

    struct u2f_sign_resp sign_resp;
    base::AutoLock(tpm_proxy_->GetLock());
    sign_status = tpm_proxy_->SendU2fSign(sign_req, &sign_resp);
    brillo::SecureClear(&sign_req.userSecret, sizeof(sign_req.userSecret));
  } else {
    return HasCredentialsResponse::INVALID_REQUEST;
  }

  // Return status of 0 indicates the credential is valid.
  return (sign_status == 0) ? HasCredentialsResponse::SUCCESS
                            : HasCredentialsResponse::UNKNOWN_CREDENTIAL_ID;
}

void WebAuthnHandler::IsUvpaa(
    std::unique_ptr<IsUvpaaMethodResponse> method_response,
    const IsUvpaaRequest& request) {
  // Checking with the authentication dialog (in Ash) will not work, because
  // currently in Chrome the IsUvpaa is a blocking call, and Ash can't respond
  // to us since it runs in the same process as Chrome. After the Chrome side
  // is refactored to take a callback or Ash is split into a separate binary,
  // we can change the implementation here to query with Ash.

  IsUvpaaResponse response;

  if (!Initialized()) {
    LOG(INFO) << "IsUvpaa called but WebAuthnHandler not initialized. Maybe "
                 "U2F is on.";
    response.set_available(false);
    method_response->Return(response);
    return;
  }

  base::Optional<std::string> account_id = user_state_->GetUser();
  if (!account_id) {
    LOG(ERROR) << "IsUvpaa called but no user.";
    response.set_available(false);
    method_response->Return(response);
    return;
  }

  if (HasPin(*account_id)) {
    response.set_available(true);
    method_response->Return(response);
    return;
  }

  base::Optional<std::string> sanitized_user = user_state_->GetSanitizedUser();
  DCHECK(sanitized_user);
  if (HasFingerprint(*sanitized_user)) {
    response.set_available(true);
    method_response->Return(response);
    return;
  }

  response.set_available(false);
  method_response->Return(response);
}

bool WebAuthnHandler::HasPin(const std::string& account_id) {
  cryptohome::AccountIdentifier id;
  id.set_account_id(account_id);
  cryptohome::AuthorizationRequest auth;
  cryptohome::GetKeyDataRequest req;
  req.mutable_key()->mutable_data()->set_label(kCryptohomePinLabel);
  cryptohome::BaseReply reply;
  brillo::ErrorPtr error;

  if (!cryptohome_proxy_->GetKeyDataEx(id, auth, req, &reply, &error,
                                       kCryptohomeTimeout.InMilliseconds())) {
    LOG(ERROR) << "Cannot query PIN availability from cryptohome, error: "
               << error->GetMessage();
    return false;
  }

  if (reply.has_error()) {
    LOG(ERROR) << "GetKeyData response has error " << reply.error();
    return false;
  }

  if (!reply.HasExtension(cryptohome::GetKeyDataReply::reply)) {
    LOG(ERROR) << "GetKeyData response doesn't have the correct extension.";
    return false;
  }

  return reply.GetExtension(cryptohome::GetKeyDataReply::reply)
             .key_data_size() > 0;
}

bool WebAuthnHandler::HasFingerprint(const std::string& sanitized_user) {
  dbus::ObjectProxy* biod_proxy = bus_->GetObjectProxy(
      biod::kBiodServiceName,
      dbus::ObjectPath(std::string(biod::kBiodServicePath)
                           .append(kCrosFpBiometricsManagerRelativePath)));

  dbus::MethodCall method_call(biod::kBiometricsManagerInterface,
                               biod::kBiometricsManagerGetRecordsForUserMethod);
  dbus::MessageWriter method_writer(&method_call);
  method_writer.AppendString(sanitized_user);

  std::unique_ptr<dbus::Response> response = biod_proxy->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!response) {
    LOG(ERROR)
        << "Cannot check fingerprint availability: no response from biod.";
    return false;
  }

  dbus::MessageReader response_reader(response.get());
  dbus::MessageReader records_reader(nullptr);
  if (!response_reader.PopArray(&records_reader)) {
    LOG(ERROR) << "Cannot parse GetRecordsForUser response from biod.";
    return false;
  }

  int records_count = 0;
  while (records_reader.HasMoreData()) {
    dbus::ObjectPath record_path;
    if (!records_reader.PopObjectPath(&record_path)) {
      LOG(WARNING) << "Cannot parse fingerprint record path";
      continue;
    }
    records_count++;
  }
  return records_count > 0;
}

void WebAuthnHandler::SetWebAuthnStorageForTesting(
    std::unique_ptr<WebAuthnStorage> storage) {
  webauthn_storage_ = std::move(storage);
}

void WebAuthnHandler::SetCryptohomeInterfaceProxyForTesting(
    std::unique_ptr<org::chromium::CryptohomeInterfaceProxyInterface>
        cryptohome_proxy) {
  cryptohome_proxy_ = std::move(cryptohome_proxy);
}

}  // namespace u2f
