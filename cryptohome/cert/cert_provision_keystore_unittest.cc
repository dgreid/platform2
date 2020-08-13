// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <tuple>
#include <vector>

#include <base/bind.h>
#include <chaps/attributes.h>
#include <chaps/chaps_proxy_mock.h>
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
using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace {

// Some arbitrary certificate label used for testing.
const char kCertLabel[] = "test";

// Some non-zero handles for PKCS#11 session and object.
const CK_SESSION_HANDLE kSession = 17;
const CK_OBJECT_HANDLE kObject = 112;

}  // namespace

namespace cert_provision {

// Test class for testing KeyStoreImpl.
class CertProvisionKeyStoreTest : public testing::Test {
 public:
  using Pkcs11Mock = chaps::ChapsProxyMock;
  CertProvisionKeyStoreTest() = default;

  void SetUp() {
    ON_CALL(pkcs11_, OpenSession(_, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<3>(kSession), Return(0)));
    ON_CALL(pkcs11_, FindObjects(_, _, _, _))
        .WillByDefault(Invoke(this, &CertProvisionKeyStoreTest::FindObjects));
    ON_CALL(pkcs11_, GetAttributeValue(_, _, _, _, _))
        .WillByDefault(
            Invoke(this, &CertProvisionKeyStoreTest::GetAttributeValue));
    ON_CALL(pkcs11_, CreateObject(_, _, _, _))
        .WillByDefault(Invoke(this, &CertProvisionKeyStoreTest::CreateObject));
    ON_CALL(pkcs11_, Sign(_, _, _, _, _, _))
        .WillByDefault(Invoke(this, &CertProvisionKeyStoreTest::Sign));
  }

  // Mocks looking for objects.
  uint32_t FindObjects(const SecureBlob& /* isolate_credential */,
                       uint64_t /* session_id */,
                       uint64_t max_object_count,
                       std::vector<uint64_t>* objects) {
    while (!found_objects_.empty() && objects->size() < max_object_count) {
      objects->push_back(found_objects_.back());
      found_objects_.pop_back();
    }
    return CKR_OK;
  }

  // Mocks getting CKA_VALUE for an object.
  uint32_t GetAttributeValue(const SecureBlob& /* isolate_credential */,
                             uint64_t /* session_id */,
                             uint64_t /* object_handle */,
                             const std::vector<uint8_t>& attributes_in,
                             std::vector<uint8_t>* attributes_out) {
    chaps::Attributes chaps_attr;
    chaps_attr.Parse(attributes_in);
    CK_ATTRIBUTE* attr = chaps_attr.attributes();
    if (chaps_attr.num_attributes() != 1 || attr->type != CKA_VALUE) {
      return CKR_GENERAL_ERROR;
    }
    if (attr->pValue) {
      if (attr->ulValueLen == object_value_.size()) {
        memcpy(attr->pValue, object_value_.data(), object_value_.size());
      } else {
        return CKR_GENERAL_ERROR;
      }
    }
    attr->ulValueLen = object_value_.size();
    chaps_attr.Serialize(attributes_out);
    return CKR_OK;
  }

  // Mocks creating an object.
  uint32_t CreateObject(const SecureBlob& /* isolate_credential */,
                        uint64_t /* session_id */,
                        const std::vector<uint8_t>& attributes,
                        uint64_t* new_object_handle) {
    *new_object_handle = kObject;
    object_value_.clear();

    chaps::Attributes chaps_attr;
    chaps_attr.Parse(attributes);
    CK_ATTRIBUTE* attr = chaps_attr.attributes();
    for (CK_ULONG i = 0; i < chaps_attr.num_attributes(); ++i, ++attr) {
      if (attr->type == CKA_VALUE) {
        object_value_.assign(reinterpret_cast<char*>(attr->pValue),
                             attr->ulValueLen);
        break;
      }
    }
    return CKR_OK;
  }

  uint32_t Sign(const brillo::SecureBlob& /* isolate_credential */,
                uint64_t /* session_id */,
                const std::vector<uint8_t>& /* data */,
                uint64_t /* max_out_length */,
                uint64_t* actual_out_length,
                std::vector<uint8_t>* signature) {
    *actual_out_length = signature_.size();
    uint8_t* sig_data =
        reinterpret_cast<uint8_t*>(const_cast<char*>(signature_.data()));
    signature->assign(sig_data, sig_data + signature_.size());
    return CKR_OK;
  }

 protected:
  NiceMock<Pkcs11Mock> pkcs11_{false /* do not pre-initialize */};
  KeyStoreImpl key_store_;

  // Objects to return from C_FindObjects.
  std::vector<uint64_t> found_objects_;

  // CKA_VALUE attribute for getting/setting.
  std::string object_value_;

  // Signature returned by Sign().
  std::string signature_;

 private:
  DISALLOW_COPY_AND_ASSIGN(CertProvisionKeyStoreTest);
};

// Checks that initialization succeeds under normal conditions
// and that the session is closed on deleting the KeyStoreImpl object.
TEST_F(CertProvisionKeyStoreTest, InitSuccess) {
  EXPECT_TRUE(key_store_.Init());
  EXPECT_CALL(pkcs11_, CloseSession(_, _)).WillOnce(Return(0));
}

// Checks that initialization fails, if session cannot be opened.
TEST_F(CertProvisionKeyStoreTest, InitFailureSession) {
  EXPECT_CALL(pkcs11_, OpenSession(_, _, _, _))
      .WillOnce(Return(CKR_GENERAL_ERROR));
  EXPECT_FALSE(key_store_.Init());
  EXPECT_CALL(pkcs11_, CloseSession(_, _)).Times(0);
  key_store_.TearDown();
}

// Checks that reading an existing ProvisionStatus returns the right data.
TEST_F(CertProvisionKeyStoreTest, ReadProvisionStatusSuccessExists) {
  EXPECT_TRUE(key_store_.Init());

  ProvisionStatus provision_status_stored;
  provision_status_stored.set_key_id("100");  // some random value
  provision_status_stored.SerializeToString(&object_value_);
  found_objects_.assign({kObject});

  // Expect to be called twice - once to get the size, then the value itself.
  EXPECT_CALL(pkcs11_, GetAttributeValue(_, kSession, kObject, _, _)).Times(2);

  ProvisionStatus provision_status;
  EXPECT_TRUE(key_store_.ReadProvisionStatus(kCertLabel, &provision_status));
  EXPECT_EQ(provision_status_stored.key_id(), provision_status.key_id());
}

// Checks that if there is no ProvisionStatus object, an empty proto is
// returned.
TEST_F(CertProvisionKeyStoreTest, ReadProvisionStatusSuccessNew) {
  EXPECT_TRUE(key_store_.Init());

  found_objects_.clear();
  EXPECT_CALL(pkcs11_, GetAttributeValue(_, _, _, _, _)).Times(0);

  ProvisionStatus provision_status;
  EXPECT_TRUE(key_store_.ReadProvisionStatus(kCertLabel, &provision_status));
  EXPECT_FALSE(provision_status.provisioned());
}

// Checks ReadProvisionStatus behavior if not initialized.
TEST_F(CertProvisionKeyStoreTest, ReadProvisionStatusFailureNotInit) {
  EXPECT_CALL(pkcs11_, FindObjects(_, _, _, _)).Times(0);
  EXPECT_CALL(pkcs11_, GetAttributeValue(_, _, _, _, _)).Times(0);

  ProvisionStatus provision_status;
  EXPECT_FALSE(key_store_.ReadProvisionStatus(kCertLabel, &provision_status));
}

// Checks ReadProvisionStatus behavior in case of GetAttribute error.
TEST_F(CertProvisionKeyStoreTest, ReadProvisionStatusFailureGet) {
  EXPECT_TRUE(key_store_.Init());

  found_objects_.assign({kObject});
  EXPECT_CALL(pkcs11_, GetAttributeValue(_, kSession, kObject, _, _))
      .WillOnce(Return(CKR_GENERAL_ERROR));

  ProvisionStatus provision_status;
  EXPECT_FALSE(key_store_.ReadProvisionStatus(kCertLabel, &provision_status));
}

// Checks ReadProvisionStatus behavior if there are multiple objects.
TEST_F(CertProvisionKeyStoreTest, ReadProvisionStatusFailureMultiple) {
  EXPECT_TRUE(key_store_.Init());

  found_objects_.assign({kObject, kObject+1});
  EXPECT_CALL(pkcs11_, GetAttributeValue(_, _, _, _, _)).Times(0);

  ProvisionStatus provision_status;
  EXPECT_FALSE(key_store_.ReadProvisionStatus(kCertLabel, &provision_status));
}

// Checks that KeyStoreImpl saves ProvisionStatus if there was no previous
// ProvisionStatus objects.
TEST_F(CertProvisionKeyStoreTest, WriteProvisionStatusSuccessNew) {
  EXPECT_TRUE(key_store_.Init());

  found_objects_.clear();

  EXPECT_CALL(pkcs11_, DestroyObject(_, _, _)).Times(0);
  EXPECT_CALL(pkcs11_, CreateObject(_, kSession, _, _));

  ProvisionStatus provision_status;
  provision_status.set_key_id("200");  // some random value
  EXPECT_TRUE(key_store_.WriteProvisionStatus(kCertLabel, provision_status));
  EXPECT_EQ(provision_status.SerializeAsString(), object_value_);
}

// Checks that KeyStoreImpl saves ProvisionStatus if there was already some
// ProvisionStatus object(s).
TEST_F(CertProvisionKeyStoreTest, WriteProvisionStatusSuccessExist) {
  EXPECT_TRUE(key_store_.Init());

  found_objects_.assign({kObject+1, kObject+2});

  EXPECT_CALL(pkcs11_, DestroyObject(_, kSession, kObject+1));
  EXPECT_CALL(pkcs11_, DestroyObject(_, kSession, kObject+2));
  EXPECT_CALL(pkcs11_, CreateObject(_, kSession, _, _));

  ProvisionStatus provision_status;
  provision_status.set_key_id("200");  // some random value
  EXPECT_TRUE(key_store_.WriteProvisionStatus(kCertLabel, provision_status));
  EXPECT_EQ(provision_status.SerializeAsString(), object_value_);
}

// Checks WriteProvisionStatus behavior if not initialized.
TEST_F(CertProvisionKeyStoreTest, WriteProvisionStatusFailureNotInit) {
  EXPECT_CALL(pkcs11_, FindObjects(_, _, _, _)).Times(0);
  EXPECT_CALL(pkcs11_, CreateObject(_, _, _, _)).Times(0);

  ProvisionStatus provision_status;
  provision_status.set_key_id("200");  // some random value
  EXPECT_FALSE(key_store_.WriteProvisionStatus(kCertLabel, provision_status));
}

// Checks WriteProvisionStatus behavior in case of CreateObject error.
TEST_F(CertProvisionKeyStoreTest, WriteProvisionStatusFailureCreate) {
  EXPECT_TRUE(key_store_.Init());

  EXPECT_CALL(pkcs11_, CreateObject(_, kSession, _, _))
      .WillOnce(Return(CKR_GENERAL_ERROR));

  ProvisionStatus provision_status;
  EXPECT_FALSE(key_store_.WriteProvisionStatus(kCertLabel, provision_status));
}

// Checks that DeleteKeys succeeds if there are no objects to delete.
TEST_F(CertProvisionKeyStoreTest, DeleteKeysSuccessEmpty) {
  EXPECT_TRUE(key_store_.Init());

  found_objects_.clear();

  EXPECT_CALL(pkcs11_, DestroyObject(_, _, _)).Times(0);

  std::string key_id = "300";  // some random value
  EXPECT_TRUE(key_store_.DeleteKeys(key_id, kCertLabel));
}

// Checks that DeleteKeys delets all required objects.
TEST_F(CertProvisionKeyStoreTest, DeleteKeysSuccess) {
  EXPECT_TRUE(key_store_.Init());

  found_objects_.assign({kObject+1, kObject+2});

  EXPECT_CALL(pkcs11_, DestroyObject(_, kSession, kObject+1));
  EXPECT_CALL(pkcs11_, DestroyObject(_, kSession, kObject+2));

  std::string key_id = "300";  // some random value
  EXPECT_TRUE(key_store_.DeleteKeys(key_id, kCertLabel));
}

// Checks DeleteKeys behavior if not initialized.
TEST_F(CertProvisionKeyStoreTest, DeleteKeysFailureNotInit) {
  EXPECT_CALL(pkcs11_, FindObjects(_, _, _, _)).Times(0);

  std::string key_id = "300";  // some random value
  EXPECT_FALSE(key_store_.DeleteKeys(key_id, kCertLabel));
}

// Checks that DeleteKeys ignores DestroyObject errors.
TEST_F(CertProvisionKeyStoreTest, DeleteKeysSuccessDestroyError) {
  EXPECT_TRUE(key_store_.Init());

  found_objects_.assign({kObject});
  EXPECT_CALL(pkcs11_, DestroyObject(_, kSession, kObject))
      .WillOnce(Return(CKR_GENERAL_ERROR));

  std::string key_id = "300";  // some random value
  EXPECT_TRUE(key_store_.DeleteKeys(key_id, kCertLabel));
}

// Checks that Sign succeeds if there is a key.
TEST_F(CertProvisionKeyStoreTest, SignSuccess) {
  EXPECT_TRUE(key_store_.Init());

  found_objects_.assign({kObject});

  std::string key_id = "400";  // some arbitrary value
  std::string data = "data";  // arbitrary data to sign

  signature_ = "sig-sha1";
  EXPECT_CALL(pkcs11_, SignInit(_, kSession, CKM_SHA1_RSA_PKCS, _, kObject));
  EXPECT_CALL(pkcs11_, Sign(_, kSession, _, _, _, _));
  std::string sig;
  EXPECT_TRUE(key_store_.Sign(
      key_id, kCertLabel, SignMechanism::SHA1_RSA_PKCS, data, &sig));
  EXPECT_EQ("sig-sha1", sig);

  found_objects_.assign({kObject});

  signature_ = "sig-sha256";
  EXPECT_CALL(pkcs11_, SignInit(_, kSession, CKM_SHA256_RSA_PKCS, _, kObject));
  EXPECT_CALL(pkcs11_, Sign(_, kSession, _, _, _, _));
  sig.clear();
  EXPECT_TRUE(key_store_.Sign(
      key_id, kCertLabel, SignMechanism::SHA256_RSA_PKCS, data, &sig));
  EXPECT_EQ("sig-sha256", sig);

  found_objects_.assign({kObject});

  signature_ = "sig-sha256-pss";
  EXPECT_CALL(pkcs11_,
              SignInit(_, kSession, CKM_SHA256_RSA_PKCS_PSS, _, kObject));
  EXPECT_CALL(pkcs11_, Sign(_, kSession, _, _, _, _));
  sig.clear();
  EXPECT_TRUE(key_store_.Sign(key_id, kCertLabel, SignMechanism::SHA256_RSA_PSS,
                              data, &sig));
  EXPECT_EQ("sig-sha256-pss", sig);
}

// Checks that Sign fails if there's no key.
TEST_F(CertProvisionKeyStoreTest, SignFailureNoKey) {
  EXPECT_TRUE(key_store_.Init());

  found_objects_.clear();

  EXPECT_CALL(pkcs11_, SignInit(_, _, _, _, _)).Times(0);
  EXPECT_CALL(pkcs11_, Sign(_, _, _, _, _, _)).Times(0);

  std::string key_id = "400";  // some arbitrary value
  std::string data = "data";  // arbitrary data to sign
  std::string sig;
  EXPECT_FALSE(key_store_.Sign(
      key_id, kCertLabel, SignMechanism::SHA1_RSA_PKCS, data, &sig));
}

// Checks Sign behavior if not initialized.
TEST_F(CertProvisionKeyStoreTest, SignFailureNotInit) {
  found_objects_.assign({kObject});

  EXPECT_CALL(pkcs11_, SignInit(_, _, _, _, _)).Times(0);
  EXPECT_CALL(pkcs11_, Sign(_, _, _, _, _, _)).Times(0);

  std::string key_id = "400";  // some arbitrary value
  std::string data = "data";  // arbitrary data to sign
  std::string sig;
  EXPECT_FALSE(key_store_.Sign(
      key_id, kCertLabel, SignMechanism::SHA1_RSA_PKCS, data, &sig));
}

// Checks WriteProvisionStatus behavior in case of SignInit error.
TEST_F(CertProvisionKeyStoreTest, SignFailureSignInit) {
  EXPECT_TRUE(key_store_.Init());

  found_objects_.assign({kObject});

  EXPECT_CALL(pkcs11_, SignInit(_, kSession, CKM_SHA1_RSA_PKCS, _, kObject))
      .WillOnce(Return(CKR_GENERAL_ERROR));
  EXPECT_CALL(pkcs11_, Sign(_, _, _, _, _, _)).Times(0);
  std::string key_id = "400";  // some arbitrary value
  std::string data = "data";  // arbitrary data to sign
  std::string sig;
  EXPECT_FALSE(key_store_.Sign(
      key_id, kCertLabel, SignMechanism::SHA1_RSA_PKCS, data, &sig));
}

// Checks WriteProvisionStatus behavior in case of SignInit error.
TEST_F(CertProvisionKeyStoreTest, SignFailureSign) {
  EXPECT_TRUE(key_store_.Init());

  found_objects_.assign({kObject});

  EXPECT_CALL(pkcs11_, SignInit(_, kSession, CKM_SHA1_RSA_PKCS, _, kObject));
  EXPECT_CALL(pkcs11_, Sign(_, kSession, _, _, _, _))
      .WillOnce(Return(CKR_GENERAL_ERROR));
  std::string key_id = "400";  // some arbitrary value
  std::string data = "data";  // arbitrary data to sign
  std::string sig;
  EXPECT_FALSE(key_store_.Sign(
      key_id, kCertLabel, SignMechanism::SHA1_RSA_PKCS, data, &sig));
}

}  // namespace cert_provision
