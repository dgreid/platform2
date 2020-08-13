// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/install_attributes.h"

#include <sys/types.h>

#include <limits>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/time/time.h>

#include "cryptohome/install_attributes.pb.h"
#include "cryptohome/lockbox.h"
#include "cryptohome/tpm_init.h"

using base::FilePath;

namespace cryptohome {

// By default, we store this with other cryptohome state.
const char InstallAttributes::kDefaultDataFile[] =
  "/home/.shadow/install_attributes.pb";
const mode_t InstallAttributes::kDataFilePermissions = 0644;
// This is the default location for the cache file.
const char InstallAttributes::kDefaultCacheFile[] =
  "/run/lockbox/install_attributes.pb";
const mode_t InstallAttributes::kCacheFilePermissions = 0644;

InstallAttributes::InstallAttributes(Tpm* tpm)
    : data_file_(kDefaultDataFile),
      cache_file_(kDefaultCacheFile),
      default_attributes_(new SerializedInstallAttributes()),
      default_lockbox_(new Lockbox(tpm, Tpm::kLockboxIndex)),
      default_platform_(new Platform()),
      attributes_(default_attributes_.get()),
      lockbox_(default_lockbox_.get()),
      platform_(default_platform_.get()) {
  SetTpm(tpm);  // make sure to check TPM status if needed.
  version_ = attributes_->version();  // versioning controlled by pb default.
}

InstallAttributes::~InstallAttributes() {}

void InstallAttributes::SetTpm(Tpm* tpm) {
  // Technically, it is safe to call SetTpm, then Init() again, but it could
  // also cause weirdness and report that data is TPM-backed when it isn't.
  DCHECK(status_ != Status::kValid && status_ != Status::kFirstInstall)
      << "SetTpm used after a successful Init().";
  if (tpm && !tpm->IsEnabled()) {
    LOG(WARNING) << "set_tpm() missing or disabled TPM provided.";
    tpm = NULL;  // Don't give it to Lockbox.
  }
  set_is_secure(tpm != NULL);
  lockbox_->set_tpm(tpm);
}

bool InstallAttributes::Init(TpmInit* tpm_init) {
  // Ensure that if Init() was called and it failed, we can retry cleanly.
  attributes_->Clear();
  status_ = Status::kUnknown;

  // Read the cache file. If it exists, lockbox-cache has successfully
  // verified install attributes during early boot, so use them.
  brillo::Blob blob;
  if (platform_->ReadFile(cache_file_, &blob)) {
    if (!attributes_->ParseFromArray(
           static_cast<google::protobuf::uint8*>(blob.data()),
           blob.size())) {
      LOG(ERROR) << "Failed to parse data file (" << blob.size() << " bytes)";
      attributes_->Clear();
      status_ = Status::kInvalid;
      return false;
    }

    status_ = Status::kValid;
    // Install attributes are valid, no need to hold owner dependency. So,
    // repeat removing owner dependency in case it didn't succeed during the
    // first boot.
    tpm_init->RemoveTpmOwnerDependency(
        TpmPersistentState::TpmOwnerDependency::kInstallAttributes);
    LOG(INFO) << "Valid install attributes cache found.";
    return true;
  }

  // No cache file, so TPM lockbox is either not yet set up or data is invalid.
  if (!is_secure()) {
    LOG(INFO) << "Init() assuming first-time install for TPM-less system.";
    status_ = Status::kFirstInstall;
    return true;
  }

  // TPM not ready yet, i.e. setup after ownership not completed. Init() is
  // supposed to get invoked again once the TPM is owned and configured.
  if (!tpm_init->IsTpmReady()) {
    // To ensure that we get a fresh start after taking ownership, remove the
    // data file when we boot up after a TPM clear. If we didn't do so, the
    // previous data file might validate against a left-around NVRAM space,
    // incorrectly indicating that install attributes are already initialized
    // and locked.
    //
    // Note that we theoretically could delete the file unconditionally here,
    // since IsTpmReady() should always return true after the TPM has been set
    // up correctly. However, previous experience tells us that there might be
    // hardware glitches or driver/firmware bugs that could make the TPM look
    // disabled. If we accidentally delete the data file in such a situation,
    // we'd make install attributes inconsistent until next TPM clear, which has
    // far-reaching consequences (enterprise enrollment would be lost for
    // example). Thus, be careful and only clear data if the TPM looks
    // positively unowned.
    if (tpm_init->IsTpmEnabled() && !tpm_init->IsTpmOwned()) {
      ClearData();
      // Don't flag invalid here - Chrome verifies that install attributes
      // aren't invalid before locking them as part of enterprise enrollment.
      LOG(INFO) << "Init() TPM is not ready and not owned, while install "
        << "attributes are missing or invalid.";
      status_ = Status::kTpmNotOwned;
      return false;
    }

    // Cases that don't look like a cleared TPM get flagged invalid.
    LOG(INFO) << "Init() TPM is not cleared, but install attributes are "
      << "missing or invalid.";
    status_ = Status::kInvalid;
    return false;
  }

  // The TPM is ready and we haven't found valid install attributes. This
  // usually means that we haven't written and locked the lockbox yet, so
  // attempt a reset. The reset may fail in various other edge cases and the
  // error code lets us identify and handle these edge cases correctly.
  LockboxError error_id;
  if (!lockbox()->Reset(&error_id)) {
    switch (error_id) {
      case LockboxError::kNvramSpaceAbsent:
        // Legacy install that didn't create space at OOBE.
        status_ = Status::kValid;
        tpm_init->RemoveTpmOwnerDependency(
            TpmPersistentState::TpmOwnerDependency::kInstallAttributes);
        LOG(INFO) << "Found legacy install that didn't create install "
                     "attributes NVRAM space at OOBE.";
        return true;
      case LockboxError::kNvramInvalid:
        LOG(ERROR) << "Inconsistent install attributes state.";
        status_ = Status::kInvalid;
        return false;
      case LockboxError::kTpmUnavailable:
        NOTREACHED() << "Should never call lockbox when TPM is unavailable.";
        status_ = Status::kInvalid;
        return false;
      case LockboxError::kTpmError:
        LOG(ERROR) << "TPM error on install attributes initialization.";
        status_ = Status::kInvalid;
        return false;
    }
  }

  // Reset succeeded, so we have a writable lockbox now.
  // Delete data file potentially left around from previous installation.
  if (!ClearData()) {
    // ClearData() will log its own error message if it fails.
    return false;
  }

  status_ = Status::kFirstInstall;
  tpm_init->RemoveTpmOwnerDependency(
      TpmPersistentState::TpmOwnerDependency::kInstallAttributes);
  LOG(INFO) << "Install attributes reset back to first install.";
  return true;
}

int InstallAttributes::FindIndexByName(const std::string& name) const {
  std::string name_str(name);
  for (int i = 0; i < attributes_->attributes_size(); ++i) {
    if (attributes_->attributes(i).name().compare(name_str) == 0)
      return i;
  }
  return -1;
}

bool InstallAttributes::Get(const std::string& name,
                            brillo::Blob* value) const {
  int index = FindIndexByName(name);
  if (index == -1)
    return false;
  return GetByIndex(index, NULL, value);
}

bool InstallAttributes::GetByIndex(int index,
                                   std::string* name,
                                   brillo::Blob* value) const {
  if (index < 0 || index >= attributes_->attributes_size()) {
    LOG(ERROR) << "GetByIndex called with invalid index.";
    return false;
  }
  const SerializedInstallAttributes::Attribute* const attr =
    &attributes_->attributes(index);
  if (name) {
    name->assign(attr->name());
  }
  if (value) {
    value->resize(attr->value().length());
    memcpy(&value->at(0), attr->value().c_str(), value->size());
  }
  return true;
}

bool InstallAttributes::Set(const std::string& name,
                            const brillo::Blob& value) {
  if (status_ != Status::kFirstInstall) {
    LOG(ERROR) << "Set() called on immutable attributes.";
    return false;
  }

  if (Count() == std::numeric_limits<int>::max()) {
    LOG(ERROR) << "Set() cannot insert into full attribute store.";
    return false;
  }

  // Clobber an existing entry if it exists.
  int index = FindIndexByName(name);
  if (index != -1) {
    SerializedInstallAttributes::Attribute* attr =
      attributes_->mutable_attributes(index);
    attr->set_value(std::string(reinterpret_cast<const char*>(value.data()),
                                value.size()));
    return true;
  }

  SerializedInstallAttributes::Attribute* attr = attributes_->add_attributes();
  if (!attr) {
    LOG(ERROR) << "Failed to add a new attribute.";
    return false;
  }
  attr->set_name(name);
  attr->set_value(std::string(reinterpret_cast<const char*>(value.data()),
                              value.size()));
  return true;
}

bool InstallAttributes::Finalize() {
  switch (status_) {
    case Status::kUnknown:
    case Status::kTpmNotOwned:
    case Status::kInvalid:
      LOG(ERROR) << "Finalize() called with invalid/uninitialized data.";
      return false;
    case Status::kValid:
      // Repeated calls to Finalize() are idempotent.
      return true;
    case Status::kFirstInstall:
      break;
    case Status::COUNT:
      NOTREACHED();
  }

  // Restamp the version.
  attributes_->set_version(version_);

  // Serialize the bytestream
  brillo::Blob attr_bytes;
  if (!SerializeAttributes(&attr_bytes)) {
    LOG(ERROR) << "Finalize() failed to serialize the attributes.";
    return false;
  }

  LockboxError error;
  DLOG(INFO) << "Finalizing() " << attr_bytes.size() << " bytes.";
  if (is_secure() && !lockbox()->Store(attr_bytes, &error)) {
    LOG(ERROR) << "Finalize() failed with Lockbox error: " << error;
    // It may be possible to recover from a failed NVRAM store. So the
    // instance is not marked invalid.
    return false;
  }

  if (!platform_->WriteFileAtomicDurable(data_file_, attr_bytes,
                                         kDataFilePermissions)) {
    LOG(ERROR) << "Finalize() write failed after locking the Lockbox.";
    attributes_->Clear();
    status_ = Status::kInvalid;
    return false;
  }

  // As the cache file is stored on tmpfs, durable write is not required but we
  // need atomicity to be safe in case of concurrent reads.
  if (!platform_->WriteFileAtomic(cache_file_, attr_bytes,
                                  kCacheFilePermissions)) {
    LOG(WARNING) << "Finalize() failed to create cache file.";
  }

  LOG(INFO) << "InstallAttributes have been finalized.";
  status_ = Status::kValid;
  NotifyFinalized();
  return true;
}

int InstallAttributes::Count() const {
  return attributes_->attributes_size();
}

bool InstallAttributes::SerializeAttributes(brillo::Blob* out_bytes) {
  out_bytes->resize(attributes_->ByteSizeLong());
  attributes_->SerializeWithCachedSizesToArray(
    static_cast<google::protobuf::uint8*>(out_bytes->data()));
  return true;
}

bool InstallAttributes::ClearData() {
  if (platform_->FileExists(data_file_) &&
      !platform_->DeleteFile(data_file_, false)) {
    LOG(ERROR) << "Failed to delete install attributes data file!";
    return false;
  }
  return true;
}

std::unique_ptr<base::Value> InstallAttributes::GetStatus() {
  auto dv = std::make_unique<base::DictionaryValue>();
  dv->SetBoolean("initialized",
                 status_ == Status::kFirstInstall || status_ == Status::kValid);
  dv->SetInteger("version", version());
  dv->SetInteger("lockbox_index", lockbox()->nvram_index());
  dv->SetInteger("lockbox_nvram_version",
                 GetNvramVersionNumber(lockbox()->nvram_version()));
  dv->SetBoolean("secure", is_secure());
  dv->SetBoolean("invalid", status_ == Status::kInvalid);
  dv->SetBoolean("first_install", status_ == Status::kFirstInstall);
  dv->SetInteger("size", Count());
  if (Count()) {
    auto attrs = std::make_unique<base::DictionaryValue>();
    std::string key;
    brillo::Blob value;
    for (int i = 0; i < Count(); i++) {
      GetByIndex(i, &key, &value);
      std::string value_str(reinterpret_cast<const char*>(value.data()));
      attrs->SetString(key, value_str);
    }
    dv->Set("attrs", std::move(attrs));
  }
  return std::move(dv);
}

}  // namespace cryptohome
