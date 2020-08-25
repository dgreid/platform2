// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/bind.h>
#include <brillo/dbus/dbus_object_test_helpers.h>
#include <dbus/attestation/dbus-constants.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "attestation/common/mock_attestation_interface.h"
#include "attestation/server/dbus_service.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;
using testing::WithArgs;

namespace attestation {

class DBusServiceTest : public testing::Test {
 public:
  ~DBusServiceTest() override = default;
  void SetUp() override {
    dbus::Bus::Options options;
    mock_bus_ = new NiceMock<dbus::MockBus>(options);
    dbus::ObjectPath path(kAttestationServicePath);
    mock_exported_object_ =
        new NiceMock<dbus::MockExportedObject>(mock_bus_.get(), path);
    ON_CALL(*mock_bus_, GetExportedObject(path))
        .WillByDefault(Return(mock_exported_object_.get()));
    dbus_service_.reset(new DBusService(mock_bus_, &mock_service_));
    dbus_service_->Register(
        brillo::dbus_utils::AsyncEventSequencer::GetDefaultCompletionAction());
  }

  std::unique_ptr<dbus::Response> CallMethod(dbus::MethodCall* method_call) {
    return brillo::dbus_utils::testing::CallMethod(dbus_service_->dbus_object_,
                                                   method_call);
  }

  std::unique_ptr<dbus::MethodCall> CreateMethodCall(
      const std::string& method_name) {
    std::unique_ptr<dbus::MethodCall> call(
        new dbus::MethodCall(kAttestationInterface, method_name));
    call->SetSerial(1);
    return call;
  }

 protected:
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockExportedObject> mock_exported_object_;
  StrictMock<MockAttestationInterface> mock_service_;
  std::unique_ptr<DBusService> dbus_service_;
};

TEST_F(DBusServiceTest, CopyableCallback) {
  EXPECT_CALL(mock_service_, GetAttestationKeyInfo(_, _))
      .WillOnce(WithArgs<1>(
          Invoke([](const AttestationInterface::GetAttestationKeyInfoCallback&
                        callback) {
            // Copy the callback, then call the original.
            GetAttestationKeyInfoReply reply;
            base::Closure copy = base::Bind(callback, reply);
            callback.Run(reply);
          })));
  std::unique_ptr<dbus::MethodCall> call =
      CreateMethodCall(kGetAttestationKeyInfo);
  GetAttestationKeyInfoRequest request;
  dbus::MessageWriter writer(call.get());
  writer.AppendProtoAsArrayOfBytes(request);
  auto response = CallMethod(call.get());
  dbus::MessageReader reader(response.get());
  GetAttestationKeyInfoReply reply;
  EXPECT_TRUE(reader.PopArrayOfBytesAsProto(&reply));
}

TEST_F(DBusServiceTest, GetKeyInfo) {
  GetKeyInfoRequest request;
  request.set_key_label("label");
  request.set_username("username");
  EXPECT_CALL(mock_service_, GetKeyInfo(_, _))
      .WillOnce(
          Invoke([](const GetKeyInfoRequest& request,
                    const AttestationInterface::GetKeyInfoCallback& callback) {
            EXPECT_EQ("label", request.key_label());
            EXPECT_EQ("username", request.username());
            GetKeyInfoReply reply;
            reply.set_status(STATUS_SUCCESS);
            reply.set_key_type(KEY_TYPE_ECC);
            reply.set_key_usage(KEY_USAGE_SIGN);
            reply.set_public_key("public_key");
            reply.set_certify_info("certify");
            reply.set_certify_info_signature("signature");
            reply.set_certificate("certificate");
            callback.Run(reply);
          }));
  std::unique_ptr<dbus::MethodCall> call = CreateMethodCall(kGetKeyInfo);
  dbus::MessageWriter writer(call.get());
  writer.AppendProtoAsArrayOfBytes(request);
  auto response = CallMethod(call.get());
  dbus::MessageReader reader(response.get());
  GetKeyInfoReply reply;
  EXPECT_TRUE(reader.PopArrayOfBytesAsProto(&reply));
  EXPECT_EQ(STATUS_SUCCESS, reply.status());
  EXPECT_EQ(KEY_TYPE_ECC, reply.key_type());
  EXPECT_EQ(KEY_USAGE_SIGN, reply.key_usage());
  EXPECT_EQ("public_key", reply.public_key());
  EXPECT_EQ("certify", reply.certify_info());
  EXPECT_EQ("signature", reply.certify_info_signature());
  EXPECT_EQ("certificate", reply.certificate());
}

TEST_F(DBusServiceTest, GetEndorsementInfo) {
  GetEndorsementInfoRequest request;
  EXPECT_CALL(mock_service_, GetEndorsementInfo(_, _))
      .WillOnce(Invoke(
          [](const GetEndorsementInfoRequest& request,
             const AttestationInterface::GetEndorsementInfoCallback& callback) {
            GetEndorsementInfoReply reply;
            reply.set_status(STATUS_SUCCESS);
            reply.set_ek_public_key("public_key");
            reply.set_ek_certificate("certificate");
            callback.Run(reply);
          }));
  std::unique_ptr<dbus::MethodCall> call =
      CreateMethodCall(kGetEndorsementInfo);
  dbus::MessageWriter writer(call.get());
  writer.AppendProtoAsArrayOfBytes(request);
  auto response = CallMethod(call.get());
  dbus::MessageReader reader(response.get());
  GetEndorsementInfoReply reply;
  EXPECT_TRUE(reader.PopArrayOfBytesAsProto(&reply));
  EXPECT_EQ(STATUS_SUCCESS, reply.status());
  EXPECT_EQ("public_key", reply.ek_public_key());
  EXPECT_EQ("certificate", reply.ek_certificate());
}

TEST_F(DBusServiceTest, GetAttestationKeyInfo) {
  GetAttestationKeyInfoRequest request;
  EXPECT_CALL(mock_service_, GetAttestationKeyInfo(_, _))
      .WillOnce(
          Invoke([](const GetAttestationKeyInfoRequest& request,
                    const AttestationInterface::GetAttestationKeyInfoCallback&
                        callback) {
            GetAttestationKeyInfoReply reply;
            reply.set_status(STATUS_SUCCESS);
            reply.set_public_key("public_key");
            reply.set_public_key_tpm_format("public_key_tpm_format");
            reply.set_certificate("certificate");
            reply.mutable_pcr0_quote()->set_quote("pcr0");
            reply.mutable_pcr1_quote()->set_quote("pcr1");
            callback.Run(reply);
          }));
  std::unique_ptr<dbus::MethodCall> call =
      CreateMethodCall(kGetAttestationKeyInfo);
  dbus::MessageWriter writer(call.get());
  writer.AppendProtoAsArrayOfBytes(request);
  auto response = CallMethod(call.get());
  dbus::MessageReader reader(response.get());
  GetAttestationKeyInfoReply reply;
  EXPECT_TRUE(reader.PopArrayOfBytesAsProto(&reply));
  EXPECT_EQ(STATUS_SUCCESS, reply.status());
  EXPECT_EQ("public_key", reply.public_key());
  EXPECT_EQ("public_key_tpm_format", reply.public_key_tpm_format());
  EXPECT_EQ("certificate", reply.certificate());
  EXPECT_EQ("pcr0", reply.pcr0_quote().quote());
  EXPECT_EQ("pcr1", reply.pcr1_quote().quote());
}

TEST_F(DBusServiceTest, ActivateAttestationKey) {
  ActivateAttestationKeyRequest request;
  request.mutable_encrypted_certificate()->set_asym_ca_contents("encrypted1");
  request.mutable_encrypted_certificate()->set_sym_ca_attestation("encrypted2");
  request.set_save_certificate(true);
  EXPECT_CALL(mock_service_, ActivateAttestationKey(_, _))
      .WillOnce(
          Invoke([](const ActivateAttestationKeyRequest& request,
                    const AttestationInterface::ActivateAttestationKeyCallback&
                        callback) {
            EXPECT_EQ("encrypted1",
                      request.encrypted_certificate().asym_ca_contents());
            EXPECT_EQ("encrypted2",
                      request.encrypted_certificate().sym_ca_attestation());
            EXPECT_TRUE(request.save_certificate());
            ActivateAttestationKeyReply reply;
            reply.set_status(STATUS_SUCCESS);
            reply.set_certificate("certificate");
            callback.Run(reply);
          }));
  std::unique_ptr<dbus::MethodCall> call =
      CreateMethodCall(kActivateAttestationKey);
  dbus::MessageWriter writer(call.get());
  writer.AppendProtoAsArrayOfBytes(request);
  auto response = CallMethod(call.get());
  dbus::MessageReader reader(response.get());
  ActivateAttestationKeyReply reply;
  EXPECT_TRUE(reader.PopArrayOfBytesAsProto(&reply));
  EXPECT_EQ(STATUS_SUCCESS, reply.status());
  EXPECT_EQ("certificate", reply.certificate());
}

TEST_F(DBusServiceTest, CreateCertifiableKey) {
  CreateCertifiableKeyRequest request;
  request.set_key_label("label");
  request.set_key_type(KEY_TYPE_ECC);
  request.set_key_usage(KEY_USAGE_SIGN);
  request.set_username("user");
  EXPECT_CALL(mock_service_, CreateCertifiableKey(_, _))
      .WillOnce(
          Invoke([](const CreateCertifiableKeyRequest& request,
                    const AttestationInterface::CreateCertifiableKeyCallback&
                        callback) {
            EXPECT_EQ("label", request.key_label());
            EXPECT_EQ(KEY_TYPE_ECC, request.key_type());
            EXPECT_EQ(KEY_USAGE_SIGN, request.key_usage());
            EXPECT_EQ("user", request.username());
            CreateCertifiableKeyReply reply;
            reply.set_status(STATUS_SUCCESS);
            reply.set_public_key("public_key");
            reply.set_certify_info("certify_info");
            reply.set_certify_info_signature("signature");
            callback.Run(reply);
          }));
  std::unique_ptr<dbus::MethodCall> call =
      CreateMethodCall(kCreateCertifiableKey);
  dbus::MessageWriter writer(call.get());
  writer.AppendProtoAsArrayOfBytes(request);
  auto response = CallMethod(call.get());
  dbus::MessageReader reader(response.get());
  CreateCertifiableKeyReply reply;
  EXPECT_TRUE(reader.PopArrayOfBytesAsProto(&reply));
  EXPECT_EQ(STATUS_SUCCESS, reply.status());
  EXPECT_EQ("public_key", reply.public_key());
  EXPECT_EQ("certify_info", reply.certify_info());
  EXPECT_EQ("signature", reply.certify_info_signature());
}

TEST_F(DBusServiceTest, Decrypt) {
  DecryptRequest request;
  request.set_key_label("label");
  request.set_username("user");
  request.set_encrypted_data("data");
  EXPECT_CALL(mock_service_, Decrypt(_, _))
      .WillOnce(
          Invoke([](const DecryptRequest& request,
                    const AttestationInterface::DecryptCallback& callback) {
            EXPECT_EQ("label", request.key_label());
            EXPECT_EQ("user", request.username());
            EXPECT_EQ("data", request.encrypted_data());
            DecryptReply reply;
            reply.set_status(STATUS_SUCCESS);
            reply.set_decrypted_data("data");
            callback.Run(reply);
          }));
  std::unique_ptr<dbus::MethodCall> call = CreateMethodCall(kDecrypt);
  dbus::MessageWriter writer(call.get());
  writer.AppendProtoAsArrayOfBytes(request);
  auto response = CallMethod(call.get());
  dbus::MessageReader reader(response.get());
  DecryptReply reply;
  EXPECT_TRUE(reader.PopArrayOfBytesAsProto(&reply));
  EXPECT_EQ(STATUS_SUCCESS, reply.status());
  EXPECT_EQ("data", reply.decrypted_data());
}

TEST_F(DBusServiceTest, Sign) {
  SignRequest request;
  request.set_key_label("label");
  request.set_username("user");
  request.set_data_to_sign("data");
  EXPECT_CALL(mock_service_, Sign(_, _))
      .WillOnce(Invoke([](const SignRequest& request,
                          const AttestationInterface::SignCallback& callback) {
        EXPECT_EQ("label", request.key_label());
        EXPECT_EQ("user", request.username());
        EXPECT_EQ("data", request.data_to_sign());
        SignReply reply;
        reply.set_status(STATUS_SUCCESS);
        reply.set_signature("signature");
        callback.Run(reply);
      }));
  std::unique_ptr<dbus::MethodCall> call = CreateMethodCall(kSign);
  dbus::MessageWriter writer(call.get());
  writer.AppendProtoAsArrayOfBytes(request);
  auto response = CallMethod(call.get());
  dbus::MessageReader reader(response.get());
  SignReply reply;
  EXPECT_TRUE(reader.PopArrayOfBytesAsProto(&reply));
  EXPECT_EQ(STATUS_SUCCESS, reply.status());
  EXPECT_EQ("signature", reply.signature());
}

TEST_F(DBusServiceTest, RegisterKeyWithChapsToken) {
  RegisterKeyWithChapsTokenRequest request;
  request.set_key_label("label");
  request.set_username("user");
  EXPECT_CALL(mock_service_, RegisterKeyWithChapsToken(_, _))
      .WillOnce(Invoke(
          [](const RegisterKeyWithChapsTokenRequest& request,
             const AttestationInterface::RegisterKeyWithChapsTokenCallback&
                 callback) {
            EXPECT_EQ("label", request.key_label());
            EXPECT_EQ("user", request.username());
            RegisterKeyWithChapsTokenReply reply;
            reply.set_status(STATUS_SUCCESS);
            callback.Run(reply);
          }));
  std::unique_ptr<dbus::MethodCall> call =
      CreateMethodCall(kRegisterKeyWithChapsToken);
  dbus::MessageWriter writer(call.get());
  writer.AppendProtoAsArrayOfBytes(request);
  auto response = CallMethod(call.get());
  dbus::MessageReader reader(response.get());
  RegisterKeyWithChapsTokenReply reply;
  EXPECT_TRUE(reader.PopArrayOfBytesAsProto(&reply));
  EXPECT_EQ(STATUS_SUCCESS, reply.status());
}

}  // namespace attestation
