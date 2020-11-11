// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/webauthn_handler.h"

#include <regex>  // NOLINT(build/c++11)
#include <string>
#include <utility>

#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "u2fd/mock_tpm_vendor_cmd.h"
#include "u2fd/mock_user_state.h"
#include "u2fd/mock_webauthn_storage.h"
#include "u2fd/util.h"

namespace u2f {
namespace {

using ::brillo::dbus_utils::MockDBusMethodResponse;

using ::testing::_;
using ::testing::Matcher;
using ::testing::MatchesRegex;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;

constexpr int kVerificationTimeoutMs = 10000;
constexpr int kVerificationRetryDelayUs = 500 * 1000;
constexpr int kMaxRetries =
    kVerificationTimeoutMs * 1000 / kVerificationRetryDelayUs;
constexpr uint32_t kCr50StatusSuccess = 0;
constexpr uint32_t kCr50StatusNotAllowed = 0x507;
constexpr uint32_t kCr50StatusPasswordRequired = 0x50a;

// Dummy User State.
constexpr char kCredentialSecret[65] = {[0 ... 63] = 'E', '\0'};
// Dummy RP id.
constexpr char kRpId[] = "example.com";
const std::vector<uint8_t> kRpIdHash = util::Sha256(std::string(kRpId));
// Dummy key handle (credential ID).
const std::vector<uint8_t> kKeyHandle(sizeof(struct u2f_key_handle), 0xab);
// Dummy hash to sign.
const std::vector<uint8_t> kHashToSign(U2F_P256_SIZE, 0xcd);

const std::string ExpectedUserPresenceU2fGenerateRequestRegex() {
  // See U2F_GENERATE_REQ in //platform/ec/include/u2f.h
  static const std::string request_regex =
      base::HexEncode(kRpIdHash.data(), kRpIdHash.size()) +  // AppId
      std::string("[A-F0-9]{64}") +  // Credential Secret
      std::string("0B") +            // U2F_UV_ENABLED_KH | U2F_AUTH_ENFORCE
      std::string("(12){32}");       // Auth time secret hash
  return request_regex;
}

const std::string ExpectedUserVerificationU2fGenerateRequestRegex() {
  // See U2F_GENERATE_REQ in //platform/ec/include/u2f.h
  static const std::string request_regex =
      base::HexEncode(kRpIdHash.data(), kRpIdHash.size()) +  // AppId
      std::string("[A-F0-9]{64}") +  // Credential Secret
      std::string("08") +            // U2F_UV_ENABLED_KH
      std::string("(12){32}");       // Auth time secret hash
  return request_regex;
}

// Only used to test DoU2fSign, where the hash to sign can be determined.
const std::string ExpectedDeterministicU2fSignRequestRegex() {
  // See U2F_SIGN_REQ in //platform/ec/include/u2f.h
  static const std::string request_regex =
      base::HexEncode(kRpIdHash.data(), kRpIdHash.size()) +  // AppId
      std::string("(EE){32}") +  // Credential Secret
      std::string("(AB){64}") +  // Key handle
      std::string("(CD){32}") +  // Hash to sign
      std::string("03");         // U2F_AUTH_ENFORCE
  return request_regex;
}

const std::string ExpectedU2fSignRequestRegex() {
  // See U2F_SIGN_REQ in //platform/ec/include/u2f.h
  static const std::string request_regex =
      base::HexEncode(kRpIdHash.data(), kRpIdHash.size()) +  // AppId
      std::string("(EE){32}") +                              // User Secret
      std::string("(AB){64}") +                              // Key handle
      // Hash_to_sign depends on signature counter which isn't deterministic
      std::string("[A-F0-9]{64}") +  // Hash to sign
      std::string("03");             // U2F_AUTH_ENFORCE
  return request_regex;
}

const std::string ExpectedU2fSignCheckOnlyRequestRegex() {
  // See U2F_SIGN_REQ in //platform/ec/include/u2f.h
  static const std::string request_regex =
      base::HexEncode(kRpIdHash.data(), kRpIdHash.size()) +  // AppId
      std::string("(EE){32}") +                              // User Secret
      std::string("(AB){64}") +                              // Key handle
      std::string("(00){32}") +  // Hash to sign (empty)
      std::string("07");         // U2F_AUTH_CHECK_ONLY
  return request_regex;
}

// Dummy cr50 U2F_GENERATE_RESP.
const struct u2f_generate_versioned_resp kU2fGenerateVersionedResponse = {
    .pubKey = {.pointFormat = 0xAB,
               .x = {[0 ... 31] = 0xAB},
               .y = {[0 ... 31] = 0xAB}},
    .keyHandle = {.header = {.version = 0xFD,
                             .origin_seed = {[0 ... 31] = 0xFD},
                             .kh_hmac = {[0 ... 31] = 0xFD}},
                  .authorization_salt = {[0 ... 15] = 0xFD},
                  .authorization_hmac = {[0 ... 31] = 0xFD}}};

// Dummy cr50 U2F_SIGN_RESP.
const struct u2f_sign_resp kU2fSignResponse = {.sig_r = {[0 ... 31] = 0x12},
                                               .sig_s = {[0 ... 31] = 0x34}};

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
  void SetUp() override {
    PrepareMockBus();
    CreateHandler();
    PrepareMockStorage();
    // We use per-credential secret instead of the old user secret.
    ExpectNoGetUserSecret();
  }

  void TearDown() override {
    if (presence_requested_expected_ == kMaxRetries) {
      // Due to clock and scheduling variances, the actual retries before
      // timeout could be one less.
      EXPECT_TRUE(presence_requested_count_ == kMaxRetries ||
                  presence_requested_count_ == kMaxRetries - 1);
    } else {
      EXPECT_EQ(presence_requested_expected_, presence_requested_count_);
    }
  }

 protected:
  void PrepareMockBus() {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    mock_bus_ = new dbus::MockBus(options);

    mock_auth_dialog_proxy_ = new dbus::MockObjectProxy(
        mock_bus_.get(), chromeos::kUserAuthenticationServiceName,
        dbus::ObjectPath(chromeos::kUserAuthenticationServicePath));

    // Set an expectation so that the MockBus will return our mock proxy.
    EXPECT_CALL(*mock_bus_,
                GetObjectProxy(
                    chromeos::kUserAuthenticationServiceName,
                    dbus::ObjectPath(chromeos::kUserAuthenticationServicePath)))
        .WillOnce(Return(mock_auth_dialog_proxy_.get()));
  }

  void CreateHandler() {
    handler_ = std::make_unique<WebAuthnHandler>();
    handler_->Initialize(mock_bus_.get(), &mock_tpm_proxy_, &mock_user_state_,
                         [this]() { presence_requested_count_++; });
  }

  void PrepareMockStorage() {
    auto mock_storage = std::make_unique<MockWebAuthnStorage>();
    mock_webauthn_storage_ = mock_storage.get();
    handler_->SetWebAuthnStorageForTesting(std::move(mock_storage));
    mock_webauthn_storage_->set_allow_access(true);
  }

  void ExpectNoGetUserSecret() {
    EXPECT_CALL(mock_user_state_, GetUserSecret()).Times(0);
  }

  void CallAndWaitForPresence(std::function<uint32_t()> fn, uint32_t* status) {
    handler_->CallAndWaitForPresence(fn, status);
  }

  bool PresenceRequested() { return presence_requested_count_ > 0; }

  MakeCredentialResponse::MakeCredentialStatus DoU2fGenerate(
      PresenceRequirement presence_requirement,
      std::vector<uint8_t>* credential_id,
      std::vector<uint8_t>* credential_pubkey) {
    return handler_->DoU2fGenerate(
        kRpIdHash, ArrayToSecureBlob(kCredentialSecret), presence_requirement,
        /* uv_compatible = */ true, credential_id, credential_pubkey);
  }

  GetAssertionResponse::GetAssertionStatus DoU2fSign(
      const std::vector<uint8_t>& hash_to_sign,
      const std::vector<uint8_t>& credential_id,
      PresenceRequirement presence_requirement,
      std::vector<uint8_t>* signature) {
    return handler_->DoU2fSign(kRpIdHash, hash_to_sign, credential_id,
                               ArrayToSecureBlob(kCredentialSecret),
                               presence_requirement, signature);
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

  // Set up an auth-time secret hash as if a user has logged in.
  void SetUpAuthTimeSecretHash() {
    handler_->auth_time_secret_hash_ = std::make_unique<brillo::Blob>(32, 0x12);
  }

  void InsertAuthTimeSecretHashToCredentialId(std::vector<uint8_t>* input) {
    handler_->InsertAuthTimeSecretHashToCredentialId(input);
  }

  StrictMock<MockTpmVendorCommandProxy> mock_tpm_proxy_;
  StrictMock<MockUserState> mock_user_state_;

  std::unique_ptr<WebAuthnHandler> handler_;
  MockWebAuthnStorage* mock_webauthn_storage_;

  int presence_requested_expected_ = 0;

 private:
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_auth_dialog_proxy_;
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

TEST_F(WebAuthnHandlerTest, DoU2fGenerateNoAuthTimeSecretHash) {
  std::vector<uint8_t> cred_id, cred_pubkey;
  EXPECT_EQ(
      DoU2fGenerate(PresenceRequirement::kPowerButton, &cred_id, &cred_pubkey),
      MakeCredentialResponse::INTERNAL_ERROR);
}

TEST_F(WebAuthnHandlerTest, DoU2fGenerateSuccessUserPresence) {
  SetUpAuthTimeSecretHash();
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fGenerate(
          StructMatchesRegex(ExpectedUserPresenceU2fGenerateRequestRegex()),
          Matcher<u2f_generate_versioned_resp*>(_)))
      .WillOnce(Return(kCr50StatusNotAllowed))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fGenerateVersionedResponse),
                      Return(kCr50StatusSuccess)));
  std::vector<uint8_t> cred_id, cred_pubkey;
  EXPECT_EQ(
      DoU2fGenerate(PresenceRequirement::kPowerButton, &cred_id, &cred_pubkey),
      MakeCredentialResponse::SUCCESS);
  EXPECT_EQ(cred_id, std::vector<uint8_t>(113, 0xFD));
  EXPECT_EQ(cred_pubkey, std::vector<uint8_t>(65, 0xAB));
  presence_requested_expected_ = 1;
}

TEST_F(WebAuthnHandlerTest, DoU2fGenerateSuccessUserVerification) {
  SetUpAuthTimeSecretHash();
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fGenerate(
          StructMatchesRegex(ExpectedUserVerificationU2fGenerateRequestRegex()),
          Matcher<u2f_generate_versioned_resp*>(_)))
      // Should succeed at the first time since no presence is required.
      .WillOnce(DoAll(SetArgPointee<1>(kU2fGenerateVersionedResponse),
                      Return(kCr50StatusSuccess)));
  std::vector<uint8_t> cred_id, cred_pubkey;
  // UI has verified the user so do not require presence.
  EXPECT_EQ(DoU2fGenerate(PresenceRequirement::kNone, &cred_id, &cred_pubkey),
            MakeCredentialResponse::SUCCESS);
  EXPECT_EQ(cred_id, std::vector<uint8_t>(113, 0xFD));
  EXPECT_EQ(cred_pubkey, std::vector<uint8_t>(65, 0xAB));
  presence_requested_expected_ = 0;
}

TEST_F(WebAuthnHandlerTest, DoU2fSignPresenceNoPresence) {
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedDeterministicU2fSignRequestRegex())),
                          _))
      .WillRepeatedly(Return(kCr50StatusNotAllowed));
  std::vector<uint8_t> signature;
  EXPECT_EQ(DoU2fSign(kHashToSign, kKeyHandle,
                      PresenceRequirement::kPowerButton, &signature),
            MakeCredentialResponse::VERIFICATION_FAILED);
  presence_requested_expected_ = kMaxRetries;
}

TEST_F(WebAuthnHandlerTest, DoU2fSignPresenceSuccess) {
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedDeterministicU2fSignRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusNotAllowed))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fSignResponse),
                      Return(kCr50StatusSuccess)));
  std::vector<uint8_t> signature;
  EXPECT_EQ(DoU2fSign(kHashToSign, kKeyHandle,
                      PresenceRequirement::kPowerButton, &signature),
            MakeCredentialResponse::SUCCESS);
  EXPECT_EQ(signature, util::SignatureToDerBytes(kU2fSignResponse.sig_r,
                                                 kU2fSignResponse.sig_s));
  presence_requested_expected_ = 1;
}

TEST_F(WebAuthnHandlerTest, MakeCredentialUninitialized) {
  // Use an uninitialized WebAuthnHandler object.
  handler_.reset(new WebAuthnHandler());
  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<MakeCredentialResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const MakeCredentialResponse& resp) {
        EXPECT_EQ(resp.status(), MakeCredentialResponse::INTERNAL_ERROR);
        *called_ptr = true;
      },
      &called));

  MakeCredentialRequest request;
  handler_->MakeCredential(std::move(mock_method_response), request);
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTest, MakeCredentialEmptyRpId) {
  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<MakeCredentialResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const MakeCredentialResponse& resp) {
        EXPECT_EQ(resp.status(), MakeCredentialResponse::INVALID_REQUEST);
        *called_ptr = true;
      },
      &called));

  MakeCredentialRequest request;
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);
  handler_->MakeCredential(std::move(mock_method_response), request);
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTest, MakeCredentialNoAuthTimeSecretHash) {
  MakeCredentialRequest request;
  request.set_rp_id(kRpId);
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);

  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<MakeCredentialResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const MakeCredentialResponse& resp) {
        EXPECT_EQ(resp.status(), MakeCredentialResponse::INTERNAL_ERROR);
        *called_ptr = true;
      },
      &called));

  handler_->MakeCredential(std::move(mock_method_response), request);
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTest, MakeCredentialSuccess) {
  MakeCredentialRequest request;
  request.set_rp_id(kRpId);
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);

  SetUpAuthTimeSecretHash();
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fGenerate(
          StructMatchesRegex(ExpectedUserPresenceU2fGenerateRequestRegex()),
          Matcher<u2f_generate_versioned_resp*>(_)))
      .WillOnce(Return(kCr50StatusNotAllowed))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fGenerateVersionedResponse),
                      Return(kCr50StatusSuccess)));
  // TODO(yichengli): Specify the parameter to WriteRecord.
  EXPECT_CALL(*mock_webauthn_storage_, WriteRecord(_)).WillOnce(Return(true));

  const std::string expected_authenticator_data_regex =
      base::HexEncode(kRpIdHash.data(), kRpIdHash.size()) +  // RP ID hash
      std::string(
          "41"          // Flag: user present, attested credential data included
          "(..){4}"     // Signature counter
          "(00){16}"    // AAGUID
          "0091"        // Credential ID length
                        // Credential ID, from kU2fGenerateVersionedResponse:
          "(FD){65}"    // Versioned key handle header
          "(FD){16}"    // Authorization salt
          "(12){32}"    // Hash of authorization secret
          "(FD){32}"    // Authorization hmac
                        // CBOR encoded credential public key:
          "A5"          // Start a CBOR map of 5 elements
          "01"          // unsigned(1), COSE key type field
          "02"          // unsigned(2), COSE key type EC2
          "03"          // unsigned(3), COSE key algorithm field
          "26"          // negative(6) = -7, COSE key algorithm ES256
          "20"          // negative(0) = -1, COSE EC key curve field
          "01"          // unsigned(1), COSE EC key curve
          "21"          // negative(1) = -2, COSE EC key x coordinate field
          "5820"        // Start a CBOR array of 32 bytes
          "(AB){32}"    // x coordinate, from kU2fGenerateVersionedResponse
          "22"          // negative(2) = -3, COSE EC key y coordinate field
          "5820"        // Start a CBOR array of 32 bytes
          "(AB){32}");  // y coordinate, from kU2fGenerateVersionedResponse

  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<MakeCredentialResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const std::string& expected_authenticator_data,
         const MakeCredentialResponse& resp) {
        EXPECT_EQ(resp.status(), MakeCredentialResponse::SUCCESS);
        EXPECT_THAT(base::HexEncode(resp.authenticator_data().data(),
                                    resp.authenticator_data().size()),
                    MatchesRegex(expected_authenticator_data));
        EXPECT_EQ(resp.attestation_format(), "none");
        EXPECT_EQ(resp.attestation_statement(), "\xa0");
        *called_ptr = true;
      },
      &called, expected_authenticator_data_regex));

  handler_->MakeCredential(std::move(mock_method_response), request);
  presence_requested_expected_ = 1;
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTest, GetAssertionUninitialized) {
  // Use an uninitialized WebAuthnHandler object.
  handler_.reset(new WebAuthnHandler());
  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<GetAssertionResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const GetAssertionResponse& resp) {
        EXPECT_EQ(resp.status(), GetAssertionResponse::INTERNAL_ERROR);
        *called_ptr = true;
      },
      &called));

  GetAssertionRequest request;
  handler_->GetAssertion(std::move(mock_method_response), request);
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTest, GetAssertionEmptyRpId) {
  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<GetAssertionResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const GetAssertionResponse& resp) {
        EXPECT_EQ(resp.status(), GetAssertionResponse::INVALID_REQUEST);
        *called_ptr = true;
      },
      &called));

  GetAssertionRequest request;
  request.set_client_data_hash(std::string(SHA256_DIGEST_LENGTH, 0xcd));
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);
  handler_->GetAssertion(std::move(mock_method_response), request);
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTest, GetAssertionWrongClientDataHashLength) {
  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<GetAssertionResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const GetAssertionResponse& resp) {
        EXPECT_EQ(resp.status(), GetAssertionResponse::INVALID_REQUEST);
        *called_ptr = true;
      },
      &called));

  GetAssertionRequest request;
  request.set_rp_id(kRpId);
  request.set_client_data_hash(std::string(SHA256_DIGEST_LENGTH - 1, 0xcd));
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);
  handler_->GetAssertion(std::move(mock_method_response), request);
  ASSERT_TRUE(called);
}

// Simulates the case where the KH doesn't match any record in daemon-store. It
// could be that the KH itself is invalid, or the user deleted the record (for
// privacy reasons).
TEST_F(WebAuthnHandlerTest, GetAssertionNoCredentialSecret) {
  GetAssertionRequest request;
  request.set_rp_id(kRpId);
  request.set_client_data_hash(std::string(SHA256_DIGEST_LENGTH, 0xcd));
  const std::string credential_id(sizeof(struct u2f_key_handle), 0xab);
  request.add_allowed_credential_id(credential_id);
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);

  EXPECT_CALL(*mock_webauthn_storage_, GetSecretByCredentialId(credential_id))
      .WillOnce(Return(base::nullopt));

  // Since we don't have user secret, we won't even pass DoU2fSignCheckOnly, and
  // the TPM won't receive any command.
  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<GetAssertionResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const GetAssertionResponse& resp) {
        EXPECT_EQ(resp.status(), GetAssertionResponse::UNKNOWN_CREDENTIAL_ID);
        *called_ptr = true;
      },
      &called));

  handler_->GetAssertion(std::move(mock_method_response), request);
  ASSERT_TRUE(called);
}

// Simulates the case where the KH matches a record in daemon-store but is not
// recognized by cr50. This is not very likely in reality unless daemon-store
// is compromised.
TEST_F(WebAuthnHandlerTest, GetAssertionInvalidKeyHandle) {
  GetAssertionRequest request;
  request.set_rp_id(kRpId);
  request.set_client_data_hash(std::string(SHA256_DIGEST_LENGTH, 0xcd));
  const std::string credential_id(sizeof(struct u2f_key_handle), 0xab);
  request.add_allowed_credential_id(credential_id);
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);

  EXPECT_CALL(*mock_webauthn_storage_, GetSecretByCredentialId(credential_id))
      .WillOnce(Return(ArrayToSecureBlob(kCredentialSecret)));
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusPasswordRequired));

  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<GetAssertionResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const GetAssertionResponse& resp) {
        EXPECT_EQ(resp.status(), GetAssertionResponse::UNKNOWN_CREDENTIAL_ID);
        *called_ptr = true;
      },
      &called));

  handler_->GetAssertion(std::move(mock_method_response), request);
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTest, GetAssertionPresenceNoPresence) {
  GetAssertionRequest request;
  request.set_rp_id(kRpId);
  request.set_client_data_hash(std::string(SHA256_DIGEST_LENGTH, 0xcd));
  const std::string credential_id(sizeof(struct u2f_key_handle), 0xab);
  request.add_allowed_credential_id(credential_id);
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);

  EXPECT_CALL(*mock_webauthn_storage_, GetSecretByCredentialId(credential_id))
      .WillRepeatedly(Return(ArrayToSecureBlob(kCredentialSecret)));
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusSuccess));
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignRequestRegex())),
                          _))
      .WillRepeatedly(Return(kCr50StatusNotAllowed));

  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<GetAssertionResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const GetAssertionResponse& resp) {
        EXPECT_EQ(resp.status(), GetAssertionResponse::VERIFICATION_FAILED);
        *called_ptr = true;
      },
      &called));

  handler_->GetAssertion(std::move(mock_method_response), request);
  presence_requested_expected_ = kMaxRetries;
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTest, GetAssertionPresenceSuccess) {
  GetAssertionRequest request;
  request.set_rp_id(kRpId);
  request.set_client_data_hash(std::string(SHA256_DIGEST_LENGTH, 0xcd));
  const std::string credential_id(sizeof(struct u2f_key_handle), 0xab);
  request.add_allowed_credential_id(credential_id);
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);

  EXPECT_CALL(*mock_webauthn_storage_, GetSecretByCredentialId(credential_id))
      .WillRepeatedly(Return(ArrayToSecureBlob(kCredentialSecret)));
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusSuccess));
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusNotAllowed))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fSignResponse),
                      Return(kCr50StatusSuccess)));

  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<GetAssertionResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const GetAssertionResponse& resp) {
        EXPECT_EQ(resp.status(), GetAssertionResponse::SUCCESS);
        ASSERT_EQ(resp.assertion_size(), 1);
        auto assertion = resp.assertion(0);
        EXPECT_EQ(assertion.credential_id(),
                  std::string(sizeof(struct u2f_key_handle), 0xab));
        EXPECT_THAT(
            base::HexEncode(assertion.authenticator_data().data(),
                            assertion.authenticator_data().size()),
            MatchesRegex(base::HexEncode(kRpIdHash.data(),
                                         kRpIdHash.size()) +  // RP ID hash
                         std::string("01"           // Flag: user present
                                     "(..){4}")));  // Signature counter
        EXPECT_EQ(util::ToVector(assertion.signature()),
                  util::SignatureToDerBytes(kU2fSignResponse.sig_r,
                                            kU2fSignResponse.sig_s));
        *called_ptr = true;
      },
      &called));

  handler_->GetAssertion(std::move(mock_method_response), request);
  presence_requested_expected_ = 1;
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTest, HasCredentialsNoMatch) {
  HasCredentialsRequest request;
  request.set_rp_id(kRpId);
  const std::string credential_id(sizeof(struct u2f_key_handle), 0xab);
  request.add_credential_id(credential_id);

  EXPECT_CALL(*mock_webauthn_storage_, GetSecretByCredentialId(credential_id))
      .WillRepeatedly(Return(ArrayToSecureBlob(kCredentialSecret)));
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusPasswordRequired));

  auto resp = handler_->HasCredentials(request);
  EXPECT_EQ(resp.credential_id_size(), 0);
}

TEST_F(WebAuthnHandlerTest, HasCredentialsOneMatch) {
  HasCredentialsRequest request;
  request.set_rp_id(kRpId);
  const std::string credential_id(sizeof(struct u2f_key_handle), 0xab);
  request.add_credential_id(credential_id);

  EXPECT_CALL(*mock_webauthn_storage_, GetSecretByCredentialId(credential_id))
      .WillRepeatedly(Return(ArrayToSecureBlob(kCredentialSecret)));
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusSuccess));

  auto resp = handler_->HasCredentials(request);
  EXPECT_EQ(resp.credential_id_size(), 1);
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
  EXPECT_THAT(
      base::HexEncode(authenticator_data.data(), authenticator_data.size()),
      MatchesRegex(expected_authenticator_data_regex));
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
          "01"         // Flag: user present
          "(..){4}");  // Signature counter
  EXPECT_THAT(
      base::HexEncode(authenticator_data.data(), authenticator_data.size()),
      MatchesRegex(expected_authenticator_data_regex));
}

TEST_F(WebAuthnHandlerTest, InsertAuthTimeSecretHashToCredentialId) {
  SetUpAuthTimeSecretHash();
  std::vector<uint8_t> input;
  input.reserve(sizeof(u2f_versioned_key_handle));
  input.insert(input.cend(), 65, 0x01);  // header
  input.insert(input.cend(), 16, 0x02);  // authorization_salt
  input.insert(input.cend(), 32, 0x03);  // authorization_hmac
  InsertAuthTimeSecretHashToCredentialId(&input);

  const std::string expected_output(
      "(01){65}"    // header
      "(02){16}"    // authorization_salt
      "(12){32}"    // auth_time_secret_hash
      "(03){32}");  // authorization_hmac
  EXPECT_THAT(base::HexEncode(input.data(), input.size()),
              MatchesRegex(expected_output));
}

}  // namespace
}  // namespace u2f
