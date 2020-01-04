// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/webauthn_handler.h"

#include <regex>  // NOLINT(build/c++11)
#include <string>
#include <utility>

#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libhwsec/mock_dbus_method_response.h"
#include "u2fd/mock_tpm_vendor_cmd.h"
#include "u2fd/mock_user_state.h"
#include "u2fd/util.h"

namespace u2f {
namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;

constexpr int kVerificationTimeoutMs = 10000;
constexpr int kVerificationRetryDelayUs = 500 * 1000;
constexpr int kMaxRetries =
    kVerificationTimeoutMs * 1000 / kVerificationRetryDelayUs;
constexpr uint32_t kCr50StatusSuccess = 0;
constexpr uint32_t kCr50StatusNotAllowed = 0x507;

// Dummy User State.
constexpr char kUserSecret[65] = {[0 ... 63] = 'E', '\0'};
// Dummy RP id.
constexpr char kRpId[] = "example.com";
const std::vector<uint8_t> kRpIdHash = util::Sha256(std::string(kRpId));

const std::string ExpectedU2fGenerateRequestRegex() {
  // See U2F_GENERATE_REQ in //platform/ec/include/u2f.h
  static const std::string request_regex =
      base::HexEncode(kRpIdHash.data(), kRpIdHash.size()) +  // AppId
      std::string("(EE){32}") +                              // User Secret
      std::string("03");                                     // U2F_AUTH_ENFORCE
  return request_regex;
}

// Dummy cr50 U2F_GENERATE_RESP.
const U2F_GENERATE_RESP kU2fGenerateResponse = {
    .pubKey = {.pointFormat = 0xAB,
               .x = {[0 ... 31] = 0xAB},
               .y = {[0 ... 31] = 0xAB}},
    .keyHandle = {[0 ... 63] = 0xFD}};

// AuthenticatorData field sizes, in bytes.
constexpr int kRpIdHashBytes = 32;
constexpr int kAuthenticatorDataFlagBytes = 1;
constexpr int kSignatureCounterBytes = 4;
constexpr int kAaguidBytes = 16;
constexpr int kCredentialIdLengthBytes = 2;

brillo::SecureBlob ArrayToSecureBlob(const char* array) {
  brillo::SecureBlob blob;
  CHECK(brillo::SecureBlob::HexStringToSecureBlob(array, &blob));
  return blob;
}

MATCHER_P(StructMatchesRegex, pattern, "") {
  std::string arg_hex = base::HexEncode(&arg, sizeof(arg));
  if (std::regex_match(arg_hex, std::regex(pattern))) {
    return true;
  }
  *result_listener << arg_hex << " did not match regex: " << pattern;
  return false;
}

}  // namespace

class WebAuthnHandlerTest : public ::testing::Test {
 public:
  void SetUp() override { CreateHandler(); }

  void TearDown() override {
    EXPECT_EQ(presence_requested_expected_, presence_requested_count_);
  }

 protected:
  void CreateHandler() {
    handler_.reset(new WebAuthnHandler());
    handler_->Initialize(&mock_tpm_proxy_, &mock_user_state_,
                         [this]() { presence_requested_count_++; });
  }

  void ExpectGetUserSecret() {
    EXPECT_CALL(mock_user_state_, GetUserSecret())
        .WillOnce(Return(ArrayToSecureBlob(kUserSecret)));
  }

  void ExpectGetUserSecretFails() {
    EXPECT_CALL(mock_user_state_, GetUserSecret())
        .WillOnce(Return(base::Optional<brillo::SecureBlob>()));
  }

  void CallAndWaitForPresence(std::function<uint32_t()> fn, uint32_t* status) {
    handler_->CallAndWaitForPresence(fn, status);
  }

  bool PresenceRequested() { return presence_requested_count_ > 0; }

  MakeCredentialResponse::MakeCredentialStatus DoU2fGenerate(
      PresenceRequirement presence_requirement,
      std::vector<uint8_t>* credential_id,
      std::vector<uint8_t>* credential_pubkey) {
    return handler_->DoU2fGenerate(kRpIdHash, presence_requirement,
                                   credential_id, credential_pubkey);
  }

  std::vector<uint8_t> MakeAuthenticatorData(
      const std::vector<uint8_t>& credential_id,
      const std::vector<uint8_t>& credential_public_key,
      bool user_verified,
      bool include_attested_credential_data) {
    return handler_->MakeAuthenticatorData(kRpIdHash, credential_id,
                                           credential_public_key, user_verified,
                                           include_attested_credential_data);
  }

  StrictMock<MockTpmVendorCommandProxy> mock_tpm_proxy_;
  StrictMock<MockUserState> mock_user_state_;

  std::unique_ptr<WebAuthnHandler> handler_;

  int presence_requested_expected_ = 0;

 private:
  int presence_requested_count_ = 0;
};

namespace {

TEST_F(WebAuthnHandlerTest, CallAndWaitForPresenceDirectSuccess) {
  uint32_t status = kCr50StatusNotAllowed;
  // If presence is already available, we won't request it.
  CallAndWaitForPresence([]() { return kCr50StatusSuccess; }, &status);
  EXPECT_EQ(status, kCr50StatusSuccess);
  presence_requested_expected_ = 0;
}

TEST_F(WebAuthnHandlerTest, CallAndWaitForPresenceRequestSuccess) {
  uint32_t status = kCr50StatusNotAllowed;
  CallAndWaitForPresence(
      [this]() {
        if (PresenceRequested())
          return kCr50StatusSuccess;
        return kCr50StatusNotAllowed;
      },
      &status);
  EXPECT_EQ(status, kCr50StatusSuccess);
  presence_requested_expected_ = 1;
}

TEST_F(WebAuthnHandlerTest, CallAndWaitForPresenceTimeout) {
  uint32_t status = kCr50StatusSuccess;
  base::TimeTicks verification_start = base::TimeTicks::Now();
  CallAndWaitForPresence([]() { return kCr50StatusNotAllowed; }, &status);
  EXPECT_GE(base::TimeTicks::Now() - verification_start,
            base::TimeDelta::FromMilliseconds(kVerificationTimeoutMs));
  EXPECT_EQ(status, kCr50StatusNotAllowed);
  presence_requested_expected_ = kMaxRetries;
}

TEST_F(WebAuthnHandlerTest, DoU2fGenerateNoSecret) {
  ExpectGetUserSecretFails();
  std::vector<uint8_t> cred_id, cred_pubkey;
  EXPECT_EQ(
      DoU2fGenerate(PresenceRequirement::kPowerButton, &cred_id, &cred_pubkey),
      MakeCredentialResponse::INTERNAL_ERROR);
}

TEST_F(WebAuthnHandlerTest, DoU2fGeneratePresenceNoPresence) {
  ExpectGetUserSecret();
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fGenerate(StructMatchesRegex(ExpectedU2fGenerateRequestRegex()), _))
      .WillRepeatedly(Return(kCr50StatusNotAllowed));
  std::vector<uint8_t> cred_id, cred_pubkey;
  EXPECT_EQ(
      DoU2fGenerate(PresenceRequirement::kPowerButton, &cred_id, &cred_pubkey),
      MakeCredentialResponse::VERIFICATION_FAILED);
  presence_requested_expected_ = kMaxRetries;
}

TEST_F(WebAuthnHandlerTest, DoU2fGeneratePresenceSuccess) {
  ExpectGetUserSecret();
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fGenerate(StructMatchesRegex(ExpectedU2fGenerateRequestRegex()), _))
      .WillOnce(Return(kCr50StatusNotAllowed))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fGenerateResponse),
                      Return(kCr50StatusSuccess)));
  std::vector<uint8_t> cred_id, cred_pubkey;
  EXPECT_EQ(
      DoU2fGenerate(PresenceRequirement::kPowerButton, &cred_id, &cred_pubkey),
      MakeCredentialResponse::SUCCESS);
  EXPECT_EQ(cred_id, std::vector<uint8_t>(64, 0xFD));
  EXPECT_EQ(cred_pubkey, std::vector<uint8_t>(65, 0xAB));
  presence_requested_expected_ = 1;
}

TEST_F(WebAuthnHandlerTest, MakeCredentialUninitialized) {
  // Use an uninitialized WebAuthnHandler object.
  handler_.reset(new WebAuthnHandler());
  auto mock_method_response =
      std::make_unique<hwsec::MockDBusMethodResponse<MakeCredentialResponse>>();
  mock_method_response->set_return_callback(
      base::Bind([](const MakeCredentialResponse& resp) {
        EXPECT_EQ(resp.status(), MakeCredentialResponse::INTERNAL_ERROR);
      }));

  MakeCredentialRequest request;
  handler_->MakeCredential(std::move(mock_method_response), request);
}

TEST_F(WebAuthnHandlerTest, MakeCredentialEmptyRpId) {
  auto mock_method_response =
      std::make_unique<hwsec::MockDBusMethodResponse<MakeCredentialResponse>>();
  mock_method_response->set_return_callback(
      base::Bind([](const MakeCredentialResponse& resp) {
        EXPECT_EQ(resp.status(), MakeCredentialResponse::INVALID_REQUEST);
      }));

  MakeCredentialRequest request;
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);
  handler_->MakeCredential(std::move(mock_method_response), request);
}

TEST_F(WebAuthnHandlerTest, MakeCredentialNoSecret) {
  MakeCredentialRequest request;
  request.set_rp_id(kRpId);
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);

  ExpectGetUserSecretFails();
  auto mock_method_response =
      std::make_unique<hwsec::MockDBusMethodResponse<MakeCredentialResponse>>();
  mock_method_response->set_return_callback(
      base::Bind([](const MakeCredentialResponse& resp) {
        EXPECT_EQ(resp.status(), MakeCredentialResponse::INTERNAL_ERROR);
      }));

  handler_->MakeCredential(std::move(mock_method_response), request);
}

TEST_F(WebAuthnHandlerTest, MakeCredentialPresenceNoPresence) {
  MakeCredentialRequest request;
  request.set_rp_id(kRpId);
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);

  ExpectGetUserSecret();
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fGenerate(StructMatchesRegex(ExpectedU2fGenerateRequestRegex()), _))
      .WillRepeatedly(Return(kCr50StatusNotAllowed));

  auto mock_method_response =
      std::make_unique<hwsec::MockDBusMethodResponse<MakeCredentialResponse>>();
  mock_method_response->set_return_callback(
      base::Bind([](const MakeCredentialResponse& resp) {
        EXPECT_EQ(resp.status(), MakeCredentialResponse::VERIFICATION_FAILED);
      }));

  handler_->MakeCredential(std::move(mock_method_response), request);
  presence_requested_expected_ = kMaxRetries;
}

TEST_F(WebAuthnHandlerTest, MakeCredentialPresenceSuccess) {
  MakeCredentialRequest request;
  request.set_rp_id(kRpId);
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);

  ExpectGetUserSecret();
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fGenerate(StructMatchesRegex(ExpectedU2fGenerateRequestRegex()), _))
      .WillOnce(Return(kCr50StatusNotAllowed))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fGenerateResponse),
                      Return(kCr50StatusSuccess)));

  auto mock_method_response =
      std::make_unique<hwsec::MockDBusMethodResponse<MakeCredentialResponse>>();
  mock_method_response->set_return_callback(
      base::Bind([](const MakeCredentialResponse& resp) {
        EXPECT_EQ(resp.status(), MakeCredentialResponse::SUCCESS);
        // TODO(yichengli): Check resp.authenticator_data() once it's formatted
        // correctly (with CBOR encoding).
        EXPECT_EQ(resp.attestation_format(), "none");
        EXPECT_EQ(resp.attestation_statement(), "\xa0");
      }));

  handler_->MakeCredential(std::move(mock_method_response), request);
  presence_requested_expected_ = 1;
}

TEST_F(WebAuthnHandlerTest, MakeAuthenticatorDataWithAttestedCredData) {
  const std::vector<uint8_t> cred_id(64, 0xAA);
  const std::vector<uint8_t> cred_pubkey(65, 0xBB);

  std::vector<uint8_t> authenticator_data =
      MakeAuthenticatorData(cred_id, cred_pubkey, /* user_verified = */ false,
                            /* include_attested_credential_data = */ true);
  EXPECT_EQ(authenticator_data.size(),
            kRpIdHashBytes + kAuthenticatorDataFlagBytes +
                kSignatureCounterBytes + kAaguidBytes +
                kCredentialIdLengthBytes + cred_id.size() + cred_pubkey.size());

  const std::string rp_id_hash_hex =
      base::HexEncode(kRpIdHash.data(), kRpIdHash.size());
  const std::string expected_authenticator_data_regex =
      rp_id_hash_hex +  // RP ID hash
      std::string(
          "41"          // Flag: user present, attested credential data included
          "(..){4}"     // Signature counter
          "(00){16}"    // AAGUID
          "0040"        // Credential ID length
          "(AA){64}"    // Credential ID
          "(BB){65}");  // Credential public key
  EXPECT_TRUE(std::regex_match(
      base::HexEncode(authenticator_data.data(), authenticator_data.size()),
      std::regex(expected_authenticator_data_regex)));
}

TEST_F(WebAuthnHandlerTest, MakeAuthenticatorDataNoAttestedCredData) {
  std::vector<uint8_t> authenticator_data =
      MakeAuthenticatorData(std::vector<uint8_t>(), std::vector<uint8_t>(),
                            /* user_verified = */ false,
                            /* include_attested_credential_data = */ false);
  EXPECT_EQ(
      authenticator_data.size(),
      kRpIdHashBytes + kAuthenticatorDataFlagBytes + kSignatureCounterBytes);

  const std::string rp_id_hash_hex =
      base::HexEncode(kRpIdHash.data(), kRpIdHash.size());
  const std::string expected_authenticator_data_regex =
      rp_id_hash_hex +  // RP ID hash
      std::string(
          "01"          // Flag: user present
          "(..){4}");   // Signature counter
  EXPECT_TRUE(std::regex_match(
      base::HexEncode(authenticator_data.data(), authenticator_data.size()),
      std::regex(expected_authenticator_data_regex)));
}

}  // namespace
}  // namespace u2f
