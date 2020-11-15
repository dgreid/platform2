// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <tuple>
#include <vector>

// This group goes first so the next group can see the needed definitions.
#include <attestation/proto_bindings/interface.pb.h>

#include <attestation-client-test/attestation/dbus-proxy-mocks.h>
#include <base/bind.h>
#include <base/optional.h>
#include <chaps/attributes.h>
#include <chaps/chaps_proxy_mock.h>
#include <crypto/scoped_openssl_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include "cert/cert_provision.pb.h"
#include "cryptohome/cert/cert_provision_keystore.h"
#include "cryptohome/cert/cert_provision_util.h"
#include "cryptohome/cert/mock_cert_provision_keystore.h"
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
using ::testing::StrictMock;

namespace {

// Some arbitrary certificate label used for testing.
const char kCertLabel[] = "test";
const char kWrongLabel[] = "some wrong label";
const char kFakeErrorMessage[] = "fake error message";

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

namespace {
class RecordingAttestationProxy : public org::chromium::AttestationProxyMock {
 public:
  struct ReplySource {
    attestation::GetStatusReply get_status_reply;
    attestation::EnrollReply enroll_reply;
    attestation::GetCertificateReply get_cert_reply;
    attestation::RegisterKeyWithChapsTokenReply register_key_reply;
  };
  struct ErrorSource {
    brillo::ErrorPtr get_status_error;
    brillo::ErrorPtr enroll_error;
    brillo::ErrorPtr get_cert_error;
    brillo::ErrorPtr register_key_error;
  };
  struct RequestSink {
    attestation::GetStatusRequest get_status_request;
    attestation::EnrollRequest enroll_request;
    attestation::GetCertificateRequest get_cert_request;
    attestation::RegisterKeyWithChapsTokenRequest register_key_request;
  };

 public:
  RecordingAttestationProxy(ReplySource* reply_source,
                            ErrorSource* error_source,
                            RequestSink* request_sink)
      : reply_source_(reply_source),
        error_source_(error_source),
        request_sink_(request_sink) {
    ON_CALL(*this, GetStatus(_, _, _, _))
        .WillByDefault(
            Invoke(this, &RecordingAttestationProxy::HandleGetStatus));
    ON_CALL(*this, Enroll(_, _, _, _))
        .WillByDefault(Invoke(this, &RecordingAttestationProxy::HandleEnroll));
    ON_CALL(*this, GetCertificate(_, _, _, _))
        .WillByDefault(
            Invoke(this, &RecordingAttestationProxy::HandleGetCertificate));
    ON_CALL(*this, RegisterKeyWithChapsToken(_, _, _, _))
        .WillByDefault(Invoke(
            this, &RecordingAttestationProxy::HandleRegisterKeyWithChapsToken));
  }

  bool HandleGetStatus(const attestation::GetStatusRequest& request,
                       attestation::GetStatusReply* reply,
                       brillo::ErrorPtr* error,
                       int /*timeout_ms*/) {
    if (error_source_->get_status_error) {
      *error = std::move(error_source_->get_status_error);
      return false;
    }
    request_sink_->get_status_request = request;
    *reply = reply_source_->get_status_reply;
    return true;
  }
  bool HandleEnroll(const attestation::EnrollRequest& request,
                    attestation::EnrollReply* reply,
                    brillo::ErrorPtr* error,
                    int /*timeout_ms*/) {
    if (error_source_->enroll_error) {
      *error = std::move(error_source_->enroll_error);
      return false;
    }
    request_sink_->enroll_request = request;
    *reply = reply_source_->enroll_reply;
    return true;
  }
  bool HandleGetCertificate(const attestation::GetCertificateRequest& request,
                            attestation::GetCertificateReply* reply,
                            brillo::ErrorPtr* error,
                            int /*timeout_ms*/) {
    if (error_source_->get_cert_error) {
      *error = std::move(error_source_->get_cert_error);
      return false;
    }
    request_sink_->get_cert_request = request;
    *reply = reply_source_->get_cert_reply;
    return true;
  }
  bool HandleRegisterKeyWithChapsToken(
      const attestation::RegisterKeyWithChapsTokenRequest& request,
      attestation::RegisterKeyWithChapsTokenReply* reply,
      brillo::ErrorPtr* error,
      int /*timeout_ms*/) {
    if (error_source_->register_key_error) {
      *error = std::move(error_source_->register_key_error);
      return false;
    }
    request_sink_->register_key_request = request;
    *reply = reply_source_->register_key_reply;
    return true;
  }
  ReplySource* const reply_source_;
  ErrorSource* const error_source_;
  RequestSink* const request_sink_;
};

class FakeAttestationProxyFactory : public AttestationProxyFactory {
 public:
  FakeAttestationProxyFactory() {
    ReiniailizeProxyObject();
    DeferToFake(this);
  }
  ~FakeAttestationProxyFactory() override { DeferToFake(nullptr); }
  std::unique_ptr<org::chromium::AttestationProxyInterface> CreateObject()
      override {
    AssertProxyNotTaken();
    return std::move(mock_proxy_to_transfer_);
  }
  RecordingAttestationProxy::ReplySource* get_reply_source() {
    return &reply_source_;
  }
  RecordingAttestationProxy::ErrorSource* get_error_source() {
    return &error_source_;
  }
  RecordingAttestationProxy::RequestSink* get_request_sink() {
    return &request_sink_;
  }
  RecordingAttestationProxy* get_mock_proxy() { return mock_proxy_; }
  void ReiniailizeProxyObject() {
    mock_proxy_to_transfer_ =
        std::make_unique<StrictMock<RecordingAttestationProxy>>(
            &reply_source_, &error_source_, &request_sink_);
    mock_proxy_ = mock_proxy_to_transfer_.get();
  }

 private:
  void AssertProxyNotTaken() const {
    ASSERT_NE(mock_proxy_to_transfer_.get(), nullptr);
  }
  RecordingAttestationProxy::ReplySource reply_source_;
  RecordingAttestationProxy::ErrorSource error_source_;
  RecordingAttestationProxy::RequestSink request_sink_;
  std::unique_ptr<StrictMock<RecordingAttestationProxy>>
      mock_proxy_to_transfer_;
  RecordingAttestationProxy* mock_proxy_;
};

}  // namespace

// Test class for testing top-level functions.
class CertProvisionTest : public testing::Test {
 public:
  CertProvisionTest() { KeyStore::subst_obj = &key_store_; }
  CertProvisionTest(const CertProvisionTest&) = delete;
  CertProvisionTest& operator=(const CertProvisionTest&) = delete;

  ~CertProvisionTest() { KeyStore::subst_obj = nullptr; }

  void SetUp() {
    attestation_proxy_factory_.get_reply_source()
        ->get_cert_reply.set_public_key(GetTestPublicKey().to_string());

    ON_CALL(key_store_, Init()).WillByDefault(Return(OpResult()));
    ON_CALL(key_store_, Sign(_, _, _, _, _)).WillByDefault(Return(OpResult()));
    ON_CALL(key_store_, ReadProvisionStatus(_, _))
        .WillByDefault(
            Invoke([this](const std::string& /* label */, MessageLite* proto) {
              proto->ParseFromString(provision_status_.SerializeAsString());
              return OpResult();
            }));
    ON_CALL(key_store_, WriteProvisionStatus(_, _))
        .WillByDefault(Invoke(
            [this](const std::string& /* label */, const MessageLite& proto) {
              provision_status_.ParseFromString(proto.SerializeAsString());
              return OpResult();
            }));
    ON_CALL(key_store_, DeleteKeys(_, _)).WillByDefault(Return(OpResult()));
  }

  OpResult TestError(Status status) { return {status, "Test error"}; }

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
              ProvisionCertificate(
                  PCAType::kDefaultPCA, std::string(), kCertLabel,
                  CertificateProfile::CAST_CERTIFICATE, GetProgressCallback()));
    ExpectProvisioned(true);
    EXPECT_EQ(GetTestKeyID(), provision_status_.key_id());
  }

  // Perform the same thing as `Provision()` does with expectations to
  // attestation proxy, which we don't really care about if it's just in order
  // to set provision state to "provisioned". Also, at the end of the function
  // the proxy object gets reset in the factory so the test body doesn't have to
  // do it.
  void SetupProvisionState() {
    InitializeAttestationStatus(/*is_prepared=*/true, /*is_enrolled=*/false);
    EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
                GetStatus(_, _, _, _));
    EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
                GetCertificate(_, _, _, _));
    EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
                RegisterKeyWithChapsToken(_, _, _, _));
    // We don't share the code with `Provision()` to get better verbosity when
    // gmock reports unsatisfied expectations.
    EXPECT_EQ(Status::Success,
              ProvisionCertificate(
                  PCAType::kDefaultPCA, std::string(), kCertLabel,
                  CertificateProfile::CAST_CERTIFICATE, GetProgressCallback()));
    ExpectProvisioned(true);
    EXPECT_EQ(GetTestKeyID(), provision_status_.key_id());
    attestation_proxy_factory_.ReiniailizeProxyObject();
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
  void ResetObtainedTestKey() { rsa_.reset(); }
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
  std::string GetTestKeyID() { return GetKeyID(GetTestPublicKey()); }

  void InitializeAttestationStatus(bool is_prepared, bool is_enrolled) {
    attestation_proxy_factory_.get_reply_source()
        ->get_status_reply.set_prepared_for_enrollment(is_prepared);
    attestation_proxy_factory_.get_reply_source()
        ->get_status_reply.set_enrolled(is_enrolled);
  }

  // Captures progress reported through callback.
  std::vector<Progress> progress_;

  // IsPreparedForEnrollment and IsEnrolled status.
  bool prepared_ = true;
  bool enrolled_ = false;

  // Emulated storage for ProvisionStatus in keystore.
  ProvisionStatus provision_status_;

  FakeAttestationProxyFactory attestation_proxy_factory_;

  NiceMock<MockKeyStore> key_store_;

 private:
  void CaptureProgress(Status status,
                       int progress,
                       const std::string& message) {
    progress_.push_back({status, progress, message});
  }

  crypto::ScopedRSA rsa_;
};

// Checks that provisioning succeeds after sending EnrollRequest if not
// enrolled yet. Checks that the reported progress is not decreasing and
// ends with 100%, and success is reported to the callback on all steps.
TEST_F(CertProvisionTest, ProvisionCertificateSuccessEnroll) {
  ExpectProvisioned(false);

  InitializeAttestationStatus(/*is_prepared=*/true, /*is_enrolled=*/false);

  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetStatus(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetCertificate(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              RegisterKeyWithChapsToken(_, _, _, _));
  EXPECT_EQ(Status::Success,
            ProvisionCertificate(
                PCAType::kDefaultPCA, std::string(), kCertLabel,
                CertificateProfile::CAST_CERTIFICATE, GetProgressCallback()));

  int last_progress = 0;
  for (auto p : progress_) {
    EXPECT_EQ(Status::Success, p.status);
    EXPECT_LE(last_progress, p.progress);
    last_progress = p.progress;
  }
  EXPECT_EQ(100, last_progress);
  ExpectProvisioned(true);

  // Verify if the recorded requests meet expectations.
  EXPECT_TRUE(attestation_proxy_factory_.get_request_sink()
                  ->get_cert_request.username()
                  .empty());
  EXPECT_TRUE(attestation_proxy_factory_.get_request_sink()
                  ->get_cert_request.request_origin()
                  .empty());
  EXPECT_TRUE(
      attestation_proxy_factory_.get_request_sink()->get_cert_request.forced());
  EXPECT_TRUE(attestation_proxy_factory_.get_request_sink()
                  ->get_cert_request.shall_trigger_enrollment());
  EXPECT_EQ(kCertLabel, attestation_proxy_factory_.get_request_sink()
                            ->get_cert_request.key_label());
  EXPECT_EQ(::attestation::CAST_CERTIFICATE,
            attestation_proxy_factory_.get_request_sink()
                ->get_cert_request.certificate_profile());
  EXPECT_EQ(::attestation::DEFAULT_ACA,
            attestation_proxy_factory_.get_request_sink()
                ->get_cert_request.aca_type());

  // Also, verify the right key is registered.
  EXPECT_TRUE(attestation_proxy_factory_.get_request_sink()
                  ->register_key_request.username()
                  .empty());
  EXPECT_EQ(kCertLabel, attestation_proxy_factory_.get_request_sink()
                            ->register_key_request.key_label());
}

// Checks that if enrollment is not prepared, provisioning fails.
TEST_F(CertProvisionTest, ProvisionCertificateNotPrepared) {
  ExpectProvisioned(false);

  InitializeAttestationStatus(/*is_prepared=*/false, /*is_enrolled=*/false);

  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetStatus(_, _, _, _));

  EXPECT_EQ(Status::NotPrepared,
            ProvisionCertificate(
                PCAType::kDefaultPCA, std::string(), kCertLabel,
                CertificateProfile::CAST_CERTIFICATE, GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsIn(Status::NotPrepared));
  ExpectProvisioned(false);
}

TEST_F(CertProvisionTest, ProvisionCertificateDBusErrorGetStatus) {
  ExpectProvisioned(false);

  brillo::Error::AddTo(
      &attestation_proxy_factory_.get_error_source()->get_status_error,
      FROM_HERE, "", "", kFakeErrorMessage);
  const std::string exepcted_error_message =
      attestation_proxy_factory_.get_error_source()
          ->get_status_error->GetMessage();

  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetStatus(_, _, _, _));

  EXPECT_NE(Status::Success,
            ProvisionCertificate(
                PCAType::kDefaultPCA, std::string(), kCertLabel,
                CertificateProfile::CAST_CERTIFICATE, GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsIn(Status::DBusError));
  ExpectProvisioned(false);
  EXPECT_EQ(exepcted_error_message, progress_.back().message);
}

// Checks that a failure in CertRequest is handled properly.
TEST_F(CertProvisionTest, ProvisionCertificateFailureCert) {
  ExpectProvisioned(false);
  InitializeAttestationStatus(/*is_prepared=*/true, /*is_enrolled=*/false);

  attestation_proxy_factory_.get_reply_source()->get_cert_reply.set_status(
      ::attestation::STATUS_UNEXPECTED_DEVICE_ERROR);
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetStatus(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetCertificate(_, _, _, _));

  EXPECT_NE(Status::Success,
            ProvisionCertificate(
                PCAType::kDefaultPCA, std::string(), kCertLabel,
                CertificateProfile::CAST_CERTIFICATE, GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsNotIn(Status::Success));
  ExpectProvisioned(false);
}

TEST_F(CertProvisionTest, ProvisionCertificateDBusErrorCert) {
  ExpectProvisioned(false);
  InitializeAttestationStatus(/*is_prepared=*/true, /*is_enrolled=*/false);

  brillo::Error::AddTo(
      &attestation_proxy_factory_.get_error_source()->get_cert_error, FROM_HERE,
      "", "", kFakeErrorMessage);
  const std::string exepcted_error_message =
      attestation_proxy_factory_.get_error_source()
          ->get_cert_error->GetMessage();
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetStatus(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetCertificate(_, _, _, _));

  EXPECT_EQ(Status::DBusError,
            ProvisionCertificate(
                PCAType::kDefaultPCA, std::string(), kCertLabel,
                CertificateProfile::CAST_CERTIFICATE, GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsIn(Status::DBusError));
  ExpectProvisioned(false);
  EXPECT_EQ(exepcted_error_message, progress_.back().message);
}

// Checks that a failure when registering the keys is handled properly.
TEST_F(CertProvisionTest, ProvisionCertificateDBusErrorRegister) {
  ExpectProvisioned(false);
  InitializeAttestationStatus(/*is_prepared=*/true, /*is_enrolled=*/false);

  brillo::Error::AddTo(
      &attestation_proxy_factory_.get_error_source()->register_key_error,
      FROM_HERE, "", "", kFakeErrorMessage);
  const std::string exepcted_error_message =
      attestation_proxy_factory_.get_error_source()
          ->register_key_error->GetMessage();
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetStatus(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetCertificate(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              RegisterKeyWithChapsToken(_, _, _, _));

  EXPECT_EQ(Status::DBusError,
            ProvisionCertificate(
                PCAType::kDefaultPCA, std::string(), kCertLabel,
                CertificateProfile::CAST_CERTIFICATE, GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsIn(Status::DBusError));
  ExpectProvisioned(false);
  EXPECT_EQ(exepcted_error_message, progress_.back().message);
}

TEST_F(CertProvisionTest, ProvisionCertificateFailureRegister) {
  ExpectProvisioned(false);
  InitializeAttestationStatus(/*is_prepared=*/true, /*is_enrolled=*/false);

  attestation_proxy_factory_.get_reply_source()->register_key_reply.set_status(
      ::attestation::STATUS_UNEXPECTED_DEVICE_ERROR);
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetStatus(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetCertificate(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              RegisterKeyWithChapsToken(_, _, _, _));

  EXPECT_NE(Status::Success,
            ProvisionCertificate(
                PCAType::kDefaultPCA, std::string(), kCertLabel,
                CertificateProfile::CAST_CERTIFICATE, GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsNotIn(Status::Success));
  ExpectProvisioned(false);
}

// Checks that a failure when accessing the keystore is handled properly.
TEST_F(CertProvisionTest, ProvisionCertificateFailureKeyStore) {
  ExpectProvisioned(false);
  InitializeAttestationStatus(/*is_prepared=*/true, /*is_enrolled=*/false);

  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetStatus(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetCertificate(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              RegisterKeyWithChapsToken(_, _, _, _));

  EXPECT_CALL(key_store_, Init())
      .WillOnce(Return(TestError(Status::KeyStoreError)))
      .WillRepeatedly(Return(OpResult()));
  EXPECT_NE(Status::Success,
            ProvisionCertificate(
                PCAType::kDefaultPCA, std::string(), kCertLabel,
                CertificateProfile::CAST_CERTIFICATE, GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsNotIn(Status::Success));
  EXPECT_EQ("Test error", progress_.back().message);
  ExpectProvisioned(false);
}

// Checks that re-provisioning the certificate deletes the old keys and
// replaces the cert with the new one.
TEST_F(CertProvisionTest, ReProvisionCertificateSuccess) {
  SetupProvisionState();
  std::string old_key_id = provision_status_.key_id();
  ResetObtainedTestKey();

  attestation_proxy_factory_.get_reply_source()->get_cert_reply.set_public_key(
      GetTestPublicKey().to_string());

  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetStatus(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetCertificate(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              RegisterKeyWithChapsToken(_, _, _, _));

  EXPECT_CALL(key_store_, DeleteKeys(old_key_id, kCertLabel));
  Provision();
  EXPECT_NE(old_key_id, provision_status_.key_id());
}

// Checks that registration failure upon re-provisioning keeps the old cert
// in place.
TEST_F(CertProvisionTest, ReProvisionCertificateFailureRegister) {
  SetupProvisionState();
  std::string old_key_id = provision_status_.key_id();
  ResetObtainedTestKey();

  attestation_proxy_factory_.get_reply_source()->get_cert_reply.set_public_key(
      GetTestPublicKey().to_string());

  attestation_proxy_factory_.get_reply_source()->register_key_reply.set_status(
      ::attestation::STATUS_UNEXPECTED_DEVICE_ERROR);
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetStatus(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetCertificate(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              RegisterKeyWithChapsToken(_, _, _, _));

  EXPECT_CALL(key_store_, DeleteKeys(_, _)).Times(0);
  EXPECT_NE(Status::Success,
            ProvisionCertificate(
                PCAType::kDefaultPCA, std::string(), kCertLabel,
                CertificateProfile::CAST_CERTIFICATE, GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsNotIn(Status::Success));
  ExpectProvisioned(true);
  EXPECT_EQ(old_key_id, provision_status_.key_id());
}

// Checks that a failure when deleting the old keys is reported even
// though the new cert is stored. Checks that the new cert is usable,
// if the old keys were not deleted.
TEST_F(CertProvisionTest, ReProvisionCertificateFailureDeleteKeys) {
  SetupProvisionState();
  std::string old_key_id = provision_status_.key_id();
  ResetObtainedTestKey();

  attestation_proxy_factory_.get_reply_source()->get_cert_reply.set_public_key(
      GetTestPublicKey().to_string());

  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetStatus(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetCertificate(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              RegisterKeyWithChapsToken(_, _, _, _));

  EXPECT_CALL(key_store_, DeleteKeys(old_key_id, kCertLabel))
      .WillOnce(Return(TestError(Status::KeyStoreError)));
  EXPECT_NE(Status::Success,
            ProvisionCertificate(
                PCAType::kDefaultPCA, std::string(), kCertLabel,
                CertificateProfile::CAST_CERTIFICATE, GetProgressCallback()));
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
  InitializeAttestationStatus(/*is_prepared=*/true, /*is_enrolled=*/false);
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetStatus(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetCertificate(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              RegisterKeyWithChapsToken(_, _, _, _));
  attestation_proxy_factory_.get_reply_source()->get_cert_reply.set_certificate(
      cert_chain);

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
  SetupProvisionState();

  std::string data = "some data";
  std::string keystore_sig("signature");

  std::string sig;
  EXPECT_CALL(key_store_, Sign(GetTestKeyID(), kCertLabel, SHA1_RSA_PKCS, _, _))
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
  SetupProvisionState();
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
  SetupProvisionState();
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

TEST_F(CertProvisionTest, ForceEnroll) {
  ExpectProvisioned(false);

  InitializeAttestationStatus(/*is_prepared=*/true, /*is_enrolled=*/true);

  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetStatus(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(), Enroll(_, _, _, _));
  EXPECT_EQ(Status::Success, ForceEnroll(PCAType::kDefaultPCA, std::string(),
                                         GetProgressCallback()));

  int last_progress = 0;
  for (auto p : progress_) {
    EXPECT_EQ(Status::Success, p.status);
    EXPECT_LE(last_progress, p.progress);
    last_progress = p.progress;
  }
  EXPECT_EQ(100, last_progress);

  // Verify if the recorded requests meet expectations.
  EXPECT_TRUE(
      attestation_proxy_factory_.get_request_sink()->enroll_request.forced());
}

TEST_F(CertProvisionTest, ForceEnrollNotPrepared) {
  ExpectProvisioned(false);

  InitializeAttestationStatus(/*is_prepared=*/false, /*is_enrolled=*/false);

  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetStatus(_, _, _, _));

  EXPECT_EQ(
      Status::NotPrepared,
      ForceEnroll(PCAType::kDefaultPCA, std::string(), GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsIn(Status::NotPrepared));
}

TEST_F(CertProvisionTest, ForceEnrollDBusErrorGetStatus) {
  ExpectProvisioned(false);

  brillo::Error::AddTo(
      &attestation_proxy_factory_.get_error_source()->get_status_error,
      FROM_HERE, "", "", kFakeErrorMessage);
  const std::string exepcted_error_message =
      attestation_proxy_factory_.get_error_source()
          ->get_status_error->GetMessage();

  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetStatus(_, _, _, _));

  EXPECT_EQ(Status::DBusError, ForceEnroll(PCAType::kDefaultPCA, std::string(),
                                           GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsIn(Status::DBusError));
  EXPECT_EQ(exepcted_error_message, progress_.back().message);
}

TEST_F(CertProvisionTest, ForceEnrollFailure) {
  ExpectProvisioned(false);
  InitializeAttestationStatus(/*is_prepared=*/true, /*is_enrolled=*/true);

  attestation_proxy_factory_.get_reply_source()->enroll_reply.set_status(
      ::attestation::STATUS_UNEXPECTED_DEVICE_ERROR);
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetStatus(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(), Enroll(_, _, _, _));

  EXPECT_NE(Status::Success, ForceEnroll(PCAType::kDefaultPCA, std::string(),
                                         GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsNotIn(Status::Success));
}

TEST_F(CertProvisionTest, ForceEnrollDBusError) {
  ExpectProvisioned(false);
  InitializeAttestationStatus(/*is_prepared=*/true, /*is_enrolled=*/true);

  brillo::Error::AddTo(
      &attestation_proxy_factory_.get_error_source()->enroll_error, FROM_HERE,
      "", "", kFakeErrorMessage);
  const std::string exepcted_error_message =
      attestation_proxy_factory_.get_error_source()->enroll_error->GetMessage();
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(),
              GetStatus(_, _, _, _));
  EXPECT_CALL(*attestation_proxy_factory_.get_mock_proxy(), Enroll(_, _, _, _));

  EXPECT_EQ(Status::DBusError, ForceEnroll(PCAType::kDefaultPCA, std::string(),
                                           GetProgressCallback()));
  EXPECT_THAT(progress_, ResultsIn(Status::DBusError));
  EXPECT_EQ(exepcted_error_message, progress_.back().message);
}

}  // namespace cert_provision
