// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Lockbox - class for storing tamper-evident data blobs.
#ifndef CRYPTOHOME_LOCKBOX_H_
#define CRYPTOHOME_LOCKBOX_H_

#include <memory>

#include <openssl/sha.h>

#include <base/macros.h>
#include <base/strings/string_util.h>
#include <brillo/process/process.h>
#include <brillo/secure_blob.h>

#include "cryptohome/platform.h"
#include "cryptohome/tpm.h"

namespace cryptohome {

class LockboxContents;

// NVRAM structure versions. The enumerator values are chosen to reflect the
// corresponding key material sizes for code convenience.
enum class NvramVersion : size_t {
  kVersion1 = 7,
  kVersion2 = 32,
  kDefault = kVersion2,
};

enum class LockboxError {
  kNvramSpaceAbsent,
  kNvramInvalid,
  kTpmUnavailable,
  kTpmError,  // Transient or unknown TPM error.
};

// Enable LockboxError to be used in LOG output.
std::ostream& operator<<(std::ostream& out, LockboxError error);

// Translates an |NvramVersion| value to a numeric value.
int GetNvramVersionNumber(NvramVersion version);

// Lockbox stores a blob of data in a tamper-evident manner.
//
// This class provides system integration for supporting tamper-evident
// storage using the TPM NVRAM locking and restricted TPM ownership to
// ensure that a data blob stored on disk has not been tampered with to
// the degree that can be cryptographically assured.
//
// Lockbox is not thread-safe and should not be accessed in parallel.
//
// A normal usage flow for Lockbox would be something as follows:
//
// Initializing new data against a lockbox (except with error checking :)
//   Lockbox lockbox(tpm, kNvramSpace);
//   LockboxError error;
//   lockbox->Create(&error);
//   lockbox->Store(mah_locked_data, &error);
//
// Verifying data is performed via class |LockboxContents|.
class Lockbox {
 public:
  // Populates the basic internal state of the Lockbox.
  //
  // Parameters
  // - tpm: a required pointer to a TPM object.
  // - nvram_index: the TPM NVRAM Index to use for this space.
  //
  // Lockbox requires a |tpm|.  If a NULL |tpm| is supplied, none of the
  // operations will succeed, but it should not crash or behave unexpectedly.
  // The |nvram_index| should be chosen carefully. See README.lockbox for info.
  Lockbox(Tpm* tpm, uint32_t nvram_index);
  Lockbox(const Lockbox&) = delete;
  Lockbox& operator=(const Lockbox&) = delete;

  virtual ~Lockbox();

  // Sets up the backend state needed for this lockbox.
  //
  // Instantiates a new TPM NVRAM index lockable bWriteDefine to store
  // the hash and blob size of the data to lock away.
  //
  // Parameters
  // - error: will contain the LockboxError if false.
  // Returns
  // - true if a new space was instantiated or an old one could be used.
  // - false if the space cannot be created or claimed.
  virtual bool Reset(LockboxError* error);

  // Hashes, salts, sizes, and stores metadata required for verifying |data|
  // in TPM NVRAM for later verification.
  //
  // Parameters
  // - data: blob to store.
  // - error: LockboxError populated only on failure
  // Returns
  // - true if written to disk and NVRAM (if there is a tpm).
  // - false if the data could not be persisted.
  virtual bool Store(const brillo::Blob& data, LockboxError* error);

  // Replaces the tpm implementation.
  // Does NOT take ownership of the pointer.
  virtual void set_tpm(Tpm* tpm) { tpm_ = tpm; }

  // Replaces the process spawning implementation.
  // Does NOT take ownership of the pointer.
  virtual void set_process(brillo::Process* p) { process_ = p; }

  // Replaces the Platform class (only used for mount-encrypted).
  // Does NOT take ownership of the pointer.
  virtual void set_platform(Platform* p) { platform_ = p; }

  // Return NVRAM index.
  virtual uint32_t nvram_index() const { return nvram_index_; }
  // Return NVRAM version.
  virtual NvramVersion nvram_version() const { return nvram_version_; }
  // Replaces the default NVRAM structure version.
  virtual void set_nvram_version(NvramVersion version) {
    nvram_version_ = version;
  }

  virtual Tpm* tpm() { return tpm_; }

  // Tells if on this platform we store disk encryption key material in lockbox.
  // If true, it also requires additional protection for lockbox.
  // If false, the key material field is just filled with zeroes and not used.
  // Currently, the key material is stored separately for TPM 2.0.
  virtual bool IsKeyMaterialInLockbox() const {
    return tpm_->GetVersion() != Tpm::TpmVersion::TPM_2_0;
  }

 protected:
  // Call out to the mount-encrypted helper to encrypt the key.
  virtual void FinalizeMountEncrypted(const brillo::SecureBlob& entropy) const;

 private:
  Tpm* tpm_;
  uint32_t nvram_index_;
  NvramVersion nvram_version_ = NvramVersion::kDefault;
  std::unique_ptr<brillo::Process> default_process_;
  brillo::Process* process_;
  std::unique_ptr<Platform> default_platform_;
  Platform* platform_;
};

// Represents decoded lockbox NVRAM space contents and provides operations to
// encode/decode, as well as setting up and verifying integrity of a specific
// data blob.
class LockboxContents {
 private:
  static constexpr size_t kFixedPartSize =
      sizeof(uint32_t) + sizeof(uint8_t) + SHA256_DIGEST_LENGTH;

 public:
  enum class VerificationResult {
    kValid,
    kSizeMismatch,
    kHashMismatch,
  };

  // Creates a LockboxContents instance that'll handle encoded lockbox contents
  // corresponding to an NVRAM space of size |nvram_size|. Returns nullptr in
  // case the passed |nvram_size| isn't supported.
  static std::unique_ptr<LockboxContents> New(size_t nvram_size);

  NvramVersion version() const {
    return static_cast<NvramVersion>(key_material_.size());
  }
  size_t key_material_size() const { return key_material_.size(); }

  // Serialize to |nvram_data|.
  bool Encode(brillo::SecureBlob* nvram_data);

  // Deserialize from |nvram_data|.
  bool Decode(const brillo::SecureBlob& nvram_data);

  // Sets key material, which must be of key_material_size().
  bool SetKeyMaterial(const brillo::SecureBlob& key_material);

  // Protect |blob|, i.e. compute the digest that will later make Verify() to
  // succeed if and only if a copy of |blob| is being passed.
  bool Protect(const brillo::Blob& blob);

  // Verify |blob| against the lockbox contents.
  VerificationResult Verify(const brillo::Blob& blob);

  static size_t GetNvramSize(NvramVersion version) {
    return static_cast<size_t>(version) + kFixedPartSize;
  }

 private:
  LockboxContents() = default;

  uint32_t size_ = 0;
  uint8_t flags_ = 0;
  brillo::SecureBlob key_material_ = {0};
  uint8_t hash_[SHA256_DIGEST_LENGTH] = {0};
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_LOCKBOX_H_
