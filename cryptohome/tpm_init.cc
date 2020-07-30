// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class TpmInit

#include "cryptohome/tpm_init.h"

#include <stdint.h>

#include <string>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/platform.h"

using base::FilePath;
using base::PlatformThread;
using base::PlatformThreadHandle;
using brillo::SecureBlob;

namespace cryptohome {

const int kMaxTimeoutRetries = 5;

const FilePath kMiscTpmCheckEnabledFile("/sys/class/misc/tpm0/device/enabled");
const FilePath kMiscTpmCheckOwnedFile("/sys/class/misc/tpm0/device/owned");
const FilePath kTpmTpmCheckEnabledFile("/sys/class/tpm/tpm0/device/enabled");
const FilePath kTpmTpmCheckOwnedFile("/sys/class/tpm/tpm0/device/owned");
const FilePath kDefaultCryptohomeKeyFile("/home/.shadow/cryptohome.key");

const int kOwnerPasswordLength = 12;
const unsigned int kDefaultTpmRsaKeyBits = 2048;

// TpmInitTask is a private class used to handle asynchronous initialization of
// the TPM.
class TpmInitTask : public PlatformThread::Delegate {
 public:
  TpmInitTask() : tpm_(NULL), init_(NULL) {}
  virtual ~TpmInitTask() {}

  void Init(TpmInit* init) {
    init_ = init;
    if (tpm_) {
      init->SetupTpm(false);
    }
  }

  virtual void ThreadMain() {
    if (init_) {
      init_->ThreadMain();
    }
  }

  void set_tpm(Tpm* tpm) { tpm_ = tpm; }
  Tpm* get_tpm() { return tpm_; }

 private:
  Tpm* tpm_;
  TpmInit* init_;
};

TpmInit::TpmInit(Tpm* tpm, Platform* platform)
    : tpm_init_task_(new TpmInitTask()),
      platform_(platform),
      tpm_persistent_state_(platform) {
  set_tpm(tpm);
}

TpmInit::~TpmInit() {
  if (!init_thread_.is_null()) {
    // Must wait for tpm init thread to complete, because when the main thread
    // exits some libtspi data structures are freed.
    PlatformThread::Join(init_thread_);
    init_thread_ = PlatformThreadHandle();
  }
}

void TpmInit::set_tpm(Tpm* value) {
  if (tpm_init_task_.get())
    tpm_init_task_->set_tpm(value);
}

Tpm* TpmInit::get_tpm() {
  if (tpm_init_task_.get())
    return tpm_init_task_->get_tpm();
  return NULL;
}

void TpmInit::Init(OwnershipCallback ownership_callback) {
  ownership_callback_ = ownership_callback;
  tpm_init_task_->Init(this);
}

bool TpmInit::AsyncTakeOwnership() {
  tpm_persistent_state_.SetShallInitialize(true);
  take_ownership_called_ = true;
  if (!PlatformThread::Create(0, tpm_init_task_.get(), &init_thread_)) {
    LOG(ERROR) << "Unable to create background thread to take TPM ownership.";
    return false;
  }
  return true;
}

bool TpmInit::IsTpmReady() {
  // The TPM is "ready" if it is enabled, owned, and not being owned.
  Tpm* tpm = tpm_init_task_->get_tpm();
  if (!tpm->IsEnabled() || !tpm->IsOwned() || tpm->IsBeingOwned()) {
    return false;
  }
  // When |tpm| implementation uses tpm manager, readiness flag in
  // |TpmPersistentState| is meaningless since the tpm manager also take care of
  // all the follow-up actions after taking ownership.
  return tpm->DoesUseTpmManager() || tpm_persistent_state_.IsReady();
}

bool TpmInit::IsTpmEnabled() {
  return tpm_init_task_->get_tpm()->IsEnabled();
}

bool TpmInit::IsTpmOwned() {
  return tpm_init_task_->get_tpm()->IsOwned();
}

void TpmInit::SetTpmOwned(bool owned) {
  tpm_init_task_->get_tpm()->SetIsOwned(owned);
}

bool TpmInit::IsTpmBeingOwned() {
  return tpm_init_task_->get_tpm()->IsBeingOwned();
}

void TpmInit::SetTpmBeingOwned(bool being_owned) {
  tpm_init_task_->get_tpm()->SetIsBeingOwned(being_owned);
}

bool TpmInit::OwnershipRequested() {
  return take_ownership_called_;
}

bool TpmInit::GetTpmPassword(brillo::SecureBlob* password) {
  return tpm_init_task_->get_tpm()->GetOwnerPassword(password);
}

void TpmInit::ClearStoredTpmPassword() {
  if (tpm_persistent_state_.ClearStoredPasswordIfNotNeeded()) {
    get_tpm()->ClearStoredPassword();
  }
}

void TpmInit::ThreadMain() {
  base::TimeTicks start = base::TimeTicks::Now();
  bool ownership_result = TakeOwnership(&took_ownership_);
  base::TimeDelta delta = (base::TimeTicks::Now() - start);
  initialization_time_ = delta.InMilliseconds();
  if (took_ownership_) {
    LOG(ERROR) << "Taking TPM ownership took " << initialization_time_ << "ms";
  }
  if (!ownership_callback_.is_null()) {
    ownership_callback_.Run(ownership_result, took_ownership_);
  }
}

bool TpmInit::SetupTpm(bool load_key) {
  const bool was_initialized = get_tpm()->IsInitialized();
  if (!was_initialized) {
    get_tpm()->SetIsInitialized(true);
    RestoreTpmStateFromStorage();
  }

  if (load_key) {
    // load cryptohome key
    LoadOrCreateCryptohomeKey(&cryptohome_key_);
  }
  return !was_initialized;
}

void TpmInit::RestoreTpmStateFromStorage() {
  // Checking disabled and owned either via sysfs or via TSS calls will block if
  // ownership is being taken by another thread or process.  So for this to work
  // well, SetupTpm() needs to be called before TakeOwnership() is called.  At
  // that point, the public API for Tpm only checks these booleans, so other
  // threads can check without being blocked.  TakeOwnership() will reset the
  // TPM's is_owned_ bit on success.
  bool is_enabled = false;
  bool is_owned = false;
  bool successful_check = false;
  if (platform_->FileExists(kTpmTpmCheckEnabledFile)) {
    is_enabled = IsEnabledCheckViaSysfs(kTpmTpmCheckEnabledFile);
    is_owned = IsOwnedCheckViaSysfs(kTpmTpmCheckOwnedFile);
    successful_check = true;
  } else if (platform_->FileExists(kMiscTpmCheckEnabledFile)) {
    is_enabled = IsEnabledCheckViaSysfs(kMiscTpmCheckEnabledFile);
    is_owned = IsOwnedCheckViaSysfs(kMiscTpmCheckOwnedFile);
    successful_check = true;
  } else {
    if (get_tpm()->PerformEnabledOwnedCheck(&is_enabled, &is_owned)) {
      successful_check = true;
    }
  }

  get_tpm()->SetIsOwned(is_owned);
  get_tpm()->SetIsEnabled(is_enabled);

  if (successful_check && !is_owned) {
    tpm_persistent_state_.SetReady(false);
    tpm_persistent_state_.ClearStatus();
  }

  SecureBlob local_owner_password;
  if (LoadOwnerPassword(&local_owner_password)) {
    get_tpm()->SetOwnerPassword(local_owner_password);
  }
}

bool TpmInit::TakeOwnership(bool* OUT_took_ownership) {
  if (OUT_took_ownership) {
    *OUT_took_ownership = false;
  }

  if (!IsTpmEnabled()) {
    return false;
  }

  SecureBlob default_owner_password(sizeof(kTpmWellKnownPassword));
  memcpy(default_owner_password.data(), kTpmWellKnownPassword,
         sizeof(kTpmWellKnownPassword));

  bool took_ownership = false;
  if (!IsTpmOwned()) {
    SetTpmBeingOwned(true);
    tpm_persistent_state_.SetReady(false);
    tpm_persistent_state_.ClearStatus();

    if (!get_tpm()->IsEndorsementKeyAvailable()) {
      if (!get_tpm()->CreateEndorsementKey()) {
        LOG(ERROR) << "Failed to create endorsement key";
        SetTpmBeingOwned(false);
        return false;
      }
    }

    if (!get_tpm()->IsEndorsementKeyAvailable()) {
      LOG(ERROR) << "Endorsement key is not available";
      SetTpmBeingOwned(false);
      return false;
    }

    if (!get_tpm()->TakeOwnership(kMaxTimeoutRetries, default_owner_password)) {
      LOG(ERROR) << "Take Ownership failed";
      SetTpmBeingOwned(false);
      return false;
    }

    tpm_persistent_state_.SetDefaultPassword();
    SetTpmOwned(true);
    took_ownership = true;
  }

  if (OUT_took_ownership) {
    *OUT_took_ownership = took_ownership;
  }

  // In monolithic mode, if we can open the TPM with the default password, then
  // we still need to zero the SRK password and unrestrict it, then change the
  // owner password.
  if (!tpm_persistent_state_.IsReady() && !get_tpm()->DoesUseTpmManager() &&
      get_tpm()->TestTpmAuth(default_owner_password)) {
    if (!get_tpm()->InitializeSrk(default_owner_password)) {
      LOG(ERROR) << "Couldn't initialize the SRK";
      SetTpmBeingOwned(false);
      return false;
    }

    SecureBlob owner_password;
    CreateOwnerPassword(&owner_password);

    SecureBlob sealed_password;
    if (!get_tpm()->SealToPCR0(owner_password, &sealed_password)) {
      LOG(ERROR) << "Failed to seal owner password.";
      return false;
    }
    if (!tpm_persistent_state_.SetSealedPassword(sealed_password)) {
      LOG(ERROR) << "Couldn't store the owner password.";
      return false;
    }

    if ((!get_tpm()->ChangeOwnerPassword(default_owner_password,
                                         owner_password))) {
      LOG(ERROR) << "Couldn't change the owner password.";
      return false;
    }
    get_tpm()->SetOwnerPassword(owner_password);
  }

  // If we fall through here, either (1) we successfully completed the
  // initialization, or (2) then the TPM owned file doesn't exist, but we
  // couldn't auth with the well-known password. In the second case, we must
  // assume that the TPM has already been owned and set to a random password.
  // In any case, it's time to touch the TPM owned file to indicate that we
  // don't need to re-attempt completing initialization on the next boot.
  tpm_persistent_state_.SetReady(true);
  tpm_persistent_state_.SetShallInitialize(false);

  SetTpmBeingOwned(false);
  return true;
}

void TpmInit::CreateOwnerPassword(SecureBlob* password) {
  // Generate a random owner password.  The default is a 12-character,
  // hex-encoded password created from 6 bytes of random data.
  const auto random =
      CryptoLib::CreateSecureRandomBlob(kOwnerPasswordLength / 2);
  SecureBlob tpm_password(kOwnerPasswordLength);
  CryptoLib::SecureBlobToHexToBuffer(random, tpm_password.data(),
                                     tpm_password.size());
  password->swap(tpm_password);
}

bool TpmInit::LoadOwnerPassword(brillo::SecureBlob* owner_password) {
  SecureBlob sealed_password;
  if (!tpm_persistent_state_.GetSealedPassword(&sealed_password)) {
    return false;
  }
  if (sealed_password.empty()) {
    // Empty password means default password.
    owner_password->resize(sizeof(kTpmWellKnownPassword));
    memcpy(owner_password->data(), kTpmWellKnownPassword,
           sizeof(kTpmWellKnownPassword));
    return true;
  }
  if (!get_tpm()->Unseal(sealed_password, owner_password)) {
    LOG(ERROR) << "Failed to unseal the owner password.";
    return false;
  }
  return true;
}

void TpmInit::RemoveTpmOwnerDependency(
    TpmPersistentState::TpmOwnerDependency dependency) {
  if (!get_tpm()->RemoveOwnerDependency(dependency)) {
    return;
  }
  tpm_persistent_state_.ClearDependency(dependency);
}

bool TpmInit::CheckSysfsForOne(const FilePath& file_name) const {
  std::string contents;
  if (!platform_->ReadFileToString(file_name, &contents)) {
    return false;
  }
  if (contents.size() < 1) {
    return false;
  }
  return (contents[0] == '1');
}

bool TpmInit::IsEnabledCheckViaSysfs(const FilePath& enabled_file) {
  return CheckSysfsForOne(enabled_file);
}

bool TpmInit::IsOwnedCheckViaSysfs(const FilePath& owned_file) {
  return CheckSysfsForOne(owned_file);
}

bool TpmInit::CreateCryptohomeKey() {
  if (!IsTpmReady()) {
    LOG(WARNING) << "Canceled creating cryptohome key - TPM is not ready.";
    return false;
  }
  SecureBlob n;
  SecureBlob p;
  if (!CryptoLib::CreateRsaKey(kDefaultTpmRsaKeyBits, &n, &p)) {
    LOG(ERROR) << "Error creating RSA key";
    return false;
  }
  SecureBlob wrapped_key;
  if (!get_tpm()->WrapRsaKey(n, p, &wrapped_key)) {
    LOG(ERROR) << "Couldn't wrap cryptohome key";
    return false;
  }

  if (!SaveCryptohomeKey(wrapped_key)) {
    LOG(ERROR) << "Couldn't save cryptohome key";
    return false;
  }

  LOG(INFO) << "Created new cryptohome key.";
  return true;
}

bool TpmInit::SaveCryptohomeKey(const brillo::SecureBlob& wrapped_key) {
  bool ok = platform_->WriteSecureBlobToFileAtomicDurable(
      kDefaultCryptohomeKeyFile, wrapped_key, 0600);
  if (!ok)
    LOG(ERROR) << "Error writing key file of desired size: "
               << wrapped_key.size();
  return ok;
}

Tpm::TpmRetryAction TpmInit::LoadCryptohomeKey(ScopedKeyHandle* key_handle) {
  CHECK(key_handle);
  // First, try loading the key from the key file.
  {
    SecureBlob raw_key;
    if (platform_->ReadFileToSecureBlob(kDefaultCryptohomeKeyFile, &raw_key)) {
      Tpm::TpmRetryAction retry_action =
          get_tpm()->LoadWrappedKey(raw_key, key_handle);
      if (retry_action == Tpm::kTpmRetryNone ||
          get_tpm()->IsTransient(retry_action)) {
        return retry_action;
      }
    }
  }

  // Then try loading the key by the UUID (this is a legacy upgrade path).
  SecureBlob raw_key;
  if (!get_tpm()->LegacyLoadCryptohomeKey(key_handle, &raw_key)) {
    return Tpm::kTpmRetryFailNoRetry;
  }

  // Save the cryptohome key to the well-known location.
  if (!SaveCryptohomeKey(raw_key)) {
    LOG(ERROR) << "Couldn't save cryptohome key";
    return Tpm::kTpmRetryFailNoRetry;
  }
  return Tpm::kTpmRetryNone;
}

bool TpmInit::LoadOrCreateCryptohomeKey(ScopedKeyHandle* key_handle) {
  CHECK(key_handle);
  // Try to load the cryptohome key.
  Tpm::TpmRetryAction retry_action = LoadCryptohomeKey(key_handle);
  if (retry_action != Tpm::kTpmRetryNone &&
      !get_tpm()->IsTransient(retry_action)) {
    // The key couldn't be loaded, and it wasn't due to a transient error,
    // so we must create the key.
    if (CreateCryptohomeKey()) {
      retry_action = LoadCryptohomeKey(key_handle);
    }
  }
  return retry_action == Tpm::kTpmRetryNone;
}

bool TpmInit::HasCryptohomeKey() {
  return (cryptohome_key_.value() != kInvalidKeyHandle);
}

TpmKeyHandle TpmInit::GetCryptohomeKey() {
  return cryptohome_key_.value();
}

bool TpmInit::ReloadCryptohomeKey() {
  CHECK(HasCryptohomeKey());
  // Release the handle first, we know this handle doesn't contain a loaded key
  // since ReloadCryptohomeKey only called after we failed to use it.
  // Otherwise we may flush the newly loaded key and fail to use it again,
  // if it is loaded to the same handle.
  // TODO(crbug.com/687330): change to closing the handle and ignoring errors
  // once checking for stale virtual handles is implemented in trunksd.
  cryptohome_key_.release();
  if (LoadCryptohomeKey(&cryptohome_key_) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Error reloading Cryptohome key.";
    return false;
  }
  return true;
}

bool TpmInit::GetVersion(Tpm::TpmVersionInfo* version_info) {
  return get_tpm() && get_tpm()->GetVersionInfo(version_info);
}

bool TpmInit::ShallInitialize() {
  return tpm_persistent_state_.ShallInitialize();
}

}  // namespace cryptohome
