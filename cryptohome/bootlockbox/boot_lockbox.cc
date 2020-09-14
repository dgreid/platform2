// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/bootlockbox/boot_lockbox.h"

#include <stdint.h>
#include <sys/types.h>

#include <map>
#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/objects.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

#include "cryptohome/crypto.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/platform.h"
#include "cryptohome/tpm.h"

using base::FilePath;
using brillo::Blob;
using brillo::BlobFromString;
using brillo::BlobToString;
using brillo::SecureBlob;

namespace {

const uint32_t kPCRIndex = 15;

// This is an arbitrary value, our only goal is for the PCR to be non-zero.
const char kPCRExtension[] = "CROS_PCR15_845A4A757B94";

const char kKeyFilePath[] = "/var/lib/boot-lockbox/key";

const mode_t kKeyFilePermissions = 0600;

}  // namespace

namespace cryptohome {

size_t GetPcrValueSize(Tpm* tpm) {
  return (tpm && tpm->GetVersion() == Tpm::TpmVersion::TPM_2_0)
             ? SHA256_DIGEST_LENGTH
             : SHA_DIGEST_LENGTH;
}

BootLockbox::BootLockbox(Tpm* tpm, Platform* platform, Crypto* crypto)
    : tpm_(tpm),
      platform_(platform),
      crypto_(crypto),
      initial_pcr_value_(GetPcrValueSize(tpm)) {}

BootLockbox::~BootLockbox() {}

bool BootLockbox::Sign(const brillo::Blob& data,
                       brillo::SecureBlob* signature) {
  CHECK(signature);
  if (IsFinalized()) {
    LOG(INFO) << "Can't sign: boot-lockbox is finalized.";
    return false;
  }
  brillo::SecureBlob key_blob;
  if (!GetKeyBlob(&key_blob)) {
    return false;
  }

  return tpm_->Sign(key_blob, brillo::SecureBlob(data), kPCRIndex, signature);
}

bool BootLockbox::Verify(const brillo::Blob& data,
                         const brillo::SecureBlob& signature) {
  brillo::Blob public_key;
  if (!GetPublicKey(&public_key)) {
    return false;
  }
  if (!VerifySignature(public_key, data, signature)) {
    return false;
  }
  brillo::SecureBlob key_blob;
  if (!GetKeyBlob(&key_blob)) {
    return false;
  }
  brillo::SecureBlob creation_blob;
  if (!GetCreationBlob(&creation_blob)) {
    return false;
  }
  if (!tpm_->VerifyPCRBoundKey(
          std::map<uint32_t, std::string>(
              {{kPCRIndex, BlobToString(initial_pcr_value_)}}),
          key_blob, creation_blob)) {
    return false;
  }
  return true;
}

bool BootLockbox::FinalizeBoot() {
  if (IsFinalized()) {
    // The PCR is already not at the initial value, no need to extend again.
    return true;
  }
  return tpm_->ExtendPCR(kPCRIndex,
                         CryptoLib::Sha1(BlobFromString(kPCRExtension)));
}

bool BootLockbox::IsFinalized() {
  Blob actual_pcr_value;
  return tpm_->ReadPCR(kPCRIndex, &actual_pcr_value) &&
         actual_pcr_value != initial_pcr_value_;
}

bool BootLockbox::PreLoadKey() {
  if (!key_.has_key_blob() && !LoadKey() && !CreateKey()) {
    return false;
  }
  return true;
}

bool BootLockbox::GetKeyBlob(brillo::SecureBlob* key_blob) {
  if (!key_.has_key_blob() && !LoadKey() && !CreateKey()) {
    return false;
  }
  CHECK(key_.has_key_blob());
  if (key_blob) {
    key_blob->assign(key_.key_blob().begin(), key_.key_blob().end());
  }
  return true;
}

bool BootLockbox::GetPublicKey(brillo::Blob* public_key) {
  if (!key_.has_public_key_der() && !LoadKey()) {
    return false;
  }
  CHECK(key_.has_public_key_der());
  if (public_key) {
    public_key->assign(key_.public_key_der().begin(),
                       key_.public_key_der().end());
  }
  return true;
}

bool BootLockbox::GetCreationBlob(brillo::SecureBlob* creation_blob) {
  if (!key_.has_creation_blob() && !LoadKey()) {
    return false;
  }
  if (creation_blob) {
    creation_blob->assign(key_.creation_blob().begin(),
                          key_.creation_blob().end());
  }
  return true;
}

bool BootLockbox::LoadKey() {
  std::string file_contents;
  if (!platform_->ReadFileToString(FilePath(kKeyFilePath), &file_contents)) {
    return false;
  }
  brillo::SecureBlob protobuf;
  if (!crypto_->DecryptWithTpm(file_contents, &protobuf)) {
    LOG(WARNING) << "Failed to decrypt boot-lockbox key.";
    return false;
  }
  if (!key_.ParseFromArray(protobuf.data(), protobuf.size())) {
    LOG(ERROR) << "Invalid boot-lockbox key.";
    return false;
  }
  return true;
}

bool BootLockbox::SaveKey() {
  brillo::SecureBlob protobuf(key_.ByteSizeLong());
  if (!key_.SerializeToArray(protobuf.data(), protobuf.size())) {
    LOG(ERROR) << "Failed to serialize boot-lockbox key.";
    return false;
  }
  std::string encrypted_protobuf;
  if (!crypto_->EncryptWithTpm(protobuf, &encrypted_protobuf)) {
    LOG(ERROR) << "Failed to encrypt boot-lockbox key.";
    return false;
  }
  if (!platform_->WriteStringToFileAtomicDurable(
          FilePath(kKeyFilePath), encrypted_protobuf, kKeyFilePermissions)) {
    LOG(ERROR) << "Failed to write boot-lockbox key.";
    return false;
  }
  return true;
}

bool BootLockbox::CreateKey() {
  LOG(INFO) << "Creating new boot-lockbox key.";
  brillo::SecureBlob key_blob;
  brillo::SecureBlob public_key;
  brillo::SecureBlob creation_blob;
  if (!tpm_->CreatePCRBoundKey(
          std::map<uint32_t, std::string>(
              {{kPCRIndex, BlobToString(initial_pcr_value_)}}),
          AsymmetricKeyUsage::kSignKey, &key_blob, &public_key,
          &creation_blob)) {
    LOG(ERROR) << "Failed to create boot-lockbox key.";
    return false;
  }
  if (IsFinalized()) {
    LOG(WARNING) << "Boot-lockbox finalized while creating key: aborting.";
    return false;
  }
  key_.set_key_blob(key_blob.to_string());
  key_.set_public_key_der(public_key.to_string());
  key_.set_creation_blob(creation_blob.to_string());
  return SaveKey();
}

bool BootLockbox::VerifySignature(const brillo::Blob& public_key,
                                  const brillo::Blob& signed_data,
                                  const brillo::SecureBlob& signature) {
  const unsigned char* asn1_ptr = public_key.data();
  crypto::ScopedRSA rsa(
      d2i_RSAPublicKey(nullptr, &asn1_ptr, public_key.size()));
  if (!rsa.get()) {
    LOG(ERROR) << "Failed to decode public key.";
    return false;
  }
  brillo::SecureBlob digest = CryptoLib::Sha256ToSecureBlob(signed_data);
  if (!RSA_verify(NID_sha256, digest.data(), digest.size(), signature.data(),
                  signature.size(), rsa.get())) {
    LOG(ERROR) << "Failed to verify signature.";
    return false;
  }
  return true;
}

}  // namespace cryptohome
