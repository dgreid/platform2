// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/u2f_msg_handler.h"

#include <iostream>
#include <memory>
#include <regex>  // NOLINT(build/c++11)
#include <string>

#include <base/strings/string_number_conversions.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "u2fd/mock_allowlisting_util.h"
#include "u2fd/mock_tpm_vendor_cmd.h"
#include "u2fd/mock_user_state.h"

namespace u2f {
namespace {

using ::testing::_;
using ::testing::ContainsRegex;
using ::testing::MatchesRegex;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::StrictMock;

std::string AdpuToHexString(const U2fResponseAdpu& adpu) {
  std::string adpu_str;
  adpu.ToString(&adpu_str);
  return base::HexEncode(adpu_str.c_str(), adpu_str.size());
}

brillo::SecureBlob ArrayToSecureBlob(const char* array) {
  brillo::SecureBlob blob;
  CHECK(brillo::SecureBlob::HexStringToSecureBlob(array, &blob));
  return blob;
}

MATCHER_P(MsgEqStr, expected, "") {
  std::string match_hex = AdpuToHexString(arg);

  if (match_hex == expected) {
    return true;
  }

  *result_listener << match_hex
                   << " did not match expected value: " << expected;

  return false;
}

MATCHER_P(StructEqStr, expected, "") {
  std::string arg_hex = base::HexEncode(&arg, sizeof(arg));

  if (arg_hex == expected) {
    return true;
  }

  *result_listener << arg_hex << " did not match expected value: " << expected;

  return false;
}

MATCHER_P(StructMatchesRegex, pattern, "") {
  std::string arg_hex = base::HexEncode(&arg, sizeof(arg));

  if (std::regex_match(arg_hex, std::regex(pattern))) {
    return true;
  }

  *result_listener << arg_hex << " did not match regex: " << pattern;

  return false;
}

// Dummy User State.
constexpr char kUserSecret[65] = {[0 ... 63] = 'E', '\0'};
constexpr uint8_t kCounter = 37;

class U2fMessageHandlerTest : public ::testing::Test {
 public:
  void SetUp() override { CreateHandler(false, false); }

  void TearDown() override {
    EXPECT_EQ(presence_requested_expected_, presence_requested_count_);
  }

 protected:
  void CreateHandler(bool allow_legacy_kh, bool allow_g2f_attestation) {
    mock_allowlisting_util_ = new StrictMock<MockAllowlistingUtil>();

    handler_.reset(new U2fMessageHandler(
        std::unique_ptr<AllowlistingUtil>(mock_allowlisting_util_),
        [this]() { presence_requested_count_++; }, &mock_user_state_,
        &mock_tpm_proxy_, &mock_metrics_, allow_legacy_kh,
        allow_g2f_attestation));
  }

  void ExpectGetUserSecret() { ExpectGetUserSecretForTimes(1); }

  void ExpectGetUserSecretForTimes(int times) {
    EXPECT_CALL(mock_user_state_, GetUserSecret())
        .Times(times)
        .WillRepeatedly(Return(ArrayToSecureBlob(kUserSecret)));
  }

  void ExpectGetUserSecretFails() {
    EXPECT_CALL(mock_user_state_, GetUserSecret())
        .WillOnce(Return(base::Optional<brillo::SecureBlob>()));
  }

  void ExpectGetCounter() {
    EXPECT_CALL(mock_user_state_, GetCounter())
        .WillOnce(Return(base::Optional<std::vector<uint8_t>>({kCounter})));
  }

  void ExpectGetCounterFails() {
    EXPECT_CALL(mock_user_state_, GetCounter())
        .WillOnce(Return(base::Optional<std::vector<uint8_t>>()));
  }

  void ExpectIncrementCounter() {
    EXPECT_CALL(mock_user_state_, IncrementCounter()).WillOnce(Return(true));
  }

  void ExpectIncrementCounterFails() {
    EXPECT_CALL(mock_user_state_, IncrementCounter()).WillOnce(Return(false));
  }

  U2fResponseAdpu ProcessMsg(const std::string& hex) {
    std::vector<uint8_t> bytes;
    CHECK(base::HexStringToBytes(hex, &bytes));

    return handler_->ProcessMsg(std::string(bytes.begin(), bytes.end()));
  }

  U2fResponseAdpu ProcessMsg(const std::string& hex,
                             const std::string& hex2,
                             const std::string& hex3,
                             const std::string& hex4) {
    return ProcessMsg(hex + hex2 + hex3 + hex4);
  }

  void CheckResponseForMsg(const std::string& request,
                           const std::string& response) {
    EXPECT_THAT(ProcessMsg(request), MsgEqStr(response));
  }

  void CheckResponseForMsg(const std::string& request,
                           const std::string& request2,
                           const std::string& request3,
                           const std::string& request4,
                           const std::string& response) {
    EXPECT_THAT(ProcessMsg(request, request2, request3, request4),
                MsgEqStr(response));
  }

  StrictMock<MockAllowlistingUtil>* mock_allowlisting_util_;  // Not Owned.
  StrictMock<MockTpmVendorCommandProxy> mock_tpm_proxy_;
  StrictMock<MockUserState> mock_user_state_;
  NiceMock<MetricsLibraryMock> mock_metrics_;

  std::unique_ptr<U2fMessageHandler> handler_;

  int presence_requested_expected_ = 0;

 private:
  int presence_requested_count_ = 0;
};

// U2F Error Codes.
constexpr char kErrorResponseWrongData[] = "6A80";
constexpr char kErrorResponseConditionsNotSatisfied[] = "6985";
constexpr char kErrorResponseWrongLength[] = "6700";
constexpr char kErrorResponseInsNotSupported[] = "6D00";
constexpr char kErrorResponseClaNotSupported[] = "6E00";
constexpr char kErrorResponseWtf[] = "6F00";

// Dummy U2F Message Parameters
constexpr char kAppId[65] = {[0 ... 63] = 'A', '\0'};
constexpr char kChallenge[65] = {[0 ... 63] = 'C', '\0'};
constexpr char kMaxResponseSize[] = "FF";

// Cr50 Response Codes
constexpr uint32_t kCr50Success = 0;
constexpr uint32_t kCr50NotAllowed = 0x507;
constexpr uint32_t kCr50PasswordRequired = 0x50a;

// U2F_REGISTER
//
////////////////////////////////////////////////////////////////////////////////

// Message format: CLA,INS{U2F_REGISTER},P1{U2F_AUTH_ENFORCE},P2,MSG_SIZE
// All fields one byte.
constexpr char kRequestRegisterPrefix[] = "0001030040";
constexpr char kRequestRegisterShort[] = "0001030000";

TEST_F(U2fMessageHandlerTest, RegisterSuccess) {
  ExpectGetUserSecret();

  // See U2F_GENERATE_REQ in //platform/ec/include/u2f.h
  std::string expected_cr50_request_regex =
      "(AA){32}"   // AppId
      "(EE){32}"   // User Secret
      "03"         // U2F_AUTH_ENFORCE
      "(00){32}";  // Auth-time secret hash (unused)

  struct u2f_generate_resp cr50_response = {
      .keyHandle = {.origin_seed = {[0 ... 31] = 0xFD},
                    .hmac = {[0 ... 31] = 0xFD}}};

  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fGenerate(StructMatchesRegex(expected_cr50_request_regex), _))
      .WillOnce(DoAll(SetArgPointee<1>(cr50_response), Return(kCr50Success)));

  std::string adpu_response = AdpuToHexString(
      ProcessMsg(kRequestRegisterPrefix, kChallenge, kAppId, kMaxResponseSize));

  // See U2F Raw Message Formats Spec
  std::string expected_response_regex =
      "05"             // Reserved Byte
      "[0-9A-F]{130}"  // Public Key
      "40(FD){64}"     // Key Handle (matches cr50 response)
      ".*"             // Attestation Cert + Signature
      "9000";          // U2F_SW_NO_ERROR

  // Just a basic sanity check, the correctness of message contents is tested by
  // integration tests.
  EXPECT_THAT(adpu_response, MatchesRegex(expected_response_regex));
}

// Errors detected during parsing; should not read user state or call cr50.

TEST_F(U2fMessageHandlerTest, RegisterShortMsg) {
  CheckResponseForMsg(kRequestRegisterShort, kErrorResponseWrongLength);
}

TEST_F(U2fMessageHandlerTest, RegisterInvalidFlags) {
  std::string prefix = kRequestRegisterPrefix;
  prefix[5] = '0';  // Set invalid P1 flag.

  CheckResponseForMsg(prefix, kChallenge, kAppId, kMaxResponseSize,
                      kErrorResponseWtf);
}

TEST_F(U2fMessageHandlerTest, RegisterChromeDummyWinkRequest) {
  std::string app_id;
  std::string challenge;

  // Chrome Sends a bogus request with these parameters, see chromium
  // //src/device/fido/fido_constants.cc
  for (int i = 0; i < 32; i++) {
    app_id.append("41");
    challenge.append("42");
  }

  CheckResponseForMsg(kRequestRegisterPrefix, challenge, app_id,
                      kMaxResponseSize, kErrorResponseConditionsNotSatisfied);
}

// Errors while reading state, should not call cr50.

TEST_F(U2fMessageHandlerTest, RegisterSecretNotAvailable) {
  ExpectGetUserSecretFails();

  CheckResponseForMsg(kRequestRegisterPrefix, kChallenge, kAppId,
                      kMaxResponseSize, kErrorResponseWtf);
}

// Errors returned by cr50.

TEST_F(U2fMessageHandlerTest, RegisterNoPresence) {
  ExpectGetUserSecret();

  EXPECT_CALL(mock_tpm_proxy_, SendU2fGenerate(_, _))
      .WillOnce(Return(kCr50NotAllowed));

  CheckResponseForMsg(kRequestRegisterPrefix, kChallenge, kAppId,
                      kMaxResponseSize, kErrorResponseConditionsNotSatisfied);

  presence_requested_expected_ = 1;
}

TEST_F(U2fMessageHandlerTest, RegisterCr50UnknownError) {
  ExpectGetUserSecret();

  EXPECT_CALL(mock_tpm_proxy_, SendU2fGenerate(_, _))
      .WillOnce(Return(4324523 /* Random Unknown Error */));

  CheckResponseForMsg(kRequestRegisterPrefix, kChallenge, kAppId,
                      kMaxResponseSize, kErrorResponseWtf);
}

// U2F_REGISTER with G2F Attestation
//
////////////////////////////////////////////////////////////////////////////////

// Message format:
// CLA,INS{U2F_REGISTER},P1{U2F_AUTH_ENFORCE|G2F_ATTEST},P2,MSG_SIZE All fields
// one byte.
constexpr char kRequestRegisterG2fPrefix[] = "0001830040";

// See U2F_GENERATE_REQ in //platform/ec/include/u2f.h
constexpr char kCr50ExpectedGenReqRegex[] =
    "(AA){32}"   // AppId
    "(EE){32}"   // User Secret
    "03"         // U2F_AUTH_ENFORCE | G2F_ATTEST
    "(00){32}";  // Auth-time secret hash (unused)

// Dummy generate response.
constexpr struct u2f_generate_resp kCr50GenResp = {
    .keyHandle = {.origin_seed = {[0 ... 31] = 0xFD},
                  .hmac = {[0 ... 31] = 0xFD}}};  // cr50_gen_resp

// Example of a cert that would be returned by cr50.
constexpr char kDummyG2fCert[] =
    "308201363081DDA0030201020210442D32429223D041240350303716EE6B300A06082A8648"
    "CE3D040302300F310D300B06035504031304637235303022180F3230303030313031303030"
    "3030305A180F32303939313233313233353935395A300F310D300B06035504031304637235"
    "303059301306072A8648CE3D020106082A8648CE3D030107034200045165719A9975F6FD30"
    "CC2516C22FE841F65F9D2EE7B8B72F76807AEBD8CA3376005C7FA86453E4B10DB7BFAD5D2B"
    "D00DB4A7C4845AD06D686ACD0252387618ECA31730153013060B2B0601040182E51C020101"
    "040403020308300A06082A8648CE3D0403020348003045022100F09976F373920FEF8205C4"
    "B1FB1DA21EB9F3F176B7DF433A1ADE0F3F38B721960220179D9B9051BFCCCC90BA6BB42B86"
    "111D7A9C4FB56DFD39FB426081DD027AD609";

std::string GetDummyG2fCert() {
  static std::string cert_str = []() {
    std::vector<uint8_t> cert;
    base::HexStringToBytes(kDummyG2fCert, &cert);
    return std::string(reinterpret_cast<char*>(cert.data()), cert.size());
  }();

  return cert_str;
}

TEST_F(U2fMessageHandlerTest, RegisterG2fWhenDisabledSuccess) {
  // G2F Attestation is disabled (default flags set in test fixture), so we
  // should not see any additional calls to cr50 for G2F attestation.

  ExpectGetUserSecret();

  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fGenerate(StructMatchesRegex(kCr50ExpectedGenReqRegex), _))
      .WillOnce(DoAll(SetArgPointee<1>(kCr50GenResp), Return(kCr50Success)));

  std::string adpu_response = AdpuToHexString(ProcessMsg(
      kRequestRegisterG2fPrefix, kChallenge, kAppId, kMaxResponseSize));

  // See U2F Raw Message Formats Spec
  std::string expected_response_regex =
      "05"             // Reserved Byte
      "[0-9A-F]{130}"  // Public Key
      "40(FD){64}"     // Key Handle (matches kCr50GenResp)
      ".*"             // Attestation Cert + Signature
      "9000";          // U2F_SW_NO_ERROR

  // Just a basic sanity check, the correctness of message contents is tested by
  // integration tests.
  EXPECT_THAT(adpu_response, MatchesRegex(expected_response_regex));
}

TEST_F(U2fMessageHandlerTest, RegisterG2fSuccess) {
  CreateHandler(false /* legacy fallback */, true /* g2f_attest */);

  // Once to create the key, once for attestation.
  ExpectGetUserSecretForTimes(2);

  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fGenerate(StructMatchesRegex(kCr50ExpectedGenReqRegex), _))
      .WillOnce(DoAll(SetArgPointee<1>(kCr50GenResp), Return(kCr50Success)));

  EXPECT_CALL(mock_tpm_proxy_, GetG2fCertificate(_))
      .WillOnce(DoAll(SetArgPointee<0>(GetDummyG2fCert()), Return(0)));

  EXPECT_CALL(*mock_allowlisting_util_, AppendDataToCert(_))
      .WillOnce(Return(true));

  // See U2F_ATTEST_REQ in //platform/ec/include/u2f.h
  std::string expected_cr50_attest_req_regex =
      "(EE){32}"  // User Secret
      "00"        // Format
      "C2"        // Data Length
      // See U2F Raw Message formats for format of data to sign.
      "00(A){64}(C){64}(FD){64}(0){254}";  // Data

  struct u2f_attest_resp cr50_attest_resp = {.sig_r = {[0 ... 31] = 0xFF},
                                             .sig_s = {[0 ... 31] = 0x55}};

  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fAttest(StructMatchesRegex(expected_cr50_attest_req_regex), _))
      .WillOnce(
          DoAll(SetArgPointee<1>(cr50_attest_resp), Return(kCr50Success)));

  std::string adpu_response = AdpuToHexString(ProcessMsg(
      kRequestRegisterG2fPrefix, kChallenge, kAppId, kMaxResponseSize));

  // See U2F Raw Message Formats Spec
  std::string expected_response_regex =
      "05"             // Reserved Byte
      "[0-9A-F]{130}"  // Public Key
      "40(FD){64}"     // Key Handle (matches kCr50GenResp)
      ".*"             // Attestation Cert + Signature
      "9000";          // U2F_SW_NO_ERROR

  // Just a basic sanity check, the correctness of message contents is tested by
  // integration tests.
  EXPECT_THAT(adpu_response, MatchesRegex(expected_response_regex));
}

// Error from cr50

TEST_F(U2fMessageHandlerTest, RegisterG2fNoPresence) {
  CreateHandler(false /* legacy fallback */, true /* g2f_attest */);

  ExpectGetUserSecret();

  EXPECT_CALL(mock_tpm_proxy_, SendU2fGenerate(_, _))
      .WillOnce(Return(kCr50NotAllowed));

  CheckResponseForMsg(kRequestRegisterG2fPrefix, kChallenge, kAppId,
                      kMaxResponseSize, kErrorResponseConditionsNotSatisfied);

  presence_requested_expected_ = 1;
}

TEST_F(U2fMessageHandlerTest, RegisterG2fAttestSecretNotAvailable) {
  CreateHandler(false /* legacy fallback */, true /* g2f_attest */);

  // Called first to create the key, succeed.
  // Called again for attestation, fail.
  EXPECT_CALL(mock_user_state_, GetUserSecret())
      .WillOnce(Return(ArrayToSecureBlob(kUserSecret)))
      .WillOnce(Return(base::Optional<brillo::SecureBlob>()));

  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fGenerate(StructMatchesRegex(kCr50ExpectedGenReqRegex), _))
      .WillOnce(DoAll(SetArgPointee<1>(kCr50GenResp), Return(kCr50Success)));

  EXPECT_CALL(mock_tpm_proxy_, GetG2fCertificate(_))
      .WillOnce(DoAll(SetArgPointee<0>(GetDummyG2fCert()), Return(0)));

  CheckResponseForMsg(kRequestRegisterG2fPrefix, kChallenge, kAppId,
                      kMaxResponseSize, kErrorResponseWtf);
}

TEST_F(U2fMessageHandlerTest, RegisterG2fAttestFails) {
  CreateHandler(false /* legacy fallback */, true /* g2f_attest */);

  // Once to create the key, once for attestation.
  ExpectGetUserSecretForTimes(2);

  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fGenerate(StructMatchesRegex(kCr50ExpectedGenReqRegex), _))
      .WillOnce(DoAll(SetArgPointee<1>(kCr50GenResp), Return(kCr50Success)));

  EXPECT_CALL(mock_tpm_proxy_, GetG2fCertificate(_))
      .WillOnce(DoAll(SetArgPointee<0>(GetDummyG2fCert()), Return(0)));

  EXPECT_CALL(mock_tpm_proxy_, SendU2fAttest(_, _))
      .WillOnce(Return(kCr50NotAllowed));

  CheckResponseForMsg(kRequestRegisterG2fPrefix, kChallenge, kAppId,
                      kMaxResponseSize, kErrorResponseWtf);
}

// U2F_AUTHENTICATE
//
////////////////////////////////////////////////////////////////////////////////

// Message format: CLA,INS{U2F_AUTHENTICATE},P1{U2F_AUTH_ENFORCE},P2,MSG_SIZE
constexpr char kRequestAuthenticatePrefix[] = "0002030042";

// Message format: KH_LENGTH,KH_BYTE
constexpr char kRequestAuthenticateKeyHandle[] = "0100";

// All fields one byte.

TEST_F(U2fMessageHandlerTest, AuthenticateSuccess) {
  ExpectGetCounter();
  ExpectGetUserSecret();

  std::string expected_cr50_request_regex =
      "(AA){32}"      // AppId
      "(EE){32}"      // User Secret
      "(00){64}"      // Key Handle
      "[0-9A-F]{64}"  // Hash
      "03";           // U2F_AUTH_ENFORCE

  struct u2f_sign_resp cr50_response = {.sig_r = {[0 ... 31] = 0xFF},
                                        .sig_s = {[0 ... 31] = 0x55}};

  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(StructMatchesRegex(expected_cr50_request_regex), _))
      .WillOnce(DoAll(SetArgPointee<1>(cr50_response), Return(kCr50Success)));

  ExpectIncrementCounter();

  std::string adpu_response =
      AdpuToHexString(ProcessMsg(kRequestAuthenticatePrefix, kChallenge, kAppId,
                                 kRequestAuthenticateKeyHandle));

  // Just basic sanity check, validity of the message is tested in integration
  // tests.
  std::string expected_response_regex =
      "01"        // User Presence Asserted
      ".*"        // DER encoding
      "(FF){32}"  // sig_r
      ".*"        // DER encoding
      "(55){32}"  // sig_s
      "9000";     // U2F_SW_NO_ERRROR

  EXPECT_THAT(adpu_response, MatchesRegex(expected_response_regex));
}

TEST_F(U2fMessageHandlerTest, AuthenticateWithFallbackSuccess) {
  CreateHandler(true /* legacy kh fallback */, false /* g2f attestation */);

  ExpectGetCounter();
  ExpectGetUserSecret();

  // The purpose of this test is to check that the LEGACY_KH_FALLBACK flag is
  // passed to cr50.

  std::string expected_cr50_request_regex =
      "(AA){32}"      // AppId
      "(EE){32}"      // User Secret
      "(00){64}"      // Key Handle
      "[0-9A-F]{64}"  // Hash
      "43";           // U2F_AUTH_ENFORCE | LEGACY_KH_FALLBACK

  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(StructMatchesRegex(expected_cr50_request_regex), _))
      .WillOnce(Return(kCr50Success));

  ExpectIncrementCounter();

  std::string adpu_response =
      AdpuToHexString(ProcessMsg(kRequestAuthenticatePrefix, kChallenge, kAppId,
                                 kRequestAuthenticateKeyHandle));

  // Just check that is succeeds; the rest is tested in the previous test case.
  EXPECT_THAT(adpu_response, MatchesRegex(".*9000"));
}

// Errors reading state, should not call cr50.

TEST_F(U2fMessageHandlerTest, AuthenticateSecretNotAvailable) {
  ExpectGetCounter();
  ExpectGetUserSecretFails();

  CheckResponseForMsg(kRequestAuthenticatePrefix, kChallenge, kAppId,
                      kRequestAuthenticateKeyHandle, kErrorResponseWtf);
}

TEST_F(U2fMessageHandlerTest, AuthenticateCounterNotAvailable) {
  ExpectGetCounterFails();

  CheckResponseForMsg(kRequestAuthenticatePrefix, kChallenge, kAppId,
                      kRequestAuthenticateKeyHandle, kErrorResponseWtf);
}

// Errors returned by cr50.

TEST_F(U2fMessageHandlerTest, AuthenticateNoPresence) {
  ExpectGetCounter();
  ExpectGetUserSecret();

  EXPECT_CALL(mock_tpm_proxy_, SendU2fSign(_, _))
      .WillOnce(Return(kCr50NotAllowed));

  CheckResponseForMsg(kRequestAuthenticatePrefix, kChallenge, kAppId,
                      kRequestAuthenticateKeyHandle,
                      kErrorResponseConditionsNotSatisfied);

  presence_requested_expected_ = 1;
}

TEST_F(U2fMessageHandlerTest, AuthenticateInvalidKeyHandle) {
  ExpectGetCounter();
  ExpectGetUserSecret();

  EXPECT_CALL(mock_tpm_proxy_, SendU2fSign(_, _))
      .WillOnce(Return(kCr50PasswordRequired));

  CheckResponseForMsg(kRequestAuthenticatePrefix, kChallenge, kAppId,
                      kRequestAuthenticateKeyHandle, kErrorResponseWrongData);
}

TEST_F(U2fMessageHandlerTest, AuthenticateCr50UnknownError) {
  ExpectGetCounter();
  ExpectGetUserSecret();

  EXPECT_CALL(mock_tpm_proxy_, SendU2fSign(_, _))
      .WillOnce(Return(43423 /* Random Unknown Error */));

  CheckResponseForMsg(kRequestAuthenticatePrefix, kChallenge, kAppId,
                      kRequestAuthenticateKeyHandle, kErrorResponseWtf);
}

// Error updating state.

TEST_F(U2fMessageHandlerTest, AuthenticateCounterCouldNotUpdate) {
  ExpectGetCounter();
  ExpectGetUserSecret();

  struct u2f_sign_resp cr50_response = {.sig_r = {[0 ... 31] = 0xFF},
                                        .sig_s = {[0 ... 31] = 0x55}};

  EXPECT_CALL(mock_tpm_proxy_, SendU2fSign(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(cr50_response), Return(kCr50Success)));

  ExpectIncrementCounterFails();

  CheckResponseForMsg(kRequestAuthenticatePrefix, kChallenge, kAppId,
                      kRequestAuthenticateKeyHandle, kErrorResponseWtf);
}

// U2F_AUTHENTICATE "Check Only"
//
////////////////////////////////////////////////////////////////////////////////

// Message format: CLA,INS{U2F_AUTHENTICATE},P1{U2F_AUTH_CHECK_ONLY},P2,MSG_SIZE
constexpr char kRequestAuthenticateCheckOnlyPrefix[] = "0002070042";

TEST_F(U2fMessageHandlerTest, AuthenticateCheckOnlySuccess) {
  ExpectGetUserSecret();

  std::string expected_cr50_request_regex =
      "(AA){32}"      // AppId
      "(EE){32}"      // User Secret
      "(00){64}"      // Key Handle
      "[0-9A-F]{64}"  // Hash
      "07";           // U2F_AUTH_CHECK_ONLY

  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fSign(StructMatchesRegex(expected_cr50_request_regex), nullptr))
      .WillOnce(Return(kCr50Success));

  // A success response for this kind of message is indicate by a 'presence
  // required' error.
  CheckResponseForMsg(kRequestAuthenticateCheckOnlyPrefix, kChallenge, kAppId,
                      kRequestAuthenticateKeyHandle,
                      kErrorResponseConditionsNotSatisfied);
}

TEST_F(U2fMessageHandlerTest, AuthenticateCheckOnlyInvalidKeyHandle) {
  ExpectGetUserSecret();

  std::string expected_cr50_request_regex =
      "(AA){32}"      // AppId
      "(EE){32}"      // User Secret
      "(00){64}"      // Key Handle
      "[0-9A-F]{64}"  // Hash
      "07";           // U2F_AUTH_CHECK_ONLY

  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fSign(StructMatchesRegex(expected_cr50_request_regex), nullptr))
      .WillOnce(Return(kCr50PasswordRequired));

  CheckResponseForMsg(kRequestAuthenticateCheckOnlyPrefix, kChallenge, kAppId,
                      kRequestAuthenticateKeyHandle, kErrorResponseWrongData);
}

TEST_F(U2fMessageHandlerTest, AuthenticateCheckOnlySecretNotAvailable) {
  ExpectGetUserSecretFails();

  CheckResponseForMsg(kRequestAuthenticateCheckOnlyPrefix, kChallenge, kAppId,
                      kRequestAuthenticateKeyHandle, kErrorResponseWtf);
}

// U2F_VERSION
//
////////////////////////////////////////////////////////////////////////////////

// Message format: CLA,INS{U2F_VERSION},P1,P1,MSG_LENGTH,[RESPONSE_LENGTH]
// All fields one byte.
constexpr char kRequestVersion[] = "0003000000";
constexpr char kRequestVersionInvalidLength[] = "0003000001FF00";

// "U2F_V2", plus U2F_SW_NO_ERROR response code.
constexpr char kSuccessResponseVersion[] = "5532465F56329000";

TEST_F(U2fMessageHandlerTest, Version) {
  CheckResponseForMsg(kRequestVersion, kSuccessResponseVersion);
}

TEST_F(U2fMessageHandlerTest, VersionInvalidMsg) {
  CheckResponseForMsg(kRequestVersionInvalidLength, kErrorResponseWrongLength);
}

// Misc Invalid Messages
//
////////////////////////////////////////////////////////////////////////////////

// Message format: CLA,INS,P1,P2.
// All fields occupy byte.
constexpr char kRequestInvalidIns[] = "00430000";
constexpr char kRequestInvalidCla[] = "FFFFFFFF";
constexpr char kRequestInvalidMsg[] = "00";

TEST_F(U2fMessageHandlerTest, UnknownIns) {
  CheckResponseForMsg(kRequestInvalidIns, kErrorResponseInsNotSupported);
}

TEST_F(U2fMessageHandlerTest, InvalidCla) {
  CheckResponseForMsg(kRequestInvalidCla, kErrorResponseClaNotSupported);
}

TEST_F(U2fMessageHandlerTest, InvalidMsg) {
  CheckResponseForMsg(kRequestInvalidMsg, kErrorResponseWtf);
}

}  // namespace
}  // namespace u2f
