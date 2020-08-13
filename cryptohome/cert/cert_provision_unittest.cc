// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <tuple>
#include <vector>

#include <base/bind.h>
#include <chaps/attributes.h>
#include <chaps/chaps_proxy_mock.h>
#include <crypto/scoped_openssl_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include "cert/cert_provision.pb.h"
#include "cryptohome/cert/cert_provision_cryptohome.h"
#include "cryptohome/cert/cert_provision_keystore.h"
#include "cryptohome/cert/cert_provision_pca.h"
#include "cryptohome/cert/cert_provision_util.h"
#include "cryptohome/cert/mock_cert_provision_cryptohome.h"
#include "cryptohome/cert/mock_cert_provision_keystore.h"
#include "cryptohome/cert/mock_cert_provision_pca.h"
#include "cryptohome/cert_provision.h"

using ::brillo::SecureBlob;
using ::google::protobuf::MessageLite;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SetArgPointee;

namespace {

// Some arbitrary certificate label used for testing.
const char kCertLabel[] = "test";
const char kWrongLabel[] = "some wrong label";

const char kBegCertificate[] = "-----BEGIN CERTIFICATE-----";
const char kEndCertificate[] = "-----END CERTIFICATE-----";

// Format for storing captured progress by the callback.
struct Progress {
  cert_provision::Status status;
  int progress;
  std::string message;
};

// Matchers for the captured progress vector.
MATCHER_P(ResultsIn, status, "") {
  return arg.back().status == status && arg.back().progress == 100;
}
MATCHER_P(ResultsNotIn, status, "") {
  return arg.back().status != status && arg.back().progress == 100;
}

}  // namespace

namespace cert_provision {

// Test class for testing top-level functions.
class CertProvisionTest : public testing::Test {
 public:
  CertProvisionTest() {
    CryptohomeProxy::subst_obj = &c_proxy_;
    PCAProxy::subst_obj = &pca_proxy_;
    KeyStore::subst_obj = &key_store_;
  }
  ~CertProvisionTest() {
    CryptohomeProxy::subst_obj = nullptr;
    PCAProxy::subst_obj = nullptr;
    KeyStore::subst_obj = nullptr;
  }

  void SetUp() {
    ON_CALL(c_proxy_, Init())
        .WillByDefault(Return(OpResult()));
    ON_CALL(c_proxy_, CheckIfPrepared(_))
        .WillByDefault(Invoke([this](bool* prepared) {
          *prepared = prepared_;
          return OpResult();
        }));
    ON_CALL(c_proxy_, CheckIfEnrolled(_))
        .WillByDefault(Invoke([this](bool* enrolled) {
          *enrolled = enrolled_;
          return OpResult();
        }));
    ON_CALL(c_proxy_, CreateEnrollRequest(_, _))
        .WillByDefault(Return(OpResult()));
    ON_CALL(c_proxy_, ProcessEnrollResponse(_, _))
        .WillByDefault(Return(OpResult()));
    ON_CALL(c_proxy_, CreateCertRequest(_, _, _))
        .WillByDefault(Return(OpResult()));
    ON_CALL(c_proxy_, ProcessCertResponse(_, _, _))
        .WillByDefault(Return(OpResult()));
    ON_CALL(c_proxy_, GetPublicKey(_, _))
        .WillByDefault(
            Invoke([this](const std::string& /* label */, SecureBlob* key) {
              *key = GetTestPublicKey();
              return OpResult();
            }));
    ON_CALL(c_proxy_, Register(_))
        .WillByDefault(Return(OpResult()));

    ON_CALL(pca_proxy_, MakeRequest(_, _, _))
        .WillByDefault(Return(OpResult()));

    ON_CALL(key_store_, Init())
        .WillByDefault(Return(OpResult()));
    ON_CALL(key_store_, Sign(_, _, _, _, _))
        .WillByDefault(Return(OpResult()));
    ON_CALL(key_store_, ReadProvisionStatus(_, _))
        .WillByDefault(Invoke([this](const std::string& /* label */,
                                     MessageLite* proto) {
          proto->ParseFromString(provision_status_.SerializeAsString());
          return OpResult();
        }));
    ON_CALL(key_store_, WriteProvisionStatus(_, _))
        .WillByDefault(Invoke([this](const std::string& /* label */,
                                     const MessageLite& proto) {
          provision_status_.ParseFromString(proto.SerializeAsString());
          return OpResult();
        }));
    ON_CALL(key_store_, DeleteKeys(_, _))
        .WillByDefault(Return(OpResult()));
  }

  OpResult TestError(Status status) {
    return {status, "Test error"};
  }

  // Resets the captured progress and returns the progress callback to
  // be passed to ProvisionCertificate() for capturing new progress.
  ProgressCallback GetProgressCallback() {
    progress_.clear();
    return base::Bind(&CertProvisionTest::CaptureProgress,
                      base::Unretained(this));
  }

  // Successfully provisions and checks results/
  void Provision() {
    EXPECT_EQ(Status::Success,
              ProvisionCertificate(PCAType::kDefaultPCA,
                                   std::string(),
                                   kCertLabel,
                                   CertificateProfile::CAST_CERTIFICATE,
                                   GetProgressCallback()));
    ExpectProvisioned(true);
    EXPECT_EQ(GetTestKeyID(), provision_status_.key_id());
  }

  // Verifies that a cert is provisioned/not provisioned.
  // Does so by checking the stored ProvisionStatus and the result of
  // GetCertificate().
  void ExpectProvisioned(bool provisioned) {
    EXPECT_EQ(provisioned, provision_status_.provisioned());
    std::string certificate;
    EXPECT_EQ(provisioned ? Status::Success : Status::NotProvisioned,
              GetCertificate(kCertLabel, true, &certificate));
  }

 protected:
  // Returns the current test RSA key. Generates a new random one, if empty.
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
  // Resets the current test RSA key. Next time it is requested through
  // GetTestPublicKey(), a new random key will be returned.
  void ResetObtainedTestKey() {
    rsa_.reset();
  }
  // Returns the current test public key in X.509 format.
  SecureBlob GetTestPublicKey() {
    unsigned char* buffer = nullptr;
    int length = i2d_RSA_PUBKEY(rsa(), &buffer);
    if (length <= 0)
      return SecureBlob();
    SecureBlob tmp(buffer, buffer + length);
    OPENSSL_free(buffer);
    return tmp;
  }
  // Calculates the id for the current test public key.
  std::string GetTestKeyID() {
    return GetKeyID(GetTestPublicKey());
  }

  // Captures progress reported through callback.
  std::vector<Progress> progress_;

  // IsPreparedForEnrollment and IsEnrolled status.
  bool prepared_ = true;
  bool enrolled_ = false;

  // Emulated storage for ProvisionStatus in keystore.
  ProvisionStatus provision_status_;

  NiceMock<MockCryptohomeProxy> c_proxy_;
  NiceMock<MockPCAProxy> pca_proxy_;
  NiceMock<MockKeyStore> key_store_;

 private:
  void CaptureProgress(Status status,
                       int progress,
                       const std::string& message) {
    progress_.push_back({status, progress, message});
  }

  crypto::ScopedRSA rsa_;

  DISALLOW_COPY_AND_ASSIGN(CertProvisionTest);
};

// Checks that provisioning succeeds after sending EnrollRequest if not
// enrolled yet. Checks that the reported progress is not decreasing and
// ends with 100%, and success is reported to the callback on all steps.
TEST_F(CertProvisionTest, ProvisionCertificateSuccessEnroll) {
  ExpectProvisioned(false);
  prepared_ = true;
  enrolled_ = false;
  EXPECT_CALL(c_proxy_, CreateEnrollRequest(_, _));
  EXPECT_CALL(c_proxy_, ProcessEnrollResponse(_, _));
  EXPECT_CALL(pca_proxy_, MakeRequest(_, _, _)).Times(2);
  EXPECT_EQ(Status::Success,
            ProvisionCertificate(PCAType::kDefaultPCA,
                                 std::string(),
                                 kCertLabel,
                                 CertificateProfile::CAST_CERTIFICATE,
                                 GetProgressCallback()));
  int last_progress = 0;
  for (auto p : progress_) {
    EXPECT_EQ(Status::Success, p.status);
    EXPECT_LE(last_progress, p.progress);
    last_progress = p.progress;
  }
  EXPECT_EQ(100, last_progress);
  ExpectProvisioned(true);
}

// Checks that provisioning succeeds w/o sending EnrollRequest if already
// enrolled. Checks that the reported progress is not decreasing and
// ends with 100%, and success is reported to the callback on all steps.
TEST_F(CertProvisionTest, ProvisionCertificateSuccessAlreadyEnrolled) {
  ExpectProvisioned(false);
  prepared_ = true;
  enrolled_ = true;
  EXPECT_CALL(c_proxy_, CreateEnrollRequest(_, _)).Times(0);
  EXPECT_CALL(c_proxy_, ProcessEnrollResponse(_, _)).Times(0);
  EXPECT_CALL(pca_proxy_, MakeRequest(_, _, _)).Times(1);
  EXPECT_EQ(Status::Success,
            ProvisionCertificate(PCAType::kDefaultPCA,
                                 std::string(),
                                 kCertLabel,
                                 CertificateProfile::CAST_CERTIFICATE,
                                 GetProgressCallback()));
  int last_progress = 0;
  for (auto p : progress_) {
    EXPECT_EQ(Status::Success, p.status);
    EXPECT_LE(last_progress, p.progress);
    last_progress = p.progress;
  }
  EXPECT_EQ(100, last_progress);
  ExpectProvisioned(true);
}

// Checks that if enrollment is not prepared, provisioning fails.
TEST_F(CertProvisionTest, ProvisionCertificateNotPrepared) {
  ExpectProvisioned(false);
  prepared_ = false;
  EXPECT_EQ(Status::NotPrepared,
            ProvisionCertificate(PCAType::kDefaultPCA,
                                 std::string(),
                                 kCertLabel,
                                 CertificateProfile::CAST_CERTIFICATE,
                                 GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsIn(Status::NotPrepared));
  ExpectProvisioned(false);
}

// Checks that a failure in EnrollRequest is handled properly.
TEST_F(CertProvisionTest, ProvisionCertificateFailureEnroll) {
  ExpectProvisioned(false);
  enrolled_ = false;
  EXPECT_CALL(c_proxy_, ProcessEnrollResponse(_, _))
      .WillOnce(Return(TestError(Status::CryptohomeError)));
  EXPECT_NE(Status::Success,
            ProvisionCertificate(PCAType::kDefaultPCA,
                                 std::string(),
                                 kCertLabel,
                                 CertificateProfile::CAST_CERTIFICATE,
                                 GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsNotIn(Status::Success));
  EXPECT_EQ("Test error", progress_.back().message);
  ExpectProvisioned(false);
}

// Checks that a failure in CertRequest is handled properly.
TEST_F(CertProvisionTest, ProvisionCertificateFailureCert) {
  ExpectProvisioned(false);
  EXPECT_CALL(c_proxy_, ProcessCertResponse(kCertLabel, _, _))
      .WillOnce(Return(TestError(Status::CryptohomeError)));
  EXPECT_NE(Status::Success,
            ProvisionCertificate(PCAType::kDefaultPCA,
                                 std::string(),
                                 kCertLabel,
                                 CertificateProfile::CAST_CERTIFICATE,
                                 GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsNotIn(Status::Success));
  EXPECT_EQ("Test error", progress_.back().message);
  ExpectProvisioned(false);
}

// Checks that a failure to access PCA is handled properly.
TEST_F(CertProvisionTest, ProvisionCertificateFailurePCA) {
  ExpectProvisioned(false);
  EXPECT_CALL(pca_proxy_, MakeRequest(_, _, _))
      .WillOnce(Return(TestError(Status::ServerError)));
  EXPECT_NE(Status::Success,
            ProvisionCertificate(PCAType::kDefaultPCA,
                                 std::string(),
                                 kCertLabel,
                                 CertificateProfile::CAST_CERTIFICATE,
                                 GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsNotIn(Status::Success));
  EXPECT_EQ("Test error", progress_.back().message);
  ExpectProvisioned(false);
}

// Checks that a failure when registering the keys is handled properly.
TEST_F(CertProvisionTest, ProvisionCertificateFailureRegister) {
  ExpectProvisioned(false);
  EXPECT_CALL(c_proxy_, Register(kCertLabel))
      .WillOnce(Return(TestError(Status::CryptohomeError)));
  EXPECT_NE(Status::Success,
            ProvisionCertificate(PCAType::kDefaultPCA,
                                 std::string(),
                                 kCertLabel,
                                 CertificateProfile::CAST_CERTIFICATE,
                                 GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsNotIn(Status::Success));
  EXPECT_EQ("Test error", progress_.back().message);
  ExpectProvisioned(false);
}

// Checks that a failure when accessing the keystore is handled properly.
TEST_F(CertProvisionTest, ProvisionCertificateFailureKeyStore) {
  ExpectProvisioned(false);
  EXPECT_CALL(key_store_, Init())
      .WillOnce(Return(TestError(Status::KeyStoreError)))
      .WillRepeatedly(Return(OpResult()));
  EXPECT_NE(Status::Success,
            ProvisionCertificate(PCAType::kDefaultPCA,
                                 std::string(),
                                 kCertLabel,
                                 CertificateProfile::CAST_CERTIFICATE,
                                 GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsNotIn(Status::Success));
  EXPECT_EQ("Test error", progress_.back().message);
  ExpectProvisioned(false);
}

// Checks that re-provisioning the certificate deletes the old keys and
// replaces the cert with the new one.
TEST_F(CertProvisionTest, ReProvisionCertificateSuccess) {
  Provision();
  std::string old_key_id = provision_status_.key_id();
  ResetObtainedTestKey();

  EXPECT_CALL(key_store_, DeleteKeys(old_key_id, kCertLabel));
  Provision();
  EXPECT_NE(old_key_id, provision_status_.key_id());
}

// Checks that registration failure upon re-provisioning keeps the old cert
// in place.
TEST_F(CertProvisionTest, ReProvisionCertificateFailureRegister) {
  Provision();
  std::string old_key_id = provision_status_.key_id();
  ResetObtainedTestKey();

  EXPECT_CALL(c_proxy_, Register(kCertLabel))
      .WillOnce(Return(TestError(Status::CryptohomeError)));
  EXPECT_CALL(key_store_, DeleteKeys(_, _)).Times(0);
  EXPECT_NE(Status::Success,
            ProvisionCertificate(PCAType::kDefaultPCA,
                                 std::string(),
                                 kCertLabel,
                                 CertificateProfile::CAST_CERTIFICATE,
                                 GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsNotIn(Status::Success));
  ExpectProvisioned(true);
  EXPECT_EQ(old_key_id, provision_status_.key_id());
}

// Checks that a failure when deleting the old keys is reported even
// though the new cert is stored. Checks that the new cert is usable,
// if the old keys were not deleted.
TEST_F(CertProvisionTest, ReProvisionCertificateFailureDeleteKeys) {
  Provision();
  std::string old_key_id = provision_status_.key_id();
  ResetObtainedTestKey();

  EXPECT_CALL(key_store_, DeleteKeys(old_key_id, kCertLabel))
      .WillOnce(Return(TestError(Status::KeyStoreError)));
  EXPECT_NE(Status::Success,
            ProvisionCertificate(PCAType::kDefaultPCA,
                                 std::string(),
                                 kCertLabel,
                                 CertificateProfile::CAST_CERTIFICATE,
                                 GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsNotIn(Status::Success));
  ExpectProvisioned(true);
  EXPECT_NE(old_key_id, provision_status_.key_id());
}

// Checks that GetCertificate returns the provisioned certificate.
TEST_F(CertProvisionTest, GetCertificateSuccess) {
  std::string cert[] = {
      std::string(kBegCertificate) + "first" + kEndCertificate,
      std::string(kBegCertificate) + "second" + kEndCertificate};
  std::string cert_chain = cert[0] + cert[1];
  SecureBlob cert_chain_blob(cert_chain);
  EXPECT_CALL(c_proxy_, ProcessCertResponse(kCertLabel, _, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(cert_chain_blob), Return(OpResult())));
  Provision();
  std::string result_cert;
  EXPECT_EQ(Status::Success, GetCertificate(kCertLabel, true, &result_cert));
  EXPECT_EQ(cert_chain, result_cert);
  EXPECT_EQ(Status::Success, GetCertificate(kCertLabel, false, &result_cert));
  EXPECT_EQ(cert[0], result_cert);
}

// Checks that getting a certificate if it is not provisioned returns an
// error.
TEST_F(CertProvisionTest, GetCertificateNotProvisioned) {
  ExpectProvisioned(false);
  std::string certificate;
  EXPECT_EQ(Status::NotProvisioned,
            GetCertificate(kCertLabel, true, &certificate));
  EXPECT_TRUE(certificate.empty());
}

// Checks that signing succeeds and returns the requested data.
TEST_F(CertProvisionTest, SignSuccess) {
  Provision();

  std::string data = "some data";
  std::string keystore_sig("signature");

  std::string sig;
  EXPECT_CALL(key_store_,
              Sign(GetTestKeyID(), kCertLabel, SHA1_RSA_PKCS, _, _))
      .WillOnce(DoAll(SetArgPointee<4>(keystore_sig), Return(OpResult())));
  EXPECT_EQ(Status::Success, Sign(kCertLabel, SHA1_RSA_PKCS, data, &sig));
  EXPECT_EQ("signature", sig);

  sig.clear();
  EXPECT_CALL(key_store_,
              Sign(GetTestKeyID(), kCertLabel, SHA256_RSA_PKCS, _, _))
      .WillOnce(DoAll(SetArgPointee<4>(keystore_sig), Return(OpResult())));
  EXPECT_EQ(Status::Success, Sign(kCertLabel, SHA256_RSA_PKCS, data, &sig));
  EXPECT_EQ("signature", sig);
}

// Checks that signing fails if there is no provisioned certificate.
TEST_F(CertProvisionTest, SignNotProvisioned) {
  ExpectProvisioned(false);
  std::string data = "some data";
  std::string sig;
  EXPECT_EQ(Status::NotProvisioned,
            Sign(kCertLabel, SHA1_RSA_PKCS, data, &sig));
  EXPECT_TRUE(sig.empty());
}

// Checks that signing fails if keystore Sign operation fails.
TEST_F(CertProvisionTest, SignFailure) {
  Provision();
  std::string data = "some data";
  std::string sig;
  EXPECT_CALL(key_store_, Sign(GetTestKeyID(), kCertLabel, SHA1_RSA_PKCS, _, _))
      .WillOnce(Return(TestError(Status::KeyStoreError)));
  EXPECT_NE(Status::Success, Sign(kCertLabel, SHA1_RSA_PKCS, data, &sig));
  EXPECT_TRUE(sig.empty());
}

// Checks that if a cert is provisioned for one label, it doesn't affect
// other labels.
TEST_F(CertProvisionTest, WrongLabel) {
  Provision();
  EXPECT_CALL(key_store_, ReadProvisionStatus(kWrongLabel, _))
      .WillRepeatedly(Return(OpResult()));
  EXPECT_CALL(key_store_, ReadProvisionStatus(kCertLabel, _)).Times(0);
  std::string certificate;
  EXPECT_EQ(Status::NotProvisioned,
            GetCertificate(kWrongLabel, true, &certificate));
  EXPECT_TRUE(certificate.empty());
  std::string data = "some data";
  std::string sig;
  EXPECT_EQ(Status::NotProvisioned,
            Sign(kWrongLabel, SHA1_RSA_PKCS, data, &sig));
  EXPECT_TRUE(sig.empty());
}

}  // namespace cert_provision
