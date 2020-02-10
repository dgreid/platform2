// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymaster/context/chaps_client.h"

#include <base/logging.h>
#include <base/stl_util.h>

#include "arc/keymaster/context/context_adaptor.h"

namespace arc {
namespace keymaster {
namespace context {

namespace {

constexpr char kApplicationID[] =
    "CrOS_d5bbc079d2497110feadfc97c40d718ae46f4658";
constexpr char kEncryptKeyLabel[] = "arc-keymasterd_AES_key";

// Only attribute retrieved is an AES key of size 32.
constexpr size_t kMaxAttributeSize = 32;
// Arbitrary number of object handles to retrieve on a search.
constexpr CK_ULONG kMaxHandles = 100;
// Max retries for invalid session handle errors.
//
// PKCS #11 calls taking a CK_SESSION_HANDLE may fail when the handle is
// invalidated, and should be retried with a new session. This may happen e.g.
// when cryptohome or attestation install a new key.
constexpr size_t kMaxAttemps = 10;

}  // anonymous namespace

namespace internal {

// Manages a PKCS #11 session by tying its lifecycle to scope.
class ScopedSession {
 public:
  explicit ScopedSession(CK_SLOT_ID slot) : handle_(CK_INVALID_HANDLE) {
    // Ensure connection to the PKCS #11 token is initialized.
    CK_RV rv = C_Initialize(/* pInitArgs */ nullptr);
    if (CKR_OK != rv && CKR_CRYPTOKI_ALREADY_INITIALIZED != rv) {
      // May happen in a test environment.
      LOG(INFO) << "PKCS #11 is not available.";
      return;
    }

    // Start a new session.
    CK_FLAGS flags = CKF_RW_SESSION | CKF_SERIAL_SESSION;
    if (CKR_OK != C_OpenSession(slot, flags, /* pApplication */ nullptr,
                                /* Notify */ nullptr, &handle_)) {
      LOG(ERROR) << "Failed to open PKCS #11 session.";
      return;
    }
  }

  ~ScopedSession() {
    // Close current session, if it exists.
    if (CK_INVALID_HANDLE != handle_ && CKR_OK != C_CloseSession(handle_)) {
      LOG(WARNING) << "Failed to close PKCS #11 session.";
      handle_ = CK_INVALID_HANDLE;
    }
  }

  // Not copyable nor assignable.
  ScopedSession(const ScopedSession&) = delete;
  ScopedSession& operator=(const ScopedSession&) = delete;

  base::Optional<CK_SESSION_HANDLE> handle() const {
    if (CK_INVALID_HANDLE == handle_)
      return base::nullopt;
    return handle_;
  }

 private:
  CK_SESSION_HANDLE handle_;
};

}  // namespace internal

ChapsClient::ChapsClient(base::WeakPtr<ContextAdaptor> context_adaptor)
    : context_adaptor_(context_adaptor) {}

ChapsClient::~ChapsClient() = default;

base::Optional<brillo::SecureBlob>
ChapsClient::ExportOrGenerateEncryptionKey() {
  if (!context_adaptor_)
    return base::nullopt;
  if (!context_adaptor_->encryption_key().has_value()) {
    for (size_t attempts = 0; attempts < kMaxAttemps; ++attempts) {
      base::Optional<CK_OBJECT_HANDLE> handle = FindKey(kEncryptKeyLabel);
      if (!handle.has_value())
        handle = GenerateEncryptionKey();
      if (handle.has_value()) {
        brillo::SecureBlob exported_key;
        const CK_RV rv = ExportKey(handle.value(), &exported_key);

        if (CKR_SESSION_HANDLE_INVALID == rv) {
          session_.reset();
          continue;
        }

        if (CKR_OK == rv)
          context_adaptor_->set_encryption_key(exported_key);
      }

      break;
    }

    // Release allocated resources once the adaptor cache has been set. This can
    // be done here for now because ChapsClient is only used to export the
    // encryption key at the moment.
    if (context_adaptor_->encryption_key().has_value()) {
      session_.reset();
      C_Finalize(/* pReserved */ nullptr);
    }
  }

  return context_adaptor_->encryption_key();
}

base::Optional<CK_SESSION_HANDLE> ChapsClient::session_handle() {
  if (!session_ && context_adaptor_) {
    base::Optional<CK_SLOT_ID> user_slot =
        context_adaptor_->FetchPrimaryUserSlot();
    if (user_slot.has_value())
      session_ = std::make_unique<internal::ScopedSession>(user_slot.value());
  }

  return session_ ? session_->handle() : base::nullopt;
}

base::Optional<CK_OBJECT_HANDLE> ChapsClient::FindKey(
    const std::string& label) {
  if (!session_handle().has_value())
    return base::nullopt;

  std::string mutable_application_id(kApplicationID);
  std::string mutable_label(label);

  // Assemble a search template.
  CK_OBJECT_CLASS object_class = CKO_DATA;
  CK_BBOOL true_value = CK_TRUE;
  CK_BBOOL false_value = CK_FALSE;
  CK_ATTRIBUTE attributes[] = {
      {CKA_APPLICATION, base::data(mutable_application_id),
       mutable_application_id.size()},
      {CKA_CLASS, &object_class, sizeof(object_class)},
      {CKA_TOKEN, &true_value, sizeof(true_value)},
      {CKA_LABEL, base::data(mutable_label), mutable_label.size()},
      {CKA_PRIVATE, &true_value, sizeof(true_value)},
      {CKA_MODIFIABLE, &false_value, sizeof(false_value)}};
  CK_OBJECT_HANDLE handles[kMaxHandles];
  CK_ULONG count = 0;

  for (size_t attempts = 0; attempts < kMaxAttemps; ++attempts) {
    CK_RV rv =
        C_FindObjectsInit(*session_handle(), attributes, arraysize(attributes));
    if (CKR_SESSION_HANDLE_INVALID == rv) {
      session_.reset();
      continue;
    }
    if (CKR_OK != rv) {
      LOG(ERROR) << "Key search init failed for label=" << label;
      return base::nullopt;
    }

    rv = C_FindObjects(*session_handle(), handles, arraysize(handles), &count);
    if (CKR_SESSION_HANDLE_INVALID == rv) {
      session_.reset();
      continue;
    }
    if (CKR_OK != rv) {
      LOG(ERROR) << "Key search failed for label=" << label;
      return base::nullopt;
    }

    rv = C_FindObjectsFinal(*session_handle());
    if (CKR_SESSION_HANDLE_INVALID == rv) {
      session_.reset();
      continue;
    }
    if (CKR_OK != rv)
      LOG(INFO) << "Could not finalize key search, proceeding anyways.";

    break;
  }

  if (count == 0) {
    LOG(INFO) << "No objects found with label=" << label;
    return base::nullopt;
  } else if (count > 1) {
    LOG(WARNING) << count << " objects found with label=" << label
                 << ", returning the first one.";
  }

  return handles[0];
}

CK_RV ChapsClient::ExportKey(CK_OBJECT_HANDLE key_handle,
                             brillo::SecureBlob* exported_key) {
  brillo::SecureBlob material;
  CK_RV rv = GetBytesAttribute(key_handle, CKA_VALUE, &material);
  if (CKR_OK != rv) {
    LOG(INFO) << "Failed to retrieve key material.";
    return rv;
  }

  exported_key->assign(material.begin(), material.end());
  return CKR_OK;
}

base::Optional<CK_OBJECT_HANDLE> ChapsClient::GenerateEncryptionKey() {
  if (!session_handle().has_value())
    return base::nullopt;

  std::string mutable_application_id(kApplicationID);
  std::string mutable_label(kEncryptKeyLabel);

  CK_OBJECT_CLASS object_class = CKO_DATA;
  CK_ULONG key_length = 32;
  CK_BBOOL true_value = CK_TRUE;
  CK_BBOOL false_value = CK_FALSE;
  CK_ATTRIBUTE attributes[] = {
      {CKA_APPLICATION, base::data(mutable_application_id),
       mutable_application_id.size()},
      {CKA_CLASS, &object_class, sizeof(object_class)},
      {CKA_TOKEN, &true_value, sizeof(true_value)},
      {CKA_LABEL, base::data(mutable_label), mutable_label.size()},
      {CKA_PRIVATE, &true_value, sizeof(true_value)},
      {CKA_MODIFIABLE, &false_value, sizeof(false_value)},
      {CKA_EXTRACTABLE, &true_value, sizeof(true_value)},
      {CKA_SENSITIVE, &false_value, sizeof(false_value)},
      {CKA_VALUE_LEN, &key_length, sizeof(key_length)}};

  CK_OBJECT_HANDLE key_handle;
  CK_MECHANISM mechanism = {CKM_AES_KEY_GEN, /* pParameter */ nullptr,
                            /* ulParameterLen*/ 0};

  for (size_t attempts = 0; attempts < kMaxAttemps; ++attempts) {
    CK_RV rv = C_GenerateKey(*session_handle(), &mechanism, attributes,
                             arraysize(attributes), &key_handle);
    if (CKR_SESSION_HANDLE_INVALID == rv) {
      session_.reset();
      continue;
    }
    if (CKR_OK != rv) {
      LOG(ERROR) << "Failed to generate encryption key.";
      return base::nullopt;
    }

    break;
  }
  LOG(INFO) << "Encryption key generated successfully.";
  return key_handle;
}

CK_RV ChapsClient::GetBytesAttribute(CK_OBJECT_HANDLE object_handle,
                                     CK_ATTRIBUTE_TYPE attribute_type,
                                     brillo::SecureBlob* attribute_value) {
  if (!session_handle().has_value())
    return CKR_GENERAL_ERROR;

  CK_ATTRIBUTE attribute = {attribute_type, /* pValue */ nullptr,
                            /* ulValueLen */ 0};
  CK_RV rv = C_GetAttributeValue(*session_handle(), object_handle, &attribute,
                                 /* ulCount */ 1);
  if (CKR_OK != rv) {
    LOG(ERROR) << "Failed to retrieve attribute length.";
    return rv;
  }

  if (attribute.ulValueLen <= 0 || attribute.ulValueLen > kMaxAttributeSize)
    return CKR_GENERAL_ERROR;

  attribute_value->resize(attribute.ulValueLen);
  attribute.pValue = attribute_value->data();
  rv = C_GetAttributeValue(*session_handle(), object_handle, &attribute,
                           /* ulCount */ 1);
  if (CKR_OK != rv) {
    LOG(ERROR) << "Failed to retrieve attribute value.";
    return rv;
  }
  return CKR_OK;
}

}  // namespace context
}  // namespace keymaster
}  // namespace arc
