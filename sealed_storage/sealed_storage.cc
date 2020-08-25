// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/hash/sha1.h>
#include <base/run_loop.h>
#include <base/strings/string_number_conversions.h>
#include <crypto/scoped_openssl_types.h>
#include <crypto/sha2.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <tpm_manager/client/tpm_ownership_dbus_proxy.h>
#include <trunks/error_codes.h>
#include <trunks/trunks_factory_impl.h>

#include "sealed_storage/sealed_storage.h"

namespace {

// Version tag at the start of serialized sealed blob.
enum SerializedVer : char {
  kSerializedVer1 = 0x01,
  kSerializedVer2 = 0x02,
};

// Magic value, sha256 of which is extended to PCRs when requested.
constexpr char kExtendMagic[] = "SealedStorage";

constexpr size_t kPolicySize = crypto::kSHA256Length;
constexpr size_t kPCRSize = crypto::kSHA256Length;

// RAII version of SHA256_CTX, with auto-initialization on instantiation
// and auto-cleanup on leaving scope.
class SecureSHA256_CTX {
 public:
  SecureSHA256_CTX() { SHA256_Init(&ctx_); }
  ~SecureSHA256_CTX() { OPENSSL_cleanse(&ctx_, sizeof(ctx_)); }
  SHA256_CTX* get() { return &ctx_; }

 private:
  SHA256_CTX ctx_;
};

// Get an 'empty policy' digest for the case when no PCR bindings are specified.
std::string GetEmptyPolicy() {
  return std::string(kPolicySize, 0);
}

// Get value to extend to a requested PCR.
std::string GetExtendValue() {
  return crypto::SHA256HashString(kExtendMagic);
}

// Get the expected initial PCR value before anything is extended to it.
std::string GetInitialPCRValue() {
  return std::string(kPCRSize, 0);
}

// Generate AES-256 key from Z point: key = SHA256(z.x)
brillo::SecureBlob GetKeyFromZ(const trunks::TPM2B_ECC_POINT& z) {
  SecureSHA256_CTX ctx;
  const trunks::TPM2B_ECC_PARAMETER& x = z.point.x;
  SHA256_Update(ctx.get(), x.buffer, x.size);
  brillo::SecureBlob key(crypto::kSHA256Length);
  SHA256_Final(key.data(), ctx.get());
  return key;
}

// Get a PCR0 value corresponding to one of the knwon modes.
std::string GetPCR0ValueForMode(const sealed_storage::BootMode& mode) {
  std::string mode_str(std::cbegin(mode), std::cend(mode));
  std::string mode_digest = base::SHA1HashString(mode_str);
  mode_digest.resize(kPCRSize);
  return crypto::SHA256HashString(GetInitialPCRValue() + mode_digest);
}

// Universal hex dump of any container with .data() and .size()
template <typename T>
std::string HexDump(const T& obj) {
  return base::HexEncode(obj.data(), obj.size());
}

// Checks error code returned from the TPM. On error, prints to the log and
// returns false. On success, returns true.
bool CheckTpmResult(trunks::TPM_RC result, std::string op) {
  if (result == trunks::TPM_RC_SUCCESS) {
    return true;
  }

  LOG(ERROR) << "Failed to " << op << ": " << trunks::GetErrorString(result);
  return false;
}

// Sends a request to the tpm_managerd, waits for a response (or an error)
// and fills it into the provides reply protobuf (only the error fields in
// case of error).
template <typename MethodType, typename ReplyProtoType>
void SendRequestAndWait(const MethodType& method, ReplyProtoType* reply_proto) {
  auto handler = [](ReplyProtoType* target, base::RunLoop* loop,
                    const ReplyProtoType& reply) {
    *target = reply;
    loop->Quit();
  };

  base::RunLoop loop;
  auto callback = base::Bind(handler, reply_proto, &loop);
  method.Run(callback);
  loop.Run();
}

// Returns the string representing the openssl error.
std::string GetOpenSSLError() {
  BIO* bio = BIO_new(BIO_s_mem());
  ERR_print_errors(bio);
  char* data = NULL;
  int data_len = BIO_get_mem_data(bio, &data);
  std::string error_string(data, data_len);
  BIO_free(bio);
  return error_string;
}

void ReportOpenSSLError(const std::string& op_name) {
  LOG(ERROR) << "Failed to " << op_name;
  VLOG(1) << "Error details: " << GetOpenSSLError();
}

// Gets policy data from serialized blob.
// Returns base::nullopt in case of error.
base::Optional<std::string> DeserializePolicyDigest(
    std::string* serialized_data) {
  DCHECK(serialized_data);

  uint16_t size;
  if (trunks::Parse_uint16_t(serialized_data, &size,
                             nullptr /* value_bytes */) !=
      trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Failed to parse policy digest size";
    return base::nullopt;
  }
  if (serialized_data->size() < size) {
    LOG(ERROR) << "Policy digest longer than the remaining sealed data: "
               << serialized_data->size() << " < " << size;
    return base::nullopt;
  }

  std::string policy_digest = serialized_data->substr(0, size);
  serialized_data->erase(0, size);
  if (policy_digest.size() != kPolicySize) {
    LOG(ERROR) << "Unexpected policy digest size: " << policy_digest.size();
    return base::nullopt;
  }
  return policy_digest;
}

}  // namespace

namespace sealed_storage {

// Structure to hold the key and IV for software-based AES encryption and
// decryption.
struct Key {
 public:
  static const EVP_CIPHER* GetCipher() { return EVP_aes_256_cbc(); }
  static uint16_t GetIVSize() { return EVP_CIPHER_iv_length(GetCipher()); }
  static uint16_t GetKeySize() { return EVP_CIPHER_key_length(GetCipher()); }
  static uint16_t GetBlockSize() { return EVP_CIPHER_block_size(GetCipher()); }

  // Initializes the structure from private and public seeds resulting from
  // TPM-based ECDHE operation.
  bool Init(const PrivSeeds& priv_seeds, const PubSeeds& pub_seeds);

  // Encrypts the plain data using the initialized key and IV. In case of error,
  // returns nullopt.
  base::Optional<Data> Encrypt(const SecretData& plain_data) const;

  // Decrypts the data using the initialized key and IV. Verifies that the
  // resulting plaintext size matches the |expected size_|. In case of error,
  // returns nullopt.
  base::Optional<SecretData> Decrypt(const Data& encrypted_data) const;

 private:
  brillo::SecureBlob key_;
  brillo::Blob iv_;
  uint16_t expected_size_;
};

Policy::PcrMap::value_type Policy::BootModePCR(const BootMode& mode) {
  return {0, GetPCR0ValueForMode(mode)};
}

Policy::PcrMap::value_type Policy::UnchangedPCR(uint32_t pcr_num) {
  return {pcr_num, GetInitialPCRValue()};
}

SealedStorage::SealedStorage(const Policy& policy,
                             trunks::TrunksFactory* trunks_factory,
                             tpm_manager::TpmOwnershipInterface* tpm_ownership)
    : policy_(policy),
      trunks_factory_(trunks_factory),
      tpm_ownership_(tpm_ownership) {
  DCHECK(trunks_factory_);
  DCHECK(tpm_ownership_);
}

SealedStorage::SealedStorage(const Policy& policy)
    : policy_(policy),
      dft_trunks_factory_(CreateTrunksFactory()),
      dft_tpm_ownership_(CreateTpmOwnershipInterface()) {
  trunks_factory_ = dft_trunks_factory_.get();
  tpm_ownership_ = dft_tpm_ownership_.get();
  DCHECK(trunks_factory_);
  DCHECK(tpm_ownership_);
}

ScopedTrunksFactory SealedStorage::CreateTrunksFactory() {
  auto factory = std::make_unique<trunks::TrunksFactoryImpl>();
  if (!factory->Initialize()) {
    LOG(ERROR) << "Failed to initialize TrunksFactory";
    factory.reset(nullptr);
  }
  return factory;
}

ScopedTpmOwnership SealedStorage::CreateTpmOwnershipInterface() {
  auto proxy = std::make_unique<tpm_manager::TpmOwnershipDBusProxy>();
  if (!proxy->Initialize()) {
    LOG(ERROR) << "Failed to initialize TpmOwnershipDBusProxy";
    proxy.reset(nullptr);
  }
  return proxy;
}

base::Optional<Data> SealedStorage::Seal(const SecretData& plain_data) const {
  if (!CheckInitialized()) {
    return base::nullopt;
  }

  PrivSeeds priv_seeds;
  PubSeeds pub_seeds;
  if (!CreateEncryptionSeeds(&priv_seeds, &pub_seeds)) {
    return base::nullopt;
  }
  pub_seeds.plain_size = plain_data.size();
  VLOG(2) << "Created encryption seeds";

  Key key;
  if (!key.Init(priv_seeds, pub_seeds)) {
    return base::nullopt;
  }
  VLOG(2) << "Created encryption key";

  auto encrypted_data = key.Encrypt(plain_data);
  if (!encrypted_data) {
    return base::nullopt;
  }
  VLOG(2) << "Encrypted data";

  return SerializeSealedBlob(pub_seeds, encrypted_data.value());
}

base::Optional<SecretData> SealedStorage::Unseal(
    const Data& sealed_data) const {
  if (!CheckInitialized()) {
    return base::nullopt;
  }

  PubSeeds pub_seeds;
  Data encrypted_data;
  if (!DeserializeSealedBlob(sealed_data, &pub_seeds, &encrypted_data)) {
    return base::nullopt;
  }
  VLOG(2) << "Deserialized sealed blob";

  PrivSeeds priv_seeds;
  if (!RestoreEncryptionSeeds(pub_seeds, &priv_seeds)) {
    return base::nullopt;
  }
  VLOG(2) << "Restored encryption seeds";

  Key key;
  if (!key.Init(priv_seeds, pub_seeds)) {
    return base::nullopt;
  }
  VLOG(2) << "Created encryption key";

  return key.Decrypt(encrypted_data);
}

bool SealedStorage::ExtendPCR(uint32_t pcr_num) const {
  if (!CheckInitialized()) {
    return false;
  }
  auto tpm_utility = trunks_factory_->GetTpmUtility();

  return CheckTpmResult(
      tpm_utility->ExtendPCR(pcr_num, GetExtendValue(), nullptr), "extend PCR");
}

base::Optional<bool> SealedStorage::CheckState() const {
  if (!CheckInitialized()) {
    return base::nullopt;
  }
  auto tpm_utility = trunks_factory_->GetTpmUtility();

  for (const auto& pcr_val : policy_.pcr_map) {
    if (pcr_val.second.empty()) {
      continue;
    }
    std::string value;
    auto result = tpm_utility->ReadPCR(pcr_val.first, &value);
    if (!CheckTpmResult(result, "read PCR")) {
      return base::nullopt;
    }
    if (value != pcr_val.second) {
      return false;
    }
  }

  return true;
}

bool SealedStorage::PrepareSealingKeyObject(
    const base::Optional<std::string>& expected_digest,
    trunks::TPM_HANDLE* key_handle,
    std::string* key_name,
    std::string* resulting_digest) const {
  CHECK(key_handle);
  CHECK(key_name);

  std::string endorsement_password;
  if (!GetEndorsementPassword(&endorsement_password)) {
    return false;
  }
  VLOG(2) << "Obtained endorsement password";

  std::string policy_digest;
  if (!policy_.pcr_map.empty()) {
    auto tpm_utility = trunks_factory_->GetTpmUtility();
    auto result = tpm_utility->GetPolicyDigestForPcrValues(
        policy_.pcr_map, false /* use_auth_value */, &policy_digest);
    if (!CheckTpmResult(result, "calculate policy")) {
      return false;
    }
  } else {
    policy_digest = GetEmptyPolicy();
  }
  VLOG(2) << "Created policy digest: " << HexDump(policy_digest);
  if (expected_digest.has_value() && expected_digest.value() != policy_digest) {
    VLOG(2) << "Expected policy digest: " << HexDump(expected_digest.value());
    LOG(ERROR) << "Policy mismatch";
    return false;
  }
  if (resulting_digest) {
    *resulting_digest = policy_digest;
  }

  trunks::TPMS_SENSITIVE_CREATE sensitive = {};
  memset(&sensitive, 0, sizeof(sensitive));
  sensitive.user_auth = trunks::Make_TPM2B_DIGEST("");
  sensitive.data = trunks::Make_TPM2B_SENSITIVE_DATA("");

  trunks::TPMT_PUBLIC public_area = {};
  memset(&public_area, 0, sizeof(public_area));
  public_area.type = trunks::TPM_ALG_ECC;
  public_area.name_alg = trunks::TPM_ALG_SHA256;
  public_area.auth_policy = trunks::Make_TPM2B_DIGEST(policy_digest);
  public_area.object_attributes =
      trunks::kFixedTPM | trunks::kFixedParent | trunks::kSensitiveDataOrigin |
      trunks::kAdminWithPolicy | trunks::kDecrypt | trunks::kNoDA;
  public_area.parameters.ecc_detail.symmetric.algorithm = trunks::TPM_ALG_NULL;
  public_area.parameters.ecc_detail.scheme.scheme = trunks::TPM_ALG_NULL;
  public_area.parameters.ecc_detail.curve_id = trunks::TPM_ECC_NIST_P256;
  public_area.parameters.ecc_detail.kdf.scheme = trunks::TPM_ALG_NULL;
  public_area.unique.ecc.x = trunks::Make_TPM2B_ECC_PARAMETER("");
  public_area.unique.ecc.y = trunks::Make_TPM2B_ECC_PARAMETER("");

  auto endorsement_auth =
      trunks_factory_->GetPasswordAuthorization(endorsement_password);
  std::string rh_endorsement_name;
  trunks::Serialize_TPM_HANDLE(trunks::TPM_RH_ENDORSEMENT,
                               &rh_endorsement_name);

  return CreatePrimaryKeyObject(
      "sealing key object", trunks::TPM_RH_ENDORSEMENT, rh_endorsement_name,
      sensitive, public_area, endorsement_auth.get(), key_handle, key_name);
}

bool SealedStorage::GetEndorsementPassword(std::string* password) const {
  CHECK(password);

  if (!tpm_ownership_) {
    LOG(ERROR) << "TpmOwnershipInterface is not initialized";
    return false;
  }

  tpm_manager::GetTpmStatusRequest request;
  auto method = base::Bind(&tpm_manager::TpmOwnershipInterface::GetTpmStatus,
                           base::Unretained(tpm_ownership_), request);
  tpm_manager::GetTpmStatusReply tpm_status;
  SendRequestAndWait(method, &tpm_status);
  if (tpm_status.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << "Failed to get TpmStatus: " << tpm_status.status();
    return false;
  }
  *password = tpm_status.local_data().endorsement_password();
  return true;
}

bool SealedStorage::CreatePrimaryKeyObject(
    const std::string& object_descr,
    trunks::TPM_HANDLE parent_handle,
    const std::string& parent_name,
    const trunks::TPMS_SENSITIVE_CREATE& sensitive,
    const trunks::TPMT_PUBLIC& public_area,
    trunks::AuthorizationDelegate* auth_delegate,
    trunks::TPM_HANDLE* object_handle,
    std::string* object_name) const {
  DCHECK(object_handle);
  DCHECK(object_name);

  trunks::TPML_PCR_SELECTION creation_pcrs = {};
  trunks::TPM2B_PUBLIC out_public = {};
  trunks::TPM2B_CREATION_DATA out_creation_data = {};
  trunks::TPM2B_DIGEST out_creation_hash = {};
  trunks::TPMT_TK_CREATION out_creation_ticket = {};

  trunks::TPM2B_NAME out_name = {};
  auto result = trunks_factory_->GetTpm()->CreatePrimarySync(
      parent_handle, parent_name,
      trunks::Make_TPM2B_SENSITIVE_CREATE(sensitive),
      trunks::Make_TPM2B_PUBLIC(public_area),
      trunks::Make_TPM2B_DATA("") /* outside_info */, creation_pcrs,
      object_handle, &out_public, &out_creation_data, &out_creation_hash,
      &out_creation_ticket, &out_name, auth_delegate);
  if (!CheckTpmResult(result, std::string("create ") + object_descr)) {
    return false;
  }
  *object_name = trunks::StringFrom_TPM2B_NAME(out_name);
  VLOG(2) << "Created " << object_descr << ": " << *object_handle;

  return true;
}

bool SealedStorage::CheckInitialized() const {
  if (!trunks_factory_ || !trunks_factory_->GetTpm()) {
    LOG(ERROR) << "TrunksFactory is not initialized";
    return false;
  }
  return true;
}

bool SealedStorage::CreateEncryptionSeeds(PrivSeeds* priv_seeds,
                                          PubSeeds* pub_seeds) const {
  DCHECK(priv_seeds);
  DCHECK(pub_seeds);

  trunks::TPM_HANDLE key_handle;
  std::string key_name;
  std::string resulting_digest;
  if (!PrepareSealingKeyObject(base::nullopt, &key_handle, &key_name,
                               &resulting_digest)) {
    return false;
  }
  pub_seeds->policy_digest = std::move(resulting_digest);

  auto result = trunks_factory_->GetTpm()->ECDH_KeyGenSync(
      key_handle, key_name, &priv_seeds->z_point, &pub_seeds->pub_point,
      nullptr /* authorization_delegate */);
  if (!CheckTpmResult(result, "generate ECDH keypair")) {
    return false;
  }
  VLOG(2) << "Generated ECDH keypair";

  result = trunks_factory_->GetTpm()->GetRandomSync(
      Key::GetIVSize(), &pub_seeds->iv, nullptr /* authorization_delegate */);
  if (!CheckTpmResult(result, "generate IV")) {
    return false;
  }
  VLOG(2) << "Generated IV";

  return true;
}

bool SealedStorage::RestoreEncryptionSeeds(const PubSeeds& pub_seeds,
                                           PrivSeeds* priv_seeds) const {
  DCHECK(priv_seeds);

  trunks::TPM_HANDLE key_handle;
  std::string key_name;
  if (!PrepareSealingKeyObject(pub_seeds.policy_digest, &key_handle, &key_name,
                               nullptr)) {
    return false;
  }

  auto policy_session = trunks_factory_->GetPolicySession();
  auto result = policy_session->StartUnboundSession(true, false);
  if (!CheckTpmResult(result, "start policy session")) {
    return false;
  }

  if (!policy_.pcr_map.empty()) {
    result = policy_session->PolicyPCR(policy_.pcr_map);
    if (!CheckTpmResult(result, "restrict policy to PCRs")) {
      return false;
    }
  }
  VLOG(2) << "Created policy session";

  result = trunks_factory_->GetTpm()->ECDH_ZGenSync(
      key_handle, key_name, pub_seeds.pub_point, &priv_seeds->z_point,
      policy_session->GetDelegate());
  if (!CheckTpmResult(result, "restore ECDH Z point")) {
    return false;
  }
  VLOG(2) << "Restored ECDH Z point";

  return true;
}

base::Optional<Data> SealedStorage::SerializeSealedBlob(
    const PubSeeds& pub_seeds, const Data& encrypted_data) const {
  std::string serialized_data(1, kSerializedVer2);

  trunks::Serialize_uint16_t(pub_seeds.plain_size, &serialized_data);

  if (!pub_seeds.policy_digest.has_value()) {
    LOG(ERROR) << "Missing policy digest during serialization";
    return base::nullopt;
  }
  if (pub_seeds.policy_digest->size() != kPolicySize) {
    LOG(ERROR) << "Unexpected policy digest size during serialization: "
               << pub_seeds.policy_digest->size();
    return base::nullopt;
  }
  trunks::Serialize_uint16_t(pub_seeds.policy_digest->size(), &serialized_data);
  serialized_data.append(pub_seeds.policy_digest->begin(),
                         pub_seeds.policy_digest->end());

  if (trunks::Serialize_TPM2B_ECC_POINT(
          pub_seeds.pub_point, &serialized_data) != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Failed to serialize public point";
    return base::nullopt;
  }
  if (trunks::Serialize_TPM2B_DIGEST(pub_seeds.iv, &serialized_data) !=
      trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Failed to serialize IV";
    return base::nullopt;
  }

  if (encrypted_data.size() > UINT16_MAX) {
    LOG(ERROR) << "Too long encrypted data: " << encrypted_data.size();
    return base::nullopt;
  }
  uint16_t size = encrypted_data.size();
  trunks::Serialize_uint16_t(size, &serialized_data);
  serialized_data.append(encrypted_data.begin(), encrypted_data.end());
  return Data(serialized_data.begin(), serialized_data.end());
}

bool SealedStorage::DeserializeSealedBlob(const Data& sealed_data,
                                          PubSeeds* pub_seeds,
                                          Data* encrypted_data) const {
  DCHECK(pub_seeds);
  DCHECK(encrypted_data);

  std::string serialized_data(sealed_data.begin(), sealed_data.end());
  if (serialized_data.empty()) {
    LOG(ERROR) << "Empty sealed data";
    return false;
  }

  uint8_t version = serialized_data[0];
  serialized_data.erase(0, 1);
  switch (version) {
    case kSerializedVer1:
      pub_seeds->plain_size = plain_size_for_v1_;
      pub_seeds->policy_digest.reset();
      break;
    case kSerializedVer2:
      if (trunks::Parse_uint16_t(&serialized_data, &pub_seeds->plain_size,
                                 nullptr /* value_bytes */) !=
          trunks::TPM_RC_SUCCESS) {
        LOG(ERROR) << "Failed to parse plain data size";
        return false;
      }
      pub_seeds->policy_digest = DeserializePolicyDigest(&serialized_data);
      if (!pub_seeds->policy_digest.has_value()) {
        LOG(ERROR) << "Failed to parse policy digest";
        return false;
      }
      break;
    default:
      LOG(ERROR) << "Unexpected serialized version: " << version;
      return false;
  }

  if (trunks::Parse_TPM2B_ECC_POINT(&serialized_data, &pub_seeds->pub_point,
                                    nullptr /* value_bytes */) !=
      trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Failed to parse public point";
    return false;
  }
  if (trunks::Parse_TPM2B_DIGEST(&serialized_data, &pub_seeds->iv,
                                 nullptr /* value_bytes */) !=
      trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Failed to parse IV";
    return false;
  }

  uint16_t size;
  if (trunks::Parse_uint16_t(&serialized_data, &size,
                             nullptr /* value_bytes */) !=
      trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Failed to parse encrypted data size";
    return false;
  }
  if (serialized_data.size() != size) {
    LOG(ERROR) << "Unexpected encrypted data size: " << serialized_data.size()
               << " != " << size;
    return false;
  }
  encrypted_data->assign(serialized_data.begin(), serialized_data.end());
  return true;
}

bool Key::Init(const PrivSeeds& priv_seeds, const PubSeeds& pub_seeds) {
  if (pub_seeds.iv.size != GetIVSize()) {
    LOG(ERROR) << "Unexpected input IV size: " << pub_seeds.iv.size;
    return false;
  }
  iv_.assign(pub_seeds.iv.buffer, pub_seeds.iv.buffer + pub_seeds.iv.size);
  if (iv_.size() != GetIVSize()) {
    LOG(ERROR) << "Unexpected IV size: " << iv_.size();
    return false;
  }
  key_ = GetKeyFromZ(priv_seeds.z_point);
  if (key_.size() != GetKeySize()) {
    LOG(ERROR) << "Unexpected key size: " << key_.size();
    return false;
  }
  expected_size_ = pub_seeds.plain_size;
  return true;
}

base::Optional<Data> Key::Encrypt(const SecretData& plain_data) const {
  if (plain_data.size() != expected_size_) {
    LOG(ERROR) << "Unexpected plain data size: " << plain_data.size()
               << " != " << expected_size_;
    return base::nullopt;
  }

  crypto::ScopedEVP_CIPHER_CTX ctx(EVP_CIPHER_CTX_new());
  if (!ctx) {
    ReportOpenSSLError("allocate encryption context");
    return base::nullopt;
  }
  if (!EVP_EncryptInit_ex(ctx.get(), GetCipher(), nullptr, key_.data(),
                          iv_.data())) {
    ReportOpenSSLError("initialize encryption context");
    return base::nullopt;
  }

  const size_t max_encrypted_size = plain_data.size() + GetBlockSize();
  Data encrypted_data(max_encrypted_size);
  int encrypted_size;
  if (!EVP_EncryptUpdate(
          ctx.get(), static_cast<unsigned char*>(encrypted_data.data()),
          &encrypted_size, plain_data.data(), plain_data.size())) {
    ReportOpenSSLError("encrypt");
    return base::nullopt;
  }
  if (encrypted_size < 0 || encrypted_size > max_encrypted_size) {
    LOG(ERROR) << "Unexpected encrypted data size: " << encrypted_size << " > "
               << max_encrypted_size;
    return base::nullopt;
  }

  unsigned char* final_buf = nullptr;
  if (encrypted_size < max_encrypted_size) {
    final_buf =
        static_cast<unsigned char*>(encrypted_data.data() + encrypted_size);
  }
  int encrypted_size_final = 0;
  if (!EVP_EncryptFinal_ex(ctx.get(), final_buf, &encrypted_size_final)) {
    ReportOpenSSLError("finalize encryption");
    return base::nullopt;
  }
  if (encrypted_size_final < 0) {
    LOG(ERROR) << "Unexpected size for final encryption block: "
               << encrypted_size_final;
    return base::nullopt;
  }
  encrypted_size += encrypted_size_final;
  if (encrypted_size > max_encrypted_size) {
    LOG(ERROR) << "Unexpected encrypted data size after finalization: "
               << encrypted_size << " > " << max_encrypted_size;
    return base::nullopt;
  }
  encrypted_data.resize(encrypted_size);

  return encrypted_data;
}

base::Optional<SecretData> Key::Decrypt(const Data& encrypted_data) const {
  crypto::ScopedEVP_CIPHER_CTX ctx(EVP_CIPHER_CTX_new());
  if (!ctx) {
    ReportOpenSSLError("allocate decryption context");
    return base::nullopt;
  }

  if (!EVP_DecryptInit_ex(ctx.get(), GetCipher(), nullptr, key_.data(),
                          iv_.data())) {
    ReportOpenSSLError("initialize decryption context");
    return base::nullopt;
  }

  const size_t max_decrypted_size = encrypted_data.size() + GetBlockSize();
  if (max_decrypted_size < expected_size_) {
    LOG(ERROR) << "Not enough data for expected size: " << encrypted_data.size()
               << " leads to max " << max_decrypted_size << " < "
               << expected_size_;
    return base::nullopt;
  }
  SecretData decrypted_data(max_decrypted_size);
  int decrypted_size;
  if (!EVP_DecryptUpdate(
          ctx.get(), static_cast<unsigned char*>(decrypted_data.data()),
          &decrypted_size, encrypted_data.data(), encrypted_data.size())) {
    ReportOpenSSLError("decrypt");
    return base::nullopt;
  }
  if (decrypted_size < 0 || decrypted_size > max_decrypted_size) {
    LOG(ERROR) << "Unexpected decrypted data size: " << decrypted_size << " > "
               << max_decrypted_size;
    return base::nullopt;
  }

  unsigned char* final_buf = nullptr;
  if (decrypted_size < max_decrypted_size) {
    final_buf =
        static_cast<unsigned char*>(decrypted_data.data() + decrypted_size);
  }
  int decrypted_size_final = 0;
  if (!EVP_DecryptFinal_ex(ctx.get(), final_buf, &decrypted_size_final)) {
    ReportOpenSSLError("finalize decryption");
    return base::nullopt;
  }
  if (decrypted_size_final < 0) {
    LOG(ERROR) << "Unexpected size for final decryption block: "
               << decrypted_size_final;
    return base::nullopt;
  }
  decrypted_size += decrypted_size_final;
  if (decrypted_size != expected_size_) {
    LOG(ERROR) << "Unexpected decrypted data size: " << decrypted_size
               << " != " << expected_size_;
    return base::nullopt;
  }
  decrypted_data.resize(decrypted_size);

  return decrypted_data;
}

}  // namespace sealed_storage
