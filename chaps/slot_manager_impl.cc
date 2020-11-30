// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/slot_manager_impl.h"

#include <string.h>

#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <brillo/secure_blob.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "chaps/chaps_utility.h"
#include "chaps/isolate.h"
#include "chaps/object_importer.h"
#include "chaps/session.h"
#include "chaps/tpm_utility.h"
#include "pkcs11/cryptoki.h"

using base::AutoLock;
using base::FilePath;
using brillo::SecureBlob;
using std::map;
using std::shared_ptr;
using std::string;
using std::vector;

namespace chaps {

namespace {

// I18N Note: The descriptive strings are needed for PKCS #11 compliance but
// they should not appear on any UI.
constexpr base::TimeDelta kTokenInitBlockSystemShutdownFallbackTimeout =
    base::TimeDelta::FromSeconds(10);
constexpr CK_VERSION kDefaultVersion = {1, 0};
constexpr char kManufacturerID[] = "Chromium OS";
constexpr CK_ULONG kMaxPinLen = 127;
constexpr CK_ULONG kMinPinLen = 6;
constexpr char kSlotDescription[] = "TPM Slot";
constexpr char kSystemTokenAuthData[] = "000000";
constexpr char kSystemTokenLabel[] = "System TPM Token";
constexpr char kTokenLabel[] = "User-Specific TPM Token";
constexpr char kTokenModel[] = "";
constexpr char kTokenSerialNumber[] = "Not Available";
constexpr int kUserKeySize = 32;
constexpr int kAuthDataHashVersion = 1;
constexpr char kKeyPurposeEncrypt[] = "encrypt";
constexpr char kKeyPurposeMac[] = "mac";
constexpr char kAuthKeyMacInput[] = "arbitrary";
constexpr char kTokenReinitializedFlagFilePath[] =
    "/var/lib/chaps/debug_token_reinitialized";

constexpr CK_FLAGS kCommonECParameters =
    CKF_EC_F_P | CKF_EC_F_2M | CKF_EC_NAMEDCURVE | CKF_EC_ECPARAMETERS |
    CKF_EC_UNCOMPRESS;

typedef std::pair<CK_MECHANISM_TYPE, CK_MECHANISM_INFO> MechanismInfoPair;

constexpr MechanismInfoPair kDefaultMechanismInfo[] = {
    {CKM_RSA_PKCS_KEY_PAIR_GEN, {512, 2048, CKF_GENERATE_KEY_PAIR | CKF_HW}},
    {CKM_RSA_PKCS,
     {512, 2048, CKF_HW | CKF_ENCRYPT | CKF_DECRYPT | CKF_SIGN | CKF_VERIFY}},
    {CKM_MD5_RSA_PKCS, {512, 2048, CKF_HW | CKF_SIGN | CKF_VERIFY}},
    {CKM_SHA1_RSA_PKCS, {512, 2048, CKF_HW | CKF_SIGN | CKF_VERIFY}},
    {CKM_SHA256_RSA_PKCS, {512, 2048, CKF_HW | CKF_SIGN | CKF_VERIFY}},
    {CKM_SHA384_RSA_PKCS, {512, 2048, CKF_HW | CKF_SIGN | CKF_VERIFY}},
    {CKM_SHA512_RSA_PKCS, {512, 2048, CKF_HW | CKF_SIGN | CKF_VERIFY}},

    {CKM_MD5, {0, 0, CKF_DIGEST}},
    {CKM_SHA_1, {0, 0, CKF_DIGEST}},
    {CKM_SHA256, {0, 0, CKF_DIGEST}},
    {CKM_SHA384, {0, 0, CKF_DIGEST}},
    {CKM_SHA512, {0, 0, CKF_DIGEST}},

    {CKM_GENERIC_SECRET_KEY_GEN, {8, 1024, CKF_GENERATE}},

    {CKM_MD5_HMAC, {0, 0, CKF_SIGN | CKF_VERIFY}},
    {CKM_SHA_1_HMAC, {0, 0, CKF_SIGN | CKF_VERIFY}},
    {CKM_SHA256_HMAC, {0, 0, CKF_SIGN | CKF_VERIFY}},
    {CKM_SHA512_HMAC, {0, 0, CKF_SIGN | CKF_VERIFY}},
    {CKM_SHA384_HMAC, {0, 0, CKF_SIGN | CKF_VERIFY}},

    {CKM_DES_KEY_GEN, {0, 0, CKF_GENERATE}},
    {CKM_DES_ECB, {0, 0, CKF_ENCRYPT | CKF_DECRYPT}},
    {CKM_DES_CBC, {0, 0, CKF_ENCRYPT | CKF_DECRYPT}},
    {CKM_DES_CBC_PAD, {0, 0, CKF_ENCRYPT | CKF_DECRYPT}},

    {CKM_DES3_KEY_GEN, {0, 0, CKF_GENERATE}},
    {CKM_DES3_ECB, {0, 0, CKF_ENCRYPT | CKF_DECRYPT}},
    {CKM_DES3_CBC, {0, 0, CKF_ENCRYPT | CKF_DECRYPT}},
    {CKM_DES3_CBC_PAD, {0, 0, CKF_ENCRYPT | CKF_DECRYPT}},

    {CKM_AES_KEY_GEN, {16, 32, CKF_GENERATE}},
    {CKM_AES_ECB, {16, 32, CKF_ENCRYPT | CKF_DECRYPT}},
    {CKM_AES_CBC, {16, 32, CKF_ENCRYPT | CKF_DECRYPT}},
    {CKM_AES_CBC_PAD, {16, 32, CKF_ENCRYPT | CKF_DECRYPT}},
};

constexpr MechanismInfoPair kTPM2OnlyMechanismInfo[] = {
    // RSA PSS is TPM2 only.
    {CKM_RSA_PKCS_PSS, {512, 2048, CKF_HW | CKF_SIGN | CKF_VERIFY}},
    {CKM_SHA1_RSA_PKCS_PSS, {512, 2048, CKF_HW | CKF_SIGN | CKF_VERIFY}},
    {CKM_SHA256_RSA_PKCS_PSS, {512, 2048, CKF_HW | CKF_SIGN | CKF_VERIFY}},
    {CKM_SHA384_RSA_PKCS_PSS, {512, 2048, CKF_HW | CKF_SIGN | CKF_VERIFY}},
    {CKM_SHA512_RSA_PKCS_PSS, {512, 2048, CKF_HW | CKF_SIGN | CKF_VERIFY}},

    // Elliptic Curve related mechanisms are TPM2 only.
    {CKM_EC_KEY_PAIR_GEN,
     {256, 256, CKF_GENERATE_KEY_PAIR | CKF_HW | kCommonECParameters}},
    {CKM_ECDSA_SHA1,
     {256, 256, CKF_HW | CKF_SIGN | CKF_VERIFY | kCommonECParameters}},
};

// Computes an authorization data hash as it is stored in the database.
string HashAuthData(const SecureBlob& auth_data) {
  string version(1, kAuthDataHashVersion);
  SecureBlob hash = Sha512(auth_data);
  string hash_byte(1, static_cast<const char>(hash[0]));
  return version + hash_byte;
}

// Sanity checks authorization data by comparing against a hash stored in the
// token database.
// Args:
//   auth_data_hash - A hash of the authorization data to be verified.
//   saved_auth_data_hash - The hash currently stored in the database.
// Returns:
//   False if both hash values are valid and they do not match.
bool SanityCheckAuthData(const string& auth_data_hash,
                         const string& saved_auth_data_hash) {
  CHECK_EQ(auth_data_hash.length(), 2u);
  if (saved_auth_data_hash.length() != 2 ||
      saved_auth_data_hash[0] != kAuthDataHashVersion)
    return true;
  return (auth_data_hash[1] == saved_auth_data_hash[1]);
}

// TODO(https://crbug.com/844537): Remove when the root cause of disappearing
// system token certificates is found.
// Creates a persistent flag file containing the path of the token that has been
// reinitialized. The purpose is to know if this has happened even if syslog is
// not available at the time when token reinitialization is triggered (e.g.
// because the machine is shutting down). The file will be read by
// |LogTokenReinitializedFromFlagFile|.
void CreateTokenReinitializedFlagFile(const FilePath& token_path) {
  const FilePath flag_file_path(kTokenReinitializedFlagFilePath);
  const std::string& token_path_value = token_path.value();
  base::WriteFile(flag_file_path, token_path_value.c_str(),
                  static_cast<int>(token_path_value.length()));
}

// TODO(https://crbug.com/844537): Remove when the root cause of disappearing
// system token certificates is found.
// Reads the flag file written by |CreateTokenReinitizliedFlagFile| if it exists
// and logs a message if it indicates that a token has been reinitialized.
void LogTokenReinitializedFromFlagFile() {
  const FilePath flag_file_path(kTokenReinitializedFlagFilePath);
  if (!base::PathExists(flag_file_path)) {
    return;
  }

  std::string reinitialized_token_path;
  if (!base::ReadFileToStringWithMaxSize(flag_file_path,
                                         &reinitialized_token_path, 4096)) {
    PLOG(ERROR) << "Could not read flag file " << flag_file_path.value();
    return;
  }
  base::File::Info flag_file_info;
  if (!base::GetFileInfo(flag_file_path, &flag_file_info)) {
    PLOG(ERROR) << "Could not get info for flag file "
                << flag_file_path.value();
    return;
  }
  if (!base::DeleteFile(flag_file_path)) {
    PLOG(ERROR) << "Could not delete flag file " << flag_file_path.value();
  }
  LOG(WARNING) << "Flag file with timestamp " << flag_file_info.last_modified
               << " indicated that token " << reinitialized_token_path
               << " has been reinitialized.";
}

// Performs expensive tasks required to initialize a token.
class TokenInitThread : public base::PlatformThread::Delegate {
 public:
  // This class will not take ownership of any pointers.
  TokenInitThread(int slot_id,
                  FilePath path,
                  const SecureBlob& auth_data,
                  TPMUtility* tpm_utility,
                  ObjectPool* object_pool,
                  SystemShutdownBlocker* system_shutdown_blocker);
  TokenInitThread(const TokenInitThread&) = delete;
  TokenInitThread& operator=(const TokenInitThread&) = delete;

  ~TokenInitThread() override {}

  // PlatformThread::Delegate interface.
  void ThreadMain() override;

 private:
  bool InitializeKeyHierarchy(SecureBlob* master_key);

  int slot_id_;
  FilePath path_;
  SecureBlob auth_data_;
  TPMUtility* tpm_utility_;
  ObjectPool* object_pool_;
  SystemShutdownBlocker* system_shutdown_blocker_;
};

// Performs expensive tasks required to terminate a token.
class TokenTermThread : public base::PlatformThread::Delegate {
 public:
  // This class will not take ownership of any pointers.
  TokenTermThread(int slot_id, TPMUtility* tpm_utility)
      : slot_id_(slot_id), tpm_utility_(tpm_utility) {}
  TokenTermThread(const TokenTermThread&) = delete;
  TokenTermThread& operator=(const TokenTermThread&) = delete;

  ~TokenTermThread() override {}

  // PlatformThread::Delegate interface.
  void ThreadMain() override { tpm_utility_->UnloadKeysForSlot(slot_id_); }

 private:
  int slot_id_;
  TPMUtility* tpm_utility_;
};

TokenInitThread::TokenInitThread(int slot_id,
                                 FilePath path,
                                 const SecureBlob& auth_data,
                                 TPMUtility* tpm_utility,
                                 ObjectPool* object_pool,
                                 SystemShutdownBlocker* system_shutdown_blocker)
    : slot_id_(slot_id),
      path_(path),
      auth_data_(auth_data),
      tpm_utility_(tpm_utility),
      object_pool_(object_pool),
      system_shutdown_blocker_(system_shutdown_blocker) {}

void TokenInitThread::ThreadMain() {
  // Block system shutdown while TokenInitThread is running. Unblock shutdown
  // once TokenInitThread completes or a fallback timeout of
  // |kTokenInitBlockSystemShutdownFallbackTimeout| has expired.
  // |system_shutdown_blocker_| can be nullptr in tests.
  std::unique_ptr<base::ScopedClosureRunner> scoped_closure_runner;
  if (system_shutdown_blocker_) {
    auto unblock_closure =
        base::Bind(&SystemShutdownBlocker::Unblock,
                   base::Unretained(system_shutdown_blocker_), slot_id_);
    scoped_closure_runner =
        std::make_unique<base::ScopedClosureRunner>(unblock_closure);
    system_shutdown_blocker_->Block(
        slot_id_, kTokenInitBlockSystemShutdownFallbackTimeout);
  }

  string auth_data_hash = HashAuthData(auth_data_);
  string saved_auth_data_hash;
  string auth_key_blob;
  string encrypted_master_key;
  SecureBlob master_key;
  // Determine whether the key hierarchy has already been initialized based on
  // whether the relevant blobs exist.
  if (!object_pool_->GetInternalBlob(kEncryptedAuthKey, &auth_key_blob) ||
      !object_pool_->GetInternalBlob(kEncryptedMasterKey,
                                     &encrypted_master_key)) {
    LOG(INFO) << "Initializing key hierarchy for token at " << path_.value();
    if (!InitializeKeyHierarchy(&master_key)) {
      LOG(ERROR) << "Failed to initialize key hierarchy at " << path_.value();
      tpm_utility_->UnloadKeysForSlot(slot_id_);
    }
  } else {
    // Don't send the auth data to the TPM if it fails to verify against the
    // saved hash.
    object_pool_->GetInternalBlob(kAuthDataHash, &saved_auth_data_hash);
    if (!SanityCheckAuthData(auth_data_hash, saved_auth_data_hash) ||
        !tpm_utility_->Authenticate(slot_id_, Sha1(auth_data_), auth_key_blob,
                                    encrypted_master_key, &master_key)) {
      LOG(ERROR) << "Authentication failed for token at " << path_.value()
                 << ", reinitializing token.";
      CreateTokenReinitializedFlagFile(path_);
      tpm_utility_->UnloadKeysForSlot(slot_id_);
      if (object_pool_->DeleteAll() != ObjectPool::Result::Success)
        LOG(WARNING) << "Failed to delete all existing objects.";
      if (!InitializeKeyHierarchy(&master_key)) {
        LOG(ERROR) << "Failed to initialize key hierarchy at " << path_.value();
        tpm_utility_->UnloadKeysForSlot(slot_id_);
      }
    }
  }
  if (!object_pool_->SetEncryptionKey(master_key)) {
    LOG(ERROR) << "SetEncryptionKey failed for token at " << path_.value();
    tpm_utility_->UnloadKeysForSlot(slot_id_);
    return;
  }
  if (!master_key.empty()) {
    if (auth_data_hash != saved_auth_data_hash)
      object_pool_->SetInternalBlob(kAuthDataHash, auth_data_hash);
    LOG(INFO) << "Master key is ready for token at " << path_.value();
  }
}

bool TokenInitThread::InitializeKeyHierarchy(SecureBlob* master_key) {
  string master_key_str;
  if (!tpm_utility_->GenerateRandom(kUserKeySize, &master_key_str)) {
    LOG(ERROR) << "Failed to generate user encryption key.";
    return false;
  }
  *master_key = SecureBlob(master_key_str.begin(), master_key_str.end());
  string auth_key_blob;
  int auth_key_handle;
  const int key_size = 2048;
  const string public_exponent("\x01\x00\x01", 3);
  if (!tpm_utility_->GenerateRSAKey(slot_id_, key_size, public_exponent,
                                    Sha1(auth_data_), &auth_key_blob,
                                    &auth_key_handle)) {
    LOG(ERROR) << "Failed to generate user authentication key.";
    return false;
  }
  string encrypted_master_key;
  if (!tpm_utility_->Bind(auth_key_handle, master_key_str,
                          &encrypted_master_key)) {
    LOG(ERROR) << "Failed to bind user encryption key.";
    return false;
  }
  if (!object_pool_->SetInternalBlob(kEncryptedAuthKey, auth_key_blob) ||
      !object_pool_->SetInternalBlob(kEncryptedMasterKey,
                                     encrypted_master_key)) {
    LOG(ERROR) << "Failed to write key hierarchy blobs.";
    return false;
  }
  brillo::SecureClear(&master_key_str);
  return true;
}

}  // namespace

SlotManagerImpl::SlotManagerImpl(ChapsFactory* factory,
                                 TPMUtility* tpm_utility,
                                 bool auto_load_system_token,
                                 SystemShutdownBlocker* system_shutdown_blocker)
    : factory_(factory),
      last_handle_(0),
      tpm_utility_(tpm_utility),
      auto_load_system_token_(auto_load_system_token),
      is_initialized_(false),
      system_shutdown_blocker_(system_shutdown_blocker) {
  CHECK(factory_);
  CHECK(tpm_utility_);

  // Populate mechanism info for mechanisms supported by all TPM versions.
  mechanism_info_.insert(std::begin(kDefaultMechanismInfo),
                         std::end(kDefaultMechanismInfo));
  if (tpm_utility_->IsTPMAvailable() &&
      tpm_utility_->GetTPMVersion() == TPMVersion::TPM2_0) {
    // Populate mechanism info for mechanisms supported by TPM2.0 only.
    mechanism_info_.insert(std::begin(kTPM2OnlyMechanismInfo),
                           std::end(kTPM2OnlyMechanismInfo));
  }

  // Add default isolate.
  AddIsolate(IsolateCredentialManager::GetDefaultIsolateCredential());

  // By default we'll start with two slots.  This allows for one 'system' slot
  // which always has a token available, and one 'user' slot which will have no
  // token until a login event is received.
  AddSlots(2);
}

SlotManagerImpl::~SlotManagerImpl() {
  LOG(INFO) << "SlotManagerImpl is shutting down.";
  for (size_t i = 0; i < slot_list_.size(); ++i) {
    // Wait for any worker thread to finish.
    if (slot_list_[i].worker_thread.get()) {
      LOG(INFO) << "Waiting for worker thread for slot " << i << " to exit.";
      base::PlatformThread::Join(slot_list_[i].worker_thread_handle);
    }
    if (tpm_utility_->IsTPMAvailable()) {
      // Unload any keys that have been loaded in the TPM.
      LOG(INFO) << "Unloading keys for slot " << i << ".";
      tpm_utility_->UnloadKeysForSlot(i);
    }
  }
  LOG(INFO) << "SlotManagerImpl destructor done.";
}

bool SlotManagerImpl::Init() {
  LogTokenReinitializedFromFlagFile();
  // If the SRK is ready we expect the rest of the init work to succeed.
  bool expect_success =
      tpm_utility_->IsTPMAvailable() && tpm_utility_->IsSRKReady();
  if (!InitStage2() && expect_success)
    return false;

  return true;
}

bool SlotManagerImpl::InitStage2() {
  if (is_initialized_)
    return true;
  if (tpm_utility_->IsTPMAvailable()) {
    if (!tpm_utility_->IsSRKReady()) {
      LOG(ERROR) << "InitStage2 failed because SRK is not ready";
      return false;
    }
    // Mix in some random bytes from the TPM to the openssl prng.
    string random;
    if (!tpm_utility_->GenerateRandom(128, &random)) {
      LOG(ERROR) << "TPM failed to generate random data.";
      return false;
    }
    RAND_seed(ConvertStringToByteBuffer(random.data()), random.length());
  }
  if (auto_load_system_token_) {
    if (base::DirectoryExists(FilePath(kSystemTokenPath))) {
      // Setup the system token.
      int system_slot_id = 0;
      if (!LoadTokenInternal(
              IsolateCredentialManager::GetDefaultIsolateCredential(),
              FilePath(kSystemTokenPath), SecureBlob(kSystemTokenAuthData),
              kSystemTokenLabel, &system_slot_id)) {
        LOG(ERROR) << "Failed to load the system token.";
        return false;
      }
    } else {
      LOG(WARNING) << "System token not loaded because " << kSystemTokenPath
                   << " does not exist.";
    }
  }
  is_initialized_ = true;
  return true;
}

int SlotManagerImpl::GetSlotCount() {
  InitStage2();
  return slot_list_.size();
}

bool SlotManagerImpl::IsTokenAccessible(const SecureBlob& isolate_credential,
                                        int slot_id) const {
  map<SecureBlob, Isolate>::const_iterator isolate_iter =
      isolate_map_.find(isolate_credential);
  if (isolate_iter == isolate_map_.end()) {
    return false;
  }
  const Isolate& isolate = isolate_iter->second;
  return isolate.slot_ids.find(slot_id) != isolate.slot_ids.end();
}

bool SlotManagerImpl::IsTokenPresent(const SecureBlob& isolate_credential,
                                     int slot_id) const {
  CHECK(IsTokenAccessible(isolate_credential, slot_id));
  return IsTokenPresent(slot_id);
}

void SlotManagerImpl::GetSlotInfo(const SecureBlob& isolate_credential,
                                  int slot_id,
                                  CK_SLOT_INFO* slot_info) const {
  CHECK(slot_info);
  CHECK_LT(static_cast<size_t>(slot_id), slot_list_.size());
  CHECK(IsTokenAccessible(isolate_credential, slot_id));

  *slot_info = slot_list_[slot_id].slot_info;
}

void SlotManagerImpl::GetTokenInfo(const SecureBlob& isolate_credential,
                                   int slot_id,
                                   CK_TOKEN_INFO* token_info) const {
  CHECK(token_info);
  CHECK_LT(static_cast<size_t>(slot_id), slot_list_.size());
  CHECK(IsTokenAccessible(isolate_credential, slot_id));
  CHECK(IsTokenPresent(slot_id));

  *token_info = slot_list_[slot_id].token_info;
}

const MechanismMap* SlotManagerImpl::GetMechanismInfo(
    const SecureBlob& isolate_credential, int slot_id) const {
  CHECK_LT(static_cast<size_t>(slot_id), slot_list_.size());
  CHECK(IsTokenAccessible(isolate_credential, slot_id));
  CHECK(IsTokenPresent(slot_id));

  return &mechanism_info_;
}

int SlotManagerImpl::OpenSession(const SecureBlob& isolate_credential,
                                 int slot_id,
                                 bool is_read_only) {
  CHECK_LT(static_cast<size_t>(slot_id), slot_list_.size());
  CHECK(IsTokenAccessible(isolate_credential, slot_id));
  CHECK(IsTokenPresent(slot_id));

  shared_ptr<Session> session(factory_->CreateSession(
      slot_id, slot_list_[slot_id].token_object_pool.get(), tpm_utility_, this,
      is_read_only));
  CHECK(session.get());
  int session_id = CreateHandle();
  slot_list_[slot_id].sessions[session_id] = session;
  session_slot_map_[session_id] = slot_id;
  return session_id;
}

bool SlotManagerImpl::CloseSession(const SecureBlob& isolate_credential,
                                   int session_id) {
  Session* session = NULL;
  if (!GetSession(isolate_credential, session_id, &session))
    return false;
  CHECK(session);
  int slot_id = session_slot_map_[session_id];
  CHECK_LT(static_cast<size_t>(slot_id), slot_list_.size());
  CHECK(IsTokenAccessible(isolate_credential, slot_id));
  session_slot_map_.erase(session_id);
  slot_list_[slot_id].sessions.erase(session_id);
  return true;
}

void SlotManagerImpl::CloseAllSessions(const SecureBlob& isolate_credential,
                                       int slot_id) {
  CHECK_LT(static_cast<size_t>(slot_id), slot_list_.size());
  CHECK(IsTokenAccessible(isolate_credential, slot_id));

  for (map<int, shared_ptr<Session>>::iterator iter =
           slot_list_[slot_id].sessions.begin();
       iter != slot_list_[slot_id].sessions.end(); ++iter) {
    session_slot_map_.erase(iter->first);
  }
  slot_list_[slot_id].sessions.clear();
}

bool SlotManagerImpl::GetSession(const SecureBlob& isolate_credential,
                                 int session_id,
                                 Session** session) const {
  CHECK(session);

  // Lookup which slot this session belongs to.
  map<int, int>::const_iterator session_slot_iter =
      session_slot_map_.find(session_id);
  if (session_slot_iter == session_slot_map_.end())
    return false;
  int slot_id = session_slot_iter->second;
  CHECK_LT(static_cast<size_t>(slot_id), slot_list_.size());
  if (!IsTokenAccessible(isolate_credential, slot_id)) {
    return false;
  }

  // Lookup the session instance.
  map<int, shared_ptr<Session>>::const_iterator session_iter =
      slot_list_[slot_id].sessions.find(session_id);
  if (session_iter == slot_list_[slot_id].sessions.end())
    return false;
  *session = session_iter->second.get();
  return true;
}

bool SlotManagerImpl::OpenIsolate(SecureBlob* isolate_credential,
                                  bool* new_isolate_created) {
  VLOG(1) << "SlotManagerImpl::OpenIsolate enter";

  CHECK(new_isolate_created);
  if (isolate_map_.find(*isolate_credential) != isolate_map_.end()) {
    VLOG(1) << "Incrementing open count for existing isolate.";
    Isolate& isolate = isolate_map_[*isolate_credential];
    ++isolate.open_count;
    *new_isolate_created = false;
  } else {
    VLOG(1) << "Creating new isolate.";
    std::string credential_string;
    if (tpm_utility_->IsTPMAvailable()) {
      if (!tpm_utility_->GenerateRandom(kIsolateCredentialBytes,
                                        &credential_string)) {
        LOG(ERROR) << "Error generating random bytes for isolate credential";
        return false;
      }
    } else {
      credential_string.resize(kIsolateCredentialBytes);
      RAND_bytes(ConvertStringToByteBuffer(credential_string.data()),
                 kIsolateCredentialBytes);
    }
    SecureBlob new_isolate_credential(credential_string);
    brillo::SecureClear(&credential_string);

    if (isolate_map_.find(new_isolate_credential) != isolate_map_.end()) {
      // A collision on 128 bits should be extremely unlikely if the random
      // number generator is working properly. If there is a problem with the
      // random number generator we want to get out.
      LOG(FATAL) << "Collision when trying to create new isolate credential.";
      return false;
    }

    AddIsolate(new_isolate_credential);
    isolate_credential->swap(new_isolate_credential);
    *new_isolate_created = true;
  }
  VLOG(1) << "SlotManagerImpl::OpenIsolate success";
  return true;
}

void SlotManagerImpl::CloseIsolate(const SecureBlob& isolate_credential) {
  VLOG(1) << "SlotManagerImpl::CloseIsolate enter";
  if (isolate_map_.find(isolate_credential) == isolate_map_.end()) {
    LOG(ERROR) << "Attempted Close isolate with invalid isolate credential";
    return;
  }
  Isolate& isolate = isolate_map_[isolate_credential];
  CHECK_GT(isolate.open_count, 0);
  --isolate.open_count;
  if (isolate.open_count == 0) {
    DestroyIsolate(isolate);
  }
  VLOG(1) << "SlotManagerImpl::CloseIsolate success";
}

bool SlotManagerImpl::LoadToken(const SecureBlob& isolate_credential,
                                const FilePath& path,
                                const SecureBlob& auth_data,
                                const string& label,
                                int* slot_id) {
  if (!InitStage2())
    return false;
  return LoadTokenInternal(isolate_credential, path, auth_data, label, slot_id);
}

bool SlotManagerImpl::LoadTokenInternal(const SecureBlob& isolate_credential,
                                        const FilePath& path,
                                        const SecureBlob& auth_data,
                                        const string& label,
                                        int* slot_id) {
  CHECK(slot_id);
  VLOG(1) << "SlotManagerImpl::LoadToken enter";
  if (isolate_map_.find(isolate_credential) == isolate_map_.end()) {
    LOG(ERROR) << "Invalid isolate credential for LoadToken.";
    return false;
  }
  Isolate& isolate = isolate_map_[isolate_credential];

  // If we're already managing this token, just send back the existing slot.
  if (path_slot_map_.find(path) != path_slot_map_.end()) {
    // TODO(rmcilroy): Consider allowing tokens to be loaded in multiple
    // isolates.
    LOG(WARNING) << "Load token event received for existing token.";
    *slot_id = path_slot_map_[path];
    return true;
  }
  // Setup the object pool.
  *slot_id = FindEmptySlot();
  shared_ptr<ObjectPool> object_pool(factory_->CreateObjectPool(
      this, factory_->CreateObjectStore(path),
      factory_->CreateObjectImporter(*slot_id, path, tpm_utility_)));
  CHECK(object_pool.get());

  // Wait for the termination of a previous token.
  if (slot_list_[*slot_id].worker_thread.get())
    base::PlatformThread::Join(slot_list_[*slot_id].worker_thread_handle);

  if (tpm_utility_->IsTPMAvailable()) {
    // Decrypting (or creating) the master key requires the TPM so we'll put
    // this on a worker thread. This has the effect that queries for public
    // objects are responsive but queries for private objects will be waiting
    // for the master key to be ready.
    slot_list_[*slot_id].worker_thread.reset(
        new TokenInitThread(*slot_id, path, auth_data, tpm_utility_,
                            object_pool.get(), system_shutdown_blocker_));
    base::PlatformThread::Create(0, slot_list_[*slot_id].worker_thread.get(),
                                 &slot_list_[*slot_id].worker_thread_handle);
  } else {
    // Load a software-only token.
    LOG(WARNING) << "No TPM is available. Loading a software-only token.";
    if (!LoadSoftwareToken(auth_data, object_pool.get())) {
      return false;
    }
  }

  // Insert the new token into the empty slot.
  slot_list_[*slot_id].token_object_pool = object_pool;
  slot_list_[*slot_id].slot_info.flags |= CKF_TOKEN_PRESENT;
  path_slot_map_[path] = *slot_id;
  CopyStringToCharBuffer(label, slot_list_[*slot_id].token_info.label,
                         base::size(slot_list_[*slot_id].token_info.label));

  // Insert slot into the isolate.
  isolate.slot_ids.insert(*slot_id);
  LOG(INFO) << "Slot " << *slot_id << " ready for token at " << path.value();
  VLOG(1) << "SlotManagerImpl::LoadToken success";
  return true;
}

bool SlotManagerImpl::LoadSoftwareToken(const SecureBlob& auth_data,
                                        ObjectPool* object_pool) {
  SecureBlob auth_key_encrypt =
      Sha256(SecureBlob::Combine(auth_data, SecureBlob(kKeyPurposeEncrypt)));
  SecureBlob auth_key_mac =
      Sha256(SecureBlob::Combine(auth_data, SecureBlob(kKeyPurposeMac)));
  string encrypted_master_key;
  string saved_mac;
  if (!object_pool->GetInternalBlob(kEncryptedMasterKey,
                                    &encrypted_master_key) ||
      !object_pool->GetInternalBlob(kAuthDataHash, &saved_mac)) {
    return InitializeSoftwareToken(auth_data, object_pool);
  }
  if (HmacSha512(kAuthKeyMacInput, auth_key_mac) != saved_mac) {
    LOG(ERROR) << "Bad authorization data, reinitializing token.";
    if (object_pool->DeleteAll() != ObjectPool::Result::Success)
      LOG(WARNING) << "Failed to delete all existing objects.";
    return InitializeSoftwareToken(auth_data, object_pool);
  }
  // Decrypt the master key with the auth data.
  string master_key_str;
  if (!RunCipher(false,  // Decrypt.
                 auth_key_encrypt,
                 std::string(),  // Use a random IV.
                 encrypted_master_key, &master_key_str)) {
    LOG(ERROR) << "Failed to decrypt master key, reinitializing token.";
    if (object_pool->DeleteAll() != ObjectPool::Result::Success)
      LOG(WARNING) << "Failed to delete all existing objects.";
    return InitializeSoftwareToken(auth_data, object_pool);
  }
  SecureBlob master_key(master_key_str);
  brillo::SecureClear(&master_key_str);
  if (!object_pool->SetEncryptionKey(master_key)) {
    LOG(ERROR) << "SetEncryptionKey failed.";
    return false;
  }
  return true;
}

bool SlotManagerImpl::InitializeSoftwareToken(const SecureBlob& auth_data,
                                              ObjectPool* object_pool) {
  // Generate a new random master key and encrypt it with the auth data.
  SecureBlob master_key(kUserKeySize);
  if (1 != RAND_bytes(master_key.data(), kUserKeySize)) {
    LOG(ERROR) << "RAND_bytes failed: " << GetOpenSSLError();
    return false;
  }
  SecureBlob auth_key_encrypt =
      Sha256(SecureBlob::Combine(auth_data, SecureBlob(kKeyPurposeEncrypt)));
  string encrypted_master_key;
  if (!RunCipher(true,  // Encrypt.
                 auth_key_encrypt,
                 std::string(),  // Use a random IV.
                 master_key.to_string(), &encrypted_master_key)) {
    LOG(ERROR) << "Failed to encrypt new master key.";
    return false;
  }
  SecureBlob auth_key_mac =
      Sha256(SecureBlob::Combine(auth_data, SecureBlob(kKeyPurposeMac)));
  if (!object_pool->SetInternalBlob(kEncryptedMasterKey,
                                    encrypted_master_key) ||
      !object_pool->SetInternalBlob(
          kAuthDataHash, HmacSha512(kAuthKeyMacInput, auth_key_mac))) {
    LOG(ERROR) << "Failed to write new master key blobs.";
    return false;
  }
  if (!object_pool->SetEncryptionKey(master_key)) {
    LOG(ERROR) << "SetEncryptionKey failed.";
    return false;
  }
  return true;
}

void SlotManagerImpl::UnloadToken(const SecureBlob& isolate_credential,
                                  const FilePath& path) {
  VLOG(1) << "SlotManagerImpl::UnloadToken";
  if (isolate_map_.find(isolate_credential) == isolate_map_.end()) {
    LOG(WARNING) << "Invalid isolate credential for UnloadToken.";
    return;
  }
  Isolate& isolate = isolate_map_[isolate_credential];

  // If we're not managing this token, ignore the event.
  if (path_slot_map_.find(path) == path_slot_map_.end()) {
    LOG(WARNING) << "Unload Token event received for unknown path: "
                 << path.value();
    return;
  }
  int slot_id = path_slot_map_[path];
  if (!IsTokenAccessible(isolate_credential, slot_id))
    LOG(WARNING) << "Attempted to unload token with invalid isolate credential";

  // Wait for initialization to be finished before cleaning up.
  if (slot_list_[slot_id].worker_thread.get())
    base::PlatformThread::Join(slot_list_[slot_id].worker_thread_handle);

  if (tpm_utility_->IsTPMAvailable()) {
    // Spawn a thread to handle the TPM-related work.
    slot_list_[slot_id].worker_thread.reset(
        new TokenTermThread(slot_id, tpm_utility_));
    base::PlatformThread::Create(0, slot_list_[slot_id].worker_thread.get(),
                                 &slot_list_[slot_id].worker_thread_handle);
  }
  CloseAllSessions(isolate_credential, slot_id);
  slot_list_[slot_id].token_object_pool.reset();
  slot_list_[slot_id].slot_info.flags &= ~CKF_TOKEN_PRESENT;
  path_slot_map_.erase(path);
  // Remove slot from the isolate.
  isolate.slot_ids.erase(slot_id);
  LOG(INFO) << "Token at " << path.value() << " has been removed from slot "
            << slot_id;
  VLOG(1) << "SlotManagerImpl::Unload token success";
}

void SlotManagerImpl::ChangeTokenAuthData(const FilePath& path,
                                          const SecureBlob& old_auth_data,
                                          const SecureBlob& new_auth_data) {
  if (!InitStage2()) {
    LOG(ERROR) << "Initialization failed; ignoring change auth event.";
    return;
  }
  // This event can be handled whether or not we are already managing the token
  // but if we're not, we won't start until a Load Token event comes in.
  ObjectPool* object_pool = NULL;
  std::unique_ptr<ObjectPool> scoped_object_pool;
  int slot_id = 0;
  bool unload = false;
  if (path_slot_map_.find(path) == path_slot_map_.end()) {
    object_pool = factory_->CreateObjectPool(
        this, factory_->CreateObjectStore(path), NULL);
    scoped_object_pool.reset(object_pool);
    slot_id = FindEmptySlot();
    unload = true;
  } else {
    slot_id = path_slot_map_[path];
    object_pool = slot_list_[slot_id].token_object_pool.get();
  }
  CHECK(object_pool);
  if (tpm_utility_->IsTPMAvailable()) {
    // Before we attempt the change, sanity check old_auth_data.
    string saved_auth_data_hash;
    object_pool->GetInternalBlob(kAuthDataHash, &saved_auth_data_hash);
    if (!SanityCheckAuthData(HashAuthData(old_auth_data),
                             saved_auth_data_hash)) {
      LOG(ERROR) << "Old authorization data is not correct.";
      return;
    }
    string auth_key_blob;
    string new_auth_key_blob;
    if (!object_pool->GetInternalBlob(kEncryptedAuthKey, &auth_key_blob)) {
      LOG(INFO) << "Token not initialized; ignoring change auth data event.";
    } else if (!tpm_utility_->ChangeAuthData(slot_id, Sha1(old_auth_data),
                                             Sha1(new_auth_data), auth_key_blob,
                                             &new_auth_key_blob)) {
      LOG(ERROR) << "Failed to change auth data for token at " << path.value();
    } else if (!object_pool->SetInternalBlob(kEncryptedAuthKey,
                                             new_auth_key_blob)) {
      LOG(ERROR) << "Failed to write changed auth blob for token at "
                 << path.value();
    } else if (!object_pool->SetInternalBlob(kAuthDataHash,
                                             HashAuthData(new_auth_data))) {
      LOG(ERROR) << "Failed to write auth data hash for token at "
                 << path.value();
    }
    if (unload)
      tpm_utility_->UnloadKeysForSlot(slot_id);
  } else {
    // We're working with a software-only token.
    string encrypted_master_key;
    string saved_mac;
    if (!object_pool->GetInternalBlob(kEncryptedMasterKey,
                                      &encrypted_master_key) ||
        !object_pool->GetInternalBlob(kAuthDataHash, &saved_mac)) {
      LOG(INFO) << "Token not initialized; ignoring change auth data event.";
      return;
    }
    // Check if old_auth_data is valid.
    SecureBlob old_auth_key_mac =
        Sha256(SecureBlob::Combine(old_auth_data, SecureBlob(kKeyPurposeMac)));
    if (HmacSha512(kAuthKeyMacInput, old_auth_key_mac) != saved_mac) {
      LOG(ERROR) << "Old authorization data is not correct.";
      return;
    }
    // Decrypt the master key with the old_auth_data.
    SecureBlob old_auth_key_encrypt = Sha256(
        SecureBlob::Combine(old_auth_data, SecureBlob(kKeyPurposeEncrypt)));
    string master_key;
    if (!RunCipher(false,  // Decrypt.
                   old_auth_key_encrypt,
                   std::string(),  // Use a random IV.
                   encrypted_master_key, &master_key)) {
      LOG(ERROR) << "Failed to decrypt master key with old auth data.";
      return;
    }
    // Encrypt the master key with the new_auth_data.
    SecureBlob new_auth_key_encrypt = Sha256(
        SecureBlob::Combine(new_auth_data, SecureBlob(kKeyPurposeEncrypt)));
    if (!RunCipher(true,  // Encrypt.
                   new_auth_key_encrypt,
                   std::string(),  // Use a random IV.
                   master_key, &encrypted_master_key)) {
      LOG(ERROR) << "Failed to encrypt master key with new auth data.";
      return;
    }
    brillo::SecureClear(&master_key);
    // Write out the new blobs.
    SecureBlob new_auth_key_mac =
        Sha256(SecureBlob::Combine(new_auth_data, SecureBlob(kKeyPurposeMac)));
    if (!object_pool->SetInternalBlob(kEncryptedMasterKey,
                                      encrypted_master_key) ||
        !object_pool->SetInternalBlob(
            kAuthDataHash, HmacSha512(kAuthKeyMacInput, new_auth_key_mac))) {
      LOG(ERROR) << "Failed to write new master key blobs.";
      return;
    }
  }
}

bool SlotManagerImpl::GetTokenPath(const SecureBlob& isolate_credential,
                                   int slot_id,
                                   FilePath* path) {
  if (!IsTokenAccessible(isolate_credential, slot_id))
    return false;
  if (!IsTokenPresent(slot_id))
    return false;
  return PathFromSlotId(slot_id, path);
}

bool SlotManagerImpl::IsTokenPresent(int slot_id) const {
  CHECK_LT(static_cast<size_t>(slot_id), slot_list_.size());

  return ((slot_list_[slot_id].slot_info.flags & CKF_TOKEN_PRESENT) ==
          CKF_TOKEN_PRESENT);
}

int SlotManagerImpl::CreateHandle() {
  AutoLock lock(handle_generator_lock_);
  // If we use this many handles, we have a problem.
  CHECK(last_handle_ < std::numeric_limits<int>::max());
  return ++last_handle_;
}

void SlotManagerImpl::GetDefaultInfo(CK_SLOT_INFO* slot_info,
                                     CK_TOKEN_INFO* token_info) {
  memset(slot_info, 0, sizeof(CK_SLOT_INFO));
  CopyStringToCharBuffer(kSlotDescription, slot_info->slotDescription,
                         base::size(slot_info->slotDescription));
  CopyStringToCharBuffer(kManufacturerID, slot_info->manufacturerID,
                         base::size(slot_info->manufacturerID));
  slot_info->flags = CKF_HW_SLOT | CKF_REMOVABLE_DEVICE;
  slot_info->hardwareVersion = kDefaultVersion;
  slot_info->firmwareVersion = kDefaultVersion;

  memset(token_info, 0, sizeof(CK_TOKEN_INFO));
  CopyStringToCharBuffer(kTokenLabel, token_info->label,
                         base::size(token_info->label));
  CopyStringToCharBuffer(kManufacturerID, token_info->manufacturerID,
                         base::size(token_info->manufacturerID));
  CopyStringToCharBuffer(kTokenModel, token_info->model,
                         base::size(token_info->model));
  CopyStringToCharBuffer(kTokenSerialNumber, token_info->serialNumber,
                         base::size(token_info->serialNumber));
  token_info->flags = CKF_RNG | CKF_USER_PIN_INITIALIZED |
                      CKF_PROTECTED_AUTHENTICATION_PATH | CKF_TOKEN_INITIALIZED;
  token_info->ulMaxSessionCount = CK_EFFECTIVELY_INFINITE;
  token_info->ulSessionCount = CK_UNAVAILABLE_INFORMATION;
  token_info->ulMaxRwSessionCount = CK_EFFECTIVELY_INFINITE;
  token_info->ulRwSessionCount = CK_UNAVAILABLE_INFORMATION;
  token_info->ulMaxPinLen = kMaxPinLen;
  token_info->ulMinPinLen = kMinPinLen;
  token_info->ulTotalPublicMemory = CK_UNAVAILABLE_INFORMATION;
  token_info->ulFreePublicMemory = CK_UNAVAILABLE_INFORMATION;
  token_info->ulTotalPrivateMemory = CK_UNAVAILABLE_INFORMATION;
  token_info->ulFreePrivateMemory = CK_UNAVAILABLE_INFORMATION;
  token_info->hardwareVersion = kDefaultVersion;
  token_info->firmwareVersion = kDefaultVersion;
}

int SlotManagerImpl::FindEmptySlot() {
  size_t i = 0;
  for (; i < slot_list_.size(); ++i) {
    if (!IsTokenPresent(i))
      return i;
  }
  // Add a new slot.
  AddSlots(1);
  return i;
}

void SlotManagerImpl::AddSlots(int num_slots) {
  for (int i = 0; i < num_slots; ++i) {
    Slot slot;
    GetDefaultInfo(&slot.slot_info, &slot.token_info);
    LOG(INFO) << "Adding slot: " << slot_list_.size();
    slot_list_.push_back(slot);
  }
}

void SlotManagerImpl::AddIsolate(const SecureBlob& isolate_credential) {
  Isolate isolate;
  isolate.credential = isolate_credential;
  isolate.open_count = 1;
  isolate_map_[isolate_credential] = isolate;
}

void SlotManagerImpl::DestroyIsolate(const Isolate& isolate) {
  CHECK_EQ(isolate.open_count, 0);

  // Unload any existing tokens in this isolate.
  while (!isolate.slot_ids.empty()) {
    int slot_id = *isolate.slot_ids.begin();
    FilePath path;
    CHECK(PathFromSlotId(slot_id, &path));
    UnloadToken(isolate.credential, path);
  }

  isolate_map_.erase(isolate.credential);
}

bool SlotManagerImpl::PathFromSlotId(int slot_id, FilePath* path) const {
  CHECK(path);
  map<FilePath, int>::const_iterator path_iter;
  for (path_iter = path_slot_map_.begin(); path_iter != path_slot_map_.end();
       ++path_iter) {
    if (path_iter->second == slot_id) {
      *path = path_iter->first;
      return true;
    }
  }
  return false;
}

}  // namespace chaps
