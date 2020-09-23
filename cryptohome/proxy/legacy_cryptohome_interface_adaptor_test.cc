// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/proxy/legacy_cryptohome_interface_adaptor.h"

#include <attestation-client-test/attestation/dbus-proxy-mocks.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <chromeos/constants/cryptohome.h>
#include <tpm_manager-client-test/tpm_manager/dbus-proxy-mocks.h>

#include "cryptohome/mock_platform.h"
#include "user_data_auth/dbus-proxy-mocks.h"

namespace cryptohome {

namespace {

using ::brillo::dbus_utils::MockDBusMethodResponse;

using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::InvokeArgument;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;

// A mock adaptor that is used for testing. This is added so that we can capture
// the Send*() functions used for sending signals.
class LegacyCryptohomeInterfaceAdaptorForTesting
    : public LegacyCryptohomeInterfaceAdaptor {
 public:
  LegacyCryptohomeInterfaceAdaptorForTesting(
      org::chromium::AttestationProxyInterface* attestation_proxy,
      org::chromium::TpmOwnershipProxyInterface* tpm_ownership_proxy,
      org::chromium::TpmNvramProxyInterface* tpm_nvram_proxy,
      org::chromium::UserDataAuthInterfaceProxyInterface* userdataauth_proxy,
      org::chromium::ArcQuotaProxyInterface* arc_quota_proxy,
      org::chromium::CryptohomePkcs11InterfaceProxyInterface* pkcs11_proxy,
      org::chromium::InstallAttributesInterfaceProxyInterface*
          install_attributes_proxy,
      org::chromium::CryptohomeMiscInterfaceProxyInterface* misc_proxy,
      cryptohome::Platform* platform)
      : LegacyCryptohomeInterfaceAdaptor(attestation_proxy,
                                         tpm_ownership_proxy,
                                         tpm_nvram_proxy,
                                         userdataauth_proxy,
                                         arc_quota_proxy,
                                         pkcs11_proxy,
                                         install_attributes_proxy,
                                         misc_proxy,
                                         platform) {}

  MOCK_METHOD(void,
              VirtualSendAsyncCallStatusSignal,
              (int32_t, bool, int32_t),
              (override));
  MOCK_METHOD(void,
              VirtualSendAsyncCallStatusWithDataSignal,
              (int32_t, bool, const std::vector<uint8_t>&),
              (override));
  MOCK_METHOD(void,
              VirtualSendDircryptoMigrationProgressSignal,
              (int32_t, uint64_t, uint64_t),
              (override));
  MOCK_METHOD(void, VirtualSendLowDiskSpaceSignal, (uint64_t), (override));
};

// Some common constants used for testing.
constexpr char kUsername1[] = "foo@gmail.com";
constexpr char kKeyLabel[] = "somelabel";
constexpr char kSecret[] = "blah";
constexpr char kSanitizedUsername1[] = "baadf00ddeadbeeffeedcafe";
constexpr char kPCARequest[] = "PCA\0Request\xFFMay\x80Have\0None.ASCII";
constexpr char kRequestOrigin[] = "SomeOrigin";

class LegacyCryptohomeInterfaceAdaptorTest : public ::testing::Test {
 public:
  LegacyCryptohomeInterfaceAdaptorTest() = default;
  ~LegacyCryptohomeInterfaceAdaptorTest() override = default;

  void SetUp() override {
    adaptor_.reset(new LegacyCryptohomeInterfaceAdaptorForTesting(
        &attestation_, &ownership_, &nvram_, &userdataauth_, &arc_quota_,
        &pkcs11_, &install_attributes_, &misc_, &platform_));

    account_.set_account_id(kUsername1);
    auth_.mutable_key()->set_secret(kSecret);
  }

 protected:
  // Mocks that will be passed into |adaptor_| for its internal use.
  NiceMock<org::chromium::AttestationProxyMock> attestation_;
  NiceMock<org::chromium::TpmOwnershipProxyMock> ownership_;
  NiceMock<org::chromium::TpmNvramProxyMock> nvram_;
  NiceMock<org::chromium::UserDataAuthInterfaceProxyMock> userdataauth_;
  NiceMock<org::chromium::ArcQuotaProxyMock> arc_quota_;
  NiceMock<org::chromium::CryptohomePkcs11InterfaceProxyMock> pkcs11_;
  NiceMock<org::chromium::InstallAttributesInterfaceProxyMock>
      install_attributes_;
  NiceMock<org::chromium::CryptohomeMiscInterfaceProxyMock> misc_;
  NiceMock<MockPlatform> platform_;

  // The adaptor that we'll be testing.
  std::unique_ptr<LegacyCryptohomeInterfaceAdaptorForTesting> adaptor_;

  // Default account identifier and authentication request set up with sane
  // value to avoid repeating the same pattern in many test.
  cryptohome::AccountIdentifier account_;
  cryptohome::AuthorizationRequest auth_;

 private:
  DISALLOW_COPY_AND_ASSIGN(LegacyCryptohomeInterfaceAdaptorTest);
};

// -------------------------- MountEx Related Tests --------------------------
TEST_F(LegacyCryptohomeInterfaceAdaptorTest, MountExSuccess) {
  cryptohome::MountRequest req;
  user_data_auth::MountRequest proxied_request;

  req.set_require_ephemeral(false);
  req.set_force_dircrypto_if_available(true);
  req.set_to_migrate_from_ecryptfs(false);
  req.set_public_mount(false);
  req.set_hidden_mount(false);

  EXPECT_CALL(userdataauth_, MountAsync(_, _, _, _))
      .WillOnce(DoAll(
          SaveArg<0>(&proxied_request),
          Invoke([](const user_data_auth::MountRequest& in_request,
                    const base::Callback<void(
                        const user_data_auth::MountReply&)>& success_callback,
                    const base::Callback<void(brillo::Error*)>& error_callback,
                    int timeout_ms) {
            user_data_auth::MountReply proxied_reply;
            proxied_reply.set_recreated(true);
            proxied_reply.set_sanitized_username(kSanitizedUsername1);
            success_callback.Run(proxied_reply);
          })));

  base::Optional<cryptohome::BaseReply> final_reply;
  std::unique_ptr<MockDBusMethodResponse<cryptohome::BaseReply>> response(
      new MockDBusMethodResponse<cryptohome::BaseReply>(nullptr));
  response->save_return_args(&final_reply);

  adaptor_->MountEx(std::move(response), account_, auth_, req);

  // Verify that Return() is indeed called at least once.
  ASSERT_TRUE(final_reply.has_value());

  // Verify its content
  EXPECT_EQ(cryptohome::CRYPTOHOME_ERROR_NOT_SET, final_reply->error());
  EXPECT_TRUE(final_reply->HasExtension(cryptohome::MountReply::reply));
  auto ext = final_reply->GetExtension(cryptohome::MountReply::reply);
  EXPECT_TRUE(ext.recreated());
  EXPECT_EQ(ext.sanitized_username(), kSanitizedUsername1);

  // Verify that the parameters passed to DBus Proxy (New interface) is correct.
  EXPECT_EQ(proxied_request.account().account_id(), kUsername1);
  EXPECT_EQ(proxied_request.authorization().key().secret(), kSecret);
  EXPECT_FALSE(proxied_request.require_ephemeral());
  EXPECT_TRUE(proxied_request.force_dircrypto_if_available());
  EXPECT_FALSE(proxied_request.to_migrate_from_ecryptfs());
  EXPECT_FALSE(proxied_request.public_mount());
  EXPECT_FALSE(proxied_request.hidden_mount());
  EXPECT_FALSE(proxied_request.guest_mount());
  EXPECT_FALSE(proxied_request.has_create());
}

TEST_F(LegacyCryptohomeInterfaceAdaptorTest, MountExSuccessWithCreate) {
  cryptohome::MountRequest req;
  user_data_auth::MountRequest proxied_request;

  req.set_require_ephemeral(false);
  req.set_force_dircrypto_if_available(true);
  req.set_to_migrate_from_ecryptfs(false);
  req.set_public_mount(false);
  req.set_hidden_mount(false);
  req.mutable_create()->set_force_ecryptfs(true);
  req.mutable_create()->set_copy_authorization_key(true);
  auto key = req.mutable_create()->add_keys();
  key->set_secret(kSecret);

  EXPECT_CALL(userdataauth_, MountAsync(_, _, _, _))
      .WillOnce(DoAll(
          SaveArg<0>(&proxied_request),
          Invoke([](const user_data_auth::MountRequest& in_request,
                    const base::Callback<void(
                        const user_data_auth::MountReply&)>& success_callback,
                    const base::Callback<void(brillo::Error*)>& error_callback,
                    int timeout_ms) {
            user_data_auth::MountReply proxied_reply;
            proxied_reply.set_recreated(true);
            proxied_reply.set_sanitized_username(kSanitizedUsername1);
            success_callback.Run(proxied_reply);
          })));

  int respond_count = 0;
  std::unique_ptr<MockDBusMethodResponse<cryptohome::BaseReply>> response(
      new MockDBusMethodResponse<cryptohome::BaseReply>(nullptr));
  response->set_return_callback(base::Bind(
      [](int* respond_count_ptr, const cryptohome::BaseReply& reply) {
        EXPECT_EQ(cryptohome::CRYPTOHOME_ERROR_NOT_SET, reply.error());
        EXPECT_TRUE(reply.HasExtension(cryptohome::MountReply::reply));
        auto ext = reply.GetExtension(cryptohome::MountReply::reply);
        EXPECT_TRUE(ext.recreated());
        EXPECT_EQ(ext.sanitized_username(), kSanitizedUsername1);
        (*respond_count_ptr)++;
      },
      &respond_count));
  adaptor_->MountEx(std::move(response), account_, auth_, req);

  // Verify that Return() is indeed called.
  EXPECT_EQ(respond_count, 1);

  // Verify that the parameters passed to DBus Proxy (New interface) is correct.
  EXPECT_EQ(proxied_request.account().account_id(), kUsername1);
  EXPECT_EQ(proxied_request.authorization().key().secret(), kSecret);
  EXPECT_FALSE(proxied_request.require_ephemeral());
  EXPECT_TRUE(proxied_request.force_dircrypto_if_available());
  EXPECT_FALSE(proxied_request.to_migrate_from_ecryptfs());
  EXPECT_FALSE(proxied_request.public_mount());
  EXPECT_FALSE(proxied_request.hidden_mount());
  EXPECT_FALSE(proxied_request.guest_mount());
  EXPECT_TRUE(proxied_request.has_create());
  EXPECT_TRUE(proxied_request.create().force_ecryptfs());
  EXPECT_TRUE(proxied_request.create().copy_authorization_key());
  EXPECT_EQ(proxied_request.create().keys_size(), 1);
  EXPECT_EQ(proxied_request.create().keys(0).secret(), kSecret);
}

TEST_F(LegacyCryptohomeInterfaceAdaptorTest, MountExFail) {
  cryptohome::MountRequest req;
  user_data_auth::MountRequest proxied_request;

  req.set_require_ephemeral(true);
  req.set_force_dircrypto_if_available(false);
  req.set_to_migrate_from_ecryptfs(true);
  req.set_public_mount(true);
  req.set_hidden_mount(true);

  EXPECT_CALL(userdataauth_, MountAsync(_, _, _, _))
      .WillOnce(DoAll(
          SaveArg<0>(&proxied_request),
          Invoke([](const user_data_auth::MountRequest& in_request,
                    const base::Callback<void(
                        const user_data_auth::MountReply&)>& success_callback,
                    const base::Callback<void(brillo::Error*)>& error_callback,
                    int timeout_ms) {
            user_data_auth::MountReply proxied_reply;
            proxied_reply.set_error(
                user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
            proxied_reply.set_recreated(false);
            success_callback.Run(proxied_reply);
          })));

  base::Optional<cryptohome::BaseReply> final_reply;
  std::unique_ptr<MockDBusMethodResponse<cryptohome::BaseReply>> response(
      new MockDBusMethodResponse<cryptohome::BaseReply>(nullptr));
  response->save_return_args(&final_reply);

  adaptor_->MountEx(std::move(response), account_, auth_, req);

  // Verify that Return() is indeed called at least once.
  ASSERT_TRUE(final_reply.has_value());

  // Verify its content
  EXPECT_EQ(cryptohome::CRYPTOHOME_ERROR_MOUNT_FATAL, final_reply->error());
  EXPECT_TRUE(final_reply->HasExtension(cryptohome::MountReply::reply));
  auto ext = final_reply->GetExtension(cryptohome::MountReply::reply);
  EXPECT_FALSE(ext.recreated());

  // Verify that the parameters passed to DBus Proxy (New interface) is correct.
  EXPECT_EQ(proxied_request.account().account_id(), kUsername1);
  EXPECT_EQ(proxied_request.authorization().key().secret(), kSecret);
  EXPECT_TRUE(proxied_request.require_ephemeral());
  EXPECT_FALSE(proxied_request.force_dircrypto_if_available());
  EXPECT_TRUE(proxied_request.to_migrate_from_ecryptfs());
  EXPECT_TRUE(proxied_request.public_mount());
  EXPECT_TRUE(proxied_request.hidden_mount());
  EXPECT_FALSE(proxied_request.guest_mount());
}

// ------------- TpmIsAttestationPrepared Related Tests -------------
TEST_F(LegacyCryptohomeInterfaceAdaptorTest,
       TpmIsAttestationPreparedSuccessResultTrue) {
  attestation::GetEnrollmentPreparationsRequest proxied_request;
  EXPECT_CALL(attestation_, GetEnrollmentPreparationsAsync(_, _, _, _))
      .WillOnce(DoAll(
          SaveArg<0>(&proxied_request),
          Invoke([](const attestation::GetEnrollmentPreparationsRequest&
                        in_request,
                    const base::Callback<void(
                        const attestation::GetEnrollmentPreparationsReply&)>&
                        success_callback,
                    const base::Callback<void(brillo::Error*)>& error_callback,
                    int timeout_ms) {
            attestation::GetEnrollmentPreparationsReply proxied_reply;
            proxied_reply.set_status(attestation::STATUS_SUCCESS);
            (*proxied_reply.mutable_enrollment_preparations())[0] = true;
            (*proxied_reply.mutable_enrollment_preparations())[1] = false;
            success_callback.Run(proxied_reply);
          })));

  base::Optional<bool> result;
  std::unique_ptr<MockDBusMethodResponse<bool>> response(
      new MockDBusMethodResponse<bool>(nullptr));
  response->save_return_args(&result);

  adaptor_->TpmIsAttestationPrepared(std::move(response));

  // Verify that Return() is indeed called at least once.
  ASSERT_TRUE(result.has_value());

  // Verify response content.
  EXPECT_TRUE(result.value());
}

TEST_F(LegacyCryptohomeInterfaceAdaptorTest,
       TpmIsAttestationPreparedSuccessResultFalse) {
  attestation::GetEnrollmentPreparationsRequest proxied_request;
  EXPECT_CALL(attestation_, GetEnrollmentPreparationsAsync(_, _, _, _))
      .WillOnce(DoAll(
          SaveArg<0>(&proxied_request),
          Invoke([](const attestation::GetEnrollmentPreparationsRequest&
                        in_request,
                    const base::Callback<void(
                        const attestation::GetEnrollmentPreparationsReply&)>&
                        success_callback,
                    const base::Callback<void(brillo::Error*)>& error_callback,
                    int timeout_ms) {
            attestation::GetEnrollmentPreparationsReply proxied_reply;
            proxied_reply.set_status(attestation::STATUS_SUCCESS);
            (*proxied_reply.mutable_enrollment_preparations())[0] = false;
            (*proxied_reply.mutable_enrollment_preparations())[1] = false;
            success_callback.Run(proxied_reply);
          })));

  base::Optional<bool> result;
  std::unique_ptr<MockDBusMethodResponse<bool>> response(
      new MockDBusMethodResponse<bool>(nullptr));
  response->save_return_args(&result);

  adaptor_->TpmIsAttestationPrepared(std::move(response));

  // Verify that Return() is indeed called at least once.
  ASSERT_TRUE(result.has_value());

  // Verify response content.
  EXPECT_FALSE(result.value());
}

// --------- TpmAttestationGetEnrollmentPreparationsEx Related Tests ---------
TEST_F(LegacyCryptohomeInterfaceAdaptorTest,
       TpmAttestationGetEnrollmentPreparationsExSuccess) {
  attestation::GetEnrollmentPreparationsRequest proxied_request;
  EXPECT_CALL(attestation_, GetEnrollmentPreparationsAsync(_, _, _, _))
      .WillOnce(DoAll(
          SaveArg<0>(&proxied_request),
          Invoke([](const attestation::GetEnrollmentPreparationsRequest&
                        in_request,
                    const base::Callback<void(
                        const attestation::GetEnrollmentPreparationsReply&)>&
                        success_callback,
                    const base::Callback<void(brillo::Error*)>& error_callback,
                    int timeout_ms) {
            attestation::GetEnrollmentPreparationsReply proxied_reply;
            proxied_reply.set_status(attestation::STATUS_SUCCESS);
            (*proxied_reply.mutable_enrollment_preparations())[0] = true;
            (*proxied_reply.mutable_enrollment_preparations())[1] = false;
            success_callback.Run(proxied_reply);
          })));

  base::Optional<cryptohome::BaseReply> result;
  std::unique_ptr<MockDBusMethodResponse<cryptohome::BaseReply>> response(
      new MockDBusMethodResponse<cryptohome::BaseReply>(nullptr));
  response->save_return_args(&result);

  cryptohome::AttestationGetEnrollmentPreparationsRequest in_request;
  in_request.set_pca_type(1);
  adaptor_->TpmAttestationGetEnrollmentPreparationsEx(std::move(response),
                                                      in_request);

  // Verify that Return() is indeed called at least once.
  ASSERT_TRUE(result.has_value());

  // Verify response content.
  EXPECT_EQ(result->error(), cryptohome::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_TRUE(
      result->HasExtension(AttestationGetEnrollmentPreparationsReply::reply));
  auto& ext =
      result->GetExtension(AttestationGetEnrollmentPreparationsReply::reply);
  EXPECT_EQ(ext.enrollment_preparations().size(), 2);
  ASSERT_TRUE(ext.enrollment_preparations().contains(0));
  EXPECT_TRUE(ext.enrollment_preparations().at(0));
  ASSERT_TRUE(ext.enrollment_preparations().contains(1));
  EXPECT_FALSE(ext.enrollment_preparations().at(1));

  // Check that the proxied request have the right ACA
  EXPECT_EQ(proxied_request.aca_type(), attestation::ACAType::TEST_ACA);
}

TEST_F(LegacyCryptohomeInterfaceAdaptorTest,
       TpmAttestationGetEnrollmentPreparationsExInvalidACA) {
  // GetEnrollmentPreparationsAsync() shouldn't get called because the ACA
  // specified is invalid.
  EXPECT_CALL(attestation_, GetEnrollmentPreparationsAsync(_, _, _, _))
      .Times(0);

  base::Optional<cryptohome::BaseReply> result;
  std::unique_ptr<MockDBusMethodResponse<cryptohome::BaseReply>> response(
      new MockDBusMethodResponse<cryptohome::BaseReply>(nullptr));
  response->save_return_args(&result);
  EXPECT_CALL(
      *response,
      ReplyWithError(_, brillo::errors::dbus::kDomain, DBUS_ERROR_NOT_SUPPORTED,
                     "Requested ACA type 99999 is not supported in "
                     "TpmAttestationGetEnrollmentPreparationsEx()"))
      .WillOnce(Return());
  cryptohome::AttestationGetEnrollmentPreparationsRequest in_request;
  in_request.set_pca_type(99999);
  adaptor_->TpmAttestationGetEnrollmentPreparationsEx(std::move(response),
                                                      in_request);

  // Verify that Return() is not called
  ASSERT_FALSE(result.has_value());
}

TEST_F(LegacyCryptohomeInterfaceAdaptorTest,
       TpmAttestationGetEnrollmentPreparationsExFailure) {
  attestation::GetEnrollmentPreparationsRequest proxied_request;
  EXPECT_CALL(attestation_, GetEnrollmentPreparationsAsync(_, _, _, _))
      .WillOnce(DoAll(
          SaveArg<0>(&proxied_request),
          Invoke([](const attestation::GetEnrollmentPreparationsRequest&
                        in_request,
                    const base::Callback<void(
                        const attestation::GetEnrollmentPreparationsReply&)>&
                        success_callback,
                    const base::Callback<void(brillo::Error*)>& error_callback,
                    int timeout_ms) {
            attestation::GetEnrollmentPreparationsReply proxied_reply;
            proxied_reply.set_status(
                attestation::STATUS_UNEXPECTED_DEVICE_ERROR);
            success_callback.Run(proxied_reply);
          })));

  base::Optional<cryptohome::BaseReply> result;
  std::unique_ptr<MockDBusMethodResponse<cryptohome::BaseReply>> response(
      new MockDBusMethodResponse<cryptohome::BaseReply>(nullptr));
  response->save_return_args(&result);

  cryptohome::AttestationGetEnrollmentPreparationsRequest in_request;
  in_request.set_pca_type(1);
  adaptor_->TpmAttestationGetEnrollmentPreparationsEx(std::move(response),
                                                      in_request);

  // Verify that Return() is indeed called at least once.
  ASSERT_TRUE(result.has_value());

  // Verify response content.
  EXPECT_EQ(result->error(),
            cryptohome::CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR);

  // Check that the proxied request have the right ACA
  EXPECT_EQ(proxied_request.aca_type(), attestation::ACAType::TEST_ACA);
}

// ------------- TpmAttestationCreateEnrollRequest Related Tests -------------
TEST_F(LegacyCryptohomeInterfaceAdaptorTest,
       TpmAttestationCreateEnrollRequestSuccess) {
  attestation::CreateEnrollRequestRequest proxied_request;
  EXPECT_CALL(attestation_, CreateEnrollRequestAsync(_, _, _, _))
      .WillOnce(DoAll(
          SaveArg<0>(&proxied_request),
          Invoke([](const attestation::CreateEnrollRequestRequest& in_request,
                    const base::Callback<void(
                        const attestation::CreateEnrollRequestReply&)>&
                        success_callback,
                    const base::Callback<void(brillo::Error*)>& error_callback,
                    int timeout_ms) {
            attestation::CreateEnrollRequestReply proxied_reply;
            proxied_reply.set_status(attestation::STATUS_SUCCESS);
            proxied_reply.set_pca_request(
                std::string(kPCARequest, sizeof(kPCARequest)));
            success_callback.Run(proxied_reply);
          })));

  base::Optional<std::vector<uint8_t>> result_pca_request;
  std::unique_ptr<MockDBusMethodResponse<std::vector<uint8_t>>> response(
      new MockDBusMethodResponse<std::vector<uint8_t>>(nullptr));
  response->save_return_args(&result_pca_request);

  adaptor_->TpmAttestationCreateEnrollRequest(
      std::move(response), static_cast<int>(attestation::TEST_ACA));

  // Verify that Return() is indeed called at least once.
  ASSERT_TRUE(result_pca_request.has_value());

  // Verify response content.
  EXPECT_EQ(
      result_pca_request.value(),
      std::vector<uint8_t>(kPCARequest, kPCARequest + sizeof(kPCARequest)));

  // Verify that the parameters passed to DBus Proxy (New interface) is correct.
  EXPECT_EQ(proxied_request.aca_type(), attestation::TEST_ACA);
}

TEST_F(LegacyCryptohomeInterfaceAdaptorTest,
       TpmAttestationCreateEnrollRequestInvalidACA) {
  std::unique_ptr<MockDBusMethodResponse<std::vector<uint8_t>>> response(
      new MockDBusMethodResponse<std::vector<uint8_t>>(nullptr));
  EXPECT_CALL(
      *response,
      ReplyWithError(_, brillo::errors::dbus::kDomain, DBUS_ERROR_NOT_SUPPORTED,
                     "Requested ACA type 99999 is not supported"))
      .WillOnce(Return());

  // 99999 is an invalid ACA
  adaptor_->TpmAttestationCreateEnrollRequest(std::move(response), 99999);
}

TEST_F(LegacyCryptohomeInterfaceAdaptorTest,
       TpmAttestationCreateEnrollRequestFailed) {
  attestation::CreateEnrollRequestRequest proxied_request;
  EXPECT_CALL(attestation_, CreateEnrollRequestAsync(_, _, _, _))
      .WillOnce(DoAll(
          SaveArg<0>(&proxied_request),
          Invoke([](const attestation::CreateEnrollRequestRequest& in_request,
                    const base::Callback<void(
                        const attestation::CreateEnrollRequestReply&)>&
                        success_callback,
                    const base::Callback<void(brillo::Error*)>& error_callback,
                    int timeout_ms) {
            attestation::CreateEnrollRequestReply reply;
            reply.set_status(attestation::STATUS_UNEXPECTED_DEVICE_ERROR);
            success_callback.Run(reply);
          })));

  std::unique_ptr<MockDBusMethodResponse<std::vector<uint8_t>>> response(
      new MockDBusMethodResponse<std::vector<uint8_t>>(nullptr));
  EXPECT_CALL(
      *response,
      ReplyWithError(_, brillo::errors::dbus::kDomain, DBUS_ERROR_FAILED,
                     "Attestation daemon returned status " +
                         std::to_string(static_cast<int>(
                             attestation::STATUS_UNEXPECTED_DEVICE_ERROR))))
      .WillOnce(Return());
  adaptor_->TpmAttestationCreateEnrollRequest(
      std::move(response), static_cast<int>(attestation::DEFAULT_ACA));

  // Verify that the parameters passed to DBus Proxy (New interface) is correct.
  EXPECT_EQ(proxied_request.aca_type(), attestation::DEFAULT_ACA);
}

// ------------------- TpmAttestationEnroll Related Tests -------------------
TEST_F(LegacyCryptohomeInterfaceAdaptorTest, TpmAttestationEnrollSuccess) {
  attestation::FinishEnrollRequest proxied_request;
  EXPECT_CALL(attestation_, FinishEnrollAsync(_, _, _, _))
      .WillOnce(DoAll(
          SaveArg<0>(&proxied_request),
          Invoke(
              [](const attestation::FinishEnrollRequest& in_request,
                 const base::Callback<void(
                     const attestation::FinishEnrollReply&)>& success_callback,
                 const base::Callback<void(brillo::Error*)>& error_callback,
                 int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) {
                attestation::FinishEnrollReply proxied_reply;
                proxied_reply.set_status(attestation::STATUS_SUCCESS);
                success_callback.Run(proxied_reply);
              })));

  base::Optional<bool> result_success;
  std::unique_ptr<MockDBusMethodResponse<bool>> response(
      new MockDBusMethodResponse<bool>(nullptr));
  response->save_return_args(&result_success);

  std::vector<uint8_t> pca_request(kPCARequest,
                                   kPCARequest + sizeof(kPCARequest));
  adaptor_->TpmAttestationEnroll(std::move(response),
                                 static_cast<int>(attestation::TEST_ACA),
                                 pca_request);

  // Verify that Return() is indeed called at least once.
  ASSERT_TRUE(result_success.has_value());

  // Verify the response.
  EXPECT_TRUE(result_success.value());

  // Verify that the parameters passed to DBus Proxy (New interface) is correct.
  EXPECT_EQ(proxied_request.aca_type(), attestation::TEST_ACA);
  EXPECT_EQ(proxied_request.pca_response(),
            std::string(kPCARequest, kPCARequest + sizeof(kPCARequest)));
}

TEST_F(LegacyCryptohomeInterfaceAdaptorTest, TpmAttestationEnrollInvalidACA) {
  std::unique_ptr<MockDBusMethodResponse<bool>> response(
      new MockDBusMethodResponse<bool>(nullptr));
  EXPECT_CALL(
      *response,
      ReplyWithError(_, brillo::errors::dbus::kDomain, DBUS_ERROR_NOT_SUPPORTED,
                     "Requested ACA type 99999 is not supported"))
      .WillOnce(Return());
  std::vector<uint8_t> pca_request(kPCARequest,
                                   kPCARequest + sizeof(kPCARequest));

  // 99999 is an invalid ACA
  adaptor_->TpmAttestationEnroll(std::move(response), 99999, pca_request);
}

TEST_F(LegacyCryptohomeInterfaceAdaptorTest, TpmAttestationEnrollFailed) {
  attestation::FinishEnrollRequest proxied_request;
  EXPECT_CALL(attestation_, FinishEnrollAsync(_, _, _, _))
      .WillOnce(DoAll(
          SaveArg<0>(&proxied_request),
          Invoke(
              [](const attestation::FinishEnrollRequest& in_request,
                 const base::Callback<void(
                     const attestation::FinishEnrollReply&)>& success_callback,
                 const base::Callback<void(brillo::Error*)>& error_callback,
                 int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) {
                attestation::FinishEnrollReply reply;
                reply.set_status(attestation::STATUS_NOT_READY);
                success_callback.Run(reply);
              })));

  base::Optional<bool> result_success;
  std::unique_ptr<MockDBusMethodResponse<bool>> response(
      new MockDBusMethodResponse<bool>(nullptr));
  response->save_return_args(&result_success);

  std::vector<uint8_t> pca_request(kPCARequest,
                                   kPCARequest + sizeof(kPCARequest));
  adaptor_->TpmAttestationEnroll(std::move(response),
                                 static_cast<int>(attestation::DEFAULT_ACA),
                                 pca_request);

  // Verify that Return() is indeed called at least once.
  ASSERT_TRUE(result_success.has_value());

  // Verify the response.
  EXPECT_FALSE(result_success.value());

  // Verify that the parameters passed to DBus Proxy (New interface) is correct.
  EXPECT_EQ(proxied_request.aca_type(), attestation::DEFAULT_ACA);
  EXPECT_EQ(proxied_request.pca_response(),
            std::string(kPCARequest, kPCARequest + sizeof(kPCARequest)));
}

// ------------- TpmAttestationCreateCertRequest Related Tests -------------
TEST_F(LegacyCryptohomeInterfaceAdaptorTest,
       TpmAttestationCreateCertRequestSuccess) {
  attestation::CreateCertificateRequestRequest proxied_request;
  EXPECT_CALL(attestation_, CreateCertificateRequestAsync(_, _, _, _))
      .WillOnce(DoAll(
          SaveArg<0>(&proxied_request),
          Invoke(
              [](const attestation::CreateCertificateRequestRequest& in_request,
                 const base::Callback<void(
                     const attestation::CreateCertificateRequestReply&)>&
                     success_callback,
                 const base::Callback<void(brillo::Error*)>& error_callback,
                 int timeout_ms) {
                attestation::CreateCertificateRequestReply proxied_reply;
                proxied_reply.set_status(attestation::STATUS_SUCCESS);
                proxied_reply.set_pca_request(
                    std::string(kPCARequest, sizeof(kPCARequest)));
                success_callback.Run(proxied_reply);
              })));

  base::Optional<std::vector<uint8_t>> result_pca_request;
  std::unique_ptr<MockDBusMethodResponse<std::vector<uint8_t>>> response(
      new MockDBusMethodResponse<std::vector<uint8_t>>(nullptr));
  response->save_return_args(&result_pca_request);

  adaptor_->TpmAttestationCreateCertRequest(
      std::move(response), static_cast<int>(attestation::TEST_ACA),
      static_cast<int>(attestation::CONTENT_PROTECTION_CERTIFICATE), kUsername1,
      kRequestOrigin);

  // Verify that Return() is indeed called at least once.
  ASSERT_TRUE(result_pca_request.has_value());

  // Verify response content.
  EXPECT_EQ(
      result_pca_request.value(),
      std::vector<uint8_t>(kPCARequest, kPCARequest + sizeof(kPCARequest)));

  // Verify that the parameters passed to DBus Proxy (New interface) is correct.
  EXPECT_EQ(proxied_request.aca_type(), attestation::TEST_ACA);
  EXPECT_EQ(proxied_request.username(), kUsername1);
  EXPECT_EQ(proxied_request.request_origin(), kRequestOrigin);
  EXPECT_EQ(proxied_request.certificate_profile(),
            attestation::CONTENT_PROTECTION_CERTIFICATE);
}

TEST_F(LegacyCryptohomeInterfaceAdaptorTest,
       TpmAttestationCreateCertRequestInvalidACA) {
  std::unique_ptr<MockDBusMethodResponse<std::vector<uint8_t>>> response(
      new MockDBusMethodResponse<std::vector<uint8_t>>(nullptr));
  EXPECT_CALL(
      *response,
      ReplyWithError(_, brillo::errors::dbus::kDomain, DBUS_ERROR_NOT_SUPPORTED,
                     "Requested ACA type 99999 is not supported"))
      .WillOnce(Return());

  // 99999 is an invalid ACA
  adaptor_->TpmAttestationCreateCertRequest(std::move(response), 99999, 2,
                                            kUsername1, kRequestOrigin);
}

TEST_F(LegacyCryptohomeInterfaceAdaptorTest,
       TpmAttestationCreateCertRequestFailed) {
  attestation::CreateCertificateRequestRequest proxied_request;
  EXPECT_CALL(attestation_, CreateCertificateRequestAsync(_, _, _, _))
      .WillOnce(DoAll(
          SaveArg<0>(&proxied_request),
          Invoke(
              [](const attestation::CreateCertificateRequestRequest& in_request,
                 const base::Callback<void(
                     const attestation::CreateCertificateRequestReply&)>&
                     success_callback,
                 const base::Callback<void(brillo::Error*)>& error_callback,
                 int timeout_ms) {
                attestation::CreateCertificateRequestReply reply;
                reply.set_status(attestation::STATUS_UNEXPECTED_DEVICE_ERROR);
                success_callback.Run(reply);
              })));

  std::unique_ptr<MockDBusMethodResponse<std::vector<uint8_t>>> response(
      new MockDBusMethodResponse<std::vector<uint8_t>>(nullptr));
  EXPECT_CALL(
      *response,
      ReplyWithError(_, brillo::errors::dbus::kDomain, DBUS_ERROR_FAILED,
                     "Attestation daemon returned status " +
                         std::to_string(static_cast<int>(
                             attestation::STATUS_UNEXPECTED_DEVICE_ERROR))))
      .WillOnce(Return());

  // 12345 is an invalid certificate profile and should result in
  // ENTERPRISE_USER_CERTIFICATE.
  adaptor_->TpmAttestationCreateCertRequest(
      std::move(response), static_cast<int>(attestation::DEFAULT_ACA), 12345,
      kUsername1, kRequestOrigin);

  // Verify that the parameters passed to DBus Proxy (New interface) is correct.
  EXPECT_EQ(proxied_request.aca_type(), attestation::DEFAULT_ACA);
  EXPECT_EQ(proxied_request.username(), kUsername1);
  EXPECT_EQ(proxied_request.request_origin(), kRequestOrigin);
  EXPECT_EQ(proxied_request.certificate_profile(),
            attestation::ENTERPRISE_USER_CERTIFICATE);
}

// ------------- TpmAttestationDeleteKey(s) Related Tests -------------
TEST_F(LegacyCryptohomeInterfaceAdaptorTest, TpmAttestationDeleteKeysSuccess) {
  attestation::DeleteKeysRequest proxied_request;
  EXPECT_CALL(attestation_, DeleteKeysAsync(_, _, _, _))
      .WillOnce(DoAll(
          SaveArg<0>(&proxied_request),
          Invoke([](const attestation::DeleteKeysRequest& in_request,
                    const base::Callback<void(
                        const attestation::DeleteKeysReply&)>& success_callback,
                    const base::Callback<void(brillo::Error*)>& error_callback,
                    int timeout_ms) {
            attestation::DeleteKeysReply proxied_reply;
            proxied_reply.set_status(attestation::STATUS_SUCCESS);
            success_callback.Run(proxied_reply);
          })));

  base::Optional<bool> result;
  std::unique_ptr<MockDBusMethodResponse<bool>> response(
      new MockDBusMethodResponse<bool>(nullptr));
  response->save_return_args(&result);

  adaptor_->TpmAttestationDeleteKeys(std::move(response),
                                     /*is_user_specific=*/true, kUsername1,
                                     kKeyLabel);

  // Verify that Return() is indeed called at least once.
  ASSERT_TRUE(result.has_value());

  // Verify response content.
  EXPECT_TRUE(result.value());

  // Verify that the parameters passed to DBus Proxy (New interface) are
  // correct.
  EXPECT_EQ(proxied_request.username(), kUsername1);
  EXPECT_EQ(proxied_request.key_label_match(), kKeyLabel);
  EXPECT_EQ(proxied_request.match_behavior(),
            attestation::DeleteKeysRequest::MATCH_BEHAVIOR_PREFIX);
}

TEST_F(LegacyCryptohomeInterfaceAdaptorTest, TpmAttestationDeleteKeySuccess) {
  attestation::DeleteKeysRequest proxied_request;
  EXPECT_CALL(attestation_, DeleteKeysAsync(_, _, _, _))
      .WillOnce(DoAll(
          SaveArg<0>(&proxied_request),
          Invoke([](const attestation::DeleteKeysRequest& in_request,
                    const base::Callback<void(
                        const attestation::DeleteKeysReply&)>& success_callback,
                    const base::Callback<void(brillo::Error*)>& error_callback,
                    int timeout_ms) {
            attestation::DeleteKeysReply proxied_reply;
            proxied_reply.set_status(attestation::STATUS_SUCCESS);
            success_callback.Run(proxied_reply);
          })));

  base::Optional<bool> result;
  std::unique_ptr<MockDBusMethodResponse<bool>> response(
      new MockDBusMethodResponse<bool>(nullptr));
  response->save_return_args(&result);

  adaptor_->TpmAttestationDeleteKey(std::move(response),
                                    /*is_user_specific=*/true, kUsername1,
                                    kKeyLabel);

  // Verify that Return() is indeed called at least once.
  ASSERT_TRUE(result.has_value());

  // Verify response content.
  EXPECT_TRUE(result.value());

  // Verify that the parameters passed to DBus Proxy (New interface) are
  // correct.
  EXPECT_EQ(proxied_request.username(), kUsername1);
  EXPECT_EQ(proxied_request.key_label_match(), kKeyLabel);
  EXPECT_EQ(proxied_request.match_behavior(),
            attestation::DeleteKeysRequest::MATCH_BEHAVIOR_EXACT);
}

// -------------------- MigrateToDircrypto Related Tests --------------------
TEST_F(LegacyCryptohomeInterfaceAdaptorTest, MigrateToDircryptoSuccess) {
  // Note that failure case is NOT tested because this method does not return
  // anything so the failure case is no different from the success case.

  user_data_auth::StartMigrateToDircryptoRequest proxied_request;
  EXPECT_CALL(userdataauth_, StartMigrateToDircryptoAsync(_, _, _, _))
      .WillOnce(DoAll(
          SaveArg<0>(&proxied_request),
          Invoke([](const user_data_auth::StartMigrateToDircryptoRequest&
                        in_request,
                    const base::Callback<void(
                        const user_data_auth::StartMigrateToDircryptoReply&)>&
                        success_callback,
                    const base::Callback<void(brillo::Error*)>& error_callback,
                    int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) {
            user_data_auth::StartMigrateToDircryptoReply proxied_reply;
            proxied_reply.set_error(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
            success_callback.Run(proxied_reply);
          })));

  bool called = false;
  std::unique_ptr<MockDBusMethodResponse<>> response(
      new MockDBusMethodResponse<>(nullptr));
  response->set_return_callback(base::Bind(
      [](bool* called_ptr) {
        // Return can only be called once
        ASSERT_FALSE(*called_ptr);
        *called_ptr = true;
      },
      &called));

  MigrateToDircryptoRequest request;
  request.set_minimal_migration(true);
  adaptor_->MigrateToDircrypto(std::move(response), account_, request);

  // Verify that Return() is indeed called at least once.
  ASSERT_TRUE(called);

  // Verify that the parameters passed to DBus Proxy (New interface) is correct.
  EXPECT_TRUE(proxied_request.minimal_migration());
  EXPECT_EQ(proxied_request.account_id().account_id(), kUsername1);
}

TEST_F(LegacyCryptohomeInterfaceAdaptorTest,
       DircryptoMigrationProgressSignalSanity) {
  constexpr uint64_t kCurrentBytes = 1234567890123ULL;
  constexpr uint64_t kTotalBytes = 9876543210987ULL;
  static_assert(kTotalBytes > kCurrentBytes,
                "Incorrect constant test values in "
                "DircryptoMigrationProgressSignalSanity");

  user_data_auth::DircryptoMigrationProgress progress;
  progress.set_status(user_data_auth::DIRCRYPTO_MIGRATION_SUCCESS);
  progress.set_current_bytes(kCurrentBytes);
  progress.set_total_bytes(kTotalBytes);

  EXPECT_CALL(*adaptor_,
              VirtualSendDircryptoMigrationProgressSignal(
                  DIRCRYPTO_MIGRATION_SUCCESS, kCurrentBytes, kTotalBytes))
      .WillOnce(Return());

  adaptor_->OnDircryptoMigrationProgressSignalForTestingOnly(progress);
}

// -------------------- LowDiskSpace Signal Related Tests --------------------
TEST_F(LegacyCryptohomeInterfaceAdaptorTest, LowDiskSpaceSignalSanity) {
  constexpr uint64_t kFreeDiskSpace = 998877665544ULL;

  user_data_auth::LowDiskSpace payload;
  payload.set_disk_free_bytes(kFreeDiskSpace);

  EXPECT_CALL(*adaptor_, VirtualSendLowDiskSpaceSignal(kFreeDiskSpace))
      .WillOnce(Return());

  adaptor_->OnLowDiskSpaceSignalForTestingOnly(payload);
}

// --------------- TPM Ownership Interface Related Tests ---------------------
TEST_F(LegacyCryptohomeInterfaceAdaptorTest, GetVersionInfo) {
  EXPECT_CALL(ownership_, GetVersionInfoAsync(
                              _, _, _, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT))
      .WillOnce(
          Invoke([](const tpm_manager::GetVersionInfoRequest& in_request,
                    const base::Callback<void(
                        const tpm_manager::GetVersionInfoReply& /*reply*/)>&
                        success_callback,
                    const base::Callback<void(brillo::Error*)>&
                    /* error_callback */,
                    int /* timeout_ms */) {
            tpm_manager::GetVersionInfoReply info;
            info.set_family(1);
            info.set_spec_level(2);
            info.set_manufacturer(3);
            info.set_tpm_model(4);
            info.set_firmware_version(5);
            info.set_vendor_specific("ab");
            success_callback.Run(info);
          }));

  using VersionInfoResponse =
      MockDBusMethodResponse<uint32_t, uint64_t, uint32_t, uint32_t, uint64_t,
                             std::string>;
  std::unique_ptr<VersionInfoResponse> response =
      std::make_unique<VersionInfoResponse>(nullptr);

  base::Optional<uint32_t> family;
  base::Optional<uint64_t> spec_level;
  base::Optional<uint32_t> manufacture;
  base::Optional<uint32_t> tpm_model;
  base::Optional<uint64_t> firmware_version;
  base::Optional<std::string> vendor_specific;
  response->save_return_args(&family, &spec_level, &manufacture, &tpm_model,
                             &firmware_version, &vendor_specific);

  adaptor_->TpmGetVersionStructured(std::move(response));

  EXPECT_TRUE(family.has_value());
  EXPECT_EQ(*family, 1);
  EXPECT_TRUE(spec_level.has_value());
  EXPECT_EQ(*spec_level, 2);
  EXPECT_TRUE(manufacture.has_value());
  EXPECT_EQ(*manufacture, 3);
  EXPECT_TRUE(tpm_model.has_value());
  EXPECT_EQ(*tpm_model, 4);
  EXPECT_TRUE(firmware_version.has_value());
  EXPECT_EQ(*firmware_version, 5);
  EXPECT_TRUE(vendor_specific.has_value());
  EXPECT_EQ(*vendor_specific, base::HexEncode("ab", 2));
}

// This class holds the various extra setups to facilitate testing GetTpmStatus
class LegacyCryptohomeInterfaceAdaptorTestForGetTpmStatus
    : public LegacyCryptohomeInterfaceAdaptorTest {
 public:
  LegacyCryptohomeInterfaceAdaptorTestForGetTpmStatus() = default;
  ~LegacyCryptohomeInterfaceAdaptorTestForGetTpmStatus() override = default;

 protected:
  void SetUp() override {
    LegacyCryptohomeInterfaceAdaptorTest::SetUp();

    status_reply_.set_enabled(true);
    status_reply_.set_owned(true);
    status_reply_.mutable_local_data()->set_owner_password(kPassword);

    da_reply_.set_dictionary_attack_counter(kDACounter);
    da_reply_.set_dictionary_attack_threshold(kDAThreshold);
    da_reply_.set_dictionary_attack_lockout_in_effect(false);
    da_reply_.set_dictionary_attack_lockout_seconds_remaining(kDALockoutRem);

    install_attr_reply_.set_state(
        user_data_auth::InstallAttributesState::VALID);

    attestation_reply_.set_prepared_for_enrollment(true);
    attestation_reply_.set_enrolled(true);
    attestation_reply_.set_verified_boot(true);
    auto* identity1 = attestation_reply_.mutable_identities()->Add();
    identity1->set_features(kFeature1);
    auto* identity2 = attestation_reply_.mutable_identities()->Add();
    identity2->set_features(kFeature2);

    attestation::GetStatusReply::IdentityCertificate identity_cert1;
    identity_cert1.set_identity(kFeature1);
    identity_cert1.set_aca(kACA1);
    attestation_reply_.mutable_identity_certificates()->insert(
        google::protobuf::Map<
            int, attestation::GetStatusReply::IdentityCertificate>::
            value_type(kACA1, identity_cert1));

    attestation::GetStatusReply::IdentityCertificate identity_cert2;
    identity_cert2.set_identity(kFeature2);
    identity_cert2.set_aca(kACA2);
    attestation_reply_.mutable_identity_certificates()->insert(
        google::protobuf::Map<
            int, attestation::GetStatusReply::IdentityCertificate>::
            value_type(kACA2, identity_cert2));
  }

  void ExpectGetTpmStatus(
      const base::Optional<tpm_manager::GetTpmStatusReply>& reply) {
    EXPECT_CALL(
        ownership_,
        GetTpmStatusAsync(_, _, _, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT))
        .WillOnce(Invoke(
            [reply](const tpm_manager::GetTpmStatusRequest& in_request,
                    const base::Callback<void(
                        const tpm_manager::GetTpmStatusReply& /*reply*/)>&
                        success_callback,
                    const base::Callback<void(brillo::Error*)>& error_callback,
                    int timeout_ms) {
              if (reply.has_value()) {
                // If |reply| has value, then the method will be successful and
                // |reply| will be returned.
                success_callback.Run(reply.value());
              } else {
                // If not, the method will return an error.
                error_callback.Run(CreateDefaultError(FROM_HERE).get());
              }
            }));
  }

  void ExpectGetDictionaryAttackInfo(
      const base::Optional<tpm_manager::GetDictionaryAttackInfoReply>& reply) {
    EXPECT_CALL(ownership_,
                GetDictionaryAttackInfoAsync(
                    _, _, _, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT))
        .WillOnce(Invoke(
            [reply](
                const tpm_manager::GetDictionaryAttackInfoRequest& in_request,
                const base::Callback<void(
                    const tpm_manager::
                        GetDictionaryAttackInfoReply& /*reply*/)>&
                    success_callback,
                const base::Callback<void(brillo::Error*)>& error_callback,
                int timeout_ms) {
              if (reply.has_value()) {
                success_callback.Run(reply.value());
              } else {
                error_callback.Run(CreateDefaultError(FROM_HERE).get());
              }
            }));
  }

  void ExpectInstallAttributesGetStatus(
      const base::Optional<user_data_auth::InstallAttributesGetStatusReply>&
          reply) {
    EXPECT_CALL(install_attributes_,
                InstallAttributesGetStatusAsync(
                    _, _, _, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT))
        .WillOnce(Invoke(
            [reply](const user_data_auth::InstallAttributesGetStatusRequest&
                        in_request,
                    const base::Callback<void(
                        const user_data_auth::
                            InstallAttributesGetStatusReply& /*reply*/)>&
                        success_callback,
                    const base::Callback<void(brillo::Error*)>& error_callback,
                    int timeout_ms) {
              if (reply.has_value()) {
                success_callback.Run(reply.value());
              } else {
                error_callback.Run(CreateDefaultError(FROM_HERE).get());
              }
            }));
  }

  void ExpectAttestationGetStatus(
      const base::Optional<attestation::GetStatusReply>& reply) {
    EXPECT_CALL(attestation_, GetStatusAsync(_, _, _, _))
        .WillOnce(Invoke(
            [reply](const attestation::GetStatusRequest& in_request,
                    const base::Callback<void(
                        const attestation::GetStatusReply& /*reply*/)>&
                        success_callback,
                    const base::Callback<void(brillo::Error*)>& error_callback,
                    int timeout_ms) {
              if (reply.has_value()) {
                success_callback.Run(reply.value());
              } else {
                error_callback.Run(CreateDefaultError(FROM_HERE).get());
              }
            }));
  }

  // The reply we'll get from various proxies.
  tpm_manager::GetTpmStatusReply status_reply_;
  tpm_manager::GetDictionaryAttackInfoReply da_reply_;
  user_data_auth::InstallAttributesGetStatusReply install_attr_reply_;
  attestation::GetStatusReply attestation_reply_;

  // The request we send to GetTpmStatus()
  cryptohome::GetTpmStatusRequest in_request_;

  // Constants used during testing
  static constexpr char kPassword[] = "YetAnotherPassword";
  static constexpr int kDACounter = 42;      // The answer
  static constexpr int kDAThreshold = 4200;  // 100x The answer!!
  static constexpr int kDALockoutRem = 0;
  static constexpr int kFeature1 = 0xDEADBEEF;
  static constexpr int kFeature2 = 0xBAADF00D;
  static constexpr int kACA1 = 1;
  static constexpr int kACA2 = 2;

 private:
  static brillo::ErrorPtr CreateDefaultError(const base::Location& from_here) {
    brillo::ErrorPtr error;
    brillo::Error::AddTo(&error, from_here, brillo::errors::dbus::kDomain,
                         DBUS_ERROR_FAILED, "Here's a fake error");
    return error;
  }

  DISALLOW_COPY_AND_ASSIGN(LegacyCryptohomeInterfaceAdaptorTestForGetTpmStatus);
};

constexpr char LegacyCryptohomeInterfaceAdaptorTestForGetTpmStatus::kPassword[];
constexpr int LegacyCryptohomeInterfaceAdaptorTestForGetTpmStatus::kDACounter;
constexpr int LegacyCryptohomeInterfaceAdaptorTestForGetTpmStatus::kDAThreshold;
constexpr int
    LegacyCryptohomeInterfaceAdaptorTestForGetTpmStatus::kDALockoutRem;
constexpr int LegacyCryptohomeInterfaceAdaptorTestForGetTpmStatus::kFeature1;
constexpr int LegacyCryptohomeInterfaceAdaptorTestForGetTpmStatus::kFeature2;
constexpr int LegacyCryptohomeInterfaceAdaptorTestForGetTpmStatus::kACA1;
constexpr int LegacyCryptohomeInterfaceAdaptorTestForGetTpmStatus::kACA2;

TEST_F(LegacyCryptohomeInterfaceAdaptorTestForGetTpmStatus,
       GetTpmStatusSanity) {
  // Setup GetTpmStatus in tpm_manager to successfully return |status_reply_|
  ExpectGetTpmStatus(status_reply_);
  // Setup GetDictionaryAttackInfo in tpm_manager to successfully return
  // |da_reply_|
  ExpectGetDictionaryAttackInfo(da_reply_);
  // Setup GetStatus in cryptohome/install attributes interface to sucessfully
  // return |install_attr_reply_|
  ExpectInstallAttributesGetStatus(install_attr_reply_);
  // Setup GetStatus in attestation to successfully return |attestation_reply_|
  ExpectAttestationGetStatus(attestation_reply_);

  EXPECT_CALL(platform_,
              FileExists(base::FilePath(cryptohome::kLockedToSingleUserFile)))
      .WillOnce(Return(true));

  base::Optional<cryptohome::BaseReply> final_reply;

  std::unique_ptr<MockDBusMethodResponse<cryptohome::BaseReply>> response(
      new MockDBusMethodResponse<cryptohome::BaseReply>(nullptr));
  EXPECT_CALL(*response, ReplyWithError(_)).Times(0);
  EXPECT_CALL(*response, ReplyWithError(_, _, _, _)).Times(0);
  response->save_return_args(&final_reply);

  adaptor_->GetTpmStatus(std::move(response), in_request_);

  ASSERT_TRUE(final_reply.has_value());
  ASSERT_TRUE(final_reply->HasExtension(cryptohome::GetTpmStatusReply::reply));
  const auto& ext =
      final_reply->GetExtension(cryptohome::GetTpmStatusReply::reply);

  EXPECT_EQ(ext.enabled(), status_reply_.enabled());
  EXPECT_EQ(ext.owned(), status_reply_.owned());
  // |initialized| should be false because the owner password is supplied in
  // |status_reply_|.
  EXPECT_FALSE(ext.initialized());
  EXPECT_EQ(ext.owner_password(), status_reply_.local_data().owner_password());

  EXPECT_EQ(ext.dictionary_attack_counter(),
            da_reply_.dictionary_attack_counter());
  EXPECT_EQ(ext.dictionary_attack_threshold(),
            da_reply_.dictionary_attack_threshold());
  EXPECT_EQ(ext.dictionary_attack_lockout_in_effect(),
            da_reply_.dictionary_attack_lockout_in_effect());
  EXPECT_EQ(ext.dictionary_attack_lockout_seconds_remaining(),
            da_reply_.dictionary_attack_lockout_seconds_remaining());

  // |install_lockbox_finalized| is true because |install_attr_reply_.state()|
  // is VALID.
  EXPECT_TRUE(ext.install_lockbox_finalized());
  // |ext.boot_lockbox_finalized| is deprecated and always set to false.
  EXPECT_FALSE(ext.boot_lockbox_finalized());
  // |ext.is_locked_to_single_user| is set according to the flag file specified
  // in kLockedToSingleUserFile.
  EXPECT_TRUE(ext.is_locked_to_single_user());

  EXPECT_EQ(ext.attestation_prepared(),
            attestation_reply_.prepared_for_enrollment());
  EXPECT_EQ(ext.attestation_enrolled(), attestation_reply_.enrolled());
  EXPECT_EQ(ext.verified_boot_measured(), attestation_reply_.verified_boot());

  EXPECT_EQ(ext.identities().size(), attestation_reply_.identities().size());
  EXPECT_EQ(ext.identities().Get(0).features(),
            attestation_reply_.identities().Get(0).features());
  EXPECT_EQ(ext.identities().Get(1).features(),
            attestation_reply_.identities().Get(1).features());

  EXPECT_EQ(ext.identity_certificates().size(),
            attestation_reply_.identity_certificates().size());
  EXPECT_EQ(ext.identity_certificates().count(kACA1), 1);
  EXPECT_EQ(ext.identity_certificates().at(kACA1).identity(),
            attestation_reply_.identity_certificates().at(kACA1).identity());
  EXPECT_EQ(ext.identity_certificates().at(kACA1).aca(),
            attestation_reply_.identity_certificates().at(kACA1).aca());
  EXPECT_EQ(ext.identity_certificates().count(kACA2), 1);
  EXPECT_EQ(ext.identity_certificates().at(kACA2).identity(),
            attestation_reply_.identity_certificates().at(kACA2).identity());
  EXPECT_EQ(ext.identity_certificates().at(kACA2).aca(),
            attestation_reply_.identity_certificates().at(kACA2).aca());
}

TEST_F(LegacyCryptohomeInterfaceAdaptorTestForGetTpmStatus,
       GetTpmStatusInitialized) {
  // If it's owned and no there's no owner_password, then it's initialized.
  status_reply_.mutable_local_data()->clear_owner_password();

  // Setup GetTpmStatus in tpm_manager to successfully return |status_reply_|
  ExpectGetTpmStatus(status_reply_);
  // Setup GetDictionaryAttackInfo in tpm_manager to successfully return
  // |da_reply_|
  ExpectGetDictionaryAttackInfo(da_reply_);
  // Setup GetStatus in cryptohome/install attributes interface to sucessfully
  // return |install_attr_reply_|
  ExpectInstallAttributesGetStatus(install_attr_reply_);
  // Setup GetStatus in attestation to successfully return |attestation_reply_|
  ExpectAttestationGetStatus(attestation_reply_);

  EXPECT_CALL(platform_,
              FileExists(base::FilePath(cryptohome::kLockedToSingleUserFile)))
      .WillOnce(Return(false));

  base::Optional<cryptohome::BaseReply> final_reply;

  std::unique_ptr<MockDBusMethodResponse<cryptohome::BaseReply>> response(
      new MockDBusMethodResponse<cryptohome::BaseReply>(nullptr));
  EXPECT_CALL(*response, ReplyWithError(_)).Times(0);
  EXPECT_CALL(*response, ReplyWithError(_, _, _, _)).Times(0);
  response->save_return_args(&final_reply);

  adaptor_->GetTpmStatus(std::move(response), in_request_);

  ASSERT_TRUE(final_reply.has_value());
  ASSERT_TRUE(final_reply->HasExtension(cryptohome::GetTpmStatusReply::reply));
  const auto& ext =
      final_reply->GetExtension(cryptohome::GetTpmStatusReply::reply);

  // |initialized| is set to true because owner_password is cleared but owned is
  // true.
  EXPECT_TRUE(ext.initialized());
  EXPECT_EQ(ext.owner_password(), status_reply_.local_data().owner_password());

  EXPECT_FALSE(ext.is_locked_to_single_user());
}

TEST_F(LegacyCryptohomeInterfaceAdaptorTestForGetTpmStatus,
       GetTpmStatusStageOwnershipStatusFail) {
  status_reply_.set_status(tpm_manager::STATUS_DEVICE_ERROR);

  // Setup GetTpmStatus in tpm_manager to successfully return |status_reply_|
  ExpectGetTpmStatus(status_reply_);

  base::Optional<cryptohome::BaseReply> final_reply;

  std::unique_ptr<MockDBusMethodResponse<cryptohome::BaseReply>> response(
      new MockDBusMethodResponse<cryptohome::BaseReply>(nullptr));
  EXPECT_CALL(*response, ReplyWithError(_, _, _, _)).WillOnce(Return());
  response->save_return_args(&final_reply);

  adaptor_->GetTpmStatus(std::move(response), in_request_);

  ASSERT_FALSE(final_reply.has_value());
}

TEST_F(LegacyCryptohomeInterfaceAdaptorTestForGetTpmStatus,
       GetTpmStatusStageDictionaryAttackFail) {
  da_reply_.set_status(tpm_manager::STATUS_DEVICE_ERROR);

  // Setup GetTpmStatus in tpm_manager to successfully return |status_reply_|
  ExpectGetTpmStatus(status_reply_);
  // Setup GetDictionaryAttackInfo in tpm_manager to successfully return
  // |da_reply_|
  ExpectGetDictionaryAttackInfo(da_reply_);
  // Setup GetStatus in cryptohome/install attributes interface to sucessfully
  // return |install_attr_reply_|
  ExpectInstallAttributesGetStatus(install_attr_reply_);
  // Setup GetStatus in attestation to successfully return |attestation_reply_|
  ExpectAttestationGetStatus(attestation_reply_);

  base::Optional<cryptohome::BaseReply> final_reply;

  std::unique_ptr<MockDBusMethodResponse<cryptohome::BaseReply>> response(
      new MockDBusMethodResponse<cryptohome::BaseReply>(nullptr));
  EXPECT_CALL(*response, ReplyWithError(_)).Times(0);
  EXPECT_CALL(*response, ReplyWithError(_, _, _, _)).Times(0);
  response->save_return_args(&final_reply);

  adaptor_->GetTpmStatus(std::move(response), in_request_);

  ASSERT_TRUE(final_reply.has_value());
  ASSERT_TRUE(final_reply->HasExtension(cryptohome::GetTpmStatusReply::reply));
  const auto& ext =
      final_reply->GetExtension(cryptohome::GetTpmStatusReply::reply);

  // These are the default values when call to retrieve DictionaryAttack info
  // failed.
  EXPECT_EQ(ext.dictionary_attack_counter(), 0);
  EXPECT_EQ(ext.dictionary_attack_threshold(), 0);
  EXPECT_FALSE(ext.dictionary_attack_lockout_in_effect());
  EXPECT_EQ(ext.dictionary_attack_lockout_seconds_remaining(), 0);
}

TEST_F(LegacyCryptohomeInterfaceAdaptorTestForGetTpmStatus,
       GetTpmStatusStageInstallAttributesFail) {
  install_attr_reply_.set_error(
      user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Setup GetTpmStatus in tpm_manager to successfully return |status_reply_|
  ExpectGetTpmStatus(status_reply_);
  // Setup GetDictionaryAttackInfo in tpm_manager to successfully return
  // |da_reply_|
  ExpectGetDictionaryAttackInfo(da_reply_);
  // Setup GetStatus in cryptohome/install attributes interface to sucessfully
  // return |install_attr_reply_|
  ExpectInstallAttributesGetStatus(install_attr_reply_);

  base::Optional<cryptohome::BaseReply> final_reply;

  std::unique_ptr<MockDBusMethodResponse<cryptohome::BaseReply>> response(
      new MockDBusMethodResponse<cryptohome::BaseReply>(nullptr));
  EXPECT_CALL(*response, ReplyWithError(_, _, _, _)).WillOnce(Return());
  response->save_return_args(&final_reply);

  adaptor_->GetTpmStatus(std::move(response), in_request_);

  ASSERT_FALSE(final_reply.has_value());
}

TEST_F(LegacyCryptohomeInterfaceAdaptorTestForGetTpmStatus,
       GetTpmStatusStageAttestationFail) {
  attestation_reply_.set_status(attestation::STATUS_NOT_AVAILABLE);

  // Setup GetTpmStatus in tpm_manager to successfully return |status_reply_|
  ExpectGetTpmStatus(status_reply_);
  // Setup GetDictionaryAttackInfo in tpm_manager to successfully return
  // |da_reply_|
  ExpectGetDictionaryAttackInfo(da_reply_);
  // Setup GetStatus in cryptohome/install attributes interface to sucessfully
  // return |install_attr_reply_|
  ExpectInstallAttributesGetStatus(install_attr_reply_);
  // Setup GetStatus in attestation to successfully return |attestation_reply_|
  ExpectAttestationGetStatus(attestation_reply_);

  base::Optional<cryptohome::BaseReply> final_reply;

  std::unique_ptr<MockDBusMethodResponse<cryptohome::BaseReply>> response(
      new MockDBusMethodResponse<cryptohome::BaseReply>(nullptr));
  EXPECT_CALL(*response, ReplyWithError(_)).Times(0);
  EXPECT_CALL(*response, ReplyWithError(_, _, _, _)).Times(0);
  response->save_return_args(&final_reply);

  adaptor_->GetTpmStatus(std::move(response), in_request_);

  ASSERT_TRUE(final_reply.has_value());
  ASSERT_TRUE(final_reply->HasExtension(cryptohome::GetTpmStatusReply::reply));
  const auto& ext =
      final_reply->GetExtension(cryptohome::GetTpmStatusReply::reply);

  // These are the default values when call to retrieve attestation status
  // failed.
  EXPECT_FALSE(ext.attestation_prepared());
  EXPECT_FALSE(ext.attestation_enrolled());
  EXPECT_FALSE(ext.verified_boot_measured());
}

}  // namespace

}  // namespace cryptohome
