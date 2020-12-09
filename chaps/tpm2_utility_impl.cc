// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include "chaps/chaps_utility.h"
#include "chaps/tpm2_utility_impl.h"

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/hash/sha1.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/optional.h>
#include <base/stl_util.h>
#include <crypto/libcrypto-compat.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/rsa.h>
#include <trunks/background_command_transceiver.h>
#include <trunks/command_transceiver.h>
#include <trunks/error_codes.h>
#include <trunks/tpm_generated.h>
#include <trunks/tpm_state.h>
#include <trunks/trunks_dbus_proxy.h>
#include <trunks/trunks_factory_impl.h>

using base::AutoLock;
using brillo::SecureBlob;
using std::map;
using std::set;
using trunks::kStorageRootKey;
using trunks::TPM_RC;
using trunks::TPM_RC_SUCCESS;
using trunks::TrunksFactory;
using ParsedDigestInfo = std::pair<trunks::TPM_ALG_ID, std::string>;

namespace {

constexpr struct {
  trunks::TPM_ALG_ID trunks_id;
  int openssl_nid;
} kSupportedECCurveAlgorithms[] = {
    {trunks::TPM_ECC_NIST_P256, NID_X9_62_prime256v1},
};

// Supported digest algorithms in TPM 2.0.
constexpr struct {
  trunks::TPM_ALG_ID id;
  int digest_length;  // in bytes
  chaps::DigestAlgorithm alg;
} kSupportedDigestAlgorithms[] = {
    {trunks::TPM_ALG_SHA1, SHA1_DIGEST_SIZE, chaps::DigestAlgorithm::SHA1},
    {trunks::TPM_ALG_SHA256, SHA256_DIGEST_SIZE,
     chaps::DigestAlgorithm::SHA256},
    {trunks::TPM_ALG_SHA384, SHA384_DIGEST_SIZE,
     chaps::DigestAlgorithm::SHA384},
    {trunks::TPM_ALG_SHA512, SHA512_DIGEST_SIZE,
     chaps::DigestAlgorithm::SHA512},
};

// Return the TPM algorithm ID for |digest_alg|. Return TPM_ALG_NULL for not
// supported algorithm by TPM 2.0.
trunks::TPM_ALG_ID DigestAlgorithmToTrunksAlgId(
    chaps::DigestAlgorithm digest_alg) {
  switch (digest_alg) {
    case chaps::DigestAlgorithm::SHA1:
      return trunks::TPM_ALG_SHA1;
    case chaps::DigestAlgorithm::SHA256:
      return trunks::TPM_ALG_SHA256;
    case chaps::DigestAlgorithm::SHA384:
      return trunks::TPM_ALG_SHA384;
    case chaps::DigestAlgorithm::SHA512:
      return trunks::TPM_ALG_SHA512;

    // Unknown algorithm - use "padding-only" signing scheme.
    case chaps::DigestAlgorithm::MD5:
    case chaps::DigestAlgorithm::NoDigest:
      return trunks::TPM_ALG_NULL;
  }
}

// Check the |input| is <digest_info><digest> form. If so, return the matched
// trunks algorithm ID and the digest.
base::Optional<ParsedDigestInfo> ParseDigestInfo(const std::string& input) {
  for (const auto& algorithm_info : kSupportedDigestAlgorithms) {
    const std::string& digest_info =
        GetDigestAlgorithmEncoding(algorithm_info.alg);

    if (input.size() == digest_info.size() + algorithm_info.digest_length &&
        input.compare(0, digest_info.size(), digest_info) == 0) {
      return std::make_pair(algorithm_info.id,
                            input.substr(digest_info.size()));
    }
  }
  return base::nullopt;
}

uint32_t GetIntegerExponent(const std::string& public_exponent) {
  uint32_t exponent = 0;
  for (size_t i = 0; i < public_exponent.size(); i++) {
    exponent = exponent << 8;
    exponent += public_exponent[i];
  }
  return exponent;
}

bool AddPKCS1Padding(const std::string& input,
                     size_t size,
                     std::string* result) {
  if (input.size() + 11 > size) {
    LOG(ERROR) << "Error adding PKCS1 padding: message too long: "
               << input.size() << " (target size " << size << ")";
    return false;
  }
  result->assign("\x00\x01", 2);
  result->append(size - input.size() - 3, '\xff');
  result->append("\x00", 1);
  result->append(input);
  return true;
}

void InitTransceiver(trunks::CommandTransceiver* transceiver, bool* success) {
  *success = transceiver->Init();
  if (!*success) {
    LOG(ERROR) << "Error initializing transceiver.";
  }
}

void TermTransceiver(std::unique_ptr<trunks::CommandTransceiver> transceiver) {
  transceiver.reset();
}

trunks::TPMI_ECC_CURVE ConvertNIDToTrunksCurveID(int curve_nid) {
  for (auto curve_info : kSupportedECCurveAlgorithms) {
    if (curve_info.openssl_nid == curve_nid) {
      return curve_info.trunks_id;
    }
  }
  return trunks::TPM_ECC_NONE;
}

int ConvertTrunksCurveIDToNID(trunks::TPMI_ECC_CURVE trunks_id) {
  for (auto curve_info : kSupportedECCurveAlgorithms) {
    if (curve_info.trunks_id == trunks_id) {
      return curve_info.openssl_nid;
    }
  }
  return NID_undef;
}

// TPM format parse utils
crypto::ScopedEC_KEY GetECCPublicKeyFromTpmPublicArea(
    trunks::TPMT_PUBLIC public_area) {
  CHECK_EQ(public_area.type, trunks::TPM_ALG_ECC);

  int nid =
      ConvertTrunksCurveIDToNID(public_area.parameters.ecc_detail.curve_id);
  if (nid == NID_undef) {
    LOG(ERROR) << __func__ << "The trunks curve_id is unknown.";
    return nullptr;
  }

  crypto::ScopedEC_Key ecc(EC_KEY_new_by_curve_name(nid));
  if (!ecc) {
    LOG(ERROR) << "Failed to create EC_KEY from curve name " << nid << ".";
    return nullptr;
  }

  std::string xs = StringFrom_TPM2B_ECC_PARAMETER(public_area.unique.ecc.x);
  std::string ys = StringFrom_TPM2B_ECC_PARAMETER(public_area.unique.ecc.y);

  crypto::ScopedBIGNUM x(BN_new()), y(BN_new());
  if (!x || !y) {
    LOG(ERROR) << "Failed to allocate BIGNUM.";
    return nullptr;
  }

  if (!chaps::ConvertToBIGNUM(xs, x.get()) ||
      !chaps::ConvertToBIGNUM(ys, y.get())) {
    LOG(ERROR) << "Failed to convert to BIGNUM.";
    return nullptr;
  }

  // EC_KEY_set_public_key_affine_coordinates will check the pointer is valid
  if (!EC_KEY_set_public_key_affine_coordinates(ecc.get(), x.get(), y.get()))
    return nullptr;

  return ecc;
}

}  // namespace

namespace chaps {

class ScopedSession {
 public:
#ifndef CHAPS_TPM2_USE_PER_OP_SESSIONS
  ScopedSession(trunks::TrunksFactory* factory,
                std::unique_ptr<trunks::HmacSession>* session) {}
#else
  ScopedSession(trunks::TrunksFactory* factory,
                std::unique_ptr<trunks::HmacSession>* session) {
    target_session_ = session;
    if (*target_session_) {
      LOG(ERROR) << "Concurrent sessions";
    }
    std::unique_ptr<trunks::HmacSession> new_session =
        factory->GetHmacSession();
    TPM_RC result = new_session->StartUnboundSession(
        false /* salted */, false /* enable_encryption */);
    if (result != TPM_RC_SUCCESS) {
      LOG(ERROR) << "Error starting an AuthorizationSession: "
                 << trunks::GetErrorString(result);
      LOG_IF(FATAL, result == trunks::SAPI_RC_NO_CONNECTION)
          << "Fatal failure - opening session failed due to TPM daemon "
             "unavailability.";
      *target_session_ = nullptr;
    } else {
      *target_session_ = std::move(new_session);
    }
  }
  ~ScopedSession() { *target_session_ = nullptr; }

 private:
  std::unique_ptr<trunks::HmacSession>* target_session_;
#endif
};

TPM2UtilityImpl::TPM2UtilityImpl()
    : default_factory_(new trunks::TrunksFactoryImpl()),
      factory_(default_factory_.get()) {
  if (!default_factory_->Initialize()) {
    LOG(ERROR) << "Unable to initialize trunks.";
    return;
  }
#ifndef CHAPS_TPM2_USE_PER_OP_SESSIONS
  session_ = factory_->GetHmacSession();
#endif
  trunks_tpm_utility_ = factory_->GetTpmUtility();
}

TPM2UtilityImpl::TPM2UtilityImpl(
    const scoped_refptr<base::SingleThreadTaskRunner>& task_runner)
    : task_runner_(task_runner),
      default_trunks_proxy_(new trunks::TrunksDBusProxy) {
  task_runner->PostNonNestableTask(
      FROM_HERE, base::Bind(&InitTransceiver,
                            base::Unretained(default_trunks_proxy_.get()),
                            base::Unretained(&is_trunks_proxy_initialized_)));
  // We stitch the transceivers together. The call chain is:
  // ChapsTPMUtility --> TrunksFactory --> BackgroundCommandTransceiver -->
  // TrunksProxy
  default_background_transceiver_.reset(
      new trunks::BackgroundCommandTransceiver(default_trunks_proxy_.get(),
                                               task_runner));
  default_factory_.reset(
      new trunks::TrunksFactoryImpl(default_background_transceiver_.get()));
  CHECK(default_factory_->Initialize());
  factory_ = default_factory_.get();
#ifndef CHAPS_TPM2_USE_PER_OP_SESSIONS
  session_ = factory_->GetHmacSession();
#endif
  trunks_tpm_utility_ = factory_->GetTpmUtility();
}

TPM2UtilityImpl::TPM2UtilityImpl(TrunksFactory* factory)
    : factory_(factory),
#ifndef CHAPS_TPM2_USE_PER_OP_SESSIONS
      session_(factory_->GetHmacSession()),
#endif
      trunks_tpm_utility_(factory_->GetTpmUtility()) {
}

TPM2UtilityImpl::~TPM2UtilityImpl() {
  for (const auto& it : slot_handles_) {
    set<int> slot_handles = it.second;
    for (const auto& it2 : slot_handles) {
      if (factory_->GetTpm()->FlushContextSync(it2, NULL) != TPM_RC_SUCCESS) {
        LOG(WARNING) << "Error flushing handle: " << it2;
      }
    }
  }

  // If we have a task runner, then that was the task runner used to initialize
  // the |default_trunks_proxy_|. Destroy the proxy on that task runner to
  // satisfy threading restrictions.
  if (task_runner_) {
    default_factory_.reset();
    default_background_transceiver_.reset();
    // TODO(ejcaruso): replace with DeleteSoon when libchrome has the unique_ptr
    // specialization after the uprev
    task_runner_->PostNonNestableTask(
        FROM_HERE,
        base::Bind(&TermTransceiver, base::Passed(&default_trunks_proxy_)));
  }
}

bool TPM2UtilityImpl::Init() {
  AutoLock lock(lock_);
  std::unique_ptr<trunks::TpmState> tpm_state = factory_->GetTpmState();
  TPM_RC result;
  result = tpm_state->Initialize();
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting TPM state information: "
               << trunks::GetErrorString(result);
    LOG_IF(FATAL, result == trunks::SAPI_RC_NO_CONNECTION &&
                      is_trunks_proxy_initialized_)
        << "Fatal failure - initialization failed due to TPM daemon becoming "
           "unavailable.";
    return false;
  }
  // Check if firmware initialized the platform hierarchy.
  if (tpm_state->IsPlatformHierarchyEnabled()) {
    LOG(ERROR) << "Platform initialization not complete.";
    return false;
  }
  // Check if ownership is taken. If not, TPMUtility initialization fails.
  if (!tpm_state->IsOwnerPasswordSet() ||
      !tpm_state->IsEndorsementPasswordSet() ||
      !tpm_state->IsLockoutPasswordSet()) {
    LOG(ERROR) << "TPM2Utility cannot be ready if the TPM is not owned.";
    return false;
  }
#ifndef CHAPS_TPM2_USE_PER_OP_SESSIONS
  result = session_->StartUnboundSession(false /* salted */,
                                         false /* enable_encryption */);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting an AuthorizationSession: "
               << trunks::GetErrorString(result);
    LOG_IF(FATAL, result == trunks::SAPI_RC_NO_CONNECTION &&
                      is_trunks_proxy_initialized_)
        << "Fatal failure - initialization failed due to TPM daemon becoming "
           "unavailable.";
    return false;
  }
#endif
  is_initialized_ = true;
  return true;
}

bool TPM2UtilityImpl::IsTPMAvailable() {
  AutoLock lock(lock_);
  if (is_enabled_ready_) {
    return is_enabled_;
  }
  // If the TPM works, it is available.
  if (is_initialized_) {
    is_enabled_ready_ = true;
    is_enabled_ = true;
    return true;
  }
  std::unique_ptr<trunks::TpmState> tpm_state = factory_->GetTpmState();
  TPM_RC result = tpm_state->Initialize();
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting TPM state information: "
               << trunks::GetErrorString(result);
    LOG_IF(FATAL, result == trunks::SAPI_RC_NO_CONNECTION &&
                      is_trunks_proxy_initialized_)
        << "Fatal failure - initialization failed due to TPM daemon becoming "
           "unavailable.";
    return false;
  }
  is_enabled_ = tpm_state->IsEnabled();
  is_enabled_ready_ = true;
  return is_enabled_;
}

TPMVersion TPM2UtilityImpl::GetTPMVersion() {
  return TPMVersion::TPM2_0;
}

bool TPM2UtilityImpl::Authenticate(int slot_id,
                                   const SecureBlob& auth_data,
                                   const std::string& auth_key_blob,
                                   const std::string& encrypted_master_key,
                                   SecureBlob* master_key) {
  CHECK(master_key);
  AutoLock lock(lock_);
  int key_handle = 0;
  if (!LoadKeyWithParentInternal(slot_id, auth_key_blob, auth_data,
                                 kStorageRootKey, &key_handle)) {
    return false;
  }
  std::string master_key_str;
  if (!UnbindInternal(key_handle, encrypted_master_key, &master_key_str)) {
    return false;
  }
  *master_key = SecureBlob(master_key_str);
  master_key_str.clear();
  return true;
}

bool TPM2UtilityImpl::ChangeAuthData(int slot_id,
                                     const SecureBlob& old_auth_data,
                                     const SecureBlob& new_auth_data,
                                     const std::string& old_auth_key_blob,
                                     std::string* new_auth_key_blob) {
  AutoLock lock(lock_);
  int key_handle;
  if (new_auth_data.size() > SHA256_DIGEST_SIZE) {
    LOG(ERROR) << "Authorization cannot be larger than SHA256 Digest size.";
    return false;
  }
  if (!LoadKeyWithParentInternal(slot_id, old_auth_key_blob, old_auth_data,
                                 kStorageRootKey, &key_handle)) {
    LOG(ERROR) << "Error loading key under old authorization data.";
    return false;
  }
  ScopedSession session_scope(factory_, &session_);
  if (!session_) {
    return false;
  }
  session_->SetEntityAuthorizationValue(old_auth_data.to_string());
  TPM_RC result = trunks_tpm_utility_->ChangeKeyAuthorizationData(
      key_handle, new_auth_data.to_string(), session_->GetDelegate(),
      new_auth_key_blob);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error changing authorization data: "
               << trunks::GetErrorString(result);
    return false;
  }
  result = factory_->GetTpm()->FlushContextSync(key_handle, NULL);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error unloading key under old authorization: "
               << trunks::GetErrorString(result);
    return false;
  }
  slot_handles_[slot_id].erase(key_handle);
  FlushHandle(key_handle);
  return true;
}

bool TPM2UtilityImpl::GenerateRandom(int num_bytes, std::string* random_data) {
  AutoLock lock(lock_);
  TPM_RC result =
      trunks_tpm_utility_->GenerateRandom(num_bytes, nullptr, random_data);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error generating random data from the TPM: "
               << trunks::GetErrorString(result);
    return false;
  }
  return true;
}

bool TPM2UtilityImpl::StirRandom(const std::string& entropy_data) {
  AutoLock lock(lock_);
  TPM_RC result = trunks_tpm_utility_->StirRandom(entropy_data, nullptr);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error seeding TPM random number generator: "
               << trunks::GetErrorString(result);
    return false;
  }
  return true;
}

bool TPM2UtilityImpl::GenerateRSAKey(int slot,
                                     int modulus_bits,
                                     const std::string& public_exponent,
                                     const SecureBlob& auth_data,
                                     std::string* key_blob,
                                     int* key_handle) {
  AutoLock lock(lock_);
  if (public_exponent.size() > 4) {
    LOG(ERROR) << "Incorrectly formatted public_exponent.";
    return false;
  }
  if (auth_data.size() > SHA256_DIGEST_SIZE) {
    LOG(ERROR) << "Authorization cannot be larger than SHA256 Digest size.";
    return false;
  }
  if (modulus_bits < static_cast<int>(kMinModulusSize)) {
    LOG(ERROR) << "Minimum modulus size is: " << kMinModulusSize;
    return false;
  }
  ScopedSession session_scope(factory_, &session_);
  if (!session_) {
    return false;
  }
  session_->SetEntityAuthorizationValue("");  // SRK Authorization Value.
  TPM_RC result = trunks_tpm_utility_->CreateRSAKeyPair(
      trunks::TpmUtility::AsymmetricKeyUsage::kDecryptAndSignKey, modulus_bits,
      GetIntegerExponent(public_exponent), auth_data.to_string(),
      "",                       // Policy Digest
      false,                    // use_only_policy_authorization
      std::vector<uint32_t>(),  // creation_pcr_indexes
      session_->GetDelegate(), key_blob, nullptr);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error creating RSA key pair: "
               << trunks::GetErrorString(result);
    return false;
  }
  if (!LoadKeyWithParentInternal(slot, *key_blob, auth_data, kStorageRootKey,
                                 key_handle)) {
    return false;
  }
  return true;
}

bool TPM2UtilityImpl::GetRSAPublicKey(int key_handle,
                                      std::string* public_exponent,
                                      std::string* modulus) {
  AutoLock lock(lock_);
  trunks::TPMT_PUBLIC public_data;
  TPM_RC result =
      trunks_tpm_utility_->GetKeyPublicArea(key_handle, &public_data);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting key public data: " << result;
    return false;
  }
  public_exponent->clear();
  result = trunks::Serialize_UINT32(public_data.parameters.rsa_detail.exponent,
                                    public_exponent);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error serializing public exponent: " << result;
    return false;
  }
  modulus->assign(StringFrom_TPM2B_PUBLIC_KEY_RSA(public_data.unique.rsa));
  return true;
}

bool TPM2UtilityImpl::IsECCurveSupported(int curve_nid) {
  return ConvertNIDToTrunksCurveID(curve_nid) != trunks::TPM_ECC_NONE;
}

bool TPM2UtilityImpl::GenerateECCKey(int slot,
                                     int nid,
                                     const SecureBlob& auth_data,
                                     std::string* key_blob,
                                     int* key_handle) {
  AutoLock lock(lock_);
  if (!IsECCurveSupported(nid)) {
    LOG(ERROR) << "Not supported NID";
    return false;
  }
  if (auth_data.size() > SHA256_DIGEST_SIZE) {
    LOG(ERROR) << "Authorization cannot be larger than SHA256 Digest size.";
    return false;
  }

  ScopedSession session_scope(factory_, &session_);
  if (!session_) {
    return false;
  }
  session_->SetEntityAuthorizationValue("");  // SRK Authorization Value.
  TPM_RC result = trunks_tpm_utility_->CreateECCKeyPair(
      trunks::TpmUtility::AsymmetricKeyUsage::kDecryptAndSignKey,
      ConvertNIDToTrunksCurveID(nid), auth_data.to_string(),
      "",                       // Policy Digest
      false,                    // use_only_policy_authorization
      std::vector<uint32_t>(),  // creation_pcr_indexes
      session_->GetDelegate(), key_blob, nullptr);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error creating ECC key pair: "
               << trunks::GetErrorString(result);
    return false;
  }
  if (!LoadKeyWithParentInternal(slot, *key_blob, auth_data, kStorageRootKey,
                                 key_handle)) {
    return false;
  }
  return true;
}

bool TPM2UtilityImpl::GetECCPublicKey(int key_handle, std::string* ec_point) {
  AutoLock lock(lock_);
  trunks::TPMT_PUBLIC public_area;
  TPM_RC result =
      trunks_tpm_utility_->GetKeyPublicArea(key_handle, &public_area);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Error getting key public data: " << result;
    return false;
  }

  if (public_area.type != trunks::TPM_ALG_ECC) {
    LOG(ERROR) << __func__ << ": Keyhandle is not ECC key.";
    return false;
  }

  crypto::ScopedEC_KEY key = GetECCPublicKeyFromTpmPublicArea(public_area);
  if (key == nullptr) {
    LOG(ERROR) << __func__ << ": Parse key fail.";
    return false;
  }

  *ec_point = GetECPointAsString(key.get());

  return true;
}

bool TPM2UtilityImpl::WrapRSAKey(int slot,
                                 const std::string& public_exponent,
                                 const std::string& modulus,
                                 const std::string& prime_factor,
                                 const SecureBlob& auth_data,
                                 std::string* key_blob,
                                 int* key_handle) {
  AutoLock lock(lock_);
  if (public_exponent.size() > 4) {
    LOG(ERROR) << "Incorrectly formatted public_exponent.";
    return false;
  }
  if (auth_data.size() > SHA256_DIGEST_SIZE) {
    LOG(ERROR) << "Authorization cannot be larger than SHA256 Digest size.";
    return false;
  }
  if (modulus.size() < kMinModulusSize) {
    LOG(ERROR) << "Minimum modulus size is: " << kMinModulusSize;
    return false;
  }
  ScopedSession session_scope(factory_, &session_);
  if (!session_) {
    return false;
  }
  session_->SetEntityAuthorizationValue("");  // SRK Authorization Value.
  TPM_RC result = trunks_tpm_utility_->ImportRSAKey(
      trunks::TpmUtility::AsymmetricKeyUsage::kDecryptAndSignKey, modulus,
      GetIntegerExponent(public_exponent), prime_factor, auth_data.to_string(),
      session_->GetDelegate(), key_blob);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error importing RSA key to TPM: "
               << trunks::GetErrorString(result);
    return false;
  }
  if (!LoadKeyWithParentInternal(slot, *key_blob, auth_data, kStorageRootKey,
                                 key_handle)) {
    return false;
  }
  return true;
}

bool TPM2UtilityImpl::WrapECCKey(int slot,
                                 int curve_nid,
                                 const std::string& public_point_x,
                                 const std::string& public_point_y,
                                 const std::string& private_value,
                                 const brillo::SecureBlob& auth_data,
                                 std::string* key_blob,
                                 int* key_handle) {
  AutoLock lock(lock_);

  ScopedSession session_scope(factory_, &session_);
  if (!session_) {
    return false;
  }

  session_->SetEntityAuthorizationValue("");  // SRK Authorization Value.
  TPM_RC result = trunks_tpm_utility_->ImportECCKey(
      trunks::TpmUtility::AsymmetricKeyUsage::kDecryptAndSignKey,
      ConvertNIDToTrunksCurveID(curve_nid), public_point_x, public_point_y,
      private_value, auth_data.to_string(), session_->GetDelegate(), key_blob);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error importing ECC key to TPM: "
               << trunks::GetErrorString(result);
    return false;
  }

  if (!LoadKeyWithParentInternal(slot, *key_blob, auth_data, kStorageRootKey,
                                 key_handle)) {
    return false;
  }
  return true;
}

bool TPM2UtilityImpl::LoadKey(int slot,
                              const std::string& key_blob,
                              const SecureBlob& auth_data,
                              int* key_handle) {
  AutoLock lock(lock_);
  return LoadKeyWithParentInternal(slot, key_blob, auth_data, kStorageRootKey,
                                   key_handle);
}

bool TPM2UtilityImpl::LoadKeyWithParent(int slot,
                                        const std::string& key_blob,
                                        const SecureBlob& auth_data,
                                        int parent_key_handle,
                                        int* key_handle) {
  AutoLock lock(lock_);
  return LoadKeyWithParentInternal(slot, key_blob, auth_data, parent_key_handle,
                                   key_handle);
}

void TPM2UtilityImpl::UnloadKeysForSlot(int slot) {
  AutoLock Lock(lock_);
  for (const auto& it : slot_handles_[slot]) {
    if (factory_->GetTpm()->FlushContextSync(it, NULL) != TPM_RC_SUCCESS) {
      LOG(WARNING) << "Error flushing handle: " << it;
    }
    FlushHandle(it);
  }
  slot_handles_.erase(slot);
}

crypto::ScopedRSA TPM2UtilityImpl::PublicAreaToScopedRsa(
    const trunks::TPMT_PUBLIC& public_data) {
  if (public_data.type != trunks::TPM_ALG_RSA) {
    LOG(ERROR)
        << "Fail to convert public area of non RSA key to ScopedRSA object.";
    return nullptr;
  }

  // Extract modulus and exponent from public_data.
  std::string modulus;
  std::string exponent;
  modulus.assign(StringFrom_TPM2B_PUBLIC_KEY_RSA(public_data.unique.rsa));
  TPM_RC result = trunks::Serialize_UINT32(
      public_data.parameters.rsa_detail.exponent, &exponent);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error serializing public exponent: " << result;
    return nullptr;
  }

  return NumberToScopedRsa(modulus, exponent);
}

crypto::ScopedRSA TPM2UtilityImpl::KeyToScopedRsa(int key_handle) {
  std::string modulus;
  std::string exponent;
  if (!GetRSAPublicKey(key_handle, &exponent, &modulus)) {
    return nullptr;
  }
  return NumberToScopedRsa(modulus, exponent);
}

bool TPM2UtilityImpl::Bind(int key_handle,
                           const std::string& input,
                           std::string* output) {
  CHECK(output);

  crypto::ScopedRSA rsa = KeyToScopedRsa(key_handle);
  if (!rsa) {
    LOG(ERROR) << "Failed to convert TPM key to Public RSA object.";
    return false;
  }

  if (input.size() > RSA_size(rsa.get()) - 11) {
    LOG(ERROR) << "Encryption plaintext is longer than RSA modulus.";
    return false;
  }

  // RSA encrypt output should be size of the modulus.
  output->resize(RSA_size(rsa.get()));
  int rsa_result = RSA_public_encrypt(
      input.size(), reinterpret_cast<const unsigned char*>(input.data()),
      reinterpret_cast<unsigned char*>(base::data(*output)), rsa.get(),
      RSA_PKCS1_PADDING);
  if (rsa_result == -1) {
    LOG(ERROR) << "Error performing RSA_public_encrypt.";
    return false;
  }
  return true;
}

bool TPM2UtilityImpl::Unbind(int key_handle,
                             const std::string& input,
                             std::string* output) {
  AutoLock lock(lock_);
  return UnbindInternal(key_handle, input, output);
}

bool TPM2UtilityImpl::Sign(int key_handle,
                           CK_MECHANISM_TYPE signing_mechanism,
                           const std::string& mechanism_parameter,
                           const std::string& input,
                           std::string* signature) {
  AutoLock Lock(lock_);

  // Parse the various parameters for this method.
  DigestAlgorithm digest_algorithm = GetDigestAlgorithm(signing_mechanism);
  // Parse RSA PSS Parameters if applicable.
  const RsaPaddingScheme padding_scheme =
      GetSigningSchemeForMechanism(signing_mechanism);
  const CK_RSA_PKCS_PSS_PARAMS* pss_params = nullptr;
  const EVP_MD* mgf1_hash = nullptr;
  if (padding_scheme == RsaPaddingScheme::RSASSA_PSS) {
    // Check the parameters
    if (!ParseRSAPSSParams(signing_mechanism, mechanism_parameter, &pss_params,
                           &mgf1_hash, &digest_algorithm)) {
      LOG(ERROR) << "Failed to parse RSA PSS parameters in TPM2 Sign().";
      return false;
    }
  }

  trunks::TPM_ALG_ID digest_alg_id =
      DigestAlgorithmToTrunksAlgId(digest_algorithm);

  // Setup the TPM Session.
  std::string auth_data = handle_auth_data_[key_handle].to_string();
  ScopedSession session_scope(factory_, &session_);
  if (!session_) {
    return false;
  }
  session_->SetEntityAuthorizationValue(auth_data);
  trunks::TPMT_PUBLIC public_area;
  TPM_RC result =
      trunks_tpm_utility_->GetKeyPublicArea(key_handle, &public_area);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting key public data: "
               << trunks::GetErrorString(result);
    return false;
  }

  if (public_area.type == trunks::TPM_ALG_RSA) {
    // In PKCS1.5 of RSASSA, the signed data will be
    //    <DigestInfo encoding><input><padding>
    // where <input> is usually a digest
    //
    // 1. If decryption is allowed for the key, we will add DigestInfo and
    // padding in software. Then, perform raw RSA on TPM by sending Decrypt
    // command with NULL scheme.
    // 2. Otherwise, send Sign command to the TPM.
    //
    // This is done to work with TPMs that don't support all required hashing
    // algorithms, and for which the Decrypt attribute is set for signing keys.
    if (public_area.object_attributes & trunks::kDecrypt) {
      // We can handle the padding here in software.
      std::string padded_data;
      if (padding_scheme == RsaPaddingScheme::RSASSA_PKCS1_V1_5) {
        if (!AddPKCS1Padding(
                GetDigestAlgorithmEncoding(digest_algorithm) + input,
                public_area.unique.rsa.size, &padded_data)) {
          return false;
        }
      } else if (padding_scheme == RsaPaddingScheme::RSASSA_PSS) {
        // Add padding with openssl
        DCHECK(pss_params);
        DCHECK(mgf1_hash);
        crypto::ScopedRSA rsa = PublicAreaToScopedRsa(public_area);
        if (!rsa) {
          LOG(ERROR) << "Failed to get public key for TPM2 RSA PSS Sign().";
          return false;
        }
        padded_data.resize(RSA_size(rsa.get()));
        if (RSA_padding_add_PKCS1_PSS_mgf1(
                rsa.get(),
                reinterpret_cast<unsigned char*>(base::data(padded_data)),
                reinterpret_cast<const unsigned char*>(base::data(input)),
                GetOpenSSLDigest(digest_algorithm), mgf1_hash,
                pss_params->sLen) != 1) {
          LOG(ERROR)
              << "Failed to produce the PSA PSS paddings in TPM2 Sign().";
          return false;
        }
      }

      result = trunks_tpm_utility_->AsymmetricDecrypt(
          key_handle, trunks::TPM_ALG_NULL, trunks::TPM_ALG_NULL, padded_data,
          session_->GetDelegate(), signature);
    } else {
      std::string data_to_sign;

      // We are using TPM_ALG_RSASSA, and only the mechanisms below match.
      if (padding_scheme == RsaPaddingScheme::RSASSA_PKCS1_V1_5) {
        if (digest_algorithm == DigestAlgorithm::NoDigest) {
          // 2-1. For CKM_RSA_PKCS, digest type is NoDigest, but PKCS11 API
          //      caller may pass the input with prepended DigestInfo. If it
          //      can be recognized as TPM supported algorithm, strip off the
          //      prepended DigestInfo and consider it as 2-3. If not, keep
          //      pass the raw input.
          base::Optional<ParsedDigestInfo> parsed = ParseDigestInfo(input);
          if (parsed != base::nullopt) {
            digest_alg_id = parsed.value().first;
            data_to_sign = parsed.value().second;
          } else {
            digest_alg_id = trunks::TPM_ALG_NULL;
            data_to_sign = input;
          }
        } else if (digest_alg_id == trunks::TPM_ALG_NULL) {
          // 2-2. If TPM doesn't support the digest type (ex. MD5), we need to
          //      prepend DigestInfo and then call TPM Sign with NULL scheme
          //      to sign and pad.
          data_to_sign = GetDigestAlgorithmEncoding(digest_algorithm) + input;
        } else {
          // 2-3. If TPM supported the digest type, we will send the digest
          //      |input| to TPM. TPM will do both prepending DigestInfo and
          //      PKCS1 padding.
          data_to_sign = input;
        }

        result = trunks_tpm_utility_->Sign(key_handle, trunks::TPM_ALG_RSASSA,
                                           digest_alg_id, data_to_sign,
                                           false /* don't generate hash */,
                                           session_->GetDelegate(), signature);
      } else if (padding_scheme == RsaPaddingScheme::RSASSA_PSS) {
        if (digest_alg_id == trunks::TPM_ALG_NULL) {
          // If the TPM doesn't support the hash algorithm, then it's going to
          // fail. RSA PSS doesn't work with TPM_ALG_NULL.
          LOG(ERROR) << "Unsupported hash combo of mechanism "
                     << signing_mechanism << " and hash "
                     << static_cast<int>(digest_algorithm);
          return false;
        }
        int expected_size = EVP_MD_size(GetOpenSSLDigest(digest_algorithm));
        if (expected_size != input.size()) {
          LOG(ERROR) << "Size mismatch for RSA PSS Sign() for sign only TPMv2 "
                        "Key. Expected "
                     << expected_size << ", actual " << input.size();
          return false;
        }
        if (mgf1_hash != GetOpenSSLDigest(digest_algorithm)) {
          LOG(ERROR) << "RSA PSS Sign() for sign only TPMv2 Key doesn't "
                        "support difference in MGF1 hash algorithm and signing "
                        "hash algorithm, MGF: "
                     << pss_params->mgf << ", Signing Hash Alg: "
                     << static_cast<int>(digest_algorithm);
          return false;
        }
        int max_sLen = public_area.unique.rsa.size -
                       EVP_MD_size(GetOpenSSLDigest(digest_algorithm)) - 2;
        if (pss_params->sLen != max_sLen) {
          // Note: The reason why this is not fatal is because most of the time,
          // sLen is not maximized, but commonly set to the digest size, and we
          // shouldn't make the common case fail. Also, during verification,
          // sLen can be recovered, so the problem caused by using a different
          // sLen is limited.
          LOG(WARNING) << "TPMv2 only support RSA PSS sLen = " << max_sLen
                       << " for RSA " << public_area.unique.rsa.size
                       << "bit key, but sLen = " << pss_params->sLen
                       << ". Proceed to sign anyway.";
        }
        result = trunks_tpm_utility_->Sign(key_handle, trunks::TPM_ALG_RSAPSS,
                                           digest_alg_id, input,
                                           false /* don't generate hash */,
                                           session_->GetDelegate(), signature);
      } else {
        LOG(ERROR) << "Unsupported signing mechanism for tpm2 rsa key "
                   << signing_mechanism;
        return false;
      }
    }
    if (result != TPM_RC_SUCCESS) {
      LOG(ERROR) << "Error performing sign operation: "
                 << trunks::GetErrorString(result);
      return false;
    }
  } else if (public_area.type == trunks::TPM_ALG_ECC) {
    // We are using TPM_ALG_ECDSA, and only the mechanisms below match.
    if (!(signing_mechanism == CKM_ECDSA ||
          signing_mechanism == CKM_ECDSA_SHA1)) {
      LOG(ERROR) << "Unsupported signing mechanism for tpm2 ecc key "
                 << signing_mechanism;
      return false;
    }

    result = trunks_tpm_utility_->Sign(
        key_handle, trunks::TPM_ALG_ECDSA, digest_alg_id, input,
        false /* don't generate hash */, session_->GetDelegate(), signature);
    if (result != TPM_RC_SUCCESS) {
      LOG(ERROR) << "Error performing sign operation: "
                 << trunks::GetErrorString(result);
      return false;
    }

    // Transform TPM format to PKCS#11 format
    trunks::TPMT_SIGNATURE tpm_signature;
    result = trunks::Parse_TPMT_SIGNATURE(signature, &tpm_signature, nullptr);
    if (result != TPM_RC_SUCCESS) {
      LOG(ERROR) << "Error when parse TPM signing result.";
      return false;
    }

    std::string rs = ConvertByteBufferToString(
        tpm_signature.signature.ecdsa.signature_r.buffer,
        tpm_signature.signature.ecdsa.signature_r.size);
    std::string ss = ConvertByteBufferToString(
        tpm_signature.signature.ecdsa.signature_s.buffer,
        tpm_signature.signature.ecdsa.signature_s.size);

    // PKCS#11 ECDSA format is the concation of r and s (r|s).
    *signature = rs + ss;
  } else {
    LOG(ERROR) << __func__ << ": Unsupport TPM key type: " << public_area.type;
    return false;
  }
  return true;
}

bool TPM2UtilityImpl::IsSRKReady() {
  return IsTPMAvailable() && Init();
}

bool TPM2UtilityImpl::LoadKeyWithParentInternal(int slot,
                                                const std::string& key_blob,
                                                const SecureBlob& auth_data,
                                                int parent_key_handle,
                                                int* key_handle) {
  CHECK_EQ(parent_key_handle, static_cast<int>(kStorageRootKey))
      << "Chaps with TPM2.0 only loads keys under the RSA SRK.";
  if (auth_data.size() > SHA256_DIGEST_SIZE) {
    LOG(ERROR) << "Authorization cannot be larger than SHA256 Digest size.";
    return false;
  }
  ScopedSession session_scope(factory_, &session_);
  if (!session_) {
    return false;
  }
  session_->SetEntityAuthorizationValue("");  // SRK Authorization Value.
  TPM_RC result = trunks_tpm_utility_->LoadKey(
      key_blob, session_->GetDelegate(),
      reinterpret_cast<trunks::TPM_HANDLE*>(key_handle));
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error loading key into TPM: "
               << trunks::GetErrorString(result);
    LOG_IF(FATAL, result == trunks::SAPI_RC_NO_CONNECTION)
        << "Fatal failure - key loading failed due to TPM daemon "
           "unavailability.";
    return false;
  }
  std::string key_name;
  result = trunks_tpm_utility_->GetKeyName(*key_handle, &key_name);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting key name: " << trunks::GetErrorString(result);
    LOG_IF(FATAL, result == trunks::SAPI_RC_NO_CONNECTION)
        << "Fatal failure - key loading failed due to TPM daemon "
           "unavailability.";
    return false;
  }
  handle_auth_data_[*key_handle] = auth_data;
  handle_name_[*key_handle] = key_name;
  slot_handles_[slot].insert(*key_handle);
  return true;
}

bool TPM2UtilityImpl::UnbindInternal(int key_handle,
                                     const std::string& input,
                                     std::string* output) {
  trunks::TPMT_PUBLIC public_data;
  TPM_RC result =
      trunks_tpm_utility_->GetKeyPublicArea(key_handle, &public_data);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting key public data: " << result;
    LOG_IF(FATAL, result == trunks::SAPI_RC_NO_CONNECTION)
        << "Fatal failure - key unbinding failed due to TPM daemon "
           "unavailability.";
    return false;
  }
  if (input.size() > public_data.unique.rsa.size) {
    LOG(ERROR) << "RSA decrypt ciphertext is larger than modulus.";
    return false;
  }
  std::string auth_data = handle_auth_data_[key_handle].to_string();
  ScopedSession session_scope(factory_, &session_);
  if (!session_) {
    return false;
  }
  session_->SetEntityAuthorizationValue(auth_data);
  result = trunks_tpm_utility_->AsymmetricDecrypt(
      key_handle, trunks::TPM_ALG_RSAES, trunks::TPM_ALG_SHA1, input,
      session_->GetDelegate(), output);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error performing unbind operation: "
               << trunks::GetErrorString(result);
    LOG_IF(FATAL, result == trunks::SAPI_RC_NO_CONNECTION)
        << "Fatal failure - key unbinding failed due to TPM daemon "
           "unavailability.";
    return false;
  }
  return true;
}

void TPM2UtilityImpl::FlushHandle(int key_handle) {
  handle_auth_data_.erase(key_handle);
  handle_name_.erase(key_handle);
}

}  // namespace chaps
