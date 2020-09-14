// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// KeyStore interface classes for cert_provision library.

#include <base/logging.h>
#include <base/stl_util.h>

#include "cryptohome/cert/cert_provision_keystore.h"
#include <openssl/sha.h>

namespace {

// An arbitrary application ID to identify PKCS #11 objects.
const char kApplicationID[] =
    "cert_provision_0caa2ccf131a8a4ff0a2b68f38aa180b504a7f65";
const CK_OBJECT_CLASS kDataClass = CKO_DATA;
const CK_BBOOL kTrue = CK_TRUE;
const CK_BBOOL kFalse = CK_FALSE;

// Max supported signature size. With only RSASSA-PKCS1-v1_5 supported, allows
// for the signing keys of up to 16k bits (2k bytes) in size.
const CK_ULONG kMaxSignatureSize = 2048;

std::vector<CK_ATTRIBUTE> GetProvisionStatusAttributes(
    const std::string& label) {
  return std::vector<CK_ATTRIBUTE>({
      {CKA_CLASS, const_cast<CK_OBJECT_CLASS*>(&kDataClass),
       sizeof(kDataClass)},
      {CKA_APPLICATION, const_cast<char*>(kApplicationID),
       base::size(kApplicationID)},
      {CKA_TOKEN, const_cast<CK_BBOOL*>(&kTrue), sizeof(kTrue)},
      {CKA_PRIVATE, const_cast<CK_BBOOL*>(&kTrue), sizeof(kTrue)},
      {CKA_MODIFIABLE, const_cast<CK_BBOOL*>(&kFalse), sizeof(kFalse)},
      {CKA_LABEL, const_cast<char*>(label.data()), label.size()},
  });
}

}  // namespace

namespace cert_provision {

KeyStore* KeyStore::subst_obj = nullptr;

Scoped<KeyStore> KeyStore::Create() {
  return subst_obj ? Scoped<KeyStore>(subst_obj)
                   : Scoped<KeyStore>(GetDefault());
}

std::unique_ptr<KeyStore> KeyStore::GetDefault() {
  return std::unique_ptr<KeyStore>(new KeyStoreImpl());
}

KeyStoreImpl::~KeyStoreImpl() {
  TearDown();
}

OpResult KeyStoreImpl::Init() {
  CK_RV res;
  if (!initialized_) {
    res = C_Initialize(NULL /* pInitArgs */);
    if (res != CKR_OK) {
      return KeyStoreResError("Failed to initialize keystore", res);
    }
    initialized_ = true;
  }

  res = C_OpenSession(0 /* slotID */, CKF_SERIAL_SESSION | CKF_RW_SESSION,
                      NULL /* pApplication callback parameter */,
                      NULL /* Notify callback */, &session_);
  if (res != CKR_OK) {
    return KeyStoreResError("Failed to open session", res);
  }

  return OpResult();
}

void KeyStoreImpl::TearDown() {
  if (session_ != CK_INVALID_HANDLE) {
    C_CloseSession(session_);
    session_ = CK_INVALID_HANDLE;
  }
  if (initialized_) {
    C_Finalize(NULL);
    initialized_ = false;
  }
}

bool KeyStoreImpl::GetMechanismType(SignMechanism mechanism,
                                    CK_MECHANISM_TYPE* type) {
  DCHECK(type);
  switch (mechanism) {
    case SHA1_RSA_PKCS:
      *type = CKM_SHA1_RSA_PKCS;
      break;
    case SHA256_RSA_PKCS:
      *type = CKM_SHA256_RSA_PKCS;
      break;
    case SHA256_RSA_PSS:
      *type = CKM_SHA256_RSA_PKCS_PSS;
      break;
    default:
      return false;
  }
  return true;
}

OpResult KeyStoreImpl::Find(CK_ATTRIBUTE* attr,
                            CK_ULONG attr_count,
                            std::vector<CK_OBJECT_HANDLE>* objects) {
  CK_RV res = C_FindObjectsInit(session_, attr, attr_count);
  if (res != CKR_OK) {
    return KeyStoreResError("Failed to initialize object search", res);
  }

  CK_OBJECT_HANDLE obj;
  CK_ULONG found;
  do {
    res = C_FindObjects(session_, &obj, 1, &found);
    if (res != CKR_OK) {
      CK_RV res_final = C_FindObjectsFinal(session_);
      if (res_final != CKR_OK) {
        LOG(WARNING) << "Failed to finalize finding objects: " << res_final;
      }
      return KeyStoreResError("Failed to find objects", res);
    }
    if (found > 0) {
      objects->push_back(obj);
    }
  } while (found > 0);

  res = C_FindObjectsFinal(session_);
  if (res != CKR_OK) {
    return KeyStoreResError("Failed to finalize object search", res);
  }

  return OpResult();
}

OpResult KeyStoreImpl::Sign(const std::string& id,
                            const std::string& label,
                            SignMechanism mechanism,
                            const std::string& data,
                            std::string* signature) {
  CK_OBJECT_CLASS class_value = CKO_PRIVATE_KEY;
  CK_ATTRIBUTE attributes[] = {
      {CKA_CLASS, &class_value, sizeof(class_value)},
      {CKA_ID, const_cast<char*>(id.c_str()), id.size()},
      {CKA_LABEL, const_cast<char*>(label.c_str()), label.size()},
  };
  std::vector<CK_OBJECT_HANDLE> objects;
  OpResult result = Find(attributes, base::size(attributes), &objects);
  if (!result) {
    return result;
  }
  if (objects.size() != 1) {
    return {Status::KeyStoreError,
            objects.size() ? "Multiple keys found." : "No key to sign."};
  }

  CK_MECHANISM mech;
  CK_RSA_PKCS_PSS_PARAMS params;
  if (!GetMechanismType(mechanism, &mech.mechanism)) {
    return {Status::KeyStoreError, "Unknown sign mechanism."};
  }
  if (mechanism == SHA256_RSA_PSS) {
    // Get the length of the RSA key
    CK_ATTRIBUTE attribute_template[] = {
        {CKA_MODULUS, nullptr, 0},
    };
    CK_RV ret = C_GetAttributeValue(session_, objects[0], attribute_template,
                                    base::size(attribute_template));
    if (ret != CKR_OK) {
      return KeyStoreResError("Failed to get attribute value", ret);
    }

    int rsa_size = attribute_template[0].ulValueLen;
    CK_ULONG max_sLen = rsa_size - 2 - SHA256_DIGEST_LENGTH;

    params = {CKM_SHA256_RSA_PKCS_PSS, CKG_MGF1_SHA256, max_sLen};
    mech.pParameter = &params;
    mech.ulParameterLen = sizeof(params);
  } else {
    mech.pParameter = NULL;
    mech.ulParameterLen = 0;
  }

  CK_RV res = C_SignInit(session_, &mech, objects[0]);
  if (res != CKR_OK) {
    return KeyStoreResError("Failed to initialize signing", res);
  }

  CK_ULONG sig_len = kMaxSignatureSize;
  VLOG(2) << "C_Sign max signature size = " << sig_len;
  signature->assign(sig_len, 0);
  res = C_Sign(
      session_, reinterpret_cast<CK_BYTE_PTR>(const_cast<char*>(data.data())),
      data.size(),
      reinterpret_cast<CK_BYTE_PTR>(const_cast<char*>(signature->data())),
      &sig_len);
  if (res != CKR_OK) {
    return KeyStoreResError("Failed to sign", res);
  }
  VLOG(2) << "C_Sign resulting signature size = " << sig_len;
  signature->resize(sig_len);
  return OpResult();
}

OpResult KeyStoreImpl::ReadProvisionStatus(
    const std::string& label, google::protobuf::MessageLite* provision_status) {
  auto attributes = GetProvisionStatusAttributes(label);
  std::vector<CK_OBJECT_HANDLE> objects;
  OpResult result = Find(attributes.data(), attributes.size(), &objects);
  if (!result) {
    return result;
  }
  if (objects.size() == 0) {
    provision_status->Clear();
  } else if (objects.size() == 1) {
    CK_OBJECT_HANDLE object = objects[0];
    CK_ATTRIBUTE attribute = {CKA_VALUE, NULL, 0};
    CK_RV res = C_GetAttributeValue(session_, object, &attribute, 1);
    if (res != CKR_OK) {
      return KeyStoreResError("Failed to get provision status size", res);
    }

    std::string value(attribute.ulValueLen, 0);
    attribute.pValue = const_cast<char*>(value.data());
    res = C_GetAttributeValue(session_, object, &attribute, 1);
    if (res != CKR_OK) {
      return KeyStoreResError("Failed to get provision status", res);
    }

    provision_status->ParseFromString(value);
  } else {
    return {Status::KeyStoreError, "Multiple provision statuses found."};
  }
  return OpResult();
}

OpResult KeyStoreImpl::WriteProvisionStatus(
    const std::string& label,
    const google::protobuf::MessageLite& provision_status) {
  auto attributes = GetProvisionStatusAttributes(label);
  std::vector<CK_OBJECT_HANDLE> objects;
  OpResult result = Find(attributes.data(), attributes.size(), &objects);
  if (!result) {
    return result;
  }
  CK_RV res;
  for (auto& object : objects) {
    res = C_DestroyObject(session_, object);
    if (res != CKR_OK) {
      return KeyStoreResError("Failed to delete previous provision status",
                              res);
    }
  }

  std::string value;
  provision_status.SerializeToString(&value);
  attributes.push_back(
      {CKA_VALUE, const_cast<char*>(value.data()), value.size()});
  CK_OBJECT_HANDLE object;
  res = C_CreateObject(session_, attributes.data(), attributes.size(), &object);
  if (res != CKR_OK) {
    return KeyStoreResError("Failed to write provision status", res);
  }
  return OpResult();
}

OpResult KeyStoreImpl::DeleteKeys(const std::string& id,
                                  const std::string& label) {
  CK_ATTRIBUTE attributes[] = {
      {CKA_ID, const_cast<char*>(id.c_str()), id.size()},
      {CKA_LABEL, const_cast<char*>(label.c_str()), label.size()},
  };
  std::vector<CK_OBJECT_HANDLE> objects;
  OpResult result = Find(attributes, base::size(attributes), &objects);
  if (!result) {
    return result;
  }
  CK_RV res;
  for (auto object : objects) {
    VLOG(1) << "Deleting old object " << object;
    res = C_DestroyObject(session_, object);
    if (res != CKR_OK) {
      LOG(WARNING) << "Failed to delete old object " << object << " for label "
                   << label;
    }
  }
  return OpResult();
}

}  // namespace cert_provision
