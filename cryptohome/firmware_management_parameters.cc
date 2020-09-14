// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/firmware_management_parameters.h"

#include <arpa/inet.h>
#include <limits.h>
#include <stdint.h>

#include <string>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_split.h>
#include <brillo/secure_blob.h>
#include <openssl/sha.h>

#include "cryptohome/cryptolib.h"

extern "C" {
#include "cryptohome/crc8.h"
}

using brillo::SecureBlob;

namespace {
const uint32_t kNvramVersionV1_0 = 0x10;
}

namespace cryptohome {

// Defines the raw NVRAM contents.
struct FirmwareManagementParametersRawV1_0 {
  uint8_t crc;
  uint8_t struct_size;
  // Data after this is covered by the crc
  uint8_t struct_version;  // Set to kNvramVersionV1_0
  uint8_t reserved0;
  uint32_t flags;
  uint8_t developer_key_hash[SHA256_DIGEST_LENGTH];
} __attribute__((packed));

// Index must match firmware; see README.firmware_management_parameters
const uint32_t FirmwareManagementParameters::kNvramIndex = 0x100a;
const uint32_t FirmwareManagementParameters::kNvramBytes =
    sizeof(struct FirmwareManagementParametersRawV1_0);
const uint32_t FirmwareManagementParameters::kCrcDataOffset = 2;

FirmwareManagementParameters::FirmwareManagementParameters(Tpm* tpm)
    : tpm_(tpm), raw_(new FirmwareManagementParametersRawV1_0()) {}

FirmwareManagementParameters::~FirmwareManagementParameters() {}

bool FirmwareManagementParameters::TpmIsReady() const {
  if (!tpm_) {
    LOG(ERROR) << "TpmIsReady: no tpm_ instance.";
    return false;
  }
  if (!tpm_->IsEnabled()) {
    LOG(ERROR) << "TpmIsReady: is not enabled.";
    return false;
  }
  if (!tpm_->IsOwned()) {
    LOG(ERROR) << "TpmIsReady: is not owned.";
    return false;
  }
  return true;
}

bool FirmwareManagementParameters::HasAuthorization() const {
  if (!TpmIsReady()) {
    LOG(ERROR) << "HasAuthorization: TPM not ready.";
    return false;
  }
  // Need owner password to create or destroy NVRAM spaces
  brillo::SecureBlob owner_password;
  if (tpm_->GetOwnerPassword(&owner_password) && owner_password.size() != 0) {
    return true;
  }
  LOG(INFO) << "HasAuthorization: TPM Owner password not available.";
  return false;
}

bool FirmwareManagementParameters::Destroy(void) {
  if (!HasAuthorization()) {
    LOG(ERROR) << "Destroy() called with insufficient authorization.";
    return false;
  }

  // Only destroy the space if it exists
  if (tpm_->IsNvramDefined(kNvramIndex) && !tpm_->DestroyNvram(kNvramIndex)) {
    LOG(WARNING) << "Failed to destroy NVRAM Space in "
                    "FirmwareManagementParameters::Destroy()";
    return false;
  }

  loaded_ = false;
  return true;
}

bool FirmwareManagementParameters::Create(void) {
  uint32_t nvram_bytes;

  // Make sure we have what we need now.
  if (!HasAuthorization()) {
    LOG(ERROR) << "Create() called with insufficient authorization.";
    return false;
  }
  if (!Destroy()) {
    LOG(ERROR) << "Failed to destroy Firmware Management Parameters data "
                  "before creation.";
    return false;
  }

  nvram_bytes = kNvramBytes;

  // Use a WriteDefine space with no PCR0 locking
  if (!tpm_->DefineNvram(
          kNvramIndex, nvram_bytes,
          Tpm::kTpmNvramWriteDefine | Tpm::kTpmNvramFirmwareReadable)) {
    LOG(ERROR) << "Create() failed to defined NVRAM space.";
    return false;
  }

  LOG(INFO) << "Firmware Management Parameters created.";
  return true;
}

bool FirmwareManagementParameters::Load(void) {
  if (loaded_) {
    return true;
  }

  if (!tpm_->IsNvramDefined(kNvramIndex)) {
    LOG(INFO) << "Load() called with no NVRAM space defined.";
    return false;
  }

  SecureBlob nvram_data(0);
  if (!tpm_->ReadNvram(kNvramIndex, &nvram_data)) {
    LOG(ERROR) << "Load() could not read from NVRAM space.";
    return false;
  }

  // Make sure we've read enough data for a 1.0 struct
  unsigned int nvram_size = nvram_data.size();
  if (nvram_size < kNvramBytes) {
    LOG(ERROR) << "Load() found unexpected NVRAM size: " << nvram_size;
    return false;
  }

  // Copy the raw data
  memcpy(raw_.get(), nvram_data.data(), kNvramBytes);

  // Verify the size
  if (raw_->struct_size != nvram_size) {
    LOG(ERROR) << "Load() found unexpected NVRAM size: " << nvram_size;
    return false;
  }

  // Verify the CRC
  uint8_t crc =
      crc8(nvram_data.data() + kCrcDataOffset, nvram_size - kCrcDataOffset);
  if (crc != raw_->crc) {
    LOG(ERROR) << "Load() got bad CRC";
    return false;
  }

  // We are a 1.0 reader, so we can read 1.x structs
  if ((raw_->struct_version >> 4) != (kNvramVersionV1_0 >> 4)) {
    LOG(ERROR) << "Load() got incompatible NVRAM version: "
               << (unsigned int)raw_->struct_version;
    return false;
  }
  // We don't need to check minor version, because all 1.x structs are
  // compatible with us

  DLOG(INFO) << "Load() successfully loaded NVRAM data.";
  loaded_ = true;
  return true;
}

bool FirmwareManagementParameters::Store(
    uint32_t flags, const brillo::Blob* developer_key_hash) {
  if (!TpmIsReady()) {
    LOG(ERROR) << "Store() called when TPM was not ready!";
    return false;
  }

  // Ensure we have the space ready.
  if (!tpm_->IsNvramDefined(kNvramIndex)) {
    LOG(ERROR) << "Store() called with no NVRAM space.";
    return false;
  }
  if (tpm_->IsNvramLocked(kNvramIndex)) {
    LOG(ERROR) << "Store() called with a locked NVRAM space.";
    return false;
  }

  // Check defined NVRAM size.
  unsigned int nvram_size = tpm_->GetNvramSize(kNvramIndex);
  if (nvram_size != kNvramBytes) {
    LOG(ERROR) << "Store() found unexpected NVRAM size " << nvram_size << ".";
    return false;
  }

  // Reset the NVRAM contents
  loaded_ = false;
  memset(raw_.get(), 0, kNvramBytes);
  raw_->struct_size = kNvramBytes;
  raw_->struct_version = kNvramVersionV1_0;
  raw_->flags = flags;

  // Store the hash, if any
  if (developer_key_hash) {
    // Make sure hash is the right size
    if ((developer_key_hash->size() != sizeof(raw_->developer_key_hash))) {
      LOG(ERROR) << "Store() called with bad hash size "
                 << developer_key_hash->size() << ".";
      return false;
    }

    memcpy(raw_->developer_key_hash, developer_key_hash->data(),
           sizeof(raw_->developer_key_hash));
  }

  // Recalculate the CRC
  const uint8_t* raw8 = reinterpret_cast<uint8_t*>(raw_.get());
  raw_->crc = crc8(raw8 + kCrcDataOffset, raw_->struct_size - kCrcDataOffset);

  // Write the data to nvram
  SecureBlob nvram_data(raw_->struct_size);
  memcpy(nvram_data.data(), raw_.get(), raw_->struct_size);
  if (!tpm_->WriteNvram(kNvramIndex, nvram_data)) {
    LOG(ERROR) << "Store() failed to write to NVRAM";
    return false;
  }

  // Lock nvram index for writing.
  if (!tpm_->WriteLockNvram(kNvramIndex)) {
    LOG(ERROR) << "Store() failed to lock the NVRAM space";
    return false;
  }

  // Ensure the space is now locked.
  if (!tpm_->IsNvramLocked(kNvramIndex)) {
    LOG(ERROR) << "NVRAM space did not lock as expected.";
    return false;
  }

  loaded_ = true;
  return true;
}

bool FirmwareManagementParameters::GetFlags(uint32_t* flags) {
  CHECK(flags);

  // Load if needed
  if (!Load()) {
    return false;
  }

  *flags = raw_->flags;
  return true;
}

bool FirmwareManagementParameters::GetDeveloperKeyHash(brillo::Blob* hash) {
  CHECK(hash);

  // Load if needed
  if (!Load()) {
    return false;
  }

  hash->resize(sizeof(raw_->developer_key_hash));
  memcpy(hash->data(), raw_->developer_key_hash, hash->size());
  return true;
}

}  // namespace cryptohome
