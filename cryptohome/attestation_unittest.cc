// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/attestation.h"

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/data_encoding.h>
#include <brillo/http/http_transport_fake.h>
#include <brillo/mime_utils.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <gmock/gmock.h>
#include <google/protobuf/util/message_differencer.h>
#include <gtest/gtest.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include "cryptohome/crypto.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/mock_install_attributes.h"
#include "cryptohome/mock_keystore.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/mock_tpm_init.h"

#include "attestation.pb.h"  // NOLINT(build/include)

using base::FilePath;
using brillo::Blob;
using brillo::SecureBlob;
using google::protobuf::util::MessageDifferencer;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StartsWith;
using ::testing::WithArgs;

namespace cryptohome {

static const char* kTestPath = "/tmp/attestation_test.epb";
static const char* kTestUser = "test_user";

static const char kABEData[] =
    "2eac34fa74994262b907c15a3a1462e349e5108ca0d0e807f4b1a3ee741a5594";
static const char kDEN[] =
    "865cc962ffe14b3638b2d1f860e77b531644a3aba67e52e49e1f6c0a31d81daf";
static const char kEID[] =
    "809c6c9f425c59b86e551f4e8fdccd5a2200b08fe3250c4971f40d8bbcf7820a";

class AttestationBaseTest : public testing::Test {
 public:
  AttestationBaseTest() : crypto_(&platform_), attestation_() {
    crypto_.set_tpm(&tpm_);
    crypto_.set_use_tpm(true);
  }
  virtual ~AttestationBaseTest() {}

  void SetUpAttestation(Attestation* attestation) {
    attestation->set_database_path(kTestPath);
    attestation->set_key_store(&key_store_);
  }

  void SetUp() override {
    SetUpAttestation(&attestation_);
    http_transport_ = std::make_shared<brillo::http::fake::Transport>();
    attestation_.set_http_transport(http_transport_);
    // Fake up a single file by default.
    ON_CALL(platform_,
            WriteStringToFileAtomicDurable(
                Property(&FilePath::value, StartsWith(kTestPath)), _, _))
        .WillByDefault(
            WithArgs<1>(Invoke(this, &AttestationBaseTest::WriteDB)));
    ON_CALL(
        platform_,
        ReadFileToString(Property(&FilePath::value, StartsWith(kTestPath)), _))
        .WillByDefault(WithArgs<1>(Invoke(this, &AttestationBaseTest::ReadDB)));
    // Configure a TPM that is ready.
    ON_CALL(tpm_, IsEnabled()).WillByDefault(Return(true));
    ON_CALL(tpm_, IsOwned()).WillByDefault(Return(true));
    ON_CALL(tpm_, IsBeingOwned()).WillByDefault(Return(false));
    Initialize();
  }

  virtual void Initialize() {
    attestation_.Initialize(&tpm_, &tpm_init_, &platform_, &crypto_,
                            &install_attributes_,
                            brillo::SecureBlob() /* abe_data */,
                            false /* retain_endorsement_data */);
  }

  virtual bool WriteDB(const std::string& db) {
    serialized_db_.assign(db);
    return true;
  }

  virtual bool ReadDB(std::string* db) {
    if (serialized_db_.empty()) {
      return false;
    }
    db->assign(serialized_db_);
    return true;
  }

  virtual bool ComputeEnterpriseEnrollmentId(brillo::SecureBlob* blob) {
    return attestation_.ComputeEnterpriseEnrollmentId(blob);
  }

  virtual std::string GetValidEndorsementKey() {
    const std::string hex_ek =
        "3082010A0282010100D3EE9D14FAC4C42B35FEDC87363CC29807A3F39D3E45D2"
        "49586F620C6425CE981E8619DCE50D964E934A1F1FD2C1066418DD75D8916D85"
        "DD9E82C27C82A8C2C9BC76BA914B5A43F7535AEAA2F7BD985F46A46C92334643"
        "C89F5598ABD191AA5439088778774DB3B07FD08F019997893BEC1A87571AC95F"
        "66ADE2F3631A2C9CF8EF0B94D2CA62E81F1FF9CC71339838E229E63CA59E0BB6"
        "4D2134C3AF705BCF0F614E58DF848897454FFA2FA42073F80174C1D3D0C54D5B"
        "DC45747FE662D6D321AEA5375F0AE489DF6ABB018D5D11707E546E8487641290"
        "F9F9B3CC3A1F8631FB0F3486A875F6005D3539A5823F7618B007779FB31CFB7F"
        "E36A1C2D9DEFD8F5030203010001";
    std::vector<uint8_t> ek_bytes;
    base::HexStringToBytes(hex_ek, &ek_bytes);
    return std::string(ek_bytes.begin(), ek_bytes.end());
  }

 protected:
  std::string serialized_db_;
  // TODO(crbug.com/891630): Use a StrictMock<MockTpm> and fix tests.
  NiceMock<MockTpm> tpm_;
  NiceMock<MockTpmInit> tpm_init_;
  NiceMock<MockPlatform> platform_;
  Crypto crypto_;
  NiceMock<MockKeyStore> key_store_;
  NiceMock<MockInstallAttributes> install_attributes_;
  std::shared_ptr<brillo::http::fake::Transport> http_transport_;
  Attestation attestation_;
  crypto::ScopedRSA rsa_;  // Access with rsa().
  bool is_enterprise_setup_ = false;

  RSA* rsa() {
    if (!rsa_) {
      crypto::ScopedBIGNUM e(BN_new());
      CHECK(e);
      EXPECT_TRUE(BN_set_word(e.get(), 65537));
      rsa_.reset(RSA_new());
      CHECK(rsa_);
      EXPECT_TRUE(RSA_generate_key_ex(rsa_.get(), 2048, e.get(), nullptr));
    }
    return rsa_.get();
  }

  virtual void SetUpEnterprise() {
    if (!is_enterprise_setup_) {
      crypto::ScopedBIGNUM e(BN_new());
      CHECK(e);
      EXPECT_TRUE(BN_set_word(e.get(), 65537));
      crypto::ScopedRSA rsa_def_sign(RSA_new());
      crypto::ScopedRSA rsa_def_enc(RSA_new());
      crypto::ScopedRSA rsa_test_sign(RSA_new());
      crypto::ScopedRSA rsa_test_enc(RSA_new());
      CHECK(rsa_def_sign);
      CHECK(rsa_def_enc);
      CHECK(rsa_test_sign);
      CHECK(rsa_test_enc);
      EXPECT_TRUE(
          RSA_generate_key_ex(rsa_def_sign.get(), 2048, e.get(), nullptr));
      EXPECT_TRUE(
          RSA_generate_key_ex(rsa_def_enc.get(), 2048, e.get(), nullptr));
      EXPECT_TRUE(
          RSA_generate_key_ex(rsa_test_sign.get(), 2048, e.get(), nullptr));
      EXPECT_TRUE(
          RSA_generate_key_ex(rsa_test_enc.get(), 2048, e.get(), nullptr));
      attestation_.set_enterprise_test_keys(Attestation::kDefaultVA,
          rsa_def_sign.release(), rsa_def_enc.release());
      attestation_.set_enterprise_test_keys(Attestation::kTestVA,
          rsa_test_sign.release(), rsa_test_enc.release());
      is_enterprise_setup_ = true;
    }
  }

  RSA* enterprise_signing_rsa(Attestation::VAType va_type) {
    SetUpEnterprise();
    return attestation_.GetEnterpriseSigningKey(va_type);
  }

  RSA* enterprise_encryption_rsa(Attestation::VAType va_type) {
    SetUpEnterprise();
    return attestation_.GetEnterpriseEncryptionKey(va_type);
  }

  SecureBlob GetEnrollBlob() {
    AttestationEnrollmentResponse pb;
    pb.set_status(OK);
    pb.set_detail("");
    pb.mutable_encrypted_identity_credential()->set_asym_ca_contents("1234");
    pb.mutable_encrypted_identity_credential()->set_sym_ca_attestation("5678");
    std::string tmp;
    pb.SerializeToString(&tmp);
    return SecureBlob(tmp.begin(), tmp.end());
  }

  SecureBlob GetCertRequestBlob(const SecureBlob& request) {
    AttestationCertificateRequest request_pb;
    CHECK(request_pb.ParseFromArray(request.data(), request.size()));
    AttestationCertificateResponse pb;
    pb.set_message_id(request_pb.message_id());
    pb.set_status(OK);
    pb.set_detail("");
    pb.set_certified_key_credential("response_cert");
    pb.set_intermediate_ca_cert("response_ca_cert");
    std::string tmp;
    pb.SerializeToString(&tmp);
    return SecureBlob(tmp.begin(), tmp.end());
  }

  SecureBlob GetCertifiedKeyBlob(const std::string& payload,
                                 bool include_ca_cert) {
    CertifiedKey pb;
    pb.set_certified_key_credential("stored_cert");
    if (include_ca_cert)
      pb.set_intermediate_ca_cert("stored_ca_cert");
    pb.set_public_key(GetPKCS1PublicKey().to_string());
    pb.set_payload(payload);
    std::string tmp;
    pb.SerializeToString(&tmp);
    return SecureBlob(tmp.begin(), tmp.end());
  }

  bool CompareBlob(const SecureBlob& blob, const std::string& str) {
    return (blob.to_string() == str);
  }

  std::string EncodeCertChain(const std::string& cert1,
                              const std::string& cert2) {
    std::string chain = "-----BEGIN CERTIFICATE-----\n";
    chain += brillo::data_encoding::Base64EncodeWrapLines(cert1);
    chain += "-----END CERTIFICATE-----";
    if (!cert2.empty()) {
      chain += "\n-----BEGIN CERTIFICATE-----\n";
      chain += brillo::data_encoding::Base64EncodeWrapLines(cert2);
      chain += "-----END CERTIFICATE-----";
    }
    return chain;
  }

  SecureBlob GetPKCS1PublicKey() {
    unsigned char* buffer = nullptr;
    int length = i2d_RSAPublicKey(rsa(), &buffer);
    if (length <= 0)
      return SecureBlob();
    SecureBlob tmp(buffer, buffer + length);
    OPENSSL_free(buffer);
    return tmp;
  }

  SecureBlob GetX509PublicKey() {
    unsigned char* buffer = nullptr;
    int length = i2d_RSA_PUBKEY(rsa(), &buffer);
    if (length <= 0)
      return SecureBlob();
    SecureBlob tmp(buffer, buffer + length);
    OPENSSL_free(buffer);
    return tmp;
  }

  bool VerifySimpleChallenge(const SecureBlob& response,
                             const std::string& challenge,
                             const std::string& signature) {
    SignedData signed_data;
    if (!signed_data.ParseFromArray(response.data(), response.size()))
      return false;
    if (signed_data.data().find(challenge) != 0 ||
        signed_data.data() == challenge)
      return false;
    if (signed_data.signature() != signature)
      return false;
    return true;
  }

  bool VerifyEnterpriseVaChallenge(Attestation::VAType va_type,
                                   const SecureBlob& response,
                                   KeyType key_type,
                                   const std::string& domain,
                                   const std::string& device_id,
                                   const std::string& cert_chain,
                                   const std::string& signature) {
    SignedData signed_data;
    if (!signed_data.ParseFromArray(response.data(), response.size()))
      return false;
    ChallengeResponse response_pb;
    if (!response_pb.ParseFromString(signed_data.data()))
      return false;
    std::string expected_challenge = GetEnterpriseVaChallenge(
        va_type, "EnterpriseKeyChallenge", false).to_string();
    if (response_pb.challenge().data() != expected_challenge)
      return false;
    std::string key_info;
    if (!DecryptEnterpriseData(va_type, response_pb.encrypted_key_info(),
                               &key_info))
      return false;
    KeyInfo key_info_pb;
    if (!key_info_pb.ParseFromString(key_info))
      return false;
    if (key_info_pb.domain() != domain ||
        key_info_pb.device_id() != device_id ||
        key_info_pb.key_type() != key_type ||
        key_info_pb.certificate() != cert_chain)
      return false;
    if (signed_data.signature() != signature)
      return false;
    return true;
  }

  SecureBlob GetEnterpriseVaChallenge(Attestation::VAType va_type,
                                    const std::string& prefix,
                                    bool sign) {
    Challenge challenge;
    challenge.set_prefix(prefix);
    challenge.set_nonce("nonce");
    challenge.set_timestamp(123456789);
    std::string serialized;
    challenge.SerializeToString(&serialized);
    if (sign) {
      unsigned char buffer[256];
      unsigned int length = 0;
      SecureBlob digest = CryptoLib::Sha256(SecureBlob(serialized));
      RSA_sign(NID_sha256,
               digest.data(), digest.size(),
               buffer, &length,
               enterprise_signing_rsa(va_type));
      SignedData signed_challenge;
      signed_challenge.set_data(serialized);
      signed_challenge.set_signature(buffer, length);
      signed_challenge.SerializeToString(&serialized);
    }
    return SecureBlob(serialized);
  }

  bool DecryptEnterpriseData(Attestation::VAType va_type,
                             const EncryptedData& input,
                             std::string* output) {
    // Unwrap the AES key.
    unsigned char* wrapped_key_buffer = reinterpret_cast<unsigned char*>(
        const_cast<char*>(input.wrapped_key().data()));
    unsigned char buffer[256];
    int length = RSA_private_decrypt(input.wrapped_key().size(),
                                     wrapped_key_buffer,
                                     buffer,
                                     enterprise_encryption_rsa(va_type),
                                     RSA_PKCS1_OAEP_PADDING);
    if (length != 32)
      return false;
    SecureBlob aes_key(buffer, buffer + length);
    // Decrypt the blob.
    SecureBlob decrypted;
    brillo::SecureBlob encrypted(input.encrypted_data().begin(),
                                 input.encrypted_data().end());
    SecureBlob aes_iv = SecureBlob(input.iv());
    if (!CryptoLib::AesDecryptSpecifyBlockMode(encrypted, 0, encrypted.size(),
                                               aes_key, aes_iv,
                                               CryptoLib::kPaddingStandard,
                                               CryptoLib::kCbc,
                                               &decrypted))
      return false;
    *output = decrypted.to_string();
    return true;
  }

  // Returns a copy of the database as it exists on 'disk'.
  AttestationDatabase GetPersistentDatabase() {
    AttestationDatabase db;
    attestation_.DecryptDatabase(serialized_db_, &db);
    return db;
  }

  // Returns a mutable reference to the current database instance.  If a test is
  // verifying database changes it should use GetPersistentDatabase() so it will
  // also verify that the changes are written to disk correctly.
  AttestationDatabase& GetMutableDatabase() {
    return attestation_.database_pb_;
  }

  // Verify Privacy CA-related data, including the default CA's identity
  // credential.
  void VerifyPCAData(const AttestationDatabase& db,
                     const char* default_identity_credential) {
    ASSERT_EQ(default_identity_credential ? 1 : 0,
              db.identity_certificates().size());
    for (int pca = 0; pca < db.identity_certificates().size(); ++pca) {
      const AttestationDatabase::IdentityCertificate identity_certificate =
          db.identity_certificates().at(pca);
      ASSERT_EQ(0, identity_certificate.identity());
      ASSERT_EQ(pca, identity_certificate.aca());
      if (default_identity_credential && pca == Attestation::kDefaultPCA) {
        ASSERT_EQ(default_identity_credential,
                  identity_certificate.identity_credential());
      } else {
        ASSERT_FALSE(identity_certificate.has_identity_credential());
      }
    }
    // All PCAs have encrypted credentials.
    for (int pca = Attestation::kDefaultPCA; pca < Attestation::kMaxPCAType;
         ++pca) {
      ASSERT_TRUE(
          db.credentials().encrypted_endorsement_credentials().count(pca));
    }
  }

  // Verify Privacy CA-related data, including the lack of default CA's identity
  // credential.
  void VerifyPCAData(const AttestationDatabase& db) {
    VerifyPCAData(db, nullptr);
  }

  // Gets the Google Privacy CA web-origin -- this can change depending on
  // whether the test server is being targeted.
  std::string GetPCAWebOrigin(Attestation::PCAType pca_type) const {
    switch (pca_type) {
      default:
      case Attestation::kDefaultPCA:
        return attestation_.kDefaultPCAWebOrigin;
      case Attestation::kTestPCA:
        return attestation_.kTestPCAWebOrigin;
    }
  }

  size_t GetDigestSize() const { return Attestation::kDigestSize; }
};

TEST_F(AttestationBaseTest, NotPreparedForEnrollment) {
  ASSERT_FALSE(attestation_.IsPreparedForEnrollment());
}

TEST_F(AttestationBaseTest, PrepareForEnrollment) {
  attestation_.PrepareForEnrollment();
  EXPECT_TRUE(attestation_.IsPreparedForEnrollment());
  AttestationDatabase db = GetPersistentDatabase();
  // One identity has been created.
  EXPECT_EQ(1, db.identities().size());
  const AttestationDatabase::Identity& identity_data = db.identities().Get(0);
  EXPECT_TRUE(identity_data.has_identity_binding());
  EXPECT_TRUE(identity_data.has_identity_key());
  EXPECT_EQ(1, identity_data.pcr_quotes().count(0));
  EXPECT_EQ(1, identity_data.pcr_quotes().count(1));
  EXPECT_EQ(IDENTITY_FEATURE_ENTERPRISE_ENROLLMENT_ID,
            identity_data.features());
  // Deprecated identity-related values have not been set.
  EXPECT_FALSE(db.has_identity_binding());
  EXPECT_FALSE(db.has_identity_key());
  EXPECT_FALSE(db.has_pcr0_quote());
  EXPECT_FALSE(db.has_pcr1_quote());
  // We have a delegate to activate the AIK.
  EXPECT_TRUE(db.has_delegate());
  // Verify Privacy CA-related data.
  VerifyPCAData(db);
  // These deprecated fields have not been set either.
  EXPECT_TRUE(db.has_credentials());
  EXPECT_FALSE(db.credentials().has_default_encrypted_endorsement_credential());
}

TEST_F(AttestationBaseTest, PrepareForEnrollmentNoIdentityFeatures) {
  attestation_.set_default_identity_features_for_test(NO_IDENTITY_FEATURES);
  attestation_.PrepareForEnrollment();
  EXPECT_TRUE(attestation_.IsPreparedForEnrollment());
  AttestationDatabase db = GetPersistentDatabase();
  // One identity has been created.
  EXPECT_EQ(1, db.identities().size());
  const AttestationDatabase::Identity& identity_data =
      db.identities().Get(0);
  EXPECT_TRUE(identity_data.has_identity_binding());
  EXPECT_TRUE(identity_data.has_identity_key());
  EXPECT_EQ(1, identity_data.pcr_quotes().count(0));
  EXPECT_EQ(1, identity_data.pcr_quotes().count(1));
  EXPECT_EQ(NO_IDENTITY_FEATURES, identity_data.features());
  // Deprecated identity-related values have not been set.
  EXPECT_FALSE(db.has_identity_binding());
  EXPECT_FALSE(db.has_identity_key());
  EXPECT_FALSE(db.has_pcr0_quote());
  EXPECT_FALSE(db.has_pcr1_quote());
  // We have a delegate to activate the AIK.
  EXPECT_TRUE(db.has_delegate());
  // Verify Privacy CA-related data.
  VerifyPCAData(db);
  // These deprecated fields have not been set either.
  EXPECT_TRUE(db.has_credentials());
  EXPECT_FALSE(db.credentials().has_default_encrypted_endorsement_credential());
}

TEST_F(AttestationBaseTest, IdentityCertificateMapIsDeepCopied) {
  SecureBlob blob;
  EXPECT_FALSE(
      attestation_.CreateEnrollRequest(Attestation::kDefaultPCA, &blob));
  attestation_.PrepareForEnrollment();
  EXPECT_FALSE(attestation_.HasIdentityCertificate(Attestation::kFirstIdentity,
                                                   Attestation::kDefaultPCA));
  EXPECT_TRUE(
      attestation_.CreateEnrollRequest(Attestation::kDefaultPCA, &blob));
  EXPECT_TRUE(attestation_.Enroll(Attestation::kDefaultPCA, GetEnrollBlob()));
  EXPECT_TRUE(attestation_.HasIdentityCertificate(Attestation::kFirstIdentity,
                                                  Attestation::kDefaultPCA));
  // Check that the identity certificate map is not the same as what the
  // database contains.
  Attestation::IdentityCertificateMap map =
      attestation_.GetIdentityCertificateMap();
  AttestationDatabase db = GetPersistentDatabase();
  db.mutable_identity_certificates()->at(Attestation::kDefaultPCA).set_aca(
      Attestation::kMaxPCAType);
  EXPECT_EQ(Attestation::kDefaultPCA, map.at(Attestation::kDefaultPCA).aca());
}

TEST_F(AttestationBaseTest, PrepareForEnrollmentBadInitialPCR0State) {
  EXPECT_CALL(tpm_, IsCurrentPCR0ValueValid()).WillOnce(Return(false));

  attestation_.PrepareForEnrollment();

  AttestationDatabase db = GetPersistentDatabase();
  EXPECT_EQ(db.SerializeAsString(), "");
}

TEST_F(AttestationBaseTest, CreateIdentityQuotePCR0Error) {
  EXPECT_CALL(tpm_, QuotePCR(0, _, _, _, _, _, _)).WillOnce(Return(false));

  attestation_.PrepareForEnrollment();

  AttestationDatabase db = GetPersistentDatabase();
  EXPECT_EQ(db.SerializeAsString(), "");
}

TEST_F(AttestationBaseTest, PrepareForEnrollmentGenerateDatabaseKeyError) {
  EXPECT_CALL(tpm_, IsCurrentPCR0ValueValid())
      .WillOnce(Return(true))
      .WillOnce(Return(false));

  attestation_.PrepareForEnrollment();

  AttestationDatabase db = GetPersistentDatabase();
  EXPECT_EQ(db.SerializeAsString(), "");
}

// Tests DeleteKeysByPrefix with device-wide keys stored in the attestation db.
TEST_F(AttestationBaseTest, DeleteByPrefixDevice) {
  // Test with an empty db.
  ASSERT_TRUE(attestation_.DeleteKeysByPrefix(false, "", "prefix"));

  // Test with a single matching key.
  AttestationDatabase& db = GetMutableDatabase();
  db.add_device_keys()->set_key_name("prefix1");
  ASSERT_TRUE(attestation_.DeleteKeysByPrefix(false, "", "prefix"));
  EXPECT_EQ(0, db.device_keys_size());

  // Test with a single non-matching key.
  db.add_device_keys()->set_key_name("other");
  ASSERT_TRUE(attestation_.DeleteKeysByPrefix(false, "", "prefix"));
  EXPECT_EQ(1, db.device_keys_size());

  // Test with an empty prefix.
  ASSERT_TRUE(attestation_.DeleteKeysByPrefix(false, "", ""));
  EXPECT_EQ(0, db.device_keys_size());

  // Test with multiple matching / non-matching keys.
  db.add_device_keys()->set_key_name("prefix1");
  db.add_device_keys()->set_key_name("other1");
  db.add_device_keys()->set_key_name("prefix2");
  db.add_device_keys()->set_key_name("other2");
  db.add_device_keys()->set_key_name("prefix3");
  db.add_device_keys()->set_key_name("other3");
  db.add_device_keys()->set_key_name("prefix4");

  ASSERT_TRUE(attestation_.DeleteKeysByPrefix(false, "", "prefix"));

  EXPECT_EQ(3, db.device_keys_size());
  for (int i = 0; i < db.device_keys_size(); ++i) {
    EXPECT_EQ(0, db.device_keys(i).key_name().find("other"));
  }
}

// Tests DeleteKeysByPrefix with user-owned keys. This object does not manage
// user-owned keys so the test is trivial.
TEST_F(AttestationBaseTest, DeleteByPrefixUser) {
  EXPECT_TRUE(attestation_.DeleteKeysByPrefix(true, kTestUser, "prefix"));
}

TEST_F(AttestationBaseTest, GetEKInfo) {
  std::string info;
  EXPECT_TRUE(attestation_.GetEKInfo(&info));
  EXPECT_TRUE(base::IsStringASCII(info));

  // Simulate owner password not available.
  EXPECT_CALL(tpm_, GetEndorsementCredential(_))
      .WillRepeatedly(Return(false));
  info.clear();
  EXPECT_FALSE(attestation_.GetEKInfo(&info));
  EXPECT_EQ(0, info.size());
}

TEST_F(AttestationBaseTest, FinalizeEndorsementData) {
  // Simulate first login.
  attestation_.PrepareForEnrollment();
  // Expect endorsement data to be available.
  AttestationDatabase db = GetPersistentDatabase();
  EXPECT_TRUE(db.has_credentials() &&
              db.credentials().has_endorsement_public_key() &&
              db.credentials().has_endorsement_credential());

  // Simulate second login.
  attestation_.Initialize(&tpm_, &tpm_init_, &platform_, &crypto_,
      &install_attributes_, brillo::SecureBlob() /* abe_data */,
      false /* retain_endorsement_data */);
  // Expect endorsement data to be no longer available.
  db = GetPersistentDatabase();
  EXPECT_TRUE(db.has_credentials() &&
              !db.credentials().has_endorsement_public_key() &&
              !db.credentials().has_endorsement_credential());
}

TEST_F(AttestationBaseTest, RetainEndorsementData) {
  // Simulate first login.
  attestation_.PrepareForEnrollment();
  // Expect endorsement data to be available.
  AttestationDatabase db = GetPersistentDatabase();
  EXPECT_TRUE(db.has_credentials() &&
              db.credentials().has_endorsement_public_key() &&
              db.credentials().has_endorsement_credential());

  // Simulate second login.
  attestation_.Initialize(&tpm_, &tpm_init_, &platform_, &crypto_,
      &install_attributes_, brillo::SecureBlob() /* abe_data */,
      true /* retain_endorsement_data */);
  // Expect endorsement data to be still available.
  db = GetPersistentDatabase();
  EXPECT_TRUE(db.has_credentials() &&
              db.credentials().has_endorsement_public_key() &&
              db.credentials().has_endorsement_credential());

  attestation_.FinalizeEndorsementData();
  // Expect endorsement data to be still available.
  db = GetPersistentDatabase();
  EXPECT_TRUE(db.has_credentials() &&
              db.credentials().has_endorsement_public_key() &&
              db.credentials().has_endorsement_credential());
}

TEST_F(AttestationBaseTest, MigrateAttestationDatabase) {
  // Simulate first login.
  attestation_.PrepareForEnrollment();

  // Simulate an older database.
  AttestationDatabase db = GetPersistentDatabase();
  db.mutable_credentials()->clear_encrypted_endorsement_credentials();
  db.mutable_credentials()->set_endorsement_credential("endorsement_cred");
  EncryptedData default_encrypted_endorsement_credential;
  default_encrypted_endorsement_credential.set_wrapped_key("default_key");
  db.mutable_credentials()
      ->mutable_default_encrypted_endorsement_credential()
      ->CopyFrom(default_encrypted_endorsement_credential);
  db.clear_identities();
  db.clear_identity_certificates();
  db.mutable_identity_binding()->set_identity_binding("identity_binding");
  db.mutable_identity_binding()->set_identity_public_key("identity_public_key");
  db.mutable_identity_key()->set_identity_credential("identity_cred");
  db.mutable_pcr0_quote()->set_quote("pcr0_quote");
  db.mutable_pcr1_quote()->set_quote("pcr1_quote");
  // Persist that older database.
  attestation_.PersistDatabase(db);

  // Simulate second login.
  Initialize();
  attestation_.PrepareForEnrollment();
  db = GetPersistentDatabase();

  // The default encrypted endorsement credential has been migrated.
  // The deprecated field has not been cleared so that older code can still
  // use the database.
  ASSERT_TRUE(MessageDifferencer::Equals(
      default_encrypted_endorsement_credential,
      db.credentials().encrypted_endorsement_credentials().at(
          Attestation::kDefaultPCA)));
  ASSERT_TRUE(MessageDifferencer::Equals(
      default_encrypted_endorsement_credential,
      db.credentials().default_encrypted_endorsement_credential()));

  // The default identity has data copied from the deprecated database fields.
  // The deprecated fields have not been cleared so that older code can still
  // use the database.
  const AttestationDatabase::Identity& default_identity_data =
      db.identities().Get(Attestation::kDefaultPCA);
  EXPECT_EQ(IDENTITY_FEATURE_ENTERPRISE_ENROLLMENT_ID,
            default_identity_data.features());
  ASSERT_EQ("identity_binding",
            default_identity_data.identity_binding().identity_binding());
  ASSERT_EQ("identity_public_key",
            default_identity_data.identity_binding().identity_public_key());
  ASSERT_EQ("identity_binding", db.identity_binding().identity_binding());
  ASSERT_EQ("identity_public_key", db.identity_binding().identity_public_key());
  ASSERT_EQ("pcr0_quote", default_identity_data.pcr_quotes().at(0).quote());
  EXPECT_EQ("pcr0_quote", db.pcr0_quote().quote());
  ASSERT_EQ("pcr1_quote", default_identity_data.pcr_quotes().at(1).quote());
  EXPECT_EQ("pcr1_quote", db.pcr1_quote().quote());

  // No other identity has been created.
  ASSERT_EQ(1, db.identities().size());

  // The identity credential was migrated into an identity certificate.
  // As a result, identity data does not use the identity credential. The
  // deprecated field has not been cleared so that older code can still
  // use the database.
  ASSERT_FALSE(default_identity_data.identity_key().has_identity_credential());
  ASSERT_EQ("identity_cred", db.identity_key().identity_credential());
  VerifyPCAData(db, "identity_cred");

  // Attestation is prepared.
  EXPECT_TRUE(attestation_.IsPreparedForEnrollment());
}

TEST_F(AttestationBaseTest, MigrateAttestationDatabaseWithCorruptedFields) {
  // Simulate first login.
  attestation_.PrepareForEnrollment();

  // Simulate an older database.
  AttestationDatabase db = GetPersistentDatabase();
  db.mutable_credentials()->clear_encrypted_endorsement_credentials();
  db.mutable_credentials()->set_endorsement_credential("endorsement_cred");
  EncryptedData default_encrypted_endorsement_credential;
  default_encrypted_endorsement_credential.set_wrapped_key("default_key");
  db.mutable_credentials()
      ->mutable_default_encrypted_endorsement_credential()
      ->CopyFrom(default_encrypted_endorsement_credential);
  db.clear_identities();
  db.clear_identity_certificates();
  db.mutable_identity_binding()->set_identity_binding("identity_binding");
  db.mutable_identity_binding()->set_identity_public_key("identity_public_key");
  db.mutable_identity_key()->set_identity_credential("identity_cred");
  // Note that we are missing a PCR0 quote.
  db.mutable_pcr1_quote()->set_quote("pcr1_quote");
  // Persist that older database.
  attestation_.PersistDatabase(db);

  // Simulate second login.
  Initialize();
  attestation_.PrepareForEnrollment();
  db = GetPersistentDatabase();

  // The default encrypted endorsement credential has been migrated.
  // The deprecated field has not been cleared so that older code can still
  // use the database.
  ASSERT_TRUE(MessageDifferencer::Equals(
      default_encrypted_endorsement_credential,
      db.credentials().encrypted_endorsement_credentials().at(
          Attestation::kDefaultPCA)));
  ASSERT_TRUE(MessageDifferencer::Equals(
      default_encrypted_endorsement_credential,
      db.credentials().default_encrypted_endorsement_credential()));

  // The default identity is copied from the deprecated database after
  // re-generating the PCR0 quote.
  // The deprecated fields have not been cleared so that older code can still
  // use the database.
  ASSERT_EQ(1, db.identities().size());
  ASSERT_EQ("identity_binding", db.identity_binding().identity_binding());
  ASSERT_EQ("identity_public_key", db.identity_binding().identity_public_key());
  EXPECT_EQ("pcr1_quote", db.pcr1_quote().quote());

  // Check the migrated identity after re-generating the PCR0 quote is
  // correct.
  const AttestationDatabase::Identity& default_identity_data =
      db.identities().Get(Attestation::kDefaultPCA);

  ASSERT_EQ("identity_binding",
            default_identity_data.identity_binding().identity_binding());
  ASSERT_EQ("identity_public_key",
            default_identity_data.identity_binding().identity_public_key());
  ASSERT_EQ("pcr1_quote", default_identity_data.pcr_quotes().at(1).quote());
  ASSERT_TRUE(default_identity_data.pcr_quotes().at(0).has_quote());

  // There is no identity certificate since there is no identity.
  ASSERT_EQ(db.identity_certificates().size(), 1);
}

TEST_F(AttestationBaseTest,
       MigrateAttestationDatabaseAllEndorsementCredentials) {
  // Simulate first login.
  attestation_.PrepareForEnrollment();

  // Simulate an older database.
  AttestationDatabase db = GetPersistentDatabase();
  db.mutable_credentials()->clear_encrypted_endorsement_credentials();
  db.mutable_credentials()->set_endorsement_credential("endorsement_cred");
  EncryptedData default_encrypted_endorsement_credential;
  default_encrypted_endorsement_credential.set_wrapped_key("default_key");
  db.mutable_credentials()
      ->mutable_default_encrypted_endorsement_credential()
      ->CopyFrom(default_encrypted_endorsement_credential);
  EncryptedData test_encrypted_endorsement_credential;
  test_encrypted_endorsement_credential.set_wrapped_key("test_key");
  db.mutable_credentials()
      ->mutable_test_encrypted_endorsement_credential()
      ->CopyFrom(test_encrypted_endorsement_credential);
  db.clear_identities();
  db.clear_identity_certificates();
  db.mutable_identity_binding()->set_identity_binding("identity_binding");
  db.mutable_identity_binding()->set_identity_public_key("identity_public_key");
  db.mutable_identity_key()->set_identity_credential("identity_cred");
  db.mutable_pcr0_quote()->set_quote("pcr0_quote");
  db.mutable_pcr1_quote()->set_quote("pcr1_quote");
  // Persist that older database.
  attestation_.PersistDatabase(db);

  // Simulate second login.
  Initialize();
  attestation_.PrepareForEnrollment();
  db = GetPersistentDatabase();

  // The encrypted endorsement credentials have both been migrated.
  // The deprecated fields have not been cleared so that older code can still
  // use the database.
  ASSERT_TRUE(MessageDifferencer::Equals(
      default_encrypted_endorsement_credential,
      db.credentials().encrypted_endorsement_credentials().at(
          Attestation::kDefaultPCA)));
  ASSERT_TRUE(MessageDifferencer::Equals(
      default_encrypted_endorsement_credential,
      db.credentials().default_encrypted_endorsement_credential()));
  ASSERT_TRUE(MessageDifferencer::Equals(
      test_encrypted_endorsement_credential,
      db.credentials().encrypted_endorsement_credentials().at(
          Attestation::kTestPCA)));
  ASSERT_TRUE(MessageDifferencer::Equals(
      test_encrypted_endorsement_credential,
      db.credentials().test_encrypted_endorsement_credential()));

  // Attestation is prepared.
  EXPECT_TRUE(attestation_.IsPreparedForEnrollment());
}

TEST_F(AttestationBaseTest, CertChainWithNoIntermediateCA) {
  EXPECT_CALL(key_store_, Read(true, kTestUser, "test", _))
      .WillRepeatedly(DoAll(
          SetArgPointee<3>(GetCertifiedKeyBlob("", false)),
          Return(true)));
  SecureBlob blob;
  EXPECT_TRUE(attestation_.GetCertificateChain(true, kTestUser, "test",
                                               &blob));
  EXPECT_TRUE(CompareBlob(blob, EncodeCertChain("stored_cert", "")));
}

TEST_F(AttestationBaseTest, IdentityResetRequest) {
  SecureBlob blob;
  EXPECT_TRUE(attestation_.GetIdentityResetRequest("token", &blob));
  attestation_.PrepareForEnrollment();
  EXPECT_TRUE(attestation_.GetIdentityResetRequest("token", &blob));
}

// A test class which does not initialize the Attestation instance.
class AttestationBaseTestNoInitialize : public AttestationBaseTest {
 public:
  void Initialize() override {}
};

TEST_F(AttestationBaseTestNoInitialize, AutoExtendPCR1) {
  Blob default_pcr(GetDigestSize());
  EXPECT_CALL(tpm_, ReadPCR(1, _))
      .WillOnce(DoAll(SetArgPointee<1>(default_pcr), Return(true)));
  std::string fake_hwid = "hwid";
  Blob fake_hwid_expected_extension;
  ASSERT_TRUE(base::HexStringToBytes(
      "bc45e91a086497cd817cb3024ac5c0d733111a74378257b11991e1e435b7e71e",
      &fake_hwid_expected_extension));
  fake_hwid_expected_extension.resize(GetDigestSize());
  EXPECT_CALL(tpm_, ExtendPCR(1, fake_hwid_expected_extension))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, GetHardwareID()).WillRepeatedly(Return(fake_hwid));
  // Now initialize and the mocks will complain if PCR1 is not extended.
  AttestationBaseTest::Initialize();
}

TEST_F(AttestationBaseTestNoInitialize, AutoExtendPCR1NoHwID) {
  Blob default_pcr(GetDigestSize());
  EXPECT_CALL(tpm_, ReadPCR(1, _))
      .WillOnce(DoAll(SetArgPointee<1>(default_pcr), Return(true)));
  std::string no_hwid = "";
  EXPECT_CALL(tpm_, ExtendPCR(_, _)).Times(0);
  EXPECT_CALL(platform_, GetHardwareID()).WillRepeatedly(Return(no_hwid));
  // Now initialize and the mocks will complain if PCR1 is extended.
  AttestationBaseTest::Initialize();
}

class AttestationEnrollmentIdTest : public AttestationBaseTest {
 public:
  void Initialize() override {
    SecureBlob abe_data;
    EXPECT_TRUE(brillo::SecureBlob::HexStringToSecureBlob(kABEData,
                                                          &abe_data));
    attestation_.Initialize(&tpm_, &tpm_init_, &platform_, &crypto_,
                            &install_attributes_, abe_data,
                            false /* retain_endorsement_data */);
  }
};

TEST_F(AttestationEnrollmentIdTest, GetEnterpriseEnrollmentId) {
  brillo::SecureBlob pubek(GetValidEndorsementKey());
  EXPECT_CALL(tpm_, GetEndorsementPublicKey(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(pubek),
                            Return(Tpm::kTpmRetryNone)));
  brillo::SecureBlob blob;
  EXPECT_TRUE(attestation_.GetEnterpriseEnrollmentId(&blob));
  EXPECT_EQ(kEID,
            base::ToLowerASCII(base::HexEncode(blob.data(), blob.size())));
}

TEST_F(AttestationEnrollmentIdTest, GetEnterpriseEnrollmentIdCached) {
  brillo::SecureBlob pubek(GetValidEndorsementKey());
  EXPECT_CALL(tpm_, GetEndorsementPublicKey(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(pubek),
                            Return(Tpm::kTpmRetryNone)));
  attestation_.PrepareForEnrollment();
  SecureBlob enroll_blob;
  EXPECT_TRUE(attestation_.CreateEnrollRequest(Attestation::kDefaultPCA,
                                               &enroll_blob));
  attestation_.Enroll(Attestation::kDefaultPCA, GetEnrollBlob());
  // Change abe_data.
  attestation_.Initialize(
      &tpm_, &tpm_init_, &platform_, &crypto_, &install_attributes_,
      brillo::SecureBlob("new_abe_data"), false /* retain_endorsement_data */);
  // GetEnterpriseEnrollmentId should return a cached EID.
  brillo::SecureBlob blob;
  EXPECT_TRUE(attestation_.GetEnterpriseEnrollmentId(&blob));
  EXPECT_EQ(kEID,
            base::ToLowerASCII(base::HexEncode(blob.data(), blob.size())));
  // The EID should be different if recomputed since the abe_data has changed.
  EXPECT_CALL(tpm_, GetEndorsementPublicKeyWithDelegate(_, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<0>(pubek),
                            Return(Tpm::kTpmRetryNone)));
  EXPECT_TRUE(attestation_.ComputeEnterpriseEnrollmentId(&blob));
  EXPECT_NE(kEID,
            base::ToLowerASCII(base::HexEncode(blob.data(), blob.size())));
}

TEST_F(AttestationEnrollmentIdTest, ComputeEnterpriseEnrollmentId) {
  brillo::SecureBlob pubek(GetValidEndorsementKey());
  EXPECT_CALL(tpm_, GetEndorsementPublicKey(_))
      .WillOnce(DoAll(SetArgPointee<0>(pubek),
                      Return(Tpm::kTpmRetryNone)));
  brillo::SecureBlob blob;
  EXPECT_TRUE(ComputeEnterpriseEnrollmentId(&blob));
  EXPECT_EQ(kEID,
            base::ToLowerASCII(base::HexEncode(blob.data(), blob.size())));
}

TEST_F(AttestationEnrollmentIdTest, ComputeEnterpriseEnrollmentIdHasDelegate) {
  attestation_.PrepareForEnrollment();
  brillo::SecureBlob pubek(GetValidEndorsementKey());
  EXPECT_CALL(tpm_, GetEndorsementPublicKeyWithDelegate(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(pubek), Return(Tpm::kTpmRetryNone)));
  brillo::SecureBlob blob;
  EXPECT_TRUE(ComputeEnterpriseEnrollmentId(&blob));
  EXPECT_EQ(kEID,
            base::ToLowerASCII(base::HexEncode(blob.data(), blob.size())));
}

TEST_F(AttestationEnrollmentIdTest,
       ComputeEnterpriseEnrollmentIdHasDelegateWithTemporaryFailure) {
  attestation_.PrepareForEnrollment();
  brillo::SecureBlob pubek_empty("");
  brillo::SecureBlob pubek(GetValidEndorsementKey());
  EXPECT_CALL(tpm_, GetEndorsementPublicKeyWithDelegate(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(pubek_empty),
                      Return(Tpm::kTpmRetryLater)))
      .WillOnce(DoAll(SetArgPointee<0>(pubek), Return(Tpm::kTpmRetryNone)));
  brillo::SecureBlob blob;
  EXPECT_FALSE(ComputeEnterpriseEnrollmentId(&blob));   // Initial try.
  EXPECT_TRUE(blob.empty());
  EXPECT_TRUE(ComputeEnterpriseEnrollmentId(&blob));    // Retry.
  EXPECT_EQ(kEID,
            base::ToLowerASCII(base::HexEncode(blob.data(), blob.size())));
}

TEST_F(AttestationEnrollmentIdTest,
       ComputeEnterpriseEnrollmentIdHasDelegateWithoutPermissions) {
  attestation_.PrepareForEnrollment();
  GetMutableDatabase().mutable_delegate()->clear_can_read_internal_pub();
  brillo::SecureBlob pubek("");
  EXPECT_CALL(tpm_, GetEndorsementPublicKeyWithDelegate(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(pubek),
                      Return(Tpm::kTpmRetryFailNoRetry)));
  brillo::SecureBlob blob;
  EXPECT_TRUE(ComputeEnterpriseEnrollmentId(&blob));
  EXPECT_TRUE(blob.empty());
  EXPECT_TRUE(GetPersistentDatabase().delegate().has_can_read_internal_pub());
  EXPECT_FALSE(GetPersistentDatabase().delegate().can_read_internal_pub());
}

TEST_F(AttestationEnrollmentIdTest,
       ComputeEnterpriseEnrollmentIdHasDelegateWithoutPermissionsButNoOwner) {
  attestation_.PrepareForEnrollment();
  GetMutableDatabase().mutable_delegate()->clear_can_read_internal_pub();
  brillo::SecureBlob pubek_empty("");
  brillo::SecureBlob pubek(GetValidEndorsementKey());
  EXPECT_CALL(tpm_, GetEndorsementPublicKeyWithDelegate(_, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<0>(pubek_empty),
                            Return(Tpm::kTpmRetryFailNoRetry)));
  EXPECT_CALL(tpm_, GetEndorsementPublicKey(_))
      .WillOnce(DoAll(SetArgPointee<0>(pubek),
                      Return(Tpm::kTpmRetryNone)));
  brillo::SecureBlob blob;
  EXPECT_TRUE(ComputeEnterpriseEnrollmentId(&blob));  // Owner succeeds.
  EXPECT_EQ(kEID,
            base::ToLowerASCII(base::HexEncode(blob.data(), blob.size())));
  EXPECT_TRUE(GetPersistentDatabase().delegate().has_can_read_internal_pub());
  EXPECT_FALSE(GetPersistentDatabase().delegate().can_read_internal_pub());
}

TEST_F(AttestationEnrollmentIdTest,
       ComputeEnterpriseEnrollmentIdHasDelegateKnownToBeWithoutPermissions) {
  attestation_.PrepareForEnrollment();
  GetMutableDatabase().mutable_delegate()->set_can_read_internal_pub(false);
  brillo::SecureBlob pubek("");
  EXPECT_CALL(tpm_, GetEndorsementPublicKey(_))
      .WillOnce(DoAll(SetArgPointee<0>(pubek),
                      Return(Tpm::kTpmRetryFailNoRetry)));
  brillo::SecureBlob blob;
  EXPECT_TRUE(ComputeEnterpriseEnrollmentId(&blob));
  EXPECT_TRUE(blob.empty());
}

TEST_F(AttestationEnrollmentIdTest, ComputeEnterpriseEnrollmentIdEmptyAbeData) {
  attestation_.Initialize(&tpm_, &tpm_init_, &platform_, &crypto_,
                          &install_attributes_, brillo::SecureBlob(""),
                          false /* retain_endorsement_data */);
  brillo::SecureBlob blob;
  EXPECT_TRUE(ComputeEnterpriseEnrollmentId(&blob));
  EXPECT_TRUE(blob.empty());
}

TEST_F(AttestationEnrollmentIdTest, ComputeEnterpriseEnrollmentIdEmptyEkm) {
  brillo::SecureBlob pubek("");
  EXPECT_CALL(tpm_, GetEndorsementPublicKey(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(pubek),
                            Return(Tpm::kTpmRetryNone)));
  brillo::SecureBlob blob;
  EXPECT_TRUE(ComputeEnterpriseEnrollmentId(&blob));
  EXPECT_TRUE(blob.empty());
}

TEST_F(AttestationEnrollmentIdTest, ComputeEnterpriseEnrollmentIdFailToGetEkm) {
  brillo::SecureBlob pubek("ek");
  EXPECT_CALL(tpm_, GetEndorsementPublicKey(_))
      .WillOnce(DoAll(SetArgPointee<0>(pubek),
                      Return(Tpm::kTpmRetryFailNoRetry)));
  brillo::SecureBlob blob;
  EXPECT_TRUE(ComputeEnterpriseEnrollmentId(&blob));
  EXPECT_TRUE(blob.empty());
}

TEST_F(AttestationEnrollmentIdTest, CreateEnrollRequestCheckNonce) {
  attestation_.PrepareForEnrollment();
  EXPECT_TRUE(attestation_.IsPreparedForEnrollment());
  brillo::SecureBlob enroll_request;
  EXPECT_TRUE(attestation_.CreateEnrollRequest(Attestation::kDefaultPCA,
                                               &enroll_request));
  AttestationEnrollmentRequest request_pb;
  ASSERT_TRUE(request_pb.ParseFromString(enroll_request.to_string()));
  ASSERT_TRUE(request_pb.has_enterprise_enrollment_nonce());
}

TEST_F(AttestationEnrollmentIdTest,
       CreateEnrollRequestNoIdentityFeaturesCheckNonce) {
  attestation_.set_default_identity_features_for_test(NO_IDENTITY_FEATURES);
  attestation_.PrepareForEnrollment();
  EXPECT_TRUE(attestation_.IsPreparedForEnrollment());
  brillo::SecureBlob enroll_request;
  EXPECT_TRUE(attestation_.CreateEnrollRequest(Attestation::kDefaultPCA,
                                               &enroll_request));
  AttestationEnrollmentRequest request_pb;
  ASSERT_TRUE(request_pb.ParseFromString(enroll_request.to_string()));
  ASSERT_FALSE(request_pb.has_enterprise_enrollment_nonce());
}

class AttestationTest
    : public AttestationBaseTest,
      public testing::WithParamInterface<Attestation::PCAType> {
 public:
  void SetUp() override {
    AttestationBaseTest::SetUp();
    pca_type_ = GetParam();
  }

 protected:
  Attestation::PCAType pca_type_;
};

TEST_P(AttestationTest, IsAttestationPreparedForOnePca) {
  // Simulate a migrated database that only has an encrypted credential
  // for one PCA.
  AttestationDatabase& db = GetMutableDatabase();
  EncryptedData default_encrypted_endorsement_credential;
  default_encrypted_endorsement_credential.set_wrapped_key("default_key");
  db.mutable_credentials()->clear_endorsement_credential();
  (*db.mutable_credentials()
      ->mutable_encrypted_endorsement_credentials())[pca_type_] =
      default_encrypted_endorsement_credential;
  attestation_.PersistDatabase(db);

  // Attestation is prepared.
  EXPECT_TRUE(attestation_.IsPreparedForEnrollment());
}

TEST_P(AttestationTest, FirstIdentityNotEnrolled) {
  ASSERT_FALSE(attestation_.HasIdentityCertificate(
                         Attestation::kFirstIdentity, pca_type_));
}

TEST_P(AttestationTest, NullTpm) {
  Crypto crypto(nullptr);
  InstallAttributes install_attributes(nullptr);
  Attestation without_tpm;
  without_tpm.Initialize(NULL, NULL, NULL, &crypto, &install_attributes,
                         brillo::SecureBlob() /* abe_data */,
                         false /* retain_endorsement_data */);
  without_tpm.PrepareForEnrollment();
  EXPECT_FALSE(without_tpm.IsPreparedForEnrollment());
  EXPECT_FALSE(without_tpm.Verify(false));
  EXPECT_FALSE(without_tpm.VerifyEK(false));
  EXPECT_FALSE(without_tpm.CreateEnrollRequest(pca_type_, NULL));
  EXPECT_FALSE(without_tpm.Enroll(pca_type_, SecureBlob()));
  EXPECT_FALSE(without_tpm.CreateCertRequest(
      pca_type_, ENTERPRISE_USER_CERTIFICATE, "", "", nullptr));
  EXPECT_FALSE(
      without_tpm.FinishCertRequest(SecureBlob(), false, "", "", nullptr));
  EXPECT_FALSE(without_tpm.SignEnterpriseChallenge(
      false, "", "", "", SecureBlob(), false, SecureBlob(), nullptr));
  EXPECT_FALSE(
      without_tpm.SignSimpleChallenge(false, "", "", SecureBlob(), nullptr));
  EXPECT_FALSE(without_tpm.GetEKInfo(nullptr));
}

TEST_P(AttestationTest, PCARequest_Enroll) {
  std::string expected_url = GetPCAWebOrigin(pca_type_) + "/enroll";
  http_transport_->AddSimpleReplyHandler(
      expected_url, brillo::http::request_type::kPost,
      brillo::http::status_code::Ok, "response",
      brillo::mime::application::kOctet_stream);
  SecureBlob response;
  EXPECT_TRUE(attestation_.SendPCARequestAndBlock(
      pca_type_, Attestation::kEnroll, SecureBlob("request"), &response));
  EXPECT_TRUE(CompareBlob(response, "response"));
}

TEST_P(AttestationTest, PCARequest_GetCertificate) {
  std::string expected_url = GetPCAWebOrigin(pca_type_) + "/sign";
  http_transport_->AddSimpleReplyHandler(
      expected_url, brillo::http::request_type::kPost,
      brillo::http::status_code::Ok, "response",
      brillo::mime::application::kOctet_stream);
  SecureBlob response;
  EXPECT_TRUE(attestation_.SendPCARequestAndBlock(
      pca_type_, Attestation::kGetCertificate, SecureBlob("request"),
      &response));
  EXPECT_TRUE(CompareBlob(response, "response"));
}

TEST_P(AttestationTest, PCARequestWithServerError) {
  std::string expected_url = GetPCAWebOrigin(pca_type_) + "/enroll";
  http_transport_->AddSimpleReplyHandler(
      expected_url, brillo::http::request_type::kPost,
      brillo::http::status_code::BadRequest, "response",
      brillo::mime::application::kOctet_stream);
  SecureBlob response;
  EXPECT_FALSE(attestation_.SendPCARequestAndBlock(
      pca_type_, Attestation::kEnroll, SecureBlob("request"), &response));
  EXPECT_FALSE(CompareBlob(response, "response"));
}

struct AbeDataParam {
  const char* data;
  const char* enterprise_enrollment_nonce;
  const char* enterprise_enrollment_id;

  AbeDataParam(const char* data,
               const char* enterprise_enrollment_nonce,
               const char* enterprise_enrollment_id)
      : data(data),
        enterprise_enrollment_nonce(enterprise_enrollment_nonce),
        enterprise_enrollment_id(enterprise_enrollment_id) {}
};

struct AbeDataTestParam {
  AbeDataParam abe_data;
  Attestation::PCAType pca_type;

  AbeDataTestParam(AbeDataParam abe_data, Attestation::PCAType pca_type)
      : abe_data(abe_data), pca_type(pca_type) {}
};

class AttestationWithAbeDataTest
    : public AttestationBaseTest,
      public testing::WithParamInterface<AbeDataTestParam> {
 public:
  void SetUp() override {
    AttestationBaseTest::SetUp();
    pca_type_ = GetParam().pca_type;
  }

  void Initialize() override {
    SecureBlob abe_data;
    const char* data = GetParam().abe_data.data;
    if (data != NULL) {
      if (!brillo::SecureBlob::HexStringToSecureBlob(data, &abe_data)) {
        abe_data.clear();
      }
    }
    attestation_.Initialize(&tpm_, &tpm_init_, &platform_, &crypto_,
                            &install_attributes_, abe_data,
                            false /* retain_endorsement_data */);
  }

 protected:
  void VerifyAttestationEnrollmentRequest(const SecureBlob& request) {
    AttestationEnrollmentRequest request_pb;
    EXPECT_TRUE(request_pb.ParseFromArray(request.data(), request.size()));
    const AbeDataParam& param = GetParam().abe_data;
    if (param.data == nullptr) {
      EXPECT_FALSE(request_pb.has_enterprise_enrollment_nonce());
      return;
    }
    SecureBlob expected;
    EXPECT_TRUE(brillo::SecureBlob::HexStringToSecureBlob(
                    param.enterprise_enrollment_nonce, &expected));
    std::string nonce = request_pb.enterprise_enrollment_nonce();
    EXPECT_EQ(expected, SecureBlob(nonce.begin(), nonce.end()));
  }

  Attestation::PCAType pca_type_;
};

TEST_P(AttestationWithAbeDataTest,
       PrepareForEnrollmentInstallAttributesNotReady) {
  install_attributes_.set_status_for_testing(
      InstallAttributes::Status::kFirstInstall);
  attestation_.PrepareForEnrollment();
  EXPECT_TRUE(attestation_.IsPreparedForEnrollment());
  AttestationDatabase db = GetPersistentDatabase();
  EXPECT_TRUE(db.has_credentials());
  // One identity has been created.
  EXPECT_EQ(1, db.identities().size());
  const AttestationDatabase::Identity& identity_data = db.identities().Get(0);
  EXPECT_TRUE(identity_data.has_identity_binding());
  EXPECT_TRUE(identity_data.has_identity_key());
  EXPECT_EQ(1, identity_data.pcr_quotes().count(0));
  EXPECT_EQ(1, identity_data.pcr_quotes().count(1));
  // Deprecated identity-related values have not been set.
  EXPECT_FALSE(db.has_identity_binding());
  EXPECT_FALSE(db.has_identity_key());
  EXPECT_FALSE(db.has_pcr0_quote());
  EXPECT_FALSE(db.has_pcr1_quote());
  // We have a delegate to activate the AIK.
  EXPECT_TRUE(db.has_delegate());
  // Verify Privacy CA-related data.
  VerifyPCAData(db);
  // These deprecated fields have not been set either.
  EXPECT_TRUE(db.has_credentials());
  EXPECT_FALSE(db.credentials().has_default_encrypted_endorsement_credential());
}

TEST_P(AttestationWithAbeDataTest, Enroll) {
  SecureBlob blob;
  EXPECT_FALSE(attestation_.CreateEnrollRequest(pca_type_, &blob));
  attestation_.PrepareForEnrollment();
  EXPECT_FALSE(attestation_.HasIdentityCertificate(Attestation::kFirstIdentity,
                                                   pca_type_));
  EXPECT_TRUE(attestation_.CreateEnrollRequest(pca_type_, &blob));
  VerifyAttestationEnrollmentRequest(blob);
  EXPECT_TRUE(attestation_.Enroll(pca_type_, GetEnrollBlob()));
  EXPECT_TRUE(attestation_.HasIdentityCertificate(
                         Attestation::kFirstIdentity, pca_type_));
  // Check that the database is only using the new fields.
  AttestationDatabase db = GetPersistentDatabase();
  EXPECT_FALSE(db.mutable_identity_key()->has_identity_credential());
}

TEST_P(AttestationWithAbeDataTest, GetEnterpriseEnrollmentIdCached) {
  brillo::SecureBlob pubek(GetValidEndorsementKey());
  EXPECT_CALL(tpm_, GetEndorsementPublicKey(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(pubek),
                            Return(Tpm::kTpmRetryNone)));
  attestation_.PrepareForEnrollment();
  SecureBlob enroll_blob;
  EXPECT_TRUE(attestation_.CreateEnrollRequest(Attestation::kDefaultPCA,
                                               &enroll_blob));
  attestation_.Enroll(Attestation::kDefaultPCA, GetEnrollBlob());
  // Change abe_data.
  attestation_.Initialize(&tpm_, &tpm_init_, &platform_, &crypto_,
                          &install_attributes_,
                          brillo::SecureBlob("new_abe_data"),
                          false /* retain_endorsement_data */);
  // GetEnterpriseEnrollmentId should return a cached EID.
  brillo::SecureBlob blob;
  EXPECT_TRUE(attestation_.GetEnterpriseEnrollmentId(&blob));
  EXPECT_EQ(GetParam().abe_data.enterprise_enrollment_id,
            base::ToLowerASCII(base::HexEncode(blob.data(), blob.size())));
}

TEST_P(AttestationTest, CertRequest) {
  EXPECT_CALL(tpm_, CreateCertifiedKey(_, _, _, _, _, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<3>(GetPKCS1PublicKey()), Return(true)));
  SecureBlob blob;
  attestation_.PrepareForEnrollment();
  EXPECT_FALSE(attestation_.CreateCertRequest(
      pca_type_, ENTERPRISE_USER_CERTIFICATE, "", "", &blob));
  EXPECT_TRUE(attestation_.Enroll(pca_type_, GetEnrollBlob()));
  EXPECT_TRUE(attestation_.CreateCertRequest(
      pca_type_, ENTERPRISE_USER_CERTIFICATE, "", "", &blob));
  EXPECT_FALSE(attestation_.DoesKeyExist(false, kTestUser, "test"));
  EXPECT_TRUE(attestation_.FinishCertRequest(GetCertRequestBlob(blob), false,
                                             kTestUser, "test", &blob));
  EXPECT_TRUE(CompareBlob(
      blob, EncodeCertChain("response_cert", "response_ca_cert")));
  EXPECT_TRUE(attestation_.DoesKeyExist(false, kTestUser, "test"));
  EXPECT_TRUE(
      attestation_.GetCertificateChain(false, kTestUser, "test", &blob));
  EXPECT_TRUE(CompareBlob(
      blob, EncodeCertChain("response_cert", "response_ca_cert")));
  EXPECT_TRUE(attestation_.GetPublicKey(false, kTestUser, "test", &blob));
  EXPECT_TRUE(blob == GetX509PublicKey());
}

TEST_P(AttestationTest, CertRequestStorageFailure) {
  EXPECT_CALL(key_store_, Write(true, kTestUser, "test", _))
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(key_store_, Read(true, kTestUser, "test", _))
      .WillOnce(Return(false))
      .WillRepeatedly(DoAll(SetArgPointee<3>(GetCertifiedKeyBlob("", true)),
                            Return(true)));
  SecureBlob blob;
  attestation_.PrepareForEnrollment();
  EXPECT_TRUE(attestation_.Enroll(pca_type_, GetEnrollBlob()));
  EXPECT_TRUE(attestation_.CreateCertRequest(
      pca_type_, ENTERPRISE_USER_CERTIFICATE, "", "", &blob));
  // Expect storage failure here.
  EXPECT_FALSE(attestation_.FinishCertRequest(GetCertRequestBlob(blob), true,
                                              kTestUser, "test", &blob));
  EXPECT_TRUE(attestation_.CreateCertRequest(
      pca_type_, ENTERPRISE_USER_CERTIFICATE, "", "", &blob));
  EXPECT_TRUE(attestation_.FinishCertRequest(GetCertRequestBlob(blob), true,
                                             kTestUser, "test", &blob));
  EXPECT_TRUE(CompareBlob(
      blob, EncodeCertChain("response_cert", "response_ca_cert")));
  // Expect storage failure here.
  EXPECT_FALSE(
      attestation_.GetCertificateChain(true, kTestUser, "test", &blob));
  EXPECT_TRUE(attestation_.DoesKeyExist(true, kTestUser, "test"));
  EXPECT_TRUE(
      attestation_.GetCertificateChain(true, kTestUser, "test", &blob));
  EXPECT_TRUE(
      CompareBlob(blob, EncodeCertChain("stored_cert", "stored_ca_cert")));
  EXPECT_TRUE(attestation_.GetPublicKey(true, kTestUser, "test", &blob));
  EXPECT_TRUE(blob == GetX509PublicKey());
}

TEST_P(AttestationTest, SimpleChallenge) {
  EXPECT_CALL(tpm_, Sign(_, _, _, _))
      .WillOnce(Return(false))
      .WillRepeatedly(
          DoAll(SetArgPointee<3>(SecureBlob("signature")), Return(true)));
  brillo::SecureBlob blob;
  attestation_.PrepareForEnrollment();
  EXPECT_TRUE(attestation_.CreateEnrollRequest(pca_type_, &blob));
  EXPECT_TRUE(attestation_.Enroll(pca_type_, GetEnrollBlob()));
  EXPECT_TRUE(attestation_.CreateCertRequest(
      pca_type_, ENTERPRISE_USER_CERTIFICATE, "", "", &blob));
  EXPECT_TRUE(attestation_.FinishCertRequest(GetCertRequestBlob(blob), false,
                                             kTestUser, "test", &blob));
  // Expect tpm_.Sign() failure the first attempt.
  EXPECT_FALSE(attestation_.SignSimpleChallenge(
      false, kTestUser, "test", SecureBlob("challenge"), &blob));
  EXPECT_TRUE(attestation_.SignSimpleChallenge(
      false, kTestUser, "test", SecureBlob("challenge"), &blob));
  EXPECT_TRUE(VerifySimpleChallenge(blob, "challenge", "signature"));
}

TEST_P(AttestationTest, EMKChallenge) {
  EXPECT_CALL(tpm_, Sign(_, _, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<3>(SecureBlob("signature")), Return(true)));
  brillo::SecureBlob blob;
  attestation_.PrepareForEnrollment();
  EXPECT_TRUE(attestation_.CreateEnrollRequest(pca_type_, &blob));
  EXPECT_TRUE(attestation_.Enroll(pca_type_, GetEnrollBlob()));
  EXPECT_TRUE(attestation_.CreateCertRequest(
      pca_type_, ENTERPRISE_USER_CERTIFICATE, "", "", &blob));
  EXPECT_TRUE(attestation_.FinishCertRequest(GetCertRequestBlob(blob), false,
                                             kTestUser, "test", &blob));
  // Try all the VA servers in turn. We don't parameterize the test because
  // not doing so allows us to verify that the attestation code uses the
  // proper key when it has more than one.
  for (int t = Attestation::kDefaultVA; t < Attestation::kMaxVAType; ++t) {
    Attestation::VAType va_type = static_cast<Attestation::VAType>(t);
    SecureBlob bad_prefix_challenge =
        GetEnterpriseVaChallenge(va_type, "bad", true);
    EXPECT_FALSE(attestation_.SignEnterpriseVaChallenge(
        va_type, false, kTestUser, "test", "test_domain", SecureBlob("test_id"),
        false, bad_prefix_challenge, "" /* key_name_for_spkac */, &blob));
    SecureBlob challenge =
        GetEnterpriseVaChallenge(va_type, "EnterpriseKeyChallenge", true);
    EXPECT_TRUE(attestation_.SignEnterpriseVaChallenge(
        va_type, false, kTestUser, "test", "test_domain", SecureBlob("test_id"),
        false, challenge, "" /* key_name_for_spkac */, &blob));
    EXPECT_TRUE(VerifyEnterpriseVaChallenge(va_type, blob, EMK, "test_domain",
                                            "test_id", "", "signature"));
  }
  // Try the default VA server.
  SecureBlob bad_prefix_challenge =
      GetEnterpriseVaChallenge(Attestation::kDefaultVA, "bad", true);
  EXPECT_FALSE(attestation_.SignEnterpriseChallenge(
      false, kTestUser, "test", "test_domain", SecureBlob("test_id"), false,
      bad_prefix_challenge, &blob));
  SecureBlob challenge = GetEnterpriseVaChallenge(
      Attestation::kDefaultVA, "EnterpriseKeyChallenge", true);
  EXPECT_TRUE(attestation_.SignEnterpriseChallenge(
      false, kTestUser, "test", "test_domain", SecureBlob("test_id"), false,
      challenge, &blob));
  EXPECT_TRUE(VerifyEnterpriseVaChallenge(Attestation::kDefaultVA, blob, EMK,
                                          "test_domain", "test_id", "",
                                          "signature"));
}

TEST_P(AttestationTest, EMKChallengeDifferentKeyForSPKAC) {
  std::string key_name_for_spkac = "attest-ent-machine_temp_id";
  brillo::SecureBlob blob;
  brillo::SecureBlob blob_spkac;
  attestation_.PrepareForEnrollment();
  EXPECT_TRUE(attestation_.CreateEnrollRequest(pca_type_, &blob));
  EXPECT_TRUE(attestation_.Enroll(pca_type_, GetEnrollBlob()));

  EXPECT_CALL(tpm_, CreateCertifiedKey(_, _, _, _, _, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<3>(GetPKCS1PublicKey()), Return(true)));
  EXPECT_TRUE(attestation_.CreateCertRequest(
      pca_type_, ENTERPRISE_MACHINE_CERTIFICATE, "", "", &blob));
  EXPECT_TRUE(attestation_.FinishCertRequest(GetCertRequestBlob(blob), false,
                                             "", "test", &blob));
  // Create key for spkac
  EXPECT_TRUE(attestation_.CreateCertRequest(
      pca_type_, ENTERPRISE_MACHINE_CERTIFICATE, "", "", &blob_spkac));
  EXPECT_TRUE(attestation_.FinishCertRequest(GetCertRequestBlob(blob_spkac),
                                             false, "", key_name_for_spkac,
                                             &blob_spkac));

  SecureBlob challenge = GetEnterpriseVaChallenge(
      Attestation::kDefaultVA, "EnterpriseKeyChallenge", true);
  EXPECT_CALL(tpm_, Sign(_, _, _, _))
      .Times(2)
      .WillRepeatedly(
          DoAll(SetArgPointee<3>(SecureBlob("signature")), Return(true)));
  EXPECT_TRUE(attestation_.SignEnterpriseVaChallenge(
      Attestation::kDefaultVA, false /*is_user_specific*/, kTestUser, "test",
      "test_domain", SecureBlob("test_id"),
      true /* include_signed_public_key */, challenge, key_name_for_spkac,
      &blob));

  // Verify that the key corresponding to |key_name_for_spkac| was used for
  // SignedPublicKeyAndChallenge.
  EXPECT_TRUE(VerifyEnterpriseVaChallenge(Attestation::kDefaultVA, blob, EMK,
                                          "test_domain", "test_id",
                                          blob_spkac.to_string(), "signature"));
}

TEST_P(AttestationTest, Payload) {
  EXPECT_CALL(key_store_, Write(true, kTestUser, "test",
                                GetCertifiedKeyBlob("test_payload", true)))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(key_store_, Read(true, kTestUser, "test", _))
      .WillRepeatedly(
          DoAll(SetArgPointee<3>(GetCertifiedKeyBlob("stored_payload", true)),
                Return(true)));
  EXPECT_CALL(tpm_, CreateCertifiedKey(_, _, _, _, _, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<3>(GetPKCS1PublicKey()), Return(true)));
  SecureBlob blob;
  attestation_.PrepareForEnrollment();
  EXPECT_TRUE(attestation_.Enroll(pca_type_, GetEnrollBlob()));
  EXPECT_TRUE(attestation_.CreateCertRequest(
      pca_type_, ENTERPRISE_USER_CERTIFICATE, "", "", &blob));
  EXPECT_TRUE(attestation_.FinishCertRequest(GetCertRequestBlob(blob), false,
                                             kTestUser, "test", &blob));
  attestation_.GetKeyPayload(false, kTestUser, "test", &blob);
  EXPECT_EQ(0, blob.size());
  attestation_.SetKeyPayload(false, kTestUser, "test",
                             SecureBlob("test_payload"));
  attestation_.GetKeyPayload(false, kTestUser, "test", &blob);
  EXPECT_TRUE(CompareBlob(blob, "test_payload"));

  attestation_.SetKeyPayload(true, kTestUser, "test",
                             SecureBlob("test_payload"));
  attestation_.GetKeyPayload(true, kTestUser, "test", &blob);
  EXPECT_TRUE(CompareBlob(blob, "stored_payload"));
}

INSTANTIATE_TEST_CASE_P(
    PcaType, AttestationTest,
    ::testing::Values(Attestation::kDefaultPCA, Attestation::kTestPCA));

INSTANTIATE_TEST_CASE_P(
    AbeData, AttestationWithAbeDataTest,
    ::testing::Values(AbeDataTestParam(AbeDataParam(nullptr, nullptr, ""),
                                       Attestation::kDefaultPCA),
                      AbeDataTestParam(AbeDataParam(nullptr, nullptr, ""),
                                       Attestation::kTestPCA),
                      AbeDataTestParam(AbeDataParam(kABEData, kDEN, kEID),
                                       Attestation::kDefaultPCA),
                      AbeDataTestParam(AbeDataParam(kABEData, kDEN, kEID),
                                       Attestation::kTestPCA)));

}  // namespace cryptohome
