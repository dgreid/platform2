// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test methods that run on a real TPM

#include "cryptohome/tpm_live_test.h"

#include <stdint.h>

#include <map>
#include <memory>
#include <set>
#include <utility>

#include <base/macros.h>
#include <base/memory/ptr_util.h>
#include <base/optional.h>
#include <crypto/libcrypto-compat.h>
#include <crypto/scoped_openssl_types.h>
#include <crypto/sha2.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include "cryptohome/cryptolib.h"
#include "cryptohome/signature_sealing_backend.h"

#if !USE_TPM2
#include <trousers/scoped_tss_type.h>
#include <trousers/tss.h>
#include <trousers/trousers.h>  // NOLINT(build/include_alpha) - needs tss.h

#include "cryptohome/tpm_impl.h"
#endif  // !USE_TPM2

using brillo::Blob;
using brillo::BlobFromString;
using brillo::BlobToString;
using brillo::SecureBlob;

namespace cryptohome {

namespace {

// Scoped setter of the owner password of the global Tpm instance.
// Does nothing if the version is different from |TPM_1_2|.
class ScopedTpmOwnerPasswordSetter {
 public:
  explicit ScopedTpmOwnerPasswordSetter(const SecureBlob& owner_password)
      : tpm_(Tpm::GetSingleton()) {
    if (ShouldApply()) {
      tpm_->GetOwnerPassword(&previous_tpm_owner_password_);
      tpm_->SetOwnerPassword(owner_password);
    }
  }

  ~ScopedTpmOwnerPasswordSetter() {
    if (ShouldApply())
      tpm_->SetOwnerPassword(previous_tpm_owner_password_);
  }

 private:
  bool ShouldApply() const { return tpm_->GetVersion() == Tpm::TPM_1_2; }

  Tpm* const tpm_;
  SecureBlob previous_tpm_owner_password_;
};

}  // namespace

TpmLiveTest::TpmLiveTest() : tpm_(Tpm::GetSingleton()) {}

bool TpmLiveTest::RunLiveTests(const SecureBlob& owner_password,
                               bool tpm2_use_system_owner_password) {
  if (!PCRKeyTest()) {
    LOG(ERROR) << "Error running PCRKeyTest.";
    return false;
  }
  if (!MultiplePCRKeyTest()) {
    LOG(ERROR) << "Error running MultiplePCRKeyTest.";
    return false;
  }
  if (!DecryptionKeyTest()) {
    LOG(ERROR) << "Error running Decryption test.";
    return false;
  }
  if (!SealToPcrWithAuthorizationTest()) {
    LOG(ERROR) << "Error running SealToPcrWithAuthorizationTest.";
    return false;
  }
  const Tpm::TpmVersion tpm_version = tpm_->GetVersion();
  if ((tpm_version == Tpm::TPM_1_2 && !owner_password.empty()) ||
      (tpm_version == Tpm::TPM_2_0 && tpm2_use_system_owner_password)) {
    if (!NvramTest(owner_password)) {
      LOG(ERROR) << "Error running NvramTest.";
      return false;
    }
  }
  if (tpm_version != Tpm::TPM_1_2 || !owner_password.empty()) {
    if (!SignatureSealedSecretTest(owner_password)) {
      LOG(ERROR) << "Error running SignatureSealedSecretTest.";
      return false;
    }
  }
  LOG(INFO) << "All tests run successfully.";
  return true;
}

bool TpmLiveTest::SignData(const SecureBlob& pcr_bound_key,
                           const SecureBlob& public_key_der,
                           int index) {
  SecureBlob input_data("input_data");
  SecureBlob signature;
  if (!tpm_->Sign(pcr_bound_key, input_data, index, &signature)) {
    LOG(ERROR) << "Error signing with PCR bound key.";
    return false;
  }
  const unsigned char* public_key_data = public_key_der.data();
  crypto::ScopedRSA rsa(
      d2i_RSAPublicKey(nullptr, &public_key_data, public_key_der.size()));
  if (!rsa.get()) {
    LOG(ERROR) << "Failed to decode public key.";
    return false;
  }
  SecureBlob digest = CryptoLib::Sha256(input_data);
  if (!RSA_verify(NID_sha256, digest.data(), digest.size(), signature.data(),
                  signature.size(), rsa.get())) {
    LOG(ERROR) << "Failed to verify signature.";
    return false;
  }
  return true;
}

bool TpmLiveTest::EncryptAndDecryptData(
    const SecureBlob& pcr_bound_key,
    const std::map<uint32_t, std::string>& pcr_map) {
  ScopedKeyHandle handle;
  if (tpm_->LoadWrappedKey(pcr_bound_key, &handle) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Error loading wrapped key.";
    return false;
  }
  SecureBlob aes_key(32, 'a');
  SecureBlob plaintext(32, 'b');
  SecureBlob ciphertext;
  if (tpm_->EncryptBlob(handle.value(), plaintext, aes_key, &ciphertext) !=
      Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Error encrypting blob.";
    return false;
  }
  SecureBlob decrypted_plaintext;
  if (tpm_->DecryptBlob(handle.value(), ciphertext, aes_key, pcr_map,
                        &decrypted_plaintext) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Error decrypting the data.";
    return false;
  }
  if (plaintext != decrypted_plaintext) {
    LOG(ERROR) << "Decrypted plaintext does not match plaintext.";
    return false;
  }
  return true;
}

bool TpmLiveTest::PCRKeyTest() {
  LOG(INFO) << "PCRKeyTest started";
  uint32_t index = 5;
  Blob pcr_data;
  if (!tpm_->ReadPCR(index, &pcr_data)) {
    LOG(ERROR) << "Error reading pcr value from TPM.";
    return false;
  }
  SecureBlob pcr_bound_key1;  // Sign key
  SecureBlob pcr_bound_key2;  // Decrypt key
  SecureBlob pcr_bound_key3;  // Sign and decrypt key
  SecureBlob public_key_der1;
  SecureBlob public_key_der2;
  SecureBlob public_key_der3;
  SecureBlob creation_blob1;
  SecureBlob creation_blob2;
  SecureBlob creation_blob3;
  std::map<uint32_t, std::string> pcr_map({{index, BlobToString(pcr_data)}});
  // Create the keys.
  if (!tpm_->CreatePCRBoundKey(pcr_map, AsymmetricKeyUsage::kSignKey,
                               &pcr_bound_key1, &public_key_der1,
                               &creation_blob1)) {
    LOG(ERROR) << "Error creating PCR bound signing key.";
    return false;
  }
  if (!tpm_->CreatePCRBoundKey(pcr_map, AsymmetricKeyUsage::kDecryptKey,
                               &pcr_bound_key2, &public_key_der2,
                               &creation_blob2)) {
    LOG(ERROR) << "Error creating PCR bound decryption key.";
    return false;
  }
  if (!tpm_->CreatePCRBoundKey(pcr_map, AsymmetricKeyUsage::kDecryptAndSignKey,
                               &pcr_bound_key3, &public_key_der3,
                               &creation_blob3)) {
    LOG(ERROR) << "Error creating PCR bound decrypt and sign key.";
    return false;
  }
  if (!tpm_->VerifyPCRBoundKey(pcr_map, pcr_bound_key1, creation_blob1) ||
      !tpm_->VerifyPCRBoundKey(pcr_map, pcr_bound_key2, creation_blob2) ||
      !tpm_->VerifyPCRBoundKey(pcr_map, pcr_bound_key3, creation_blob3)) {
    LOG(ERROR) << "Error verifying PCR bound key.";
    return false;
  }
  // Check that signing key works.
  if (!SignData(pcr_bound_key1, public_key_der1, index)) {
    LOG(ERROR) << "Error signing the blob.";
    return false;
  }
  // Check that the key cannot be used to decrypt the data.
  if (EncryptAndDecryptData(pcr_bound_key1, pcr_map)) {
    LOG(ERROR) << "Decrypting the blob succeeded with signing only key.";
    return false;
  }
  // Check that the decryption key works as intended.
  if (!EncryptAndDecryptData(pcr_bound_key2, pcr_map)) {
    LOG(ERROR) << "Error decrypting the blob.";
    return false;
  }
  // Check that signing data doesn't work (only for TPM2).
  if (tpm_->GetVersion() != Tpm::TPM_1_2) {
    if (SignData(pcr_bound_key2, public_key_der2, index)) {
      LOG(ERROR) << "Signing data succeeded with decryption only key.";
      return false;
    }
  }
  // Check that the key created for decryption and signing works for both.
  if (!EncryptAndDecryptData(pcr_bound_key3, pcr_map)) {
    LOG(ERROR) << "Error decrypting the blob.";
    return false;
  }
  if (!SignData(pcr_bound_key3, public_key_der3, index)) {
    LOG(ERROR) << "Error signing the blob.";
    return false;
  }
  // Extend PCR to invalidate the keys.
  if (!tpm_->ExtendPCR(index, BlobFromString("01234567890123456789"))) {
    LOG(ERROR) << "Error extending PCR.";
    return false;
  }
  if (SignData(pcr_bound_key1, public_key_der1, index)) {
    LOG(ERROR) << "Sign succeeded without the correct PCR state.";
    return false;
  }
  if (EncryptAndDecryptData(pcr_bound_key2, pcr_map)) {
    LOG(ERROR) << "Decryption succeeded without the correct PCR state.";
    return false;
  }
  if (SignData(pcr_bound_key3, public_key_der3, index)) {
    LOG(ERROR) << "Sign succeeded without the correct PCR state.";
    return false;
  }
  if (EncryptAndDecryptData(pcr_bound_key3, pcr_map)) {
    LOG(ERROR) << "Decryption succeeded without the correct PCR state.";
    return false;
  }
  LOG(INFO) << "PCRKeyTest ended successfully.";
  return true;
}

bool TpmLiveTest::MultiplePCRKeyTest() {
  LOG(INFO) << "MultiplePCRKeyTest started";
  uint32_t index1 = 7;
  uint32_t index2 = 12;
  Blob pcr_data1;
  Blob pcr_data2;
  if (!tpm_->ReadPCR(index1, &pcr_data1) ||
      !tpm_->ReadPCR(index2, &pcr_data2)) {
    LOG(ERROR) << "Error reading pcr value from TPM.";
    return false;
  }
  SecureBlob pcr_bound_key;
  SecureBlob public_key_der;
  SecureBlob creation_blob;
  std::map<uint32_t, std::string> pcr_map(
      {{index1, BlobToString(pcr_data1)}, {index2, BlobToString(pcr_data2)}});
  if (!tpm_->CreatePCRBoundKey(pcr_map, AsymmetricKeyUsage::kDecryptKey,
                               &pcr_bound_key, &public_key_der,
                               &creation_blob)) {
    LOG(ERROR) << "Error creating PCR bound key.";
    return false;
  }
  ScopedKeyHandle handle;
  if (tpm_->LoadWrappedKey(pcr_bound_key, &handle) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Error loading wrapped key.";
    return false;
  }
  SecureBlob aes_key(32, 'a');
  SecureBlob plaintext(32, 'b');
  SecureBlob ciphertext;
  if (tpm_->EncryptBlob(handle.value(), plaintext, aes_key, &ciphertext) !=
      Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Error encrypting blob.";
    return false;
  }
  SecureBlob decrypted_plaintext;
  if (tpm_->DecryptBlob(handle.value(), ciphertext, aes_key, pcr_map,
                        &decrypted_plaintext) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Error decrypting blob.";
    return false;
  }
  if (plaintext != decrypted_plaintext) {
    LOG(ERROR) << "Decrypted plaintext does not match plaintext.";
    return false;
  }
  if (!tpm_->VerifyPCRBoundKey(pcr_map, pcr_bound_key, creation_blob)) {
    LOG(ERROR) << "Error verifying PCR bound key.";
    return false;
  }
  // Extend a PCR that is bound to the key, to invalidate it.
  if (!tpm_->ExtendPCR(index2, BlobFromString("01234567890123456789"))) {
    LOG(ERROR) << "Error extending PCR.";
    return false;
  }
  // Check that the text cannot be decrypted anymore, after the PCR change.
  if (tpm_->DecryptBlob(handle.value(), ciphertext, aes_key, pcr_map,
                        &decrypted_plaintext) == Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Decrypt succeeded without the correct PCR state.";
    return false;
  }
  if (!tpm_->ReadPCR(index2, &pcr_data2)) {
    LOG(ERROR) << "Error reading pcr value from TPM.";
    return false;
  }
  // Check that the text cannot be decrypted even with the right PCR values.
  pcr_map[index2] = BlobToString(pcr_data2);
  if (tpm_->DecryptBlob(handle.value(), ciphertext, aes_key, pcr_map,
                        &decrypted_plaintext) == Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Decrypt succeeded without the correct PCR state.";
    return false;
  }
  // Check that VerifyPCRBoundKey also fails.
  if (tpm_->VerifyPCRBoundKey(pcr_map, pcr_bound_key, creation_blob)) {
    LOG(ERROR) << "VerifyPCRBoundKey succeeded without the correct PCR state.";
    return false;
  }
  // Check that even a newly encrypted text cannot be decrypted.
  if (tpm_->EncryptBlob(handle.value(), plaintext, aes_key, &ciphertext) !=
      Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Error encrypting blob.";
    return false;
  }
  if (tpm_->DecryptBlob(handle.value(), ciphertext, aes_key, pcr_map,
                        &decrypted_plaintext) == Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Decrypt succeeded without the correct PCR state.";
    return false;
  }
  LOG(INFO) << "MultiplePCRKeyTest ended successfully.";
  return true;
}

bool TpmLiveTest::DecryptionKeyTest() {
  LOG(INFO) << "DecryptionKeyTest started";
  SecureBlob n;
  SecureBlob p;
  uint32_t tpm_key_bits = 2048;
  if (!CryptoLib::CreateRsaKey(tpm_key_bits, &n, &p)) {
    LOG(ERROR) << "Error creating RSA key.";
    return false;
  }
  SecureBlob wrapped_key;
  if (!tpm_->WrapRsaKey(n, p, &wrapped_key)) {
    LOG(ERROR) << "Error wrapping RSA key.";
    return false;
  }
  ScopedKeyHandle handle;
  if (tpm_->LoadWrappedKey(wrapped_key, &handle) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Error loading key.";
    return false;
  }
  SecureBlob aes_key(32, 'a');
  SecureBlob plaintext(32, 'b');
  SecureBlob ciphertext;
  if (tpm_->EncryptBlob(handle.value(), plaintext, aes_key, &ciphertext) !=
      Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Error encrypting blob.";
    return false;
  }
  SecureBlob decrypted_plaintext;
  if (tpm_->DecryptBlob(handle.value(), ciphertext, aes_key,
                        std::map<uint32_t, std::string>(),
                        &decrypted_plaintext) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Error decrypting blob.";
    return false;
  }
  if (plaintext != decrypted_plaintext) {
    LOG(ERROR) << "Decrypted plaintext does not match plaintext.";
    return false;
  }
  LOG(INFO) << "DecryptionKeyTest ended successfully.";
  return true;
}

bool TpmLiveTest::SealToPcrWithAuthorizationTest() {
  LOG(INFO) << "SealToPcrWithAuthorizationTest started";
  SecureBlob n;
  SecureBlob p;
  uint32_t tpm_key_bits = 2048;
  if (!CryptoLib::CreateRsaKey(tpm_key_bits, &n, &p)) {
    LOG(ERROR) << "Error creating RSA key.";
    return false;
  }
  SecureBlob wrapped_key;
  if (!tpm_->WrapRsaKey(n, p, &wrapped_key)) {
    LOG(ERROR) << "Error wrapping RSA key.";
    return false;
  }
  ScopedKeyHandle handle;
  if (tpm_->LoadWrappedKey(wrapped_key, &handle) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Error loading key.";
    return false;
  }

  uint32_t index1 = 4;
  uint32_t index2 = 11;
  std::map<uint32_t, std::string> pcr_map({{index1, ""}, {index2, ""}});
  SecureBlob plaintext(32, 'a');
  SecureBlob auth_blob(256, 'b');
  SecureBlob ciphertext;
  if (tpm_->SealToPcrWithAuthorization(handle.value(), plaintext, auth_blob,
                                       pcr_map,
                                       &ciphertext) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Error sealing the blob.";
    return false;
  }
  SecureBlob unsealed_text;
  if (tpm_->UnsealWithAuthorization(handle.value(), ciphertext, auth_blob,
                                    pcr_map,
                                    &unsealed_text) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Error unsealing blob.";
    return false;
  }
  if (plaintext != unsealed_text) {
    LOG(ERROR) << "Unsealed plaintext does not match plaintext.";
    return false;
  }

  // Check that unsealing doesn't work with wrong auth_blob.
  auth_blob.char_data()[255] = 'a';
  if (tpm_->UnsealWithAuthorization(handle.value(), ciphertext, auth_blob,
                                    pcr_map,
                                    &unsealed_text) == Tpm::kTpmRetryNone &&
      plaintext == unsealed_text) {
    LOG(ERROR) << "UnsealWithAuthorization failed to fail.";
    return false;
  }

  LOG(INFO) << "SealToPcrWithAuthorizationTest ended successfully.";
  return true;
}

bool TpmLiveTest::NvramTest(const SecureBlob& owner_password) {
  LOG(INFO) << "NvramTest started";
  const ScopedTpmOwnerPasswordSetter scoped_tpm_owner_password_setter(
      owner_password);
  uint32_t index = 12;
  SecureBlob nvram_data("nvram_data");
  if (tpm_->IsNvramDefined(index)) {
    if (!tpm_->DestroyNvram(index)) {
      LOG(ERROR) << "Error destroying old Nvram.";
      return false;
    }
    if (tpm_->IsNvramDefined(index)) {
      LOG(ERROR) << "Nvram still defined after it was destroyed.";
      return false;
    }
  }
  if (!tpm_->DefineNvram(
          index, nvram_data.size(),
          Tpm::kTpmNvramWriteDefine | Tpm::kTpmNvramBindToPCR0)) {
    LOG(ERROR) << "Defining Nvram index.";
    return false;
  }
  if (!tpm_->IsNvramDefined(index)) {
    LOG(ERROR) << "Nvram index is not defined after creating.";
    return false;
  }
  if (tpm_->GetNvramSize(index) != nvram_data.size()) {
    LOG(ERROR) << "Nvram space is of incorrect size.";
    return false;
  }
  if (tpm_->IsNvramLocked(index)) {
    LOG(ERROR) << "Nvram should not be locked before writing.";
    return false;
  }
  if (!tpm_->WriteNvram(index, nvram_data)) {
    LOG(ERROR) << "Error writing to Nvram.";
    return false;
  }
  if (!tpm_->WriteLockNvram(index)) {
    LOG(ERROR) << "Error locking Nvram space.";
    return false;
  }
  if (!tpm_->IsNvramLocked(index)) {
    LOG(ERROR) << "Nvram should be locked after locking.";
    return false;
  }
  SecureBlob data;
  if (!tpm_->ReadNvram(index, &data)) {
    LOG(ERROR) << "Error reading from Nvram.";
    return false;
  }
  if (data != nvram_data) {
    LOG(ERROR) << "Data read from Nvram did not match data written.";
    return false;
  }
  if (tpm_->WriteNvram(index, nvram_data)) {
    LOG(ERROR) << "We should not be able to write to a locked Nvram space.";
    return false;
  }
  if (!tpm_->DestroyNvram(index)) {
    LOG(ERROR) << "Error destroying Nvram space.";
    return false;
  }
  if (tpm_->IsNvramDefined(index)) {
    LOG(ERROR) << "Nvram still defined after it was destroyed.";
    return false;
  }
  LOG(INFO) << "NvramTest ended successfully.";
  return true;
}

namespace {

struct SignatureSealedSecretTestCaseParam {
  SignatureSealedSecretTestCaseParam(
      const std::string& test_case_description,
      Tpm* tpm,
      int key_size_bits,
      const std::vector<ChallengeSignatureAlgorithm>& supported_algorithms,
      base::Optional<ChallengeSignatureAlgorithm> expected_algorithm,
      int openssl_algorithm_nid)
      : test_case_description(test_case_description),
        tpm(tpm),
        key_size_bits(key_size_bits),
        supported_algorithms(supported_algorithms),
        expected_algorithm(expected_algorithm),
        openssl_algorithm_nid(openssl_algorithm_nid) {}

  SignatureSealedSecretTestCaseParam(SignatureSealedSecretTestCaseParam&&) =
      default;

  static SignatureSealedSecretTestCaseParam MakeSuccessful(
      const std::string& test_case_description,
      Tpm* tpm,
      int key_size_bits,
      const std::vector<ChallengeSignatureAlgorithm>& supported_algorithms,
      ChallengeSignatureAlgorithm expected_algorithm,
      int openssl_algorithm_nid) {
    return SignatureSealedSecretTestCaseParam(
        test_case_description, tpm, key_size_bits, supported_algorithms,
        expected_algorithm, openssl_algorithm_nid);
  }

  static SignatureSealedSecretTestCaseParam MakeFailing(
      const std::string& test_case_description,
      Tpm* tpm,
      int key_size_bits,
      const std::vector<ChallengeSignatureAlgorithm>& supported_algorithms) {
    return SignatureSealedSecretTestCaseParam(
        test_case_description, tpm, key_size_bits, supported_algorithms, {}, 0);
  }

  bool expect_success() const { return expected_algorithm.has_value(); }

  std::string test_case_description;
  Tpm* tpm;
  int key_size_bits;
  std::vector<ChallengeSignatureAlgorithm> supported_algorithms;
  base::Optional<ChallengeSignatureAlgorithm> expected_algorithm;
  int openssl_algorithm_nid;
};

class SignatureSealedSecretTestCase final {
 public:
  using UnsealingSession = SignatureSealingBackend::UnsealingSession;

  SignatureSealedSecretTestCase(SignatureSealedSecretTestCaseParam param,
                                const SecureBlob& owner_password)
      : param_(std::move(param)), owner_password_(owner_password) {
    LOG(INFO) << "SignatureSealedSecretTestCase: " << param_.key_size_bits
              << "-bit key, " << param_.test_case_description;
  }

  ~SignatureSealedSecretTestCase() { CleanUpDelegate(); }

  bool SetUp() {
    if (!GenerateRsaKey(param_.key_size_bits, &pkey_, &key_spki_der_)) {
      LOG(ERROR) << "Error generating the RSA key";
      return false;
    }
    if (!InitDelegate()) {
      LOG(ERROR) << "Error creating the delegate";
      return false;
    }
    return true;
  }

  bool Run() {
    if (!param_.expect_success()) {
      if (!CheckSecretCreationFails()) {
        LOG(ERROR) << "Error: successfully created secret unexpectedly";
        return false;
      }
      return true;
    }
    // Create a secret.
    SecureBlob secret_value;
    SignatureSealedData sealed_secret_data;
    if (!CreateSecret(&secret_value, &sealed_secret_data)) {
      LOG(ERROR) << "Error creating a secret";
      return false;
    }
    // Unseal the secret.
    Blob first_challenge_value;
    Blob first_challenge_signature;
    SecureBlob first_unsealed_value;
    if (!Unseal(sealed_secret_data, &first_challenge_value,
                &first_challenge_signature, &first_unsealed_value)) {
      LOG(ERROR) << "Error unsealing a secret";
      return false;
    }
    if (first_unsealed_value != secret_value) {
      LOG(ERROR)
          << "Error: unsealing returned different value than at creation time";
      return false;
    }
    // Unseal the secret again - the challenge is different, but the result is
    // the same.
    Blob second_challenge_value;
    Blob second_challenge_signature;
    SecureBlob second_unsealed_value;
    if (!Unseal(sealed_secret_data, &second_challenge_value,
                &second_challenge_signature, &second_unsealed_value)) {
      LOG(ERROR) << "Error unsealing secret for the second time";
      return false;
    }
    if (first_challenge_value == second_challenge_value) {
      LOG(ERROR) << "Error: challenge value collision";
      return false;
    }
    if (second_unsealed_value != secret_value) {
      LOG(ERROR)
          << "Error: unsealing returned different value than at creation time";
      return false;
    }
    // Unsealing with a bad challenge response fails.
    if (!CheckUnsealingFailsWithOldSignature(sealed_secret_data,
                                             first_challenge_signature) ||
        !CheckUnsealingFailsWithBadAlgorithmSignature(sealed_secret_data) ||
        !CheckUnsealingFailsWithBadSignature(sealed_secret_data)) {
      LOG(ERROR) << "Failed testing against bad challenge responses";
      return false;
    }
    // Unsealing with a bad key fails.
    if (!CheckUnsealingFailsWithWrongAlgorithm(sealed_secret_data) ||
        !CheckUnsealingFailsWithWrongKey(sealed_secret_data)) {
      LOG(ERROR) << "Failed testing against bad keys";
      return false;
    }
    // Create and unseal another secret - it has a different value.
    SecureBlob another_secret_value;
    SignatureSealedData another_sealed_secret_data;
    if (!CreateSecret(&another_secret_value, &another_sealed_secret_data)) {
      LOG(ERROR) << "Error creating another secret";
      return false;
    }
    if (another_secret_value == secret_value) {
      LOG(ERROR) << "Error: secret value collision";
      return false;
    }
    Blob third_challenge_value;
    Blob third_challenge_signature;
    SecureBlob third_unsealed_value;
    if (!Unseal(another_sealed_secret_data, &third_challenge_value,
                &third_challenge_signature, &third_unsealed_value)) {
      LOG(ERROR) << "Error unsealing another secret";
      return false;
    }
    if (third_unsealed_value != another_secret_value) {
      LOG(ERROR)
          << "Error: unsealing returned different value than at creation time";
      return false;
    }
    // Unsealing after PCRs change fails.
    if (!CheckUnsealingFailsWithChangedPcrs(another_sealed_secret_data)) {
      LOG(ERROR) << "Failed testing against changed PCRs";
      return false;
    }
    return true;
  }

 private:
  static constexpr uint32_t kPcrIndexToExtend = 15;
  const std::set<uint32_t> kPcrIndexes{0, kPcrIndexToExtend};
  static constexpr uint8_t kDelegateFamilyLabel = 100;
  static constexpr uint8_t kDelegateLabel = 101;

  Tpm* tpm() { return param_.tpm; }

  SignatureSealingBackend* backend() {
    return tpm()->GetSignatureSealingBackend();
  }

  static bool GenerateRsaKey(int key_size_bits,
                             crypto::ScopedEVP_PKEY* pkey,
                             Blob* key_spki_der) {
    crypto::ScopedEVP_PKEY_CTX pkey_context(
        EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr));
    if (!pkey_context)
      return false;
    if (EVP_PKEY_keygen_init(pkey_context.get()) <= 0)
      return false;
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(pkey_context.get(), key_size_bits) <=
        0) {
      return false;
    }
    EVP_PKEY* pkey_raw = nullptr;
    if (EVP_PKEY_keygen(pkey_context.get(), &pkey_raw) <= 0)
      return false;
    pkey->reset(pkey_raw);
    // Obtain the DER-encoded Subject Public Key Info.
    const int key_spki_der_length = i2d_PUBKEY(pkey->get(), nullptr);
    if (key_spki_der_length < 0)
      return false;
    key_spki_der->resize(key_spki_der_length);
    unsigned char* key_spki_der_buffer = key_spki_der->data();
    return i2d_PUBKEY(pkey->get(), &key_spki_der_buffer) ==
           key_spki_der->size();
  }

  // Creates the TPM 1.2 delegate.
  bool InitDelegate() {
    if (tpm()->GetVersion() != Tpm::TPM_1_2)
      return true;
    const ScopedTpmOwnerPasswordSetter scoped_tpm_owner_password_setter(
        owner_password_);
    return tpm()->CreateDelegate(kPcrIndexes, kDelegateFamilyLabel,
                                 kDelegateLabel, &delegate_blob_,
                                 &delegate_secret_);
  }

  // Deletes the TPM 1.2 delegate and family from the TPM's NVRAM. Not doing
  // that will result in the NVRAM space exhaustion after several launches of
  // the test.
  void CleanUpDelegate() {
#if !USE_TPM2
    CHECK_EQ(Tpm::TPM_1_2, tpm()->GetVersion());
    using trousers::ScopedTssContext;
    using trousers::ScopedTssMemory;
    using trousers::ScopedTssObject;
    if (delegate_blob_.empty() || delegate_secret_.empty())
      return;
    // Obtain the TPM context and handle with the owner authorization.
    const ScopedTpmOwnerPasswordSetter scoped_tpm_owner_password_setter(
        owner_password_);
    ScopedTssContext tpm_context;
    TSS_HTPM tpm_handle = 0;
    if (!static_cast<TpmImpl*>(tpm())->ConnectContextAsOwner(tpm_context.ptr(),
                                                             &tpm_handle)) {
      LOG(ERROR)
          << "Failed to clean up the delegate: error connecting to the TPM";
      return;
    }
    // Obtain all TPM delegates and delegate families.
    UINT32 family_table_size = 0;
    TSS_FAMILY_TABLE_ENTRY* family_table_ptr = nullptr;
    UINT32 delegate_table_size = 0;
    TSS_DELEGATION_TABLE_ENTRY* delegate_table_ptr = nullptr;
    TSS_RESULT tss_result = Tspi_TPM_Delegate_ReadTables(
        tpm_context, &family_table_size, &family_table_ptr,
        &delegate_table_size, &delegate_table_ptr);
    if (TPM_ERROR(tss_result)) {
      LOG(ERROR)
          << "Failed to clean up the delegate: error reading delegate table: "
          << Trspi_Error_String(tss_result);
      return;
    }
    ScopedTssMemory scoped_family_table(
        tpm_context, reinterpret_cast<BYTE*>(family_table_ptr));
    ScopedTssMemory scoped_delegate_table(
        tpm_context, reinterpret_cast<BYTE*>(delegate_table_ptr));
    // Invalidate the delegate families which have the test label. Note that
    // this removes from the NVRAM both the delegate families and the delegates
    // themselves.
    int invalidated_family_count = 0;
    UINT64 family_table_offset = 0;
    for (int family_index = 0; family_index < family_table_size;
         ++family_index) {
      TSS_FAMILY_TABLE_ENTRY family_entry;
      Trspi_UnloadBlob_TSS_FAMILY_TABLE_ENTRY(
          &family_table_offset, scoped_family_table.value(), &family_entry);
      if (family_entry.label == kDelegateFamilyLabel) {
        ScopedTssObject<TSS_HDELFAMILY> family_handle(tpm_context);
        tss_result = Tspi_TPM_Delegate_GetFamily(
            tpm_handle, family_entry.familyID, family_handle.ptr());
        if (TPM_ERROR(tss_result)) {
          LOG(ERROR) << "Failed to clean up the delegate: error getting "
                        "delegate family handle: "
                     << Trspi_Error_String(tss_result);
          continue;
        }
        tss_result =
            Tspi_TPM_Delegate_InvalidateFamily(tpm_handle, family_handle);
        if (TPM_ERROR(tss_result)) {
          LOG(ERROR) << "Failed to clean up the delegate: error invalidating "
                        "delegate family: "
                     << Trspi_Error_String(tss_result);
          continue;
        }
        ++invalidated_family_count;
      }
    }
    if (!invalidated_family_count) {
      LOG(ERROR) << "Failed to clean up the delegate: no entry was "
                    "successfully invalidated";
      return;
    }
    VLOG(1) << "Delegate families cleaned up: " << invalidated_family_count;
#endif  // !USE_TPM2
  }

  bool CreateSecret(SecureBlob* secret_value,
                    SignatureSealedData* sealed_secret_data) {
    std::map<uint32_t, Blob> pcr_values;
    if (!GetCurrentPcrValues(&pcr_values)) {
      LOG(ERROR) << "Error reading PCR values";
      return false;
    }
    if (!backend()->CreateSealedSecret(
            key_spki_der_, param_.supported_algorithms,
            {pcr_values, pcr_values}, delegate_blob_, delegate_secret_,
            secret_value, sealed_secret_data)) {
      LOG(ERROR) << "Error creating signature-sealed secret";
      return false;
    }
    return true;
  }

  bool CheckSecretCreationFails() {
    std::map<uint32_t, Blob> pcr_values;
    if (!GetCurrentPcrValues(&pcr_values)) {
      LOG(ERROR) << "Error reading PCR values";
      return false;
    }
    SecureBlob secret_value;
    SignatureSealedData sealed_secret_data;
    if (backend()->CreateSealedSecret(key_spki_der_,
                                      param_.supported_algorithms, {pcr_values},
                                      delegate_blob_, delegate_secret_,
                                      &secret_value, &sealed_secret_data)) {
      LOG(ERROR) << "Error: secret creation completed unexpectedly";
      return false;
    }
    return true;
  }

  bool GetCurrentPcrValues(std::map<uint32_t, Blob>* pcr_values) {
    for (auto pcr_index : kPcrIndexes) {
      if (!tpm()->ReadPCR(pcr_index, &(*pcr_values)[pcr_index])) {
        LOG(ERROR) << "Error reading PCR value " << pcr_index;
        return false;
      }
    }
    return true;
  }

  bool Unseal(const SignatureSealedData& sealed_secret_data,
              Blob* challenge_value,
              Blob* challenge_signature,
              SecureBlob* unsealed_value) {
    std::unique_ptr<UnsealingSession> unsealing_session(
        backend()->CreateUnsealingSession(sealed_secret_data, key_spki_der_,
                                          param_.supported_algorithms,
                                          delegate_blob_, delegate_secret_));
    if (!unsealing_session) {
      LOG(ERROR) << "Error starting the unsealing session";
      return false;
    }
    if (unsealing_session->GetChallengeAlgorithm() !=
        *param_.expected_algorithm) {
      LOG(ERROR) << "Wrong challenge signature algorithm";
      return false;
    }
    *challenge_value = unsealing_session->GetChallengeValue();
    if (challenge_value->empty()) {
      LOG(ERROR) << "The challenge is empty";
      return false;
    }
    if (!SignWithKey(*challenge_value, param_.openssl_algorithm_nid,
                     challenge_signature)) {
      LOG(ERROR) << "Error generating signature of challenge";
      return false;
    }
    if (!unsealing_session->Unseal(*challenge_signature, unsealed_value)) {
      LOG(ERROR) << "Error unsealing the secret";
      return false;
    }
    if (unsealed_value->empty()) {
      LOG(ERROR) << "Error: empty unsealing result";
      return false;
    }
    return true;
  }

  bool CheckUnsealingFailsWithOldSignature(
      const SignatureSealedData& sealed_secret_data,
      const Blob& challenge_signature) {
    std::unique_ptr<UnsealingSession> unsealing_session(
        backend()->CreateUnsealingSession(sealed_secret_data, key_spki_der_,
                                          param_.supported_algorithms,
                                          delegate_blob_, delegate_secret_));
    if (!unsealing_session) {
      LOG(ERROR) << "Error starting the unsealing session";
      return false;
    }
    SecureBlob unsealed_value;
    if (unsealing_session->Unseal(challenge_signature, &unsealed_value)) {
      LOG(ERROR)
          << "Error: unsealing completed with an old challenge signature";
      return false;
    }
    return true;
  }

  bool CheckUnsealingFailsWithBadAlgorithmSignature(
      const SignatureSealedData& sealed_secret_data) {
    std::unique_ptr<UnsealingSession> unsealing_session(
        backend()->CreateUnsealingSession(sealed_secret_data, key_spki_der_,
                                          param_.supported_algorithms,
                                          delegate_blob_, delegate_secret_));
    if (!unsealing_session) {
      LOG(ERROR) << "Error starting the unsealing session";
      return false;
    }
    const int wrong_openssl_algorithm_nid =
        param_.openssl_algorithm_nid == NID_sha1 ? NID_sha256 : NID_sha1;
    Blob challenge_signature;
    if (!SignWithKey(unsealing_session->GetChallengeValue(),
                     wrong_openssl_algorithm_nid, &challenge_signature)) {
      LOG(ERROR) << "Error generating signature of challenge";
      return false;
    }
    SecureBlob unsealed_value;
    if (unsealing_session->Unseal(challenge_signature, &unsealed_value)) {
      LOG(ERROR) << "Error: unsealing completed with a wrong signature";
      return false;
    }
    return true;
  }

  bool CheckUnsealingFailsWithBadSignature(
      const SignatureSealedData& sealed_secret_data) {
    std::unique_ptr<UnsealingSession> unsealing_session(
        backend()->CreateUnsealingSession(sealed_secret_data, key_spki_der_,
                                          param_.supported_algorithms,
                                          delegate_blob_, delegate_secret_));
    if (!unsealing_session) {
      LOG(ERROR) << "Error starting the unsealing session";
      return false;
    }
    Blob challenge_signature;
    if (!SignWithKey(unsealing_session->GetChallengeValue(),
                     param_.openssl_algorithm_nid, &challenge_signature)) {
      LOG(ERROR) << "Error generating signature of challenge";
      return false;
    }
    challenge_signature.front() ^= 1;
    SecureBlob unsealed_value;
    if (unsealing_session->Unseal(challenge_signature, &unsealed_value)) {
      LOG(ERROR) << "Error: unsealing completed with a wrong signature";
      return false;
    }
    return true;
  }

  bool CheckUnsealingFailsWithWrongAlgorithm(
      const SignatureSealedData& sealed_secret_data) {
    const ChallengeSignatureAlgorithm wrong_algorithm =
        *param_.expected_algorithm == CHALLENGE_RSASSA_PKCS1_V1_5_SHA1
            ? CHALLENGE_RSASSA_PKCS1_V1_5_SHA256
            : CHALLENGE_RSASSA_PKCS1_V1_5_SHA1;
    if (backend()->CreateUnsealingSession(sealed_secret_data, key_spki_der_,
                                          {wrong_algorithm}, delegate_blob_,
                                          delegate_secret_)) {
      LOG(ERROR) << "Error: unsealing session creation completed with a "
                    "wrong algorithm";
      return false;
    }
    return true;
  }

  bool CheckUnsealingFailsWithWrongKey(
      const SignatureSealedData& sealed_secret_data) {
    crypto::ScopedEVP_PKEY other_pkey;
    Blob other_key_spki_der;
    if (!GenerateRsaKey(param_.key_size_bits, &other_pkey,
                        &other_key_spki_der)) {
      LOG(ERROR) << "Error generating the other RSA key";
      return false;
    }
    if (backend()->CreateUnsealingSession(
            sealed_secret_data, other_key_spki_der, param_.supported_algorithms,
            delegate_blob_, delegate_secret_)) {
      LOG(ERROR)
          << "Error: unsealing session creation completed with a wrong key";
      return false;
    }
    return true;
  }

  bool CheckUnsealingFailsWithChangedPcrs(
      const SignatureSealedData& sealed_secret_data) {
    if (!tpm()->ExtendPCR(kPcrIndexToExtend,
                          BlobFromString("01234567890123456789"))) {
      LOG(ERROR) << "Error extending PCR";
      return false;
    }
    std::unique_ptr<UnsealingSession> unsealing_session(
        backend()->CreateUnsealingSession(sealed_secret_data, key_spki_der_,
                                          param_.supported_algorithms,
                                          delegate_blob_, delegate_secret_));
    if (!unsealing_session) {
      // Unsealing expectedly failed, so the test is passed. (Whether it fails
      // here or below after Unseal() depends on the specific
      // SignatureSealingBackend implementation.)
      return true;
    }
    Blob challenge_signature;
    if (!SignWithKey(unsealing_session->GetChallengeValue(),
                     param_.openssl_algorithm_nid, &challenge_signature)) {
      LOG(ERROR) << "Error generating signature of challenge";
      return false;
    }
    SecureBlob unsealed_value;
    if (unsealing_session->Unseal(challenge_signature, &unsealed_value)) {
      LOG(ERROR) << "Error: unsealing completed with changed PCRs";
      return false;
    }
    return true;
  }

  bool SignWithKey(const Blob& unhashed_data,
                   int algorithm_nid,
                   Blob* signature) {
    signature->resize(EVP_PKEY_size(pkey_.get()));
    crypto::ScopedEVP_MD_CTX sign_context(EVP_MD_CTX_new());
    unsigned signature_size = 0;
    if (!sign_context) {
      LOG(ERROR) << "Error creating signing context";
      return false;
    }
    if (!EVP_SignInit(sign_context.get(), EVP_get_digestbynid(algorithm_nid))) {
      LOG(ERROR) << "Error initializing signature operation";
      return false;
    }
    if (!EVP_SignUpdate(sign_context.get(), unhashed_data.data(),
                        unhashed_data.size())) {
      LOG(ERROR) << "Error updating signature operation with data";
      return false;
    }
    if (!EVP_SignFinal(sign_context.get(), signature->data(), &signature_size,
                       pkey_.get())) {
      LOG(ERROR) << "Error finalizing signature operation";
      return false;
    }
    CHECK_LE(signature_size, signature->size());
    signature->resize(signature_size);
    return true;
  }

  const SignatureSealedSecretTestCaseParam param_;
  const SecureBlob owner_password_;
  Blob delegate_blob_;
  Blob delegate_secret_;
  crypto::ScopedEVP_PKEY pkey_;
  Blob key_spki_der_;

  DISALLOW_COPY_AND_ASSIGN(SignatureSealedSecretTestCase);
};

}  // namespace

bool TpmLiveTest::SignatureSealedSecretTest(const SecureBlob& owner_password) {
  using TestCaseParam = SignatureSealedSecretTestCaseParam;
  if (!tpm_->GetSignatureSealingBackend()) {
    // Not supported by the Tpm implementation, just skip the test.
    return true;
  }
  LOG(INFO) << "SignatureSealedSecretTest started";
  std::vector<TestCaseParam> test_case_params;
  for (int key_size_bits : {1024, 2048}) {
    test_case_params.push_back(TestCaseParam::MakeSuccessful(
        "SHA-1", tpm_, key_size_bits, {CHALLENGE_RSASSA_PKCS1_V1_5_SHA1},
        CHALLENGE_RSASSA_PKCS1_V1_5_SHA1, NID_sha1));
    if (tpm_->GetVersion() == Tpm::TPM_1_2) {
      test_case_params.push_back(
          TestCaseParam::MakeFailing("SHA-256", tpm_, key_size_bits,
                                     {CHALLENGE_RSASSA_PKCS1_V1_5_SHA256}));
      test_case_params.push_back(
          TestCaseParam::MakeFailing("SHA-384", tpm_, key_size_bits,
                                     {CHALLENGE_RSASSA_PKCS1_V1_5_SHA384}));
      test_case_params.push_back(
          TestCaseParam::MakeFailing("SHA-512", tpm_, key_size_bits,
                                     {CHALLENGE_RSASSA_PKCS1_V1_5_SHA512}));
      test_case_params.push_back(TestCaseParam::MakeSuccessful(
          "{SHA-1,SHA-256}", tpm_, key_size_bits,
          {CHALLENGE_RSASSA_PKCS1_V1_5_SHA256,
           CHALLENGE_RSASSA_PKCS1_V1_5_SHA1},
          CHALLENGE_RSASSA_PKCS1_V1_5_SHA1, NID_sha1));
    } else {
      test_case_params.push_back(TestCaseParam::MakeSuccessful(
          "SHA-256", tpm_, key_size_bits, {CHALLENGE_RSASSA_PKCS1_V1_5_SHA256},
          CHALLENGE_RSASSA_PKCS1_V1_5_SHA256, NID_sha256));
      test_case_params.push_back(TestCaseParam::MakeSuccessful(
          "SHA-384", tpm_, key_size_bits, {CHALLENGE_RSASSA_PKCS1_V1_5_SHA384},
          CHALLENGE_RSASSA_PKCS1_V1_5_SHA384, NID_sha384));
      test_case_params.push_back(TestCaseParam::MakeSuccessful(
          "SHA-512", tpm_, key_size_bits, {CHALLENGE_RSASSA_PKCS1_V1_5_SHA512},
          CHALLENGE_RSASSA_PKCS1_V1_5_SHA512, NID_sha512));
      test_case_params.push_back(TestCaseParam::MakeSuccessful(
          "{SHA-384,SHA-256,SHA-512}", tpm_, key_size_bits,
          {CHALLENGE_RSASSA_PKCS1_V1_5_SHA384,
           CHALLENGE_RSASSA_PKCS1_V1_5_SHA256,
           CHALLENGE_RSASSA_PKCS1_V1_5_SHA512},
          CHALLENGE_RSASSA_PKCS1_V1_5_SHA384, NID_sha384));
      test_case_params.push_back(TestCaseParam::MakeSuccessful(
          "{SHA-1,SHA-256}", tpm_, key_size_bits,
          {CHALLENGE_RSASSA_PKCS1_V1_5_SHA1,
           CHALLENGE_RSASSA_PKCS1_V1_5_SHA256},
          CHALLENGE_RSASSA_PKCS1_V1_5_SHA256, NID_sha256));
    }
  }
  for (auto&& test_case_param : test_case_params) {
    SignatureSealedSecretTestCase test_case(std::move(test_case_param),
                                            owner_password);
    if (!test_case.SetUp() || !test_case.Run())
      return false;
  }
  LOG(INFO) << "SignatureSealedSecretTest ended successfully.";
  return true;
}

}  // namespace cryptohome
