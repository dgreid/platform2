// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/lockbox.h"

#include <arpa/inet.h>
#include <limits.h>
#include <openssl/sha.h>
#include <stdint.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/sys_byteorder.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <brillo/secure_blob.h>

#include "cryptohome/cryptolib.h"
#include "cryptohome/platform.h"

using base::FilePath;
using brillo::SecureBlob;

namespace cryptohome {
namespace {

// Literals for running mount-encrypted helper.
constexpr char kMountEncrypted[] = "/usr/sbin/mount-encrypted";
constexpr char kMountEncryptedFinalize[] = "finalize";

}  // namespace

std::ostream& operator<<(std::ostream& out, LockboxError error) {
  return out << static_cast<int>(error);
}

int GetNvramVersionNumber(NvramVersion version) {
  switch (version) {
    case NvramVersion::kVersion1:
      return 1;
    case NvramVersion::kVersion2:
      return 2;
  }
  NOTREACHED();
  return 0;
}

Lockbox::Lockbox(Tpm* tpm, uint32_t nvram_index)
    : tpm_(tpm),
      nvram_index_(nvram_index),
      default_process_(new brillo::ProcessImpl()),
      process_(default_process_.get()),
      default_platform_(new Platform()),
      platform_(default_platform_.get()) {}

Lockbox::~Lockbox() {}

bool Lockbox::Reset(LockboxError* error) {
  if (!tpm_ || !tpm_->IsEnabled() || !tpm_->IsOwned()) {
    *error = LockboxError::kTpmUnavailable;
    LOG(ERROR) << "TPM unavailable";
    return false;
  }

  // If we have authorization, recreate the lockbox space.
  brillo::SecureBlob owner_password;
  if (tpm_->GetOwnerPassword(&owner_password) && owner_password.size() != 0) {
    if (tpm_->IsNvramDefined(nvram_index_) &&
        !tpm_->DestroyNvram(nvram_index_)) {
      LOG(ERROR) << "Failed to destroy lockbox data before creation.";
      *error = LockboxError::kTpmError;
      return false;
    }

    // If we store the encryption salt in lockbox, protect it from reading
    // in non-verified boot mode.
    uint32_t nvram_perm =
        Tpm::kTpmNvramWriteDefine |
        (IsKeyMaterialInLockbox() ? Tpm::kTpmNvramBindToPCR0 : 0);
    uint32_t nvram_bytes = LockboxContents::GetNvramSize(nvram_version_);
    if (!tpm_->DefineNvram(nvram_index_, nvram_bytes, nvram_perm)) {
      *error = LockboxError::kTpmError;
      LOG(ERROR) << "Failed to define NVRAM space.";
      return false;
    }
    LOG(INFO) << "Lockbox created.";
    return true;
  } else {
    LOG(WARNING) << "No owner password when trying to reset LockBox.";
  }

  // Check if the space is already set up correctly.
  if (!tpm_->IsNvramDefined(nvram_index_)) {
    LOG(ERROR) << "NVRAM space absent when resetting LockBox.";
    *error = LockboxError::kNvramSpaceAbsent;
    return false;
  }

  if (tpm_->IsNvramLocked(nvram_index_)) {
    LOG(ERROR) << "NVRAM space not locked when resetting LockBox.";
    *error = LockboxError::kNvramInvalid;
    return false;
  }

  // The NVRAM space that we are looking at is not created by us, and it is too
  // expensive to extensively inspect it. Given the above, we aren't sure about
  // all its attributes, all we know is that:
  // 1. It's not locked.
  // 2. It exists (is defined).
  // Therefore, it is highly likely that the NVRAM space is writable, and
  // suitable for our use case. The most probable scenario is that this NVRAM
  // index is created by previous installations of Chromium OS, so we'll just
  // continue to use it.
  LOG(INFO) << "Existing Lockbox seems writable.";
  return true;
}

bool Lockbox::Store(const brillo::Blob& blob, LockboxError* error) {
  if (!tpm_ || !tpm_->IsEnabled()) {
    *error = LockboxError::kTpmUnavailable;
    LOG(ERROR) << "TPM unavailable";
    return false;
  }

  if (!tpm_->IsNvramDefined(nvram_index_) ||
      tpm_->IsNvramLocked(nvram_index_)) {
    *error = LockboxError::kNvramInvalid;
    return false;
  }

  // Check defined NVRAM size and construct a suitable LockboxContents instance.
  unsigned int nvram_size = tpm_->GetNvramSize(nvram_index_);
  std::unique_ptr<LockboxContents> contents = LockboxContents::New(nvram_size);
  if (!contents) {
    LOG(ERROR) << "Unsupported NVRAM space size " << nvram_size << ".";
    *error = LockboxError::kNvramInvalid;
    return false;
  }

  // Grab key material from the TPM.
  brillo::SecureBlob key_material(contents->key_material_size());
  if (IsKeyMaterialInLockbox()) {
    if (!tpm_->GetRandomDataSecureBlob(key_material.size(), &key_material)) {
      LOG(ERROR) << "Failed to get key material from the TPM.";
      *error = LockboxError::kTpmError;
      return false;
    }
  } else {
    // Save a TPM command, and just fill the salt field with zeroes.
    LOG(INFO) << "Skipping random salt generation.";
  }

  brillo::SecureBlob nvram_blob;
  if (!contents->SetKeyMaterial(key_material) || !contents->Protect(blob) ||
      !contents->Encode(&nvram_blob)) {
    LOG(ERROR) << "Failed to set up lockbox contents.";
    *error = LockboxError::kNvramInvalid;
    return false;
  }

  // Write the hash to nvram
  if (!tpm_->WriteNvram(nvram_index_,
                        SecureBlob(nvram_blob.begin(), nvram_blob.end()))) {
    LOG(ERROR) << "Store() failed to write the attribute hash to NVRAM";
    *error = LockboxError::kTpmError;
    return false;
  }
  // Lock nvram index for writing.
  if (!tpm_->WriteLockNvram(nvram_index_)) {
    LOG(ERROR) << "Store() failed to lock the NVRAM space";
    *error = LockboxError::kTpmError;
    return false;
  }
  // Ensure the space is now locked.
  if (!tpm_->IsNvramLocked(nvram_index_)) {
    LOG(ERROR) << "NVRAM space did not lock as expected.";
    *error = LockboxError::kTpmError;
    return false;
  }

  // Call out to mount-encrypted now that salt has been written.
  FinalizeMountEncrypted(contents->version() == NvramVersion::kVersion1
                             ? nvram_blob
                             : key_material);

  return true;
}

// TODO(keescook) Write unittests for this.
void Lockbox::FinalizeMountEncrypted(const brillo::SecureBlob& entropy) const {
  std::string hex;
  FilePath outfile_path;
  FILE* outfile;
  int rc;

  // Take hash of entropy and convert to hex string for cmdline.
  SecureBlob hash = CryptoLib::Sha256(entropy);
  hex = CryptoLib::SecureBlobToHex(hash);

  process_->Reset(0);
  process_->AddArg(kMountEncrypted);
  process_->AddArg(kMountEncryptedFinalize);
  process_->AddArg(hex);

  // Redirect stdout/stderr somewhere useful for error reporting.
  outfile = platform_->CreateAndOpenTemporaryFile(&outfile_path);
  if (outfile) {
    process_->BindFd(fileno(outfile), STDOUT_FILENO);
    process_->BindFd(fileno(outfile), STDERR_FILENO);
  }

  rc = process_->Run();

  if (rc) {
    LOG(ERROR) << "Request to finalize encrypted mount failed ('"
               << kMountEncrypted << " " << kMountEncryptedFinalize << " "
               << hex << "', rc:" << rc << ")";
    if (outfile) {
      std::vector<std::string> output;
      std::vector<std::string>::iterator it;
      std::string contents;

      if (platform_->ReadFileToString(outfile_path, &contents)) {
        output = base::SplitString(contents, "\n", base::KEEP_WHITESPACE,
                                   base::SPLIT_WANT_ALL);
        for (it = output.begin(); it < output.end(); it++) {
          LOG(ERROR) << *it;
        }
      }
    }
  } else {
    LOG(INFO) << "Encrypted partition finalized.";
  }

  if (outfile)
    platform_->CloseFile(outfile);

  return;
}

// static
std::unique_ptr<LockboxContents> LockboxContents::New(size_t nvram_size) {
  // Make sure |nvram_size| corresponds to one of the encoding versions.
  if (GetNvramSize(NvramVersion::kVersion1) != nvram_size &&
      GetNvramSize(NvramVersion::kVersion2) != nvram_size) {
    return nullptr;
  }

  std::unique_ptr<LockboxContents> result(new LockboxContents());
  result->key_material_.resize(nvram_size - kFixedPartSize);
  return result;
}

bool LockboxContents::Decode(const brillo::SecureBlob& nvram_data) {
  // Reject data of incorrect size.
  if (nvram_data.size() != GetNvramSize(version())) {
    return false;
  }

  brillo::SecureBlob::const_iterator cursor = nvram_data.begin();

  // Extract the expected data size from the NVRAM. For historic reasons, this
  // is encoded in reverse host byte order (!).
  uint32_t reversed_size;
  uint8_t* reversed_size_ptr = reinterpret_cast<uint8_t*>(&reversed_size);
  std::copy(cursor, cursor + sizeof(reversed_size), reversed_size_ptr);
  cursor += sizeof(reversed_size);
  size_ = base::ByteSwap(reversed_size);

  // Grab the flags.
  flags_ = *cursor++;

  // Grab the key material.
  key_material_.assign(cursor, cursor + key_material_size());
  cursor += key_material_size();

  // Grab the hash.
  std::copy(cursor, cursor + sizeof(hash_), hash_);
  cursor += sizeof(hash_);

  // Per the checks at function entry we should have exactly reached the end.
  CHECK(cursor == nvram_data.end());

  return true;
}

bool LockboxContents::Encode(brillo::SecureBlob* blob) {
  // Encode the data size. For historic reasons, this is encoded in reverse host
  // byte order (!).
  uint32_t reversed_size = base::ByteSwap(size_);
  uint8_t* reversed_size_ptr = reinterpret_cast<uint8_t*>(&reversed_size);
  blob->insert(blob->end(), reversed_size_ptr,
               reversed_size_ptr + sizeof(reversed_size));

  // Append the flags byte.
  blob->push_back(flags_);

  // Append the key material.
  blob->insert(blob->end(), std::begin(key_material_), std::end(key_material_));

  // Append the hash.
  blob->insert(blob->end(), std::begin(hash_), std::end(hash_));

  return true;
}

bool LockboxContents::SetKeyMaterial(const brillo::SecureBlob& key_material) {
  if (key_material.size() != key_material_size()) {
    return false;
  }

  key_material_ = key_material;
  return true;
}

bool LockboxContents::Protect(const brillo::Blob& blob) {
  brillo::SecureBlob salty_blob(blob);
  salty_blob.insert(salty_blob.end(), key_material_.begin(),
                    key_material_.end());
  SecureBlob salty_blob_hash = CryptoLib::Sha256(salty_blob);
  CHECK_EQ(sizeof(hash_), salty_blob_hash.size());
  std::copy(salty_blob_hash.begin(), salty_blob_hash.end(), std::begin(hash_));
  size_ = blob.size();
  return true;
}

LockboxContents::VerificationResult LockboxContents::Verify(
    const brillo::Blob& blob) {
  // Make sure that the file size matches what was stored in nvram.
  if (blob.size() != size_) {
    LOG(ERROR) << "Verify() expected " << size_ << " , but received "
               << blob.size() << " bytes.";
    return VerificationResult::kSizeMismatch;
  }

  // Compute the hash of the blob to verify.
  brillo::SecureBlob salty_blob(blob);
  salty_blob.insert(salty_blob.end(), key_material_.begin(),
                    key_material_.end());
  SecureBlob salty_blob_hash = CryptoLib::Sha256(salty_blob);

  // Validate the blob hash versus the stored hash.
  if (sizeof(hash_) != salty_blob_hash.size() ||
      brillo::SecureMemcmp(hash_, salty_blob_hash.data(), sizeof(hash_))) {
    LOG(ERROR) << "Verify() hash mismatch!";
    return VerificationResult::kHashMismatch;
  }

  return VerificationResult::kValid;
}

}  // namespace cryptohome
