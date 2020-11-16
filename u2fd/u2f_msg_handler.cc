// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/u2f_msg_handler.h"

#include <utility>

#include <brillo/secure_blob.h>
#include <trunks/cr50_headers/u2f.h>

#include "u2fd/util.h"

namespace u2f {

namespace {

// Response to the APDU requesting the U2F protocol version
constexpr char kSupportedU2fVersion[] = "U2F_V2";

// U2F_REGISTER response prefix, indicating U2F_VER_2.
// See FIDO "U2F Raw Message Formats" spec.
constexpr uint8_t kU2fVer2Prefix = 5;

// UMA Metric names.
constexpr char kU2fCommand[] = "Platform.U2F.Command";

}  // namespace

U2fMessageHandler::U2fMessageHandler(
    std::unique_ptr<AllowlistingUtil> allowlisting_util,
    std::function<void()> request_user_presence,
    UserState* user_state,
    TpmVendorCommandProxy* proxy,
    MetricsLibraryInterface* metrics,
    bool allow_legacy_kh_sign,
    bool allow_g2f_attestation)
    : allowlisting_util_(std::move(allowlisting_util)),
      request_user_presence_(request_user_presence),

      user_state_(user_state),
      proxy_(proxy),
      metrics_(metrics),
      allow_legacy_kh_sign_(allow_legacy_kh_sign),
      allow_g2f_attestation_(allow_g2f_attestation) {}

U2fResponseAdpu U2fMessageHandler::ProcessMsg(const std::string& req) {
  uint16_t u2f_status = 0;

  base::Optional<U2fCommandAdpu> adpu =
      U2fCommandAdpu::ParseFromString(req, &u2f_status);

  if (!adpu.has_value()) {
    return BuildEmptyResponse(u2f_status ?: U2F_SW_WTF);
  }

  U2fIns ins = adpu->Ins();

  metrics_->SendEnumToUMA(kU2fCommand, static_cast<int>(ins),
                          static_cast<int>(U2fIns::kU2fVersion));

  // TODO(louiscollard): Check expected response length is large enough.

  switch (ins) {
    case U2fIns::kU2fRegister: {
      base::Optional<U2fRegisterRequestAdpu> reg_adpu =
          U2fRegisterRequestAdpu::FromCommandAdpu(*adpu, &u2f_status);
      // Chrome may send a dummy register request, which is designed to
      // cause a USB device to flash it's LED. We should simply ignore
      // these.
      if (reg_adpu.has_value()) {
        if (reg_adpu->IsChromeDummyWinkRequest()) {
          return BuildEmptyResponse(U2F_SW_CONDITIONS_NOT_SATISFIED);
        } else {
          return ProcessU2fRegister(*reg_adpu);
        }
      }
      break;  // Handle error.
    }
    case U2fIns::kU2fAuthenticate: {
      base::Optional<U2fAuthenticateRequestAdpu> auth_adpu =
          U2fAuthenticateRequestAdpu::FromCommandAdpu(*adpu, &u2f_status);
      if (auth_adpu.has_value()) {
        return ProcessU2fAuthenticate(*auth_adpu);
      }
      break;  // Handle error.
    }
    case U2fIns::kU2fVersion: {
      if (!adpu->Body().empty()) {
        u2f_status = U2F_SW_WRONG_LENGTH;
        break;
      }

      U2fResponseAdpu response;
      response.AppendString(kSupportedU2fVersion);
      response.SetStatus(U2F_SW_NO_ERROR);
      return response;
    }
    default:
      u2f_status = U2F_SW_INS_NOT_SUPPORTED;
      break;
  }

  return BuildEmptyResponse(u2f_status ?: U2F_SW_WTF);
}

namespace {

// Builds data to be signed as part of a U2F_REGISTER response, as defined by
// the "U2F Raw Message Formats" specification.
std::vector<uint8_t> BuildU2fRegisterResponseSignedData(
    const std::vector<uint8_t>& app_id,
    const std::vector<uint8_t>& challenge,
    const std::vector<uint8_t>& pub_key,
    const std::vector<uint8_t>& key_handle) {
  std::vector<uint8_t> signed_data;
  signed_data.push_back('\0');  // reserved byte
  util::AppendToVector(app_id, &signed_data);
  util::AppendToVector(challenge, &signed_data);
  util::AppendToVector(key_handle, &signed_data);
  util::AppendToVector(pub_key, &signed_data);
  return signed_data;
}

bool DoSoftwareAttest(const std::vector<uint8_t>& data_to_sign,
                      std::vector<uint8_t>* attestation_cert,
                      std::vector<uint8_t>* signature) {
  crypto::ScopedEC_KEY attestation_key = util::CreateAttestationKey();
  if (!attestation_key) {
    return false;
  }

  base::Optional<std::vector<uint8_t>> cert_result =
      util::CreateAttestationCertificate(attestation_key.get());
  base::Optional<std::vector<uint8_t>> attest_result =
      util::AttestToData(data_to_sign, attestation_key.get());

  if (!cert_result.has_value() || !attest_result.has_value()) {
    // These functions are never expected to fail.
    LOG(ERROR) << "U2F software attestation failed.";
    return false;
  }

  *attestation_cert = std::move(*cert_result);
  *signature = std::move(*attest_result);
  return true;
}

}  // namespace

U2fResponseAdpu U2fMessageHandler::ProcessU2fRegister(
    const U2fRegisterRequestAdpu& request) {
  std::vector<uint8_t> pub_key;
  std::vector<uint8_t> key_handle;

  Cr50CmdStatus generate_status =
      DoU2fGenerate(request.GetAppId(), &pub_key, &key_handle);

  if (generate_status == Cr50CmdStatus::kNotAllowed) {
    request_user_presence_();
  }

  if (generate_status != Cr50CmdStatus::kSuccess) {
    return BuildErrorResponse(generate_status);
  }

  std::vector<uint8_t> data_to_sign = BuildU2fRegisterResponseSignedData(
      request.GetAppId(), request.GetChallenge(), pub_key, key_handle);

  std::vector<uint8_t> attestation_cert;
  std::vector<uint8_t> signature;
  std::vector<uint8_t> allowlisting_data;

  if (allow_g2f_attestation_ && request.UseG2fAttestation()) {
    base::Optional<std::vector<uint8_t>> g2f_cert = GetG2fCert();

    if (g2f_cert.has_value()) {
      attestation_cert = *g2f_cert;
    } else {
      return BuildEmptyResponse(U2F_SW_WTF);
    }

    Cr50CmdStatus attest_status =
        DoG2fAttest(data_to_sign, U2F_ATTEST_FORMAT_REG_RESP, &signature);

    if (attest_status != Cr50CmdStatus::kSuccess) {
      return BuildEmptyResponse(U2F_SW_WTF);
    }

    if (allowlisting_util_ != nullptr &&
        !allowlisting_util_->AppendDataToCert(&attestation_cert)) {
      LOG(ERROR) << "Failed to get allowlisting data for G2F Enroll Request";
      return BuildEmptyResponse(U2F_SW_WTF);
    }
  } else if (!DoSoftwareAttest(data_to_sign, &attestation_cert, &signature)) {
    return BuildEmptyResponse(U2F_SW_WTF);
  }

  // Prepare response, as specified by "U2F Raw Message Formats".
  U2fResponseAdpu register_resp;
  register_resp.AppendByte(kU2fVer2Prefix);
  register_resp.AppendBytes(pub_key);
  register_resp.AppendByte(key_handle.size());
  register_resp.AppendBytes(key_handle);
  register_resp.AppendBytes(attestation_cert);
  register_resp.AppendBytes(signature);
  register_resp.SetStatus(U2F_SW_NO_ERROR);

  return register_resp;
}

namespace {

// A success response to a U2F_AUTHENTICATE request includes a signature over
// the following data, in this format.
std::vector<uint8_t> BuildU2fAuthenticateResponseSignedData(
    const std::vector<uint8_t>& app_id,
    const std::vector<uint8_t>& challenge,
    const std::vector<uint8_t>& counter) {
  std::vector<uint8_t> to_sign;
  util::AppendToVector(app_id, &to_sign);
  to_sign.push_back(U2F_AUTH_FLAG_TUP);
  util::AppendToVector(counter, &to_sign);
  util::AppendToVector(challenge, &to_sign);
  return to_sign;
}

}  // namespace

U2fResponseAdpu U2fMessageHandler::ProcessU2fAuthenticate(
    const U2fAuthenticateRequestAdpu& request) {
  if (request.IsAuthenticateCheckOnly()) {
    // The authenticate only version of this command always returns an error (on
    // success, returns an error requesting presence).
    Cr50CmdStatus sign_status =
        DoU2fSignCheckOnly(request.GetAppId(), request.GetKeyHandle());
    if (sign_status == Cr50CmdStatus::kSuccess) {
      return BuildEmptyResponse(U2F_SW_CONDITIONS_NOT_SATISFIED);
    } else {
      return BuildErrorResponse(sign_status);
    }
  }

  base::Optional<std::vector<uint8_t>> counter = user_state_->GetCounter();
  if (!counter.has_value()) {
    LOG(ERROR) << "Failed to retrieve counter value";
    return BuildEmptyResponse(U2F_SW_WTF);
  }

  std::vector<uint8_t> to_sign = BuildU2fAuthenticateResponseSignedData(
      request.GetAppId(), request.GetChallenge(), *counter);

  std::vector<uint8_t> signature;

  Cr50CmdStatus sign_status =
      DoU2fSign(request.GetAppId(), request.GetKeyHandle(),
                util::Sha256(to_sign), &signature);

  if (sign_status == Cr50CmdStatus::kNotAllowed) {
    request_user_presence_();
  }

  if (sign_status != Cr50CmdStatus::kSuccess) {
    return BuildErrorResponse(sign_status);
  }

  if (!user_state_->IncrementCounter()) {
    // If we can't increment the counter we must not return the signed
    // response, as the next authenticate response would end up having
    // the same counter value.
    return BuildEmptyResponse(U2F_SW_WTF);
  }

  // Everything succeeded; build response.

  // Prepare response, as specified by "U2F Raw Message Formats".
  U2fResponseAdpu auth_resp;
  auth_resp.AppendByte(U2F_AUTH_FLAG_TUP);
  auth_resp.AppendBytes(*counter);
  auth_resp.AppendBytes(signature);
  auth_resp.SetStatus(U2F_SW_NO_ERROR);

  return auth_resp;
}

U2fMessageHandler::Cr50CmdStatus U2fMessageHandler::DoU2fGenerate(
    const std::vector<uint8_t>& app_id,
    std::vector<uint8_t>* pub_key,
    std::vector<uint8_t>* key_handle) {
  base::AutoLock(proxy_->GetLock());
  base::Optional<brillo::SecureBlob> user_secret = user_state_->GetUserSecret();
  if (!user_secret.has_value()) {
    return Cr50CmdStatus::kInvalidState;
  }

  struct u2f_generate_req generate_req = {
      .flags = U2F_AUTH_ENFORCE  // Require user presence, consume.
  };
  if (!util::VectorToObject(app_id, generate_req.appId,
                            sizeof(generate_req.appId))) {
    return Cr50CmdStatus::kInvalidState;
  }
  if (!util::VectorToObject(*user_secret, generate_req.userSecret,
                            sizeof(generate_req.userSecret))) {
    return Cr50CmdStatus::kInvalidState;
  }

  struct u2f_generate_resp generate_resp = {};
  Cr50CmdStatus generate_status = static_cast<Cr50CmdStatus>(
      proxy_->SendU2fGenerate(generate_req, &generate_resp));

  brillo::SecureClear(&generate_req.userSecret,
                      sizeof(generate_req.userSecret));

  if (generate_status != Cr50CmdStatus::kSuccess) {
    return generate_status;
  }

  util::AppendToVector(generate_resp.pubKey, pub_key);
  util::AppendToVector(generate_resp.keyHandle, key_handle);

  return Cr50CmdStatus::kSuccess;
}

U2fMessageHandler::Cr50CmdStatus U2fMessageHandler::DoU2fSign(
    const std::vector<uint8_t>& app_id,
    const std::vector<uint8_t>& key_handle,
    const std::vector<uint8_t>& hash,
    std::vector<uint8_t>* signature_out) {
  base::AutoLock(proxy_->GetLock());
  base::Optional<brillo::SecureBlob> user_secret = user_state_->GetUserSecret();
  if (!user_secret.has_value()) {
    return Cr50CmdStatus::kInvalidState;
  }

  struct u2f_sign_req sign_req = {
      .flags = U2F_AUTH_ENFORCE  // Require user presence, consume.
  };
  if (allow_legacy_kh_sign_)
    sign_req.flags |= SIGN_LEGACY_KH;
  if (!util::VectorToObject(app_id, sign_req.appId, sizeof(sign_req.appId))) {
    return Cr50CmdStatus::kInvalidState;
  }
  if (!util::VectorToObject(*user_secret, sign_req.userSecret,
                            sizeof(sign_req.userSecret))) {
    return Cr50CmdStatus::kInvalidState;
  }
  if (!util::VectorToObject(key_handle, &sign_req.keyHandle,
                            sizeof(sign_req.keyHandle))) {
    return Cr50CmdStatus::kInvalidState;
  }
  if (!util::VectorToObject(hash, sign_req.hash, sizeof(sign_req.hash))) {
    return Cr50CmdStatus::kInvalidState;
  }

  struct u2f_sign_resp sign_resp = {};
  Cr50CmdStatus sign_status =
      static_cast<Cr50CmdStatus>(proxy_->SendU2fSign(sign_req, &sign_resp));

  brillo::SecureClear(&sign_req.userSecret, sizeof(sign_req.userSecret));

  if (sign_status != Cr50CmdStatus::kSuccess) {
    return sign_status;
  }

  base::Optional<std::vector<uint8_t>> signature =
      util::SignatureToDerBytes(sign_resp.sig_r, sign_resp.sig_s);

  if (!signature.has_value()) {
    return Cr50CmdStatus::kInvalidResponseData;
  }

  *signature_out = *signature;

  return Cr50CmdStatus::kSuccess;
}

U2fMessageHandler::Cr50CmdStatus U2fMessageHandler::DoU2fSignCheckOnly(
    const std::vector<uint8_t>& app_id,
    const std::vector<uint8_t>& key_handle) {
  base::AutoLock(proxy_->GetLock());
  base::Optional<brillo::SecureBlob> user_secret = user_state_->GetUserSecret();
  if (!user_secret.has_value()) {
    return Cr50CmdStatus::kInvalidState;
  }

  struct u2f_sign_req sign_req = {
      .flags = U2F_AUTH_CHECK_ONLY  // No user presence required, no consume.
  };
  if (!util::VectorToObject(app_id, sign_req.appId, sizeof(sign_req.appId))) {
    return Cr50CmdStatus::kInvalidState;
  }
  if (!util::VectorToObject(*user_secret, sign_req.userSecret,
                            sizeof(sign_req.userSecret))) {
    return Cr50CmdStatus::kInvalidState;
  }
  if (!util::VectorToObject(key_handle, &sign_req.keyHandle,
                            sizeof(sign_req.keyHandle))) {
    return Cr50CmdStatus::kInvalidState;
  }

  Cr50CmdStatus sign_status =
      static_cast<Cr50CmdStatus>(proxy_->SendU2fSign(sign_req, nullptr));

  brillo::SecureClear(&sign_req.userSecret, sizeof(sign_req.userSecret));

  return sign_status;
}

U2fMessageHandler::Cr50CmdStatus U2fMessageHandler::DoG2fAttest(
    const std::vector<uint8_t>& data,
    uint8_t format,
    std::vector<uint8_t>* signature_out) {
  base::AutoLock(proxy_->GetLock());
  base::Optional<brillo::SecureBlob> user_secret = user_state_->GetUserSecret();
  if (!user_secret.has_value()) {
    return Cr50CmdStatus::kInvalidState;
  }

  struct u2f_attest_req attest_req = {
      .format = format, .dataLen = static_cast<uint8_t>(data.size())};
  if (!util::VectorToObject(*user_secret, attest_req.userSecret,
                            sizeof(attest_req.userSecret))) {
    return Cr50CmdStatus::kInvalidState;
  }
  if (!util::VectorToObject(data, attest_req.data, sizeof(attest_req.data))) {
    return Cr50CmdStatus::kInvalidState;
  }

  struct u2f_attest_resp attest_resp = {};
  Cr50CmdStatus attest_status = static_cast<Cr50CmdStatus>(
      proxy_->SendU2fAttest(attest_req, &attest_resp));

  brillo::SecureClear(&attest_req.userSecret, sizeof(attest_req.userSecret));

  if (attest_status != Cr50CmdStatus::kSuccess) {
    // We are attesting to a key handle that we just created, so if
    // attestation fails we have hit some internal error.
    LOG(ERROR) << "U2F_ATTEST failed, status: " << std::hex
               << static_cast<uint32_t>(attest_status);
    return attest_status;
  }

  base::Optional<std::vector<uint8_t>> signature =
      util::SignatureToDerBytes(attest_resp.sig_r, attest_resp.sig_s);

  if (!signature.has_value()) {
    LOG(ERROR) << "DER encoding of U2F_ATTEST signature failed.";
    return Cr50CmdStatus::kInvalidResponseData;
  }

  *signature_out = *signature;

  return Cr50CmdStatus::kSuccess;
}

U2fResponseAdpu U2fMessageHandler::BuildEmptyResponse(uint16_t sw) {
  U2fResponseAdpu resp_adpu;
  resp_adpu.SetStatus(sw);
  return resp_adpu;
}

U2fResponseAdpu U2fMessageHandler::BuildErrorResponse(Cr50CmdStatus status) {
  uint16_t sw;

  switch (status) {
    case Cr50CmdStatus::kNotAllowed:
      sw = U2F_SW_CONDITIONS_NOT_SATISFIED;
      break;
    case Cr50CmdStatus::kPasswordRequired:
      sw = U2F_SW_WRONG_DATA;
      break;
    case Cr50CmdStatus::kInvalidState:
      sw = U2F_SW_WTF;
      break;
    default:
      LOG(ERROR) << "Unexpected Cr50CmdStatus: " << std::hex
                 << static_cast<uint32_t>(status);
      sw = U2F_SW_WTF;
  }

  return BuildEmptyResponse(sw);
}

base::Optional<std::vector<uint8_t>> U2fMessageHandler::GetG2fCert() {
  std::string cert_str;
  std::vector<uint8_t> cert;

  Cr50CmdStatus get_cert_status =
      static_cast<Cr50CmdStatus>(proxy_->GetG2fCertificate(&cert_str));

  if (get_cert_status != Cr50CmdStatus::kSuccess) {
    LOG(ERROR) << "Failed to retrieve G2F certificate, status: " << std::hex
               << static_cast<uint32_t>(get_cert_status);
    return base::nullopt;
  }

  util::AppendToVector(cert_str, &cert);

  if (!util::RemoveCertificatePadding(&cert)) {
    LOG(ERROR) << "Failed to remove padding from G2F certificate ";
    return base::nullopt;
  }

  return cert;
}

}  // namespace u2f
