// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests for the ChallengeCredentialsHelperImpl class.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/challenge_credentials/challenge_credentials_constants.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper_impl.h"
#include "cryptohome/challenge_credentials/challenge_credentials_test_utils.h"
#include "cryptohome/credentials.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/key.pb.h"
#include "cryptohome/mock_key_challenge_service.h"
#include "cryptohome/mock_signature_sealing_backend.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/rpc.pb.h"
#include "cryptohome/signature_sealed_data.pb.h"
#include "cryptohome/signature_sealing_backend.h"
#include "cryptohome/signature_sealing_backend_test_utils.h"
#include "cryptohome/vault_keyset.pb.h"

using brillo::Blob;
using brillo::BlobToString;
using brillo::CombineBlobs;
using brillo::SecureBlob;
using testing::_;
using testing::AnyNumber;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;
using testing::Values;

namespace cryptohome {

using KeysetSignatureChallengeInfo =
    SerializedVaultKeyset::SignatureChallengeInfo;

namespace {

KeyData MakeKeyData(
    const Blob& set_public_key_spki_der,
    const std::vector<ChallengeSignatureAlgorithm>& key_algorithms) {
  KeyData key_data;
  key_data.set_type(KeyData::KEY_TYPE_CHALLENGE_RESPONSE);
  ChallengePublicKeyInfo* const public_key_info =
      key_data.add_challenge_response_key();
  public_key_info->set_public_key_spki_der(
      BlobToString(set_public_key_spki_der));
  for (auto key_algorithm : key_algorithms)
    public_key_info->add_signature_algorithm(key_algorithm);
  return key_data;
}

KeysetSignatureChallengeInfo MakeFakeKeysetChallengeInfo(
    const Blob& public_key_spki_der,
    const Blob& salt,
    ChallengeSignatureAlgorithm salt_challenge_algorithm) {
  KeysetSignatureChallengeInfo keyset_challenge_info;
  keyset_challenge_info.set_public_key_spki_der(
      BlobToString(public_key_spki_der));
  *keyset_challenge_info.mutable_sealed_secret() =
      MakeFakeSignatureSealedData(public_key_spki_der);
  keyset_challenge_info.set_salt(BlobToString(salt));
  keyset_challenge_info.set_salt_signature_algorithm(salt_challenge_algorithm);
  return keyset_challenge_info;
}

// Base fixture class that provides some common constants, helpers and mocks for
// testing ChallengeCredentialsHelperImpl.
class ChallengeCredentialsHelperImplTestBase : public testing::Test {
 protected:
  ChallengeCredentialsHelperImplTestBase()
      : challenge_credentials_helper_(&tpm_, kDelegateBlob, kDelegateSecret) {}

  void PrepareSignatureSealingBackend(bool enabled) {
    SignatureSealingBackend* const return_value =
        enabled ? &sealing_backend_ : nullptr;
    EXPECT_CALL(tpm_, GetSignatureSealingBackend())
        .WillRepeatedly(Return(return_value));
  }

  // Starts the asynchronous GenerateNew() operation.  The result, once the
  // operation completes, will be stored in |generate_new_result|.
  void CallGenerateNew(
      const std::vector<ChallengeSignatureAlgorithm>& key_algorithms,
      std::unique_ptr<ChallengeCredentialsGenerateNewResult>*
          generate_new_result) {
    DCHECK(challenge_service_);
    const KeyData key_data = MakeKeyData(kPublicKeySpkiDer, key_algorithms);
    challenge_credentials_helper_.GenerateNew(
        kUserEmail, key_data, kPcrRestrictions, std::move(challenge_service_),
        MakeChallengeCredentialsGenerateNewResultWriter(generate_new_result));
  }

  // Starts the asynchronous Decrypt() operation.  The result, once the
  // operation completes, will be stored in |decrypt_result|.
  void CallDecrypt(
      const std::vector<ChallengeSignatureAlgorithm>& key_algorithms,
      ChallengeSignatureAlgorithm salt_challenge_algorithm,
      const Blob& salt,
      std::unique_ptr<ChallengeCredentialsDecryptResult>* decrypt_result) {
    DCHECK(challenge_service_);
    const KeyData key_data = MakeKeyData(kPublicKeySpkiDer, key_algorithms);
    const KeysetSignatureChallengeInfo keyset_challenge_info =
        MakeFakeKeysetChallengeInfo(kPublicKeySpkiDer, salt,
                                    salt_challenge_algorithm);
    challenge_credentials_helper_.Decrypt(
        kUserEmail, key_data, keyset_challenge_info,
        std::move(challenge_service_),
        MakeChallengeCredentialsDecryptResultWriter(decrypt_result));
  }

  // Starts the Decrypt() operation without observing the challenge requests it
  // makes or its result. Intended to be used for testing the corner case of
  // starting an operation before the previous one is completed.
  void StartSurplusOperation() {
    // Use different parameters here, to avoid clashing with mocks set up for
    // the normal operation.
    constexpr ChallengeSignatureAlgorithm kLocalAlgorithm =
        CHALLENGE_RSASSA_PKCS1_V1_5_SHA256;
    const Blob kLocalPublicKeySpkiDer =
        CombineBlobs({kPublicKeySpkiDer, Blob(1)});

    auto unsealing_mocker =
        MakeUnsealingMocker({kLocalAlgorithm} /* key_algorithms */,
                            kLocalAlgorithm /* unsealing_algorithm */);
    unsealing_mocker->set_public_key_spki_der(kLocalPublicKeySpkiDer);
    unsealing_mocker->SetUpUnsealingNotCalledMock();

    auto mock_key_challenge_service =
        std::make_unique<MockKeyChallengeService>();
    EXPECT_CALL(*mock_key_challenge_service, ChallengeKeyMovable(_, _, _))
        .Times(AnyNumber());
    const KeyData key_data = MakeKeyData(
        kLocalPublicKeySpkiDer, {kLocalAlgorithm} /* key_algorithms */);
    const KeysetSignatureChallengeInfo keyset_challenge_info =
        MakeFakeKeysetChallengeInfo(
            kLocalPublicKeySpkiDer, kSalt,
            kLocalAlgorithm /* salt_challenge_algorithm */);
    challenge_credentials_helper_.Decrypt(
        kUserEmail, key_data, keyset_challenge_info,
        std::move(mock_key_challenge_service),
        base::Bind([](std::unique_ptr<Credentials>) {}));
  }

  // Assert that the given GenerateNew() operation result is a valid success
  // result.
  void VerifySuccessfulGenerateNewResult(
      const ChallengeCredentialsGenerateNewResult& generate_new_result) const {
    VerifySuccessfulChallengeCredentialsGenerateNewResult(
        generate_new_result, kUserEmail,
        SecureBlob(kPasskey.begin(), kPasskey.end()));
  }

  // Assert that the given Decrypt() operation result is a valid success result.
  void VerifySuccessfulDecryptResult(
      const ChallengeCredentialsDecryptResult& decrypt_result) const {
    VerifySuccessfulChallengeCredentialsDecryptResult(
        decrypt_result, kUserEmail,
        SecureBlob(kPasskey.begin(), kPasskey.end()));
  }

  // Returns a helper object that aids mocking of the sealed secret creation
  // functionality (SignatureSealingBackend::CreateSealedSecret()).
  std::unique_ptr<SignatureSealedCreationMocker> MakeSealedCreationMocker(
      const std::vector<ChallengeSignatureAlgorithm>& key_algorithms) {
    auto mocker =
        std::make_unique<SignatureSealedCreationMocker>(&sealing_backend_);
    mocker->set_public_key_spki_der(kPublicKeySpkiDer);
    mocker->set_key_algorithms(key_algorithms);
    mocker->set_pcr_restrictions(kPcrRestrictions);
    mocker->set_delegate_blob(kDelegateBlob);
    mocker->set_delegate_secret(kDelegateSecret);
    mocker->set_secret_value(kTpmProtectedSecret);
    return mocker;
  }

  // Returns a helper object that aids mocking of the secret unsealing
  // functionality (SignatureSealingBackend::CreateUnsealingSession() et al.).
  std::unique_ptr<SignatureSealedUnsealingMocker> MakeUnsealingMocker(
      const std::vector<ChallengeSignatureAlgorithm>& key_algorithms,
      ChallengeSignatureAlgorithm unsealing_algorithm) {
    auto mocker =
        std::make_unique<SignatureSealedUnsealingMocker>(&sealing_backend_);
    mocker->set_public_key_spki_der(kPublicKeySpkiDer);
    mocker->set_key_algorithms(key_algorithms);
    mocker->set_delegate_blob(kDelegateBlob);
    mocker->set_delegate_secret(kDelegateSecret);
    mocker->set_chosen_algorithm(unsealing_algorithm);
    mocker->set_challenge_value(kUnsealingChallengeValue);
    mocker->set_challenge_signature(kUnsealingChallengeSignature);
    mocker->set_secret_value(kTpmProtectedSecret);
    return mocker;
  }

  // Sets up an expectation that the salt challenge request will be issued via
  // |challenge_service_|.
  void ExpectSaltChallenge(
      ChallengeSignatureAlgorithm salt_challenge_algorithm) {
    salt_challenge_mock_controller_.ExpectSignatureChallenge(
        kUserEmail, kPublicKeySpkiDer, kSalt, salt_challenge_algorithm);
  }

  // Whether the salt challenge request has been started.
  bool is_salt_challenge_requested() const {
    return salt_challenge_mock_controller_.is_challenge_requested();
  }

  // Injects a simulated successful response for the currently running salt
  // challenge request.
  void SimulateSaltChallengeResponse() {
    salt_challenge_mock_controller_.SimulateSignatureChallengeResponse(
        kSaltSignature);
  }

  // Injects a simulated failure response for the currently running salt
  // challenge request.
  void SimulateSaltChallengeFailure() {
    salt_challenge_mock_controller_.SimulateFailureResponse();
  }

  // Sets up an expectation that the secret unsealing challenge request will be
  // issued via |challenge_service_|.
  void ExpectUnsealingChallenge(
      ChallengeSignatureAlgorithm unsealing_algorithm) {
    unsealing_challenge_mock_controller_.ExpectSignatureChallenge(
        kUserEmail, kPublicKeySpkiDer, kUnsealingChallengeValue,
        unsealing_algorithm);
  }

  // Whether the secret unsealing challenge request has been started.
  bool is_unsealing_challenge_requested() const {
    return unsealing_challenge_mock_controller_.is_challenge_requested();
  }

  // Injects a simulated successful response for the currently running secret
  // unsealing challenge request.
  void SimulateUnsealingChallengeResponse() {
    unsealing_challenge_mock_controller_.SimulateSignatureChallengeResponse(
        kUnsealingChallengeSignature);
  }

  // Injects a simulated failure response for the currently running secret
  // unsealing challenge request.
  void SimulateUnsealingChallengeFailure() {
    unsealing_challenge_mock_controller_.SimulateFailureResponse();
  }

  // Sets up a mock for the successful salt generation.
  void SetSuccessfulSaltGenerationMock() {
    EXPECT_CALL(tpm_,
                GetRandomDataBlob(kChallengeCredentialsSaltRandomByteCount, _))
        .WillOnce(DoAll(SetArgPointee<1>(kSaltRandomPart), Return(true)));
  }

  // Sets up a mock for the failure during salt generation.
  void SetFailingSaltGenerationMock() {
    EXPECT_CALL(tpm_,
                GetRandomDataBlob(kChallengeCredentialsSaltRandomByteCount, _))
        .WillOnce(Return(false));
  }

 protected:
  // Constants which are passed as fake data inputs to the
  // ChallengeCredentialsHelperImpl methods:

  // Fake TPM delegate. It's supplied to the ChallengeCredentialsHelperImpl
  // constructor. Then it's verified to be passed into SignatureSealingBackend
  // methods.
  const Blob kDelegateBlob{{1, 1, 1}};
  const Blob kDelegateSecret{{2, 2, 2}};
  // Fake user e-mail. It's supplied to the ChallengeCredentialsHelperImpl
  // operation methods. Then it's verified to be passed alongside challenge
  // requests made via KeyChallengeService, and to be present in the resulting
  // Credentials.
  const std::string kUserEmail = "foo@example.com";
  // Fake Subject Public Key Information of the challenged cryptographic key.
  // It's supplied to the ChallengeCredentialsHelperImpl operation methods as a
  // field of both |key_data| and |keyset_challenge_info| parameters. Then it's
  // verified to be passed into SignatureSealingBackend methods and to be used
  // for challenge requests made via KeyChallengeService.
  const Blob kPublicKeySpkiDer{{3, 3, 3}};
  // Fake random part of the salt. When testing the GenerateNew() operation,
  // it's injected as a fake result of the TPM GetRandomDataBlob(). It's also
  // used as part of the |kSalt| constant in a few other places.
  const Blob kSaltRandomPart = Blob(20, 4);
  // Fake salt value. It's supplied to the ChallengeCredentialsHelperImpl
  // operation methods as a field of the |keyset_challenge_info| parameter. Then
  // it's verified to be used as the challenge value for one of requests made
  // via KeyChallengeService.
  const Blob kSalt = CombineBlobs(
      {GetChallengeCredentialsSaltConstantPrefix(), kSaltRandomPart});
  // Fake PCR restrictions: a list of maps from PCR indexes to PCR values. It's
  // supplied to the GenerateNew() operation. Then it's verified to be passed
  // into the SignatureSealingBackend::CreateSealedSecret() method.
  const std::vector<std::map<uint32_t, Blob>> kPcrRestrictions{
      std::map<uint32_t, Blob>{{0, {9, 9, 9}}, {10, {11, 11, 11}}},
      std::map<uint32_t, Blob>{{0, {9, 9, 9}}, {10, {12, 12, 12}}}};

  // Constants which are injected as fake data into intermediate steps of the
  // ChallengeCredentialsHelperImpl operations:

  // Fake signature of |kSalt| using the |salt_challenge_algorithm_| algorithm.
  // It's injected as a fake response to the salt challenge request made via
  // KeyChallengeService. Then it's implicitly verified to be used for the
  // generation of the passkey in the resulting Credentials - see the |kPasskey|
  // constant.
  const Blob kSaltSignature{{5, 5, 5}};
  // Fake challenge value for unsealing the secret. It's injected as a fake
  // value returned from SignatureSealingBackend::UnsealingSession. Then it's
  // verified to be used as the challenge value for one of requests made via
  // KeyChallengeService.
  const Blob kUnsealingChallengeValue{{6, 6, 6}};
  // Fake signature of |kUnsealingChallengeValue| using the
  // |unsealing_algorithm_| algorithm. It's injected as a fake response to the
  // unsealing challenge request made via KeyChallengeService. Then it's
  // verified to be passed to the Unseal() method of
  // SignatureSealingBackend::UnsealingSession.
  const Blob kUnsealingChallengeSignature{{7, 7, 7}};
  // Fake TPM-protected secret. When testing the GenerateNew() operation, it's
  // injected as a fake result of SignatureSealingBackend::CreateSealedSecret()
  // method. When testing the Decrypt() operation, it's injected as a fake
  // result of the Unseal() method of SignatureSealingBackend::UnsealingSession.
  // Also this constant is implicitly verified to be used for the generation of
  // the passkey in the resulting Credentials - see the |kPasskey| constant.
  const Blob kTpmProtectedSecret{{8, 8, 8}};

  // The expected passkey of the resulting Credentials returned from the
  // ChallengeCredentialsHelperImpl operations. Its value is derived from the
  // injected fake data.
  const Blob kPasskey =
      CombineBlobs({kTpmProtectedSecret, CryptoLib::Sha256(kSaltSignature)});

 private:
  // Mock objects:

  StrictMock<MockSignatureSealingBackend> sealing_backend_;
  StrictMock<MockTpm> tpm_;
  std::unique_ptr<StrictMock<MockKeyChallengeService>> challenge_service_ =
      std::make_unique<StrictMock<MockKeyChallengeService>>();
  KeyChallengeServiceMockController salt_challenge_mock_controller_{
      challenge_service_.get()};
  KeyChallengeServiceMockController unsealing_challenge_mock_controller_{
      challenge_service_.get()};

  // The tested instance.
  ChallengeCredentialsHelperImpl challenge_credentials_helper_;
};

// Base fixture class that uses a single algorithm for simplicity.
class ChallengeCredentialsHelperImplSingleAlgorithmTestBase
    : public ChallengeCredentialsHelperImplTestBase {
 protected:
  // The single algorithm to be used in this test.
  static constexpr ChallengeSignatureAlgorithm kAlgorithm =
      CHALLENGE_RSASSA_PKCS1_V1_5_SHA256;
};

// Base fixture class that uses a single algorithm and have the sealing backend
// available.
class ChallengeCredentialsHelperImplBasicTest
    : public ChallengeCredentialsHelperImplSingleAlgorithmTestBase {
 protected:
  // The single algorithm to be used in this test.
  static constexpr ChallengeSignatureAlgorithm kAlgorithm =
      CHALLENGE_RSASSA_PKCS1_V1_5_SHA256;

  ChallengeCredentialsHelperImplBasicTest() {
    PrepareSignatureSealingBackend(true /* enabled */);
  }
};

}  // namespace

// Test success of the GenerateNew() operation.
TEST_F(ChallengeCredentialsHelperImplBasicTest, GenerateNewSuccess) {
  SetSuccessfulSaltGenerationMock();
  ExpectSaltChallenge(kAlgorithm /* salt_challenge_algorithm */);
  MakeSealedCreationMocker({kAlgorithm} /* key_algorithms */)
      ->SetUpSuccessfulMock();

  std::unique_ptr<ChallengeCredentialsGenerateNewResult> generate_new_result;
  CallGenerateNew({kAlgorithm} /* key_algorithms */, &generate_new_result);
  EXPECT_FALSE(generate_new_result);
  EXPECT_TRUE(is_salt_challenge_requested());

  SimulateSaltChallengeResponse();
  ASSERT_TRUE(generate_new_result);
  VerifySuccessfulGenerateNewResult(*generate_new_result);
}

// Test failure of the GenerateNew() operation due to failure in salt
// generation.
TEST_F(ChallengeCredentialsHelperImplBasicTest,
       GenerateNewFailureInSaltGeneration) {
  SetFailingSaltGenerationMock();

  std::unique_ptr<ChallengeCredentialsGenerateNewResult> generate_new_result;
  CallGenerateNew({kAlgorithm} /* key_algorithms */, &generate_new_result);
  ASSERT_TRUE(generate_new_result);
  VerifyFailedChallengeCredentialsGenerateNewResult(*generate_new_result);
}

// Test failure of the GenerateNew() operation due to failure of salt challenge
// request.
TEST_F(ChallengeCredentialsHelperImplBasicTest,
       GenerateNewFailureInSaltChallenge) {
  SetSuccessfulSaltGenerationMock();
  ExpectSaltChallenge(kAlgorithm /* salt_challenge_algorithm */);
  MakeSealedCreationMocker({kAlgorithm} /* key_algorithms */)
      ->SetUpSuccessfulMock();

  std::unique_ptr<ChallengeCredentialsGenerateNewResult> generate_new_result;
  CallGenerateNew({kAlgorithm} /* key_algorithms */, &generate_new_result);
  EXPECT_FALSE(generate_new_result);
  EXPECT_TRUE(is_salt_challenge_requested());

  SimulateSaltChallengeFailure();
  ASSERT_TRUE(generate_new_result);
  VerifyFailedChallengeCredentialsGenerateNewResult(*generate_new_result);
}

// Test failure of the GenerateNew() operation due to failure of sealed secret
// creation.
TEST_F(ChallengeCredentialsHelperImplBasicTest,
       GenerateNewFailureInSealedCreation) {
  SetSuccessfulSaltGenerationMock();
  ExpectSaltChallenge(kAlgorithm /* salt_challenge_algorithm */);
  MakeSealedCreationMocker({kAlgorithm} /* key_algorithms */)
      ->SetUpFailingMock();

  std::unique_ptr<ChallengeCredentialsGenerateNewResult> generate_new_result;
  CallGenerateNew({kAlgorithm} /* key_algorithms */, &generate_new_result);
  ASSERT_TRUE(generate_new_result);
  VerifyFailedChallengeCredentialsGenerateNewResult(*generate_new_result);
}

// Test failure of the Decrypt() operation due to the input salt being empty.
TEST_F(ChallengeCredentialsHelperImplBasicTest,
       DecryptFailureInSaltCheckEmpty) {
  std::unique_ptr<ChallengeCredentialsDecryptResult> decrypt_result;
  CallDecrypt({kAlgorithm} /* key_algorithms */,
              kAlgorithm /* salt_challenge_algorithm */, Blob() /* salt */,
              &decrypt_result);
  ASSERT_TRUE(decrypt_result);
  VerifyFailedChallengeCredentialsDecryptResult(*decrypt_result);
}

// Test failure of the Decrypt() operation due to the input salt not starting
// with the expected constant prefix.
TEST_F(ChallengeCredentialsHelperImplBasicTest,
       DecryptFailureInSaltCheckNotPrefixed) {
  Blob salt = kSalt;
  salt[GetChallengeCredentialsSaltConstantPrefix().size() - 1] ^= 1;
  std::unique_ptr<ChallengeCredentialsDecryptResult> decrypt_result;
  CallDecrypt({kAlgorithm} /* key_algorithms */,
              kAlgorithm /* salt_challenge_algorithm */, salt, &decrypt_result);
  ASSERT_TRUE(decrypt_result);
  VerifyFailedChallengeCredentialsDecryptResult(*decrypt_result);
}

// Test failure of the Decrypt() operation due to the input salt containing
// nothing besides the expected constant prefix.
TEST_F(ChallengeCredentialsHelperImplBasicTest,
       DecryptFailureInSaltCheckNothingBesidesPrefix) {
  std::unique_ptr<ChallengeCredentialsDecryptResult> decrypt_result;
  CallDecrypt({kAlgorithm} /* key_algorithms */,
              kAlgorithm /* salt_challenge_algorithm */,
              GetChallengeCredentialsSaltConstantPrefix() /* salt */,
              &decrypt_result);
  ASSERT_TRUE(decrypt_result);
  VerifyFailedChallengeCredentialsDecryptResult(*decrypt_result);
}

// Test success of the Decrypt() operation in scenario when the salt challenge
// response comes before the unsealing challenge response.
TEST_F(ChallengeCredentialsHelperImplBasicTest,
       DecryptSuccessSaltThenUnsealing) {
  ExpectSaltChallenge(kAlgorithm /* salt_challenge_algorithm */);
  ExpectUnsealingChallenge(kAlgorithm /* unsealing_algorithm */);
  MakeUnsealingMocker({kAlgorithm} /* key_algorithms */,
                      kAlgorithm /* unsealing_algorithm */)
      ->SetUpSuccessfulMock();

  std::unique_ptr<ChallengeCredentialsDecryptResult> decrypt_result;
  CallDecrypt({kAlgorithm} /* key_algorithms */,
              kAlgorithm /* salt_challenge_algorithm */, kSalt,
              &decrypt_result);
  EXPECT_TRUE(is_salt_challenge_requested());
  EXPECT_TRUE(is_unsealing_challenge_requested());

  SimulateSaltChallengeResponse();
  EXPECT_FALSE(decrypt_result);

  SimulateUnsealingChallengeResponse();
  ASSERT_TRUE(decrypt_result);
  VerifySuccessfulDecryptResult(*decrypt_result);
}

// Test success of the Decrypt() operation in scenario when the unsealing
// challenge response comes before the salt challenge response.
TEST_F(ChallengeCredentialsHelperImplBasicTest,
       DecryptSuccessUnsealingThenSalt) {
  ExpectSaltChallenge(kAlgorithm /* salt_challenge_algorithm */);
  ExpectUnsealingChallenge(kAlgorithm /* unsealing_algorithm */);
  MakeUnsealingMocker({kAlgorithm} /* key_algorithms */,
                      kAlgorithm /* unsealing_algorithm */)
      ->SetUpSuccessfulMock();

  std::unique_ptr<ChallengeCredentialsDecryptResult> decrypt_result;
  CallDecrypt({kAlgorithm} /* key_algorithms */,
              kAlgorithm /* salt_challenge_algorithm */, kSalt,
              &decrypt_result);
  EXPECT_TRUE(is_salt_challenge_requested());
  EXPECT_TRUE(is_unsealing_challenge_requested());

  SimulateUnsealingChallengeResponse();
  EXPECT_FALSE(decrypt_result);

  SimulateSaltChallengeResponse();
  ASSERT_TRUE(decrypt_result);
  VerifySuccessfulDecryptResult(*decrypt_result);
}

// Test failure of the Decrypt() operation due to failure of unsealing session
// creation.
TEST_F(ChallengeCredentialsHelperImplBasicTest,
       DecryptFailureInUnsealingSessionCreation) {
  for (int attempt_number = 0;
       attempt_number < ChallengeCredentialsHelperImpl::kRetryAttemptCount;
       ++attempt_number) {
    ExpectSaltChallenge(kAlgorithm /* salt_challenge_algorithm */);
  }
  MakeUnsealingMocker({kAlgorithm} /* key_algorithms */,
                      kAlgorithm /* unsealing_algorithm */)
      ->SetUpCreationFailingMock(true /* mock_repeatedly */);

  std::unique_ptr<ChallengeCredentialsDecryptResult> decrypt_result;
  CallDecrypt({kAlgorithm} /* key_algorithms */,
              kAlgorithm /* salt_challenge_algorithm */, kSalt,
              &decrypt_result);
  EXPECT_TRUE(is_salt_challenge_requested());
  ASSERT_TRUE(decrypt_result);
  VerifyFailedChallengeCredentialsDecryptResult(*decrypt_result);

  // Responding to the salt challenge shouldn't have any effect.
  SimulateSaltChallengeResponse();
}

// Test failure of the Decrypt() operation due to failure of unsealing.
TEST_F(ChallengeCredentialsHelperImplBasicTest, DecryptFailureInUnsealing) {
  for (int attempt_number = 0;
       attempt_number < ChallengeCredentialsHelperImpl::kRetryAttemptCount;
       ++attempt_number) {
    ExpectSaltChallenge(kAlgorithm /* salt_challenge_algorithm */);
    ExpectUnsealingChallenge(kAlgorithm /* unsealing_algorithm */);
    MakeUnsealingMocker({kAlgorithm} /* key_algorithms */,
                        kAlgorithm /* unsealing_algorithm */)
        ->SetUpUsealingFailingMock();
  }

  std::unique_ptr<ChallengeCredentialsDecryptResult> decrypt_result;
  CallDecrypt({kAlgorithm} /* key_algorithms */,
              kAlgorithm /* salt_challenge_algorithm */, kSalt,
              &decrypt_result);
  EXPECT_TRUE(is_salt_challenge_requested());

  for (int attempt_number = 0;
       attempt_number < ChallengeCredentialsHelperImpl::kRetryAttemptCount;
       ++attempt_number) {
    EXPECT_TRUE(is_unsealing_challenge_requested());
    EXPECT_FALSE(decrypt_result);
    SimulateUnsealingChallengeResponse();
  }
  ASSERT_TRUE(decrypt_result);
  VerifyFailedChallengeCredentialsDecryptResult(*decrypt_result);

  // Responding to the salt challenge shouldn't have any effect.
  SimulateSaltChallengeResponse();
}

// Test failure of the Decrypt() operation due to failure of salt challenge
// request.
TEST_F(ChallengeCredentialsHelperImplBasicTest, DecryptFailureInSaltChallenge) {
  ExpectSaltChallenge(kAlgorithm /* salt_challenge_algorithm */);
  ExpectUnsealingChallenge(kAlgorithm /* unsealing_algorithm */);
  MakeUnsealingMocker({kAlgorithm} /* key_algorithms */,
                      kAlgorithm /* unsealing_algorithm */)
      ->SetUpUnsealingNotCalledMock();

  std::unique_ptr<ChallengeCredentialsDecryptResult> decrypt_result;
  CallDecrypt({kAlgorithm} /* key_algorithms */,
              kAlgorithm /* salt_challenge_algorithm */, kSalt,
              &decrypt_result);
  EXPECT_TRUE(is_salt_challenge_requested());
  EXPECT_TRUE(is_unsealing_challenge_requested());
  EXPECT_FALSE(decrypt_result);

  SimulateSaltChallengeFailure();
  ASSERT_TRUE(decrypt_result);
  VerifyFailedChallengeCredentialsDecryptResult(*decrypt_result);

  // Responding to the unsealing challenge shouldn't have any effect.
  SimulateUnsealingChallengeResponse();
}

// Test failure of the Decrypt() operation due to failure of unsealing challenge
// request.
TEST_F(ChallengeCredentialsHelperImplBasicTest,
       DecryptFailureInUnsealingChallenge) {
  ExpectSaltChallenge(kAlgorithm /* salt_challenge_algorithm */);
  ExpectUnsealingChallenge(kAlgorithm /* unsealing_algorithm */);
  MakeUnsealingMocker({kAlgorithm} /* key_algorithms */,
                      kAlgorithm /* unsealing_algorithm */)
      ->SetUpUnsealingNotCalledMock();

  std::unique_ptr<ChallengeCredentialsDecryptResult> decrypt_result;
  CallDecrypt({kAlgorithm} /* key_algorithms */,
              kAlgorithm /* salt_challenge_algorithm */, kSalt,
              &decrypt_result);
  EXPECT_TRUE(is_salt_challenge_requested());
  EXPECT_TRUE(is_unsealing_challenge_requested());
  EXPECT_FALSE(decrypt_result);

  SimulateUnsealingChallengeFailure();
  ASSERT_TRUE(decrypt_result);
  VerifyFailedChallengeCredentialsDecryptResult(*decrypt_result);

  // Responding to the salt challenge shouldn't have any effect.
  SimulateSaltChallengeResponse();
}

// Test failure of the Decrypt() operation due to its abortion before any of the
// challenges is completed.
TEST_F(ChallengeCredentialsHelperImplBasicTest,
       DecryptAbortionBeforeChallenges) {
  ExpectSaltChallenge(kAlgorithm /* salt_challenge_algorithm */);
  ExpectUnsealingChallenge(kAlgorithm /* unsealing_algorithm */);
  MakeUnsealingMocker({kAlgorithm} /* key_algorithms */,
                      kAlgorithm /* unsealing_algorithm */)
      ->SetUpUnsealingNotCalledMock();

  std::unique_ptr<ChallengeCredentialsDecryptResult> decrypt_result;
  CallDecrypt({kAlgorithm} /* key_algorithms */,
              kAlgorithm /* salt_challenge_algorithm */, kSalt,
              &decrypt_result);
  EXPECT_TRUE(is_salt_challenge_requested());
  EXPECT_TRUE(is_unsealing_challenge_requested());
  EXPECT_FALSE(decrypt_result);

  // Abort the first operation by starting a new one.
  StartSurplusOperation();
  ASSERT_TRUE(decrypt_result);
  VerifyFailedChallengeCredentialsDecryptResult(*decrypt_result);
}

// Test failure of the Decrypt() operation due to its abortion after the salt
// challenge completes.
TEST_F(ChallengeCredentialsHelperImplBasicTest,
       DecryptAbortionAfterSaltChallenge) {
  ExpectSaltChallenge(kAlgorithm /* salt_challenge_algorithm */);
  ExpectUnsealingChallenge(kAlgorithm /* unsealing_algorithm */);
  MakeUnsealingMocker({kAlgorithm} /* key_algorithms */,
                      kAlgorithm /* unsealing_algorithm */)
      ->SetUpUnsealingNotCalledMock();

  std::unique_ptr<ChallengeCredentialsDecryptResult> decrypt_result;
  CallDecrypt({kAlgorithm} /* key_algorithms */,
              kAlgorithm /* salt_challenge_algorithm */, kSalt,
              &decrypt_result);
  EXPECT_TRUE(is_salt_challenge_requested());
  EXPECT_TRUE(is_unsealing_challenge_requested());

  SimulateSaltChallengeResponse();
  EXPECT_FALSE(decrypt_result);

  // Abort the first operation by starting a new one.
  StartSurplusOperation();
  ASSERT_TRUE(decrypt_result);
  VerifyFailedChallengeCredentialsDecryptResult(*decrypt_result);
}

// Test failure of the Decrypt() operation due to its abortion after the
// unsealing completes.
TEST_F(ChallengeCredentialsHelperImplBasicTest, DecryptAbortionAfterUnsealing) {
  ExpectSaltChallenge(kAlgorithm /* salt_challenge_algorithm */);
  ExpectUnsealingChallenge(kAlgorithm /* unsealing_algorithm */);
  MakeUnsealingMocker({kAlgorithm} /* key_algorithms */,
                      kAlgorithm /* unsealing_algorithm */)
      ->SetUpSuccessfulMock();

  std::unique_ptr<ChallengeCredentialsDecryptResult> decrypt_result;
  CallDecrypt({kAlgorithm} /* key_algorithms */,
              kAlgorithm /* salt_challenge_algorithm */, kSalt,
              &decrypt_result);
  EXPECT_TRUE(is_salt_challenge_requested());
  EXPECT_TRUE(is_unsealing_challenge_requested());

  SimulateUnsealingChallengeResponse();
  EXPECT_FALSE(decrypt_result);

  // Abort the first operation by starting a new one.
  StartSurplusOperation();
  ASSERT_TRUE(decrypt_result);
  VerifyFailedChallengeCredentialsDecryptResult(*decrypt_result);
}

namespace {

// Tests with simulation of SignatureSealingBackend absence.
class ChallengeCredentialsHelperImplNoBackendTest
    : public ChallengeCredentialsHelperImplSingleAlgorithmTestBase {
 protected:
  ChallengeCredentialsHelperImplNoBackendTest() {
    PrepareSignatureSealingBackend(false /* enabled */);
  }
};

}  // namespace

// Test failure of the Decrypt() operation due to the absence of the sealing
// backend.
TEST_F(ChallengeCredentialsHelperImplNoBackendTest, DecryptFailure) {
  std::unique_ptr<ChallengeCredentialsDecryptResult> decrypt_result;
  CallDecrypt({kAlgorithm} /* key_algorithms */,
              kAlgorithm /* salt_challenge_algorithm */, kSalt,
              &decrypt_result);
  ASSERT_TRUE(decrypt_result);
  VerifyFailedChallengeCredentialsDecryptResult(*decrypt_result);
}

namespace {

// Test parameters for ChallengeCredentialsHelperImplAlgorithmsTest.
struct AlgorithmsTestParam {
  std::vector<ChallengeSignatureAlgorithm> key_algorithms;
  ChallengeSignatureAlgorithm salt_challenge_algorithm;
  ChallengeSignatureAlgorithm unsealing_algorithm;
};

// Tests various combinations of multiple algorithms.
class ChallengeCredentialsHelperImplAlgorithmsTest
    : public ChallengeCredentialsHelperImplTestBase,
      public testing::WithParamInterface<AlgorithmsTestParam> {
 protected:
  ChallengeCredentialsHelperImplAlgorithmsTest() {
    PrepareSignatureSealingBackend(true /* enabled */);
  }
};

}  // namespace

// Test success of the Decrypt() operation with the specified combination of
// algorithms.
TEST_P(ChallengeCredentialsHelperImplAlgorithmsTest, DecryptSuccess) {
  ExpectSaltChallenge(GetParam().salt_challenge_algorithm);
  ExpectUnsealingChallenge(GetParam().unsealing_algorithm);
  MakeUnsealingMocker(GetParam().key_algorithms, GetParam().unsealing_algorithm)
      ->SetUpSuccessfulMock();

  std::unique_ptr<ChallengeCredentialsDecryptResult> decrypt_result;
  CallDecrypt(GetParam().key_algorithms, GetParam().salt_challenge_algorithm,
              kSalt, &decrypt_result);
  EXPECT_TRUE(is_salt_challenge_requested());
  EXPECT_TRUE(is_unsealing_challenge_requested());

  SimulateSaltChallengeResponse();
  EXPECT_FALSE(decrypt_result);

  SimulateUnsealingChallengeResponse();
  ASSERT_TRUE(decrypt_result);
  VerifySuccessfulDecryptResult(*decrypt_result);
}

// Test that SHA-1 algorithms are the least preferred and chosen only if there's
// no other option.
INSTANTIATE_TEST_SUITE_P(
    LowPriorityOfSha1,
    ChallengeCredentialsHelperImplAlgorithmsTest,
    Values(
        AlgorithmsTestParam{
            {CHALLENGE_RSASSA_PKCS1_V1_5_SHA1,
             CHALLENGE_RSASSA_PKCS1_V1_5_SHA256} /* key_algorithms */,
            CHALLENGE_RSASSA_PKCS1_V1_5_SHA256 /* salt_challenge_algorithm */,
            CHALLENGE_RSASSA_PKCS1_V1_5_SHA256 /* unsealing_algorithm */},
        AlgorithmsTestParam{
            {CHALLENGE_RSASSA_PKCS1_V1_5_SHA1} /* key_algorithms */,
            CHALLENGE_RSASSA_PKCS1_V1_5_SHA1 /* salt_challenge_algorithm */,
            CHALLENGE_RSASSA_PKCS1_V1_5_SHA1 /* unsealing_algorithm */}));

// Test prioritization of algorithms according to their order in the input.
INSTANTIATE_TEST_SUITE_P(
    InputPrioritization,
    ChallengeCredentialsHelperImplAlgorithmsTest,
    Values(
        AlgorithmsTestParam{
            {CHALLENGE_RSASSA_PKCS1_V1_5_SHA256,
             CHALLENGE_RSASSA_PKCS1_V1_5_SHA512} /* key_algorithms */,
            CHALLENGE_RSASSA_PKCS1_V1_5_SHA256 /* salt_challenge_algorithm */,
            CHALLENGE_RSASSA_PKCS1_V1_5_SHA256 /* unsealing_algorithm */},
        AlgorithmsTestParam{
            {CHALLENGE_RSASSA_PKCS1_V1_5_SHA512,
             CHALLENGE_RSASSA_PKCS1_V1_5_SHA256} /* key_algorithms */,
            CHALLENGE_RSASSA_PKCS1_V1_5_SHA512 /* salt_challenge_algorithm */,
            CHALLENGE_RSASSA_PKCS1_V1_5_SHA512 /* unsealing_algorithm */}));

}  // namespace cryptohome
