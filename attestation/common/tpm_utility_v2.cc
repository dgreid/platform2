// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/common/tpm_utility_v2.h"

#include <ios>
#include <memory>
#include <vector>

#include <base/bind.h>
#include <base/hash/sha1.h>
#include <base/logging.h>
#include <base/optional.h>
#include <crypto/libcrypto-compat.h>
#include <crypto/scoped_openssl_types.h>
#include <crypto/sha2.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>

#include "trunks/authorization_delegate.h"
#include "trunks/error_codes.h"
#include "trunks/tpm_generated.h"

namespace {

using trunks::AuthorizationDelegate;
using trunks::HmacSession;
using trunks::TPM_HANDLE;
using trunks::TPM_RC;
using trunks::TPM_RC_SUCCESS;

const unsigned int kWellKnownExponent = 65537;

// TODO(crbug/916023): move these utility functions to shared library.
inline const uint8_t* StringToByteBuffer(const char* str) {
  return reinterpret_cast<const uint8_t*>(str);
}

inline std::string BytesToString(const std::vector<uint8_t>& bytes) {
  return std::string(bytes.begin(), bytes.end());
}

inline std::string BytesToString(
    const base::Optional<std::vector<uint8_t>>& maybe_bytes) {
  return BytesToString(maybe_bytes.value_or(std::vector<uint8_t>()));
}

bool StringToBignum(const std::string& big_integer, BIGNUM* b) {
  if (big_integer.empty() || !b)
    return false;

  return BN_bin2bn(StringToByteBuffer(big_integer.data()), big_integer.length(),
                   b);
}

crypto::ScopedRSA CreateRSAFromRawModulus(const uint8_t* modulus_buffer,
                                          size_t modulus_size) {
  crypto::ScopedRSA rsa(RSA_new());
  crypto::ScopedBIGNUM e(BN_new()), n(BN_new());
  if (!rsa || !e || !n) {
    LOG(ERROR) << __func__ << ": Failed to allocate RSA or BIGNUMs.";
    return nullptr;
  }

  if (!BN_set_word(e.get(), kWellKnownExponent) ||
      !BN_bin2bn(modulus_buffer, modulus_size, n.get())) {
    LOG(ERROR) << __func__ << ": Failed to generate exponent or modulus.";
    return nullptr;
  }

  if (!RSA_set0_key(rsa.get(), n.release(), e.release(), nullptr)) {
    LOG(ERROR) << __func__ << ": Failed to set exponent or modulus.";
    return nullptr;
  }

  return rsa;
}

// Convert TPMT_PUBLIC TPM public area |public_area| of RSA key to a OpenSSL RSA
// key.
crypto::ScopedRSA GetRsaPublicKeyFromTpmPublicArea(
    const trunks::TPMT_PUBLIC& public_area) {
  if (public_area.type != trunks::TPM_ALG_RSA) {
    return nullptr;
  }
  crypto::ScopedRSA key = CreateRSAFromRawModulus(public_area.unique.rsa.buffer,
                                                  public_area.unique.rsa.size);
  if (key == nullptr) {
    LOG(ERROR) << __func__ << ": Failed to decode public key.";
    return nullptr;
  }
  return key;
}

int TrunksCurveIDToNID(int trunks_curve_id) {
  switch (trunks_curve_id) {
    case trunks::TPM_ECC_NIST_P256:
      return NID_X9_62_prime256v1;
    default:
      return NID_undef;
  }
}

// Convert TPMT_PUBLIC TPM public area |public_area| of ECC key to a OpenSSL EC
// key.
crypto::ScopedEC_KEY GetEccPublicKeyFromTpmPublicArea(
    const trunks::TPMT_PUBLIC& public_area) {
  if (public_area.type != trunks::TPM_ALG_ECC) {
    return nullptr;
  }

  int nid = TrunksCurveIDToNID(public_area.parameters.ecc_detail.curve_id);
  if (nid == NID_undef) {
    LOG(ERROR) << __func__ << "Unknown trunks curve_id: " << std::hex
               << std::showbase << public_area.parameters.ecc_detail.curve_id;
    return nullptr;
  }
  crypto::ScopedEC_Key key(EC_KEY_new_by_curve_name(nid));

  // Ensure that the curve is recorded in the key by reference to its ASN.1
  // object ID rather than explicitly by value.
  EC_KEY_set_asn1_flag(key.get(), OPENSSL_EC_NAMED_CURVE);

  std::string xs = StringFrom_TPM2B_ECC_PARAMETER(public_area.unique.ecc.x);
  std::string ys = StringFrom_TPM2B_ECC_PARAMETER(public_area.unique.ecc.y);

  crypto::ScopedBIGNUM x(BN_new()), y(BN_new());
  if (!x || !y) {
    LOG(ERROR) << __func__ << ": Failed to allocate BIGNUMs for ECC parameters";
    return nullptr;
  }

  if (!StringToBignum(xs, x.get()) || !StringToBignum(ys, y.get())) {
    LOG(ERROR) << __func__ << ": Failed to parse ECC parameters";
    return nullptr;
  }

  // EC_KEY_set_public_key_affine_coordinates will check the pointers are valid
  if (!EC_KEY_set_public_key_affine_coordinates(key.get(), x.get(), y.get())) {
    return nullptr;
  }

  if (!EC_KEY_check_key(key.get())) {
    LOG(ERROR) << __func__
               << ": Bad ECC key created from TPM public key object.";
    return nullptr;
  }

  return key;
}

template <typename OpenSSLType>
base::Optional<std::vector<uint8_t>> OpenSSLObjectToBytes(
    int (*i2d_convert_function)(OpenSSLType*, unsigned char**),
    typename std::remove_const<OpenSSLType>::type* type) {
  if (type == nullptr) {
    return base::nullopt;
  }

  unsigned char* openssl_buffer = nullptr;

  int size = i2d_convert_function(type, &openssl_buffer);
  if (size < 0) {
    return base::nullopt;
  }

  crypto::ScopedOpenSSLBytes scoped_buffer(openssl_buffer);
  return std::vector<uint8_t>(openssl_buffer, openssl_buffer + size);
}

// TODO(menghuan): consider use EVP_PKEY and related APIs
// Return RSAPublicKey DER encoded string
std::string RSAPublicKeyToString(const crypto::ScopedRSA& key) {
  return BytesToString(OpenSSLObjectToBytes(i2d_RSAPublicKey, key.get()));
}

// Return SubjectPublicKeyInfo DER encoded string for RSA key.
std::string RsaSubjectPublicKeyInfoToString(const crypto::ScopedRSA& key) {
  return BytesToString(OpenSSLObjectToBytes(i2d_RSA_PUBKEY, key.get()));
}

// Return SubjectPublicKeyInfo DER encoded string for ECC key.
std::string EccSubjectPublicKeyInfoToString(const crypto::ScopedEC_KEY& key) {
  return BytesToString(OpenSSLObjectToBytes(i2d_EC_PUBKEY, key.get()));
}

crypto::ScopedECDSA_SIG CreateEcdsaSigFromRS(std::string r, std::string s) {
  crypto::ScopedECDSA_SIG sig(ECDSA_SIG_new());
  crypto::ScopedBIGNUM r_bn(BN_new()), s_bn(BN_new());
  if (!sig || !r_bn || !s_bn) {
    LOG(ERROR) << __func__ << ": Failed to allocate RSA or BIGNUM";
    return nullptr;
  }

  if (!StringToBignum(r, r_bn.get()) || !StringToBignum(s, s_bn.get())) {
    LOG(ERROR) << __func__ << ": Failed to parse ECDSA SIG parameters";
    return nullptr;
  }

  if (!ECDSA_SIG_set0(sig.get(), r_bn.release(), s_bn.release())) {
    LOG(ERROR) << __func__ << ": Failed to set ECDSA SIG parameters";
    return nullptr;
  }

  return sig;
}

base::Optional<std::string> SerializeFromTpmSignature(
    const trunks::TPMT_SIGNATURE& signature) {
  switch (signature.sig_alg) {
    case trunks::TPM_ALG_RSASSA:
      return StringFrom_TPM2B_PUBLIC_KEY_RSA(signature.signature.rsassa.sig);
    case trunks::TPM_ALG_ECDSA: {
      crypto::ScopedECDSA_SIG sig = CreateEcdsaSigFromRS(
          StringFrom_TPM2B_ECC_PARAMETER(signature.signature.ecdsa.signature_r),
          StringFrom_TPM2B_ECC_PARAMETER(
              signature.signature.ecdsa.signature_s));

      return BytesToString(OpenSSLObjectToBytes(i2d_ECDSA_SIG, sig.get()));
    }
    default:
      LOG(ERROR) << __func__
                 << ": unkown TPM 2.0 signature type: " << signature.sig_alg;
      return base::nullopt;
  }
}

// An authorization delegate to manage multiple authorization sessions for a
// single command.
class MultipleAuthorizations : public AuthorizationDelegate {
 public:
  MultipleAuthorizations() = default;
  ~MultipleAuthorizations() override = default;

  void AddAuthorizationDelegate(AuthorizationDelegate* delegate) {
    delegates_.push_back(delegate);
  }

  bool GetCommandAuthorization(const std::string& command_hash,
                               bool is_command_parameter_encryption_possible,
                               bool is_response_parameter_encryption_possible,
                               std::string* authorization) override {
    std::string combined_authorization;
    for (auto delegate : delegates_) {
      std::string authorization;
      if (!delegate->GetCommandAuthorization(
              command_hash, is_command_parameter_encryption_possible,
              is_response_parameter_encryption_possible, &authorization)) {
        return false;
      }
      combined_authorization += authorization;
    }
    *authorization = combined_authorization;
    return true;
  }

  bool CheckResponseAuthorization(const std::string& response_hash,
                                  const std::string& authorization) override {
    std::string mutable_authorization = authorization;
    for (auto delegate : delegates_) {
      if (!delegate->CheckResponseAuthorization(
              response_hash,
              ExtractSingleAuthorizationResponse(&mutable_authorization))) {
        return false;
      }
    }
    return true;
  }

  bool EncryptCommandParameter(std::string* parameter) override {
    for (auto delegate : delegates_) {
      if (!delegate->EncryptCommandParameter(parameter)) {
        return false;
      }
    }
    return true;
  }

  bool DecryptResponseParameter(std::string* parameter) override {
    for (auto delegate : delegates_) {
      if (!delegate->DecryptResponseParameter(parameter)) {
        return false;
      }
    }
    return true;
  }

  bool GetTpmNonce(std::string* nonce) override { return false; }

 private:
  std::string ExtractSingleAuthorizationResponse(std::string* all_responses) {
    std::string response;
    trunks::TPMS_AUTH_RESPONSE not_used;
    if (TPM_RC_SUCCESS !=
        Parse_TPMS_AUTH_RESPONSE(all_responses, &not_used, &response)) {
      return std::string();
    }
    return response;
  }

  std::vector<AuthorizationDelegate*> delegates_;
};

void FlushObject(trunks::TrunksFactory* factory, TPM_HANDLE object_handle) {
  if (object_handle >= trunks::TRANSIENT_FIRST &&
      object_handle <= trunks::TRANSIENT_LAST) {
    factory->GetTpm()->FlushContextSync(object_handle,
                                        nullptr /* authorization */);
  }
}

class TpmObjectScoper {
 public:
  TpmObjectScoper(trunks::TrunksFactory* factory, TPM_HANDLE object_handle)
      : factory_(factory), object_handle_(object_handle) {}
  ~TpmObjectScoper() { FlushObject(factory_, object_handle_); }

 private:
  trunks::TrunksFactory* factory_;
  TPM_HANDLE object_handle_;
};

}  // namespace

namespace attestation {

TpmUtilityV2::TpmUtilityV2(tpm_manager::TpmManagerUtility* tpm_manager_utility,
                           trunks::TrunksFactory* trunks_factory)
    : TpmUtilityCommon(tpm_manager_utility), trunks_factory_(trunks_factory) {}

TpmUtilityV2::~TpmUtilityV2() {
  for (auto& i : endorsement_keys_) {
    FlushObject(trunks_factory_, i.second);
  }
}

bool TpmUtilityV2::Initialize() {
  if (!TpmUtilityCommon::Initialize()) {
    return false;
  }

  if (!trunks_factory_) {
    default_trunks_factory_ = std::make_unique<trunks::TrunksFactoryImpl>();
    if (!default_trunks_factory_->Initialize()) {
      LOG(ERROR) << "Failed to initialize trunks.";
      return false;
    }
    trunks_factory_ = default_trunks_factory_.get();
  }
  trunks_utility_ = trunks_factory_->GetTpmUtility();
  return true;
}

bool TpmUtilityV2::ActivateIdentity(const std::string& identity_key_blob,
                                    const std::string& asym_ca_contents,
                                    const std::string& sym_ca_attestation,
                                    std::string* credential) {
  LOG(ERROR) << __func__ << ": Not implemented.";
  return false;
}

bool TpmUtilityV2::ActivateIdentityForTpm2(
    KeyType key_type,
    const std::string& identity_key_blob,
    const std::string& encrypted_seed,
    const std::string& credential_mac,
    const std::string& wrapped_credential,
    std::string* credential) {
  std::unique_ptr<AuthorizationDelegate> empty_password_authorization =
      trunks_factory_->GetPasswordAuthorization(std::string());
  TPM_HANDLE identity_key_handle;
  TPM_RC result = trunks_utility_->LoadKey(identity_key_blob,
                                           empty_password_authorization.get(),
                                           &identity_key_handle);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to load identity key: "
               << trunks::GetErrorString(result);
    return false;
  }
  TpmObjectScoper scoper(trunks_factory_, identity_key_handle);
  std::string identity_key_name;
  result = trunks_utility_->GetKeyName(identity_key_handle, &identity_key_name);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to get identity key name: "
               << trunks::GetErrorString(result);
    return false;
  }

  TPM_HANDLE endorsement_key_handle;
  if (!GetEndorsementKey(key_type, &endorsement_key_handle)) {
    LOG(ERROR) << __func__ << ": Endorsement key is not available.";
    return false;
  }
  std::string endorsement_key_name;
  result = trunks_utility_->GetKeyName(endorsement_key_handle,
                                       &endorsement_key_name);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to get endorsement key name: "
               << trunks::GetErrorString(result);
    return false;
  }

  std::string endorsement_password;
  if (!GetEndorsementPassword(&endorsement_password)) {
    LOG(ERROR) << __func__ << ": Failed to get endorsement password";
    return false;
  }

  std::unique_ptr<HmacSession> endorsement_session =
      trunks_factory_->GetHmacSession();
  result = endorsement_session->StartUnboundSession(
      true /* salted */, false /* enable_encryption */);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to setup endorsement session: "
               << trunks::GetErrorString(result);
    return false;
  }
  endorsement_session->SetEntityAuthorizationValue(endorsement_password);

  std::unique_ptr<trunks::PolicySession> session =
      trunks_factory_->GetPolicySession();
  result = session->StartUnboundSession(true /* salted */,
                                        false /* enable_encryption */);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to start session: "
               << trunks::GetErrorString(result);
    return false;
  }

  trunks::TPMI_DH_ENTITY auth_entity = trunks::TPM_RH_ENDORSEMENT;
  std::string auth_entity_name;
  trunks::Serialize_TPM_HANDLE(auth_entity, &auth_entity_name);

  result = session->PolicySecret(auth_entity, auth_entity_name, std::string(),
                                 std::string(), std::string(), 0,
                                 endorsement_session->GetDelegate());
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to set the secret: "
               << trunks::GetErrorString(result);
    return false;
  }

  MultipleAuthorizations authorization;
  authorization.AddAuthorizationDelegate(empty_password_authorization.get());
  authorization.AddAuthorizationDelegate(session->GetDelegate());
  std::string identity_object_data;
  trunks::Serialize_TPM2B_DIGEST(trunks::Make_TPM2B_DIGEST(credential_mac),
                                 &identity_object_data);
  identity_object_data += wrapped_credential;
  trunks::TPM2B_DIGEST encoded_credential;
  result = trunks_factory_->GetTpm()->ActivateCredentialSync(
      identity_key_handle, identity_key_name, endorsement_key_handle,
      endorsement_key_name, trunks::Make_TPM2B_ID_OBJECT(identity_object_data),
      trunks::Make_TPM2B_ENCRYPTED_SECRET(encrypted_seed), &encoded_credential,
      &authorization);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to activate: " << trunks::GetErrorString(result);
    return false;
  }
  *credential = trunks::StringFrom_TPM2B_DIGEST(encoded_credential);
  return true;
}

bool TpmUtilityV2::CreateCertifiedKey(KeyType key_type,
                                      KeyUsage key_usage,
                                      const std::string& identity_key_blob,
                                      const std::string& external_data,
                                      std::string* key_blob,
                                      std::string* public_key_der,
                                      std::string* public_key_tpm_format,
                                      std::string* key_info,
                                      std::string* proof) {
  if (identity_key_blob.empty()) {
    LOG(ERROR) << __func__ << ": Unexpected empty identity_key_blob.";
    return false;
  }

  std::unique_ptr<AuthorizationDelegate> empty_password_authorization =
      trunks_factory_->GetPasswordAuthorization(std::string());
  trunks::TpmUtility::AsymmetricKeyUsage trunks_key_usage =
      (key_usage == KEY_USAGE_SIGN) ? trunks::TpmUtility::kSignKey
                                    : trunks::TpmUtility::kDecryptKey;

  TPM_RC result;
  switch (key_type) {
    case KEY_TYPE_RSA:
      result = trunks_utility_->CreateRSAKeyPair(
          trunks_key_usage, 2048 /* modulus_bits */,
          0 /* Use default public exponent */, std::string() /* password */,
          std::string() /* policy_digest */,
          false /* use_only_policy_authorization */,
          std::vector<uint32_t>() /* creation_pcr_indexes */,
          empty_password_authorization.get(), key_blob,
          nullptr /* creation_blob */);
      break;
    case KEY_TYPE_ECC:
      result = trunks_utility_->CreateECCKeyPair(
          trunks_key_usage, trunks::TPM_ECC_NIST_P256 /* curve_id */,
          std::string() /* password */, std::string() /* policy_digest */,
          false /* use_only_policy_authorization */,
          std::vector<uint32_t>() /* creation_pcr_indexes */,
          empty_password_authorization.get(), key_blob,
          nullptr /* creation_blob */);
      break;
    default:
      LOG(ERROR) << __func__ << ": Not implemented.";
      return false;
  }
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to create key: " << trunks::GetErrorString(result);
    return false;
  }

  TPM_HANDLE key_handle;
  result = trunks_utility_->LoadKey(
      *key_blob, empty_password_authorization.get(), &key_handle);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to load key: " << trunks::GetErrorString(result);
    return false;
  }
  TpmObjectScoper scoper(trunks_factory_, key_handle);

  std::string key_name;
  result = trunks_utility_->GetKeyName(key_handle, &key_name);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to get key name: "
               << trunks::GetErrorString(result);
    return false;
  }

  trunks::TPMT_PUBLIC public_area;
  result = trunks_utility_->GetKeyPublicArea(key_handle, &public_area);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to get key public area: "
               << trunks::GetErrorString(result);
    return false;
  }

  result = trunks::Serialize_TPMT_PUBLIC(public_area, public_key_tpm_format);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to serialize key public area: "
               << trunks::GetErrorString(result);
    return false;
  }

  switch (key_type) {
    case KEY_TYPE_RSA:
      *public_key_der =
          RSAPublicKeyToString(GetRsaPublicKeyFromTpmPublicArea(public_area));
      break;
    case KEY_TYPE_ECC:
      *public_key_der = EccSubjectPublicKeyInfoToString(
          GetEccPublicKeyFromTpmPublicArea(public_area));
      break;
  }
  if (public_key_der->empty()) {
    LOG(ERROR) << __func__ << ": Failed to convert public key.";
    return false;
  }

  TPM_HANDLE identity_key_handle;
  result = trunks_utility_->LoadKey(identity_key_blob,
                                    empty_password_authorization.get(),
                                    &identity_key_handle);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to load key: " << trunks::GetErrorString(result);
    return false;
  }
  TpmObjectScoper scoper2(trunks_factory_, identity_key_handle);
  result = trunks_utility_->GetKeyPublicArea(identity_key_handle, &public_area);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to get identity key public area: "
               << trunks::GetErrorString(result);
    return false;
  }

  std::string identity_key_name;
  result = trunks_utility_->GetKeyName(identity_key_handle, &identity_key_name);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to get identity key name: "
               << trunks::GetErrorString(result);
    return false;
  }

  trunks::TPMT_SIG_SCHEME scheme;
  scheme.details.any.hash_alg = trunks::TPM_ALG_SHA256;
  switch (public_area.type) {
    case trunks::TPM_ALG_RSA:
      scheme.scheme = trunks::TPM_ALG_RSASSA;
      break;
    case trunks::TPM_ALG_ECC:
      scheme.scheme = trunks::TPM_ALG_ECDSA;
      break;
    default:
      LOG(ERROR) << __func__ << ": Unknown TPM key type of TPM handle.";
      return false;
  }
  trunks::TPM2B_ATTEST certify_info;
  trunks::TPMT_SIGNATURE signature;
  MultipleAuthorizations authorization;
  authorization.AddAuthorizationDelegate(empty_password_authorization.get());
  authorization.AddAuthorizationDelegate(empty_password_authorization.get());
  result = trunks_factory_->GetTpm()->CertifySync(
      key_handle, key_name, identity_key_handle, identity_key_name,
      trunks::Make_TPM2B_DATA(external_data), scheme, &certify_info, &signature,
      &authorization);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to certify key: " << trunks::GetErrorString(result);
    return false;
  }
  *key_info = StringFrom_TPM2B_ATTEST(certify_info);
  *proof = SerializeFromTpmSignature(signature).value_or("");
  return true;
}

bool TpmUtilityV2::SealToPCR0(const std::string& data,
                              std::string* sealed_data) {
  std::string policy_digest;
  TPM_RC result = trunks_utility_->GetPolicyDigestForPcrValues(
      std::map<uint32_t, std::string>(
          {{0, std::string() /* Use current PCR value */}}),
      false, /* No authorization session */
      &policy_digest);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to compute policy digest: "
               << trunks::GetErrorString(result);
    return false;
  }
  std::unique_ptr<AuthorizationDelegate> empty_password_authorization =
      trunks_factory_->GetPasswordAuthorization(std::string());
  result = trunks_utility_->SealData(
      data, policy_digest, "", empty_password_authorization.get(), sealed_data);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to seal data: " << trunks::GetErrorString(result);
    return false;
  }
  return true;
}

bool TpmUtilityV2::Unseal(const std::string& sealed_data, std::string* data) {
  std::unique_ptr<trunks::PolicySession> session =
      trunks_factory_->GetPolicySession();
  TPM_RC result = session->StartUnboundSession(true /* salted */,
                                               true /* enable_encryption */);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to start encrypted session: "
               << trunks::GetErrorString(result);
    return false;
  }
  result = session->PolicyPCR(std::map<uint32_t, std::string>(
      {{0, std::string() /* Use current PCR value */}}));
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to setup policy session: "
               << trunks::GetErrorString(result);
    return false;
  }
  result =
      trunks_utility_->UnsealData(sealed_data, session->GetDelegate(), data);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to unseal data: " << trunks::GetErrorString(result);
    return false;
  }
  return true;
}

bool TpmUtilityV2::GetEndorsementPublicKey(KeyType key_type,
                                           std::string* public_key_der) {
  TPM_HANDLE key_handle;
  if (!GetEndorsementKey(key_type, &key_handle)) {
    LOG(ERROR) << __func__ << ": EK not available.";
    return false;
  }

  trunks::TPMT_PUBLIC public_area;
  TPM_RC result = trunks_utility_->GetKeyPublicArea(key_handle, &public_area);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to get EK public area: "
               << trunks::GetErrorString(result);
    return false;
  }

  switch (key_type) {
    case KEY_TYPE_RSA:
      *public_key_der = RsaSubjectPublicKeyInfoToString(
          GetRsaPublicKeyFromTpmPublicArea(public_area));
      break;
    case KEY_TYPE_ECC:
      *public_key_der = EccSubjectPublicKeyInfoToString(
          GetEccPublicKeyFromTpmPublicArea(public_area));
      break;
  }

  if (public_key_der->empty()) {
    LOG(ERROR) << __func__
               << ": Failed to convert EK public key to DER format.";
    return false;
  }
  return true;
}

bool TpmUtilityV2::GetEndorsementCertificate(KeyType key_type,
                                             std::string* certificate) {
  // TODO(crbug/956855): Use the real index instead of non-real ones.
  uint32_t index = (key_type == KEY_TYPE_RSA)
                       ? trunks::kRsaEndorsementCertificateNonRealIndex
                       : trunks::kEccEndorsementCertificateNonRealIndex;
  if (!tpm_manager_utility_->ReadSpace(index, false /*owner auth*/,
                                       certificate)) {
    LOG(ERROR) << __func__ << ": Failed to read endorsement certificate";
    return false;
  }
  return true;
}

bool TpmUtilityV2::Unbind(const std::string& key_blob,
                          const std::string& bound_data,
                          std::string* data) {
  std::unique_ptr<AuthorizationDelegate> empty_password_authorization =
      trunks_factory_->GetPasswordAuthorization(std::string());
  TPM_HANDLE key_handle;
  TPM_RC result = trunks_utility_->LoadKey(
      key_blob, empty_password_authorization.get(), &key_handle);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to load key: " << trunks::GetErrorString(result);
    return false;
  }
  TpmObjectScoper scoper(trunks_factory_, key_handle);
  result = trunks_utility_->AsymmetricDecrypt(
      key_handle, trunks::TPM_ALG_OAEP, trunks::TPM_ALG_SHA256, bound_data,
      empty_password_authorization.get(), data);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to decrypt: " << trunks::GetErrorString(result);
    return false;
  }
  return true;
}

bool TpmUtilityV2::Sign(const std::string& key_blob,
                        const std::string& data_to_sign,
                        std::string* signature) {
  std::unique_ptr<AuthorizationDelegate> empty_password_authorization =
      trunks_factory_->GetPasswordAuthorization(std::string());
  TPM_HANDLE key_handle;
  TPM_RC result = trunks_utility_->LoadKey(
      key_blob, empty_password_authorization.get(), &key_handle);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to load key: " << trunks::GetErrorString(result);
    return false;
  }

  trunks::TPMT_PUBLIC public_area;
  result = trunks_utility_->GetKeyPublicArea(key_handle, &public_area);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to get key public area: "
               << trunks::GetErrorString(result);
    return false;
  }

  trunks::TPM_ALG_ID sign_algorithm;
  switch (public_area.type) {
    case trunks::TPM_ALG_RSA:
      sign_algorithm = trunks::TPM_ALG_RSASSA;
      break;
    case trunks::TPM_ALG_ECC:
      sign_algorithm = trunks::TPM_ALG_ECDSA;
      break;
    default:
      LOG(ERROR) << __func__ << ": Unknown TPM key type: " << public_area.type;
      return false;
  }

  TpmObjectScoper scoper(trunks_factory_, key_handle);
  result = trunks_utility_->Sign(
      key_handle, sign_algorithm, trunks::TPM_ALG_SHA256, data_to_sign,
      true /* generate_hash */, empty_password_authorization.get(), signature);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to sign data: " << trunks::GetErrorString(result);
    return false;
  }

  // For ECDSA, trunks_utility_->Sign will return serialized TPM_SIGNATURE
  // instead of signal signature data.
  if (sign_algorithm == trunks::TPM_ALG_ECDSA) {
    trunks::TPMT_SIGNATURE tpm_signature;
    trunks::TPM_RC result =
        trunks::Parse_TPMT_SIGNATURE(signature, &tpm_signature, nullptr);
    if (result != trunks::TPM_RC_SUCCESS) {
      LOG(ERROR) << "Error when parse TPM signing result.";
      return -1;
    }
    *signature = SerializeFromTpmSignature(tpm_signature).value_or("");
  }
  return true;
}

bool TpmUtilityV2::CreateRestrictedKey(KeyType key_type,
                                       KeyUsage key_usage,
                                       std::string* public_key_der,
                                       std::string* public_key_tpm_format,
                                       std::string* private_key_blob) {
  if (key_usage != KEY_USAGE_SIGN) {
    LOG(ERROR) << __func__ << ": Not implemented.";
    return false;
  }

  std::unique_ptr<AuthorizationDelegate> empty_password_authorization =
      trunks_factory_->GetPasswordAuthorization(std::string());
  trunks::TPM_ALG_ID algorithm =
      (key_type == KEY_TYPE_RSA) ? trunks::TPM_ALG_RSA : trunks::TPM_ALG_ECC;
  TPM_RC result = trunks_utility_->CreateIdentityKey(
      algorithm, empty_password_authorization.get(), private_key_blob);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to create restricted key: "
               << trunks::GetErrorString(result);
    return false;
  }

  std::unique_ptr<trunks::BlobParser> parser = trunks_factory_->GetBlobParser();
  trunks::TPM2B_PUBLIC public_info;
  trunks::TPM2B_PRIVATE not_used;
  if (!parser->ParseKeyBlob(*private_key_blob, &public_info, &not_used)) {
    LOG(ERROR) << __func__ << ": Failed to parse key blob.";
    return false;
  }

  result = trunks::Serialize_TPMT_PUBLIC(public_info.public_area,
                                         public_key_tpm_format);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to serialize key public area: "
               << trunks::GetErrorString(result);
    return false;
  }

  switch (key_type) {
    case KEY_TYPE_RSA:
      *public_key_der = RSAPublicKeyToString(
          GetRsaPublicKeyFromTpmPublicArea(public_info.public_area));
      break;
    case KEY_TYPE_ECC:
      *public_key_der = EccSubjectPublicKeyInfoToString(
          GetEccPublicKeyFromTpmPublicArea(public_info.public_area));
      break;
  }
  if (public_key_der->empty()) {
    LOG(ERROR) << __func__ << ": Failed to convert public key to DER encoded";
    return false;
  }

  return true;
}

std::string TpmUtilityV2::GetPCRValueForMode(const std::string& mode) {
  std::string mode_digest = base::SHA1HashString(mode);
  mode_digest.resize(crypto::kSHA256Length);

  // PCR0 value immediately after power on.
  const std::string pcr_initial_value(crypto::kSHA256Length, 0);

  return crypto::SHA256HashString(pcr_initial_value + mode_digest);
}

bool TpmUtilityV2::QuotePCR(uint32_t pcr_index,
                            const std::string& key_blob,
                            std::string* quoted_pcr_value,
                            std::string* quoted_data,
                            std::string* quote) {
  TPM_RC result = trunks_utility_->ReadPCR(pcr_index, quoted_pcr_value);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to read PCR " << pcr_index << ": "
               << trunks::GetErrorString(result);
    return false;
  }

  std::unique_ptr<AuthorizationDelegate> empty_password_authorization =
      trunks_factory_->GetPasswordAuthorization(std::string());
  TPM_HANDLE key_handle;
  result = trunks_utility_->LoadKey(
      key_blob, empty_password_authorization.get(), &key_handle);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to load key: " << trunks::GetErrorString(result);
    return false;
  }

  TpmObjectScoper scoper(trunks_factory_, key_handle);
  std::string key_name;
  result = trunks_utility_->GetKeyName(key_handle, &key_name);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to get key name: "
               << trunks::GetErrorString(result);
    return false;
  }

  trunks::TPMT_PUBLIC public_area;
  result = trunks_utility_->GetKeyPublicArea(key_handle, &public_area);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to get key public data: "
               << trunks::GetErrorString(result);
    return false;
  }

  trunks::TPMT_SIG_SCHEME scheme;
  scheme.details.any.hash_alg = trunks::TPM_ALG_SHA256;
  switch (public_area.type) {
    case trunks::TPM_ALG_RSA:
      scheme.scheme = trunks::TPM_ALG_RSASSA;
      break;
    case trunks::TPM_ALG_ECC:
      scheme.scheme = trunks::TPM_ALG_ECDSA;
      break;
    default:
      LOG(ERROR) << __func__ << ": Unknown TPM key type of TPM handle.";
      return false;
  }

  // This process of selecting pcrs is highlighted in TPM 2.0 Library Spec
  // Part 2 (Section 10.5 - PCR structures).
  trunks::TPML_PCR_SELECTION pcr_selection;
  uint8_t pcr_select_index = pcr_index / 8;
  uint8_t pcr_select_byte = 1 << (pcr_index % 8);
  pcr_selection.count = 1;
  pcr_selection.pcr_selections[0].hash = trunks::TPM_ALG_SHA256;
  pcr_selection.pcr_selections[0].sizeof_select = PCR_SELECT_MIN;
  memset(pcr_selection.pcr_selections[0].pcr_select, 0, PCR_SELECT_MIN);
  pcr_selection.pcr_selections[0].pcr_select[pcr_select_index] =
      pcr_select_byte;
  trunks::TPM2B_ATTEST quoted_struct;
  trunks::TPMT_SIGNATURE signature;
  result = trunks_factory_->GetTpm()->QuoteSync(
      key_handle, key_name,
      trunks::Make_TPM2B_DATA("") /* No qualifying data */, scheme,
      pcr_selection, &quoted_struct, &signature,
      empty_password_authorization.get());
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to quote PCR " << pcr_index << ": "
               << trunks::GetErrorString(result);
    return false;
  }

  *quoted_data = StringFrom_TPM2B_ATTEST(quoted_struct);
  *quote = SerializeFromTpmSignature(signature).value_or("");
  return true;
}

// TODO(b/141520502): Verify the quote against expected output.
bool TpmUtilityV2::IsQuoteForPCR(const std::string& /* quoted_pcr_value */,
                                 const std::string& /* quoted_data */,
                                 const std::string& quote,
                                 uint32_t pcr_index) const {
  std::string buffer = quote;
  trunks::TPMS_ATTEST parsed_quote;
  TPM_RC result = trunks::Parse_TPMS_ATTEST(&buffer, &parsed_quote, nullptr);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to parse the quote: "
               << trunks::GetErrorString(result);
    return false;
  }
  if (parsed_quote.magic != trunks::TPM_GENERATED_VALUE) {
    LOG(ERROR) << __func__ << ": Bad magic value";
    return false;
  }
  if (parsed_quote.type != trunks::TPM_ST_ATTEST_QUOTE) {
    LOG(ERROR) << __func__ << ": Not a quote";
    return false;
  }
  trunks::TPML_PCR_SELECTION* pcr_select =
      &parsed_quote.attested.quote.pcr_select;
  if (pcr_select->count != 1) {
    LOG(ERROR) << __func__ << ": PCR selection count=" << pcr_select->count;
    return false;
  }
  uint32_t pcr_select_byte = pcr_index / 8;
  trunks::BYTE pcr_select_mask = 1 << (pcr_index % 8);
  trunks::TPMS_PCR_SELECTION* pcr_selection = pcr_select->pcr_selections;
  if (pcr_selection->sizeof_select <= pcr_select_byte) {
    LOG(ERROR) << __func__ << ": PCR selection is too short: "
               << pcr_selection->sizeof_select;
    return false;
  }
  int i;
  for (i = 0; i < pcr_selection->sizeof_select; ++i) {
    if (i == pcr_select_byte) {
      if (pcr_selection->pcr_select[i] != pcr_select_mask) {
        LOG(ERROR) << __func__ << ": wrong bits in PCR selection mask at " << i;
        return false;
      }
    } else {
      if (pcr_selection->pcr_select[i]) {
        LOG(ERROR) << __func__ << ": non-zero byte in PCR selection mask at "
                   << i;
        return false;
      }
    }
  }
  return true;
}

bool TpmUtilityV2::ReadPCR(uint32_t pcr_index, std::string* pcr_value) {
  TPM_RC result = trunks_utility_->ReadPCR(pcr_index, pcr_value);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to read PCR " << pcr_index << ": "
               << trunks::GetErrorString(result);
    return false;
  }
  return true;
}

bool TpmUtilityV2::GetNVDataSize(uint32_t nv_index, uint16_t* nv_size) const {
  trunks::TPMS_NV_PUBLIC public_data;
  if (trunks_utility_->GetNVSpacePublicArea(nv_index & ~trunks::HR_NV_INDEX,
                                            &public_data) != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to get NV space public area for index "
               << std::hex << nv_index << ".";
    return false;
  }
  *nv_size = public_data.data_size;
  return true;
}

bool TpmUtilityV2::CertifyNV(uint32_t nv_index,
                             int nv_size,
                             const std::string& key_blob,
                             std::string* quoted_data,
                             std::string* quote) {
  TPM_RC result;

  std::unique_ptr<AuthorizationDelegate> empty_password_authorization =
      trunks_factory_->GetPasswordAuthorization(std::string());

  MultipleAuthorizations authorization;
  authorization.AddAuthorizationDelegate(empty_password_authorization.get());
  authorization.AddAuthorizationDelegate(empty_password_authorization.get());

  TPM_HANDLE key_handle;
  result = trunks_utility_->LoadKey(
      key_blob, empty_password_authorization.get(), &key_handle);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to load key: " << trunks::GetErrorString(result);
    return false;
  }
  TpmObjectScoper scoper(trunks_factory_, key_handle);
  std::string key_name;
  result = trunks_utility_->GetKeyName(key_handle, &key_name);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to get key name: "
               << trunks::GetErrorString(result);
    return false;
  }

  trunks::TPMT_PUBLIC public_area;
  result = trunks_utility_->GetKeyPublicArea(key_handle, &public_area);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to get key public data: "
               << trunks::GetErrorString(result);
    return false;
  }

  trunks::TPMT_SIG_SCHEME scheme;
  scheme.details.any.hash_alg = trunks::TPM_ALG_SHA256;
  switch (public_area.type) {
    case trunks::TPM_ALG_RSA:
      scheme.scheme = trunks::TPM_ALG_RSASSA;
      break;
    case trunks::TPM_ALG_ECC:
      scheme.scheme = trunks::TPM_ALG_ECDSA;
      break;
    default:
      LOG(ERROR) << __func__ << ": Unknown TPM key type of TPM handle.";
      return false;
  }

  trunks::TPM2B_ATTEST quoted_struct;
  trunks::TPMT_SIGNATURE signature;
  result = trunks_factory_->GetTpm()->NV_CertifySync(
      key_handle,                   // sign_handle
      key_name,                     // sign_handle_name
      nv_index,                     // auth_handle
      "",                           // auth_handle_name
      nv_index,                     // nv_index
      "",                           // nv_index_name
      trunks::Make_TPM2B_DATA(""),  // qualifying data
      scheme,                       // in_scheme
      nv_size,                      // size to read
      0,                            // offset
      &quoted_struct, &signature, &authorization);

  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to certify the NVs: "
               << trunks::GetErrorString(result);
    return false;
  }

  *quoted_data = StringFrom_TPM2B_ATTEST(quoted_struct);
  *quote = SerializeFromTpmSignature(signature).value_or("");
  return true;
}

bool TpmUtilityV2::GetEndorsementKey(KeyType key_type, TPM_HANDLE* key_handle) {
  if (endorsement_keys_.count(key_type) > 0) {
    *key_handle = endorsement_keys_[key_type];
    return true;
  }
  std::string endorsement_password;
  if (!GetEndorsementPassword(&endorsement_password)) {
    return false;
  }
  std::unique_ptr<HmacSession> endorsement_session =
      trunks_factory_->GetHmacSession();
  TPM_RC result = endorsement_session->StartUnboundSession(
      true /* salted */, false /* enable_encryption */);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to setup endorsement session: "
               << trunks::GetErrorString(result);
    return false;
  }
  endorsement_session->SetEntityAuthorizationValue(endorsement_password);
  // Don't fail if the owner password is not available, it may not be needed.
  std::string owner_password;
  GetOwnerPassword(&owner_password);
  std::unique_ptr<HmacSession> owner_session =
      trunks_factory_->GetHmacSession();
  result = owner_session->StartUnboundSession(true /* salted */,
                                              false /* enable_encryption */);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to setup owner session: "
               << trunks::GetErrorString(result);
    return false;
  }
  owner_session->SetEntityAuthorizationValue(owner_password);
  trunks::TPM_ALG_ID algorithm =
      (key_type == KEY_TYPE_RSA) ? trunks::TPM_ALG_RSA : trunks::TPM_ALG_ECC;
  result = trunks_utility_->GetEndorsementKey(
      algorithm, endorsement_session->GetDelegate(),
      owner_session->GetDelegate(), key_handle);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to get endorsement key: "
               << trunks::GetErrorString(result);
    return false;
  }
  endorsement_keys_[key_type] = *key_handle;
  return true;
}

bool TpmUtilityV2::GetEndorsementPublicKeyModulus(KeyType key_type,
                                                  std::string* ekm) {
  if (key_type == KEY_TYPE_RSA) {
    return trunks_utility_->GetPublicRSAEndorsementKeyModulus(ekm) ==
           TPM_RC_SUCCESS;
  }

  LOG(ERROR) << __func__ << ": Not implemented.";
  return false;
}

bool TpmUtilityV2::CreateIdentity(KeyType key_type,
                                  AttestationDatabase::Identity* identity) {
  IdentityKey* key_pb = identity->mutable_identity_key();
  IdentityBinding* binding_pb = identity->mutable_identity_binding();
  if (!CreateRestrictedKey(key_type, KEY_USAGE_SIGN,
                           key_pb->mutable_identity_public_key_der(),
                           binding_pb->mutable_identity_public_key_tpm_format(),
                           key_pb->mutable_identity_key_blob())) {
    LOG(ERROR) << __func__ << ": Failed to create restricted key.";
    return false;
  }
  key_pb->set_identity_key_type(key_type);
  binding_pb->set_identity_public_key_der(key_pb->identity_public_key_der());
  return true;
}

bool TpmUtilityV2::GetRsuDeviceId(std::string* rsu_device_id) {
  return trunks_utility_->GetRsuDeviceId(rsu_device_id) ==
         trunks::TPM_RC_SUCCESS;
}

}  // namespace attestation
