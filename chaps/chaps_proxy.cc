// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/chaps_proxy.h"

#include <utility>

#include <base/logging.h>
#include <base/macros.h>
#include <base/memory/ptr_util.h>
#include <base/memory/ref_counted.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <dbus/message.h>
#include <dbus/object_path.h>

#include "chaps/chaps.h"
#include "chaps/chaps_utility.h"
#include "chaps/dbus/dbus_proxy_wrapper.h"
#include "chaps/dbus/scoped_bus.h"
#include "chaps/dbus_bindings/constants.h"
#include "chaps/isolate.h"
#include "pkcs11/cryptoki.h"

using base::AutoLock;
using brillo::SecureBlob;
using brillo::dbus_utils::ExtractMethodCallResults;
using std::string;
using std::vector;

namespace {

const char kDBusThreadName[] = "chaps_dbus_client_thread";

// These methods are equivalent to static_casting the blob to
// a regular std::vector since this is just an upcast. The reason why
// we use them is that the libbrillo D-Bus bindings rely on template
// argument type deduction to figure out which MessageReader and
// MessageWriter methods need to be called to marshal and unmarshal
// data into D-Bus messages, and SecureBlob has no specializations
// there.
inline const brillo::SecureVector AsVector(const SecureBlob& blob) {
  return blob;
}

inline brillo::SecureVector* AsVector(SecureBlob* blob) {
  return blob;
}

// We need to be able to shadow AtExitManagers because we don't know if the
// caller has an AtExitManager already or not (on Chrome it might, but on Linux
// it probably won't).
class ProxyAtExitManager : public base::AtExitManager {
 public:
  ProxyAtExitManager() : AtExitManager(true) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ProxyAtExitManager);
};

}  // namespace

namespace chaps {

// Below is the real implementation.

ChapsProxyImpl::ChapsProxyImpl(std::unique_ptr<base::AtExitManager> at_exit,
                               std::unique_ptr<base::Thread> dbus_thread,
                               scoped_refptr<DBusProxyWrapper> proxy)
    : at_exit_(std::move(at_exit)),
      dbus_thread_(std::move(dbus_thread)),
      proxy_(proxy) {}

ChapsProxyImpl::~ChapsProxyImpl() {}

// static
std::unique_ptr<ChapsProxyImpl> ChapsProxyImpl::Create(bool shadow_at_exit) {
  std::unique_ptr<base::AtExitManager> at_exit;
  if (shadow_at_exit) {
    at_exit = std::make_unique<ProxyAtExitManager>();
  }

  base::Thread::Options options(base::MessagePumpType::IO, 0);
  auto dbus_thread = std::make_unique<base::Thread>(kDBusThreadName);
  dbus_thread->StartWithOptions(options);

  scoped_refptr<ProxyWrapperConstructionTask> task(
      new ProxyWrapperConstructionTask);
  scoped_refptr<DBusProxyWrapper> proxy =
      task->ConstructProxyWrapper(dbus_thread->task_runner());
  if (!proxy)
    return nullptr;

  VLOG(1) << "Chaps proxy initialized (" << kChapsServicePath << ").";
  return base::WrapUnique(
      new ChapsProxyImpl(std::move(at_exit), std::move(dbus_thread), proxy));
}

bool ChapsProxyImpl::OpenIsolate(SecureBlob* isolate_credential,
                                 bool* new_isolate_created) {
  bool result = false;
  SecureBlob isolate_credential_in;
  SecureBlob isolate_credential_out;
  isolate_credential_in.swap(*isolate_credential);
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kOpenIsolateMethod, AsVector(isolate_credential_in));
  if (resp && ExtractMethodCallResults(resp.get(), nullptr,
                                       AsVector(&isolate_credential_out),
                                       new_isolate_created, &result))
    isolate_credential->swap(isolate_credential_out);
  return result;
}

void ChapsProxyImpl::CloseIsolate(const SecureBlob& isolate_credential) {
  proxy_->CallMethod(kCloseIsolateMethod, AsVector(isolate_credential));
}

bool ChapsProxyImpl::LoadToken(const SecureBlob& isolate_credential,
                               const string& path,
                               const SecureBlob& auth_data,
                               const string& label,
                               uint64_t* slot_id) {
  bool result = false;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kLoadTokenMethod, AsVector(isolate_credential), path,
                         AsVector(auth_data), label);
  return resp &&
         ExtractMethodCallResults(resp.get(), nullptr, slot_id, &result) &&
         result;
}

void ChapsProxyImpl::UnloadToken(const SecureBlob& isolate_credential,
                                 const string& path) {
  proxy_->CallMethod(kUnloadTokenMethod, AsVector(isolate_credential), path);
}

void ChapsProxyImpl::ChangeTokenAuthData(
    const string& path,
    const brillo::SecureBlob& old_auth_data,
    const brillo::SecureBlob& new_auth_data) {
  proxy_->CallMethod(kChangeTokenAuthDataMethod, path, AsVector(old_auth_data),
                     AsVector(new_auth_data));
}

bool ChapsProxyImpl::GetTokenPath(const SecureBlob& isolate_credential,
                                  uint64_t slot_id,
                                  string* path) {
  bool result = false;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kGetTokenPathMethod, AsVector(isolate_credential), slot_id);
  return resp && ExtractMethodCallResults(resp.get(), nullptr, path, &result) &&
         result;
}

void ChapsProxyImpl::SetLogLevel(const int32_t& level) {
  proxy_->CallMethod(kSetLogLevelMethod, level);
}

uint32_t ChapsProxyImpl::GetSlotList(const SecureBlob& isolate_credential,
                                     bool token_present,
                                     vector<uint64_t>* slot_list) {
  LOG_CK_RV_AND_RETURN_IF(!slot_list, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kGetSlotListMethod, AsVector(isolate_credential), token_present);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, slot_list, &result);
  return result;
}

uint32_t ChapsProxyImpl::GetSlotInfo(const SecureBlob& isolate_credential,
                                     uint64_t slot_id,
                                     SlotInfo* slot_info) {
  LOG_CK_RV_AND_RETURN_IF(!slot_info, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kGetSlotInfoMethod, AsVector(isolate_credential), slot_id);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, slot_info, &result);
  return result;
}

uint32_t ChapsProxyImpl::GetTokenInfo(const SecureBlob& isolate_credential,
                                      uint64_t slot_id,
                                      TokenInfo* token_info) {
  LOG_CK_RV_AND_RETURN_IF(!token_info, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kGetTokenInfoMethod, AsVector(isolate_credential), slot_id);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, token_info, &result);
  return result;
}

uint32_t ChapsProxyImpl::GetMechanismList(const SecureBlob& isolate_credential,
                                          uint64_t slot_id,
                                          vector<uint64_t>* mechanism_list) {
  LOG_CK_RV_AND_RETURN_IF(!mechanism_list, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kGetMechanismListMethod, AsVector(isolate_credential), slot_id);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, mechanism_list, &result);
  return result;
}

uint32_t ChapsProxyImpl::GetMechanismInfo(const SecureBlob& isolate_credential,
                                          uint64_t slot_id,
                                          uint64_t mechanism_type,
                                          MechanismInfo* mechanism_info) {
  LOG_CK_RV_AND_RETURN_IF(!mechanism_info, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kGetMechanismInfoMethod, AsVector(isolate_credential),
                         slot_id, mechanism_type);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, mechanism_info, &result);
  return result;
}

uint32_t ChapsProxyImpl::InitToken(const SecureBlob& isolate_credential,
                                   uint64_t slot_id,
                                   const string* so_pin,
                                   const vector<uint8_t>& label) {
  uint32_t result = CKR_GENERAL_ERROR;
  string tmp_pin;
  if (so_pin)
    tmp_pin = *so_pin;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kInitTokenMethod, AsVector(isolate_credential),
                         slot_id, (so_pin == nullptr), tmp_pin, label);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::InitPIN(const SecureBlob& isolate_credential,
                                 uint64_t session_id,
                                 const string* pin) {
  uint32_t result = CKR_GENERAL_ERROR;
  string tmp_pin;
  if (pin)
    tmp_pin = *pin;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kInitPINMethod, AsVector(isolate_credential),
                         session_id, (pin == nullptr), tmp_pin);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::SetPIN(const SecureBlob& isolate_credential,
                                uint64_t session_id,
                                const string* old_pin,
                                const string* new_pin) {
  uint32_t result = CKR_GENERAL_ERROR;
  string tmp_old_pin;
  if (old_pin)
    tmp_old_pin = *old_pin;
  string tmp_new_pin;
  if (new_pin)
    tmp_new_pin = *new_pin;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kSetPINMethod, AsVector(isolate_credential), session_id,
      (old_pin == nullptr), tmp_old_pin, (new_pin == nullptr), tmp_new_pin);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::OpenSession(const SecureBlob& isolate_credential,
                                     uint64_t slot_id,
                                     uint64_t flags,
                                     uint64_t* session_id) {
  LOG_CK_RV_AND_RETURN_IF(!session_id, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kOpenSessionMethod, AsVector(isolate_credential), slot_id, flags);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, session_id, &result);
  return result;
}

uint32_t ChapsProxyImpl::CloseSession(const SecureBlob& isolate_credential,
                                      uint64_t session_id) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kCloseSessionMethod, AsVector(isolate_credential), session_id);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::GetSessionInfo(const SecureBlob& isolate_credential,
                                        uint64_t session_id,
                                        SessionInfo* session_info) {
  LOG_CK_RV_AND_RETURN_IF(!session_info, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kGetSessionInfoMethod, AsVector(isolate_credential), session_id);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, session_info, &result);
  return result;
}

uint32_t ChapsProxyImpl::GetOperationState(const SecureBlob& isolate_credential,
                                           uint64_t session_id,
                                           vector<uint8_t>* operation_state) {
  LOG_CK_RV_AND_RETURN_IF(!operation_state, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kGetOperationStateMethod, AsVector(isolate_credential), session_id);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, operation_state, &result);
  return result;
}

uint32_t ChapsProxyImpl::SetOperationState(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    const vector<uint8_t>& operation_state,
    uint64_t encryption_key_handle,
    uint64_t authentication_key_handle) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kSetOperationStateMethod, AsVector(isolate_credential), session_id,
      operation_state, encryption_key_handle, authentication_key_handle);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::Login(const SecureBlob& isolate_credential,
                               uint64_t session_id,
                               uint64_t user_type,
                               const string* pin) {
  uint32_t result = CKR_GENERAL_ERROR;
  string tmp_pin;
  if (pin)
    tmp_pin = *pin;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kLoginMethod, AsVector(isolate_credential), session_id,
                         user_type, (pin == nullptr), tmp_pin);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::Logout(const SecureBlob& isolate_credential,
                                uint64_t session_id) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kLogoutMethod, AsVector(isolate_credential), session_id);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::CreateObject(const SecureBlob& isolate_credential,
                                      uint64_t session_id,
                                      const vector<uint8_t>& attributes,
                                      uint64_t* new_object_handle) {
  LOG_CK_RV_AND_RETURN_IF(!new_object_handle, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kCreateObjectMethod, AsVector(isolate_credential),
                         session_id, attributes);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, new_object_handle, &result);
  return result;
}

uint32_t ChapsProxyImpl::CopyObject(const SecureBlob& isolate_credential,
                                    uint64_t session_id,
                                    uint64_t object_handle,
                                    const vector<uint8_t>& attributes,
                                    uint64_t* new_object_handle) {
  LOG_CK_RV_AND_RETURN_IF(!new_object_handle, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kCopyObjectMethod, AsVector(isolate_credential),
                         session_id, object_handle, attributes);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, new_object_handle, &result);
  return result;
}

uint32_t ChapsProxyImpl::DestroyObject(const SecureBlob& isolate_credential,
                                       uint64_t session_id,
                                       uint64_t object_handle) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kDestroyObjectMethod, AsVector(isolate_credential),
                         session_id, object_handle);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::GetObjectSize(const SecureBlob& isolate_credential,
                                       uint64_t session_id,
                                       uint64_t object_handle,
                                       uint64_t* object_size) {
  LOG_CK_RV_AND_RETURN_IF(!object_size, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kGetObjectSizeMethod, AsVector(isolate_credential),
                         session_id, object_handle);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, object_size, &result);
  return result;
}

uint32_t ChapsProxyImpl::GetAttributeValue(const SecureBlob& isolate_credential,
                                           uint64_t session_id,
                                           uint64_t object_handle,
                                           const vector<uint8_t>& attributes_in,
                                           vector<uint8_t>* attributes_out) {
  LOG_CK_RV_AND_RETURN_IF(!attributes_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kGetAttributeValueMethod, AsVector(isolate_credential),
                         session_id, object_handle, attributes_in);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, attributes_out, &result);
  return result;
}

uint32_t ChapsProxyImpl::SetAttributeValue(const SecureBlob& isolate_credential,
                                           uint64_t session_id,
                                           uint64_t object_handle,
                                           const vector<uint8_t>& attributes) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kSetAttributeValueMethod, AsVector(isolate_credential),
                         session_id, object_handle, attributes);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::FindObjectsInit(const SecureBlob& isolate_credential,
                                         uint64_t session_id,
                                         const vector<uint8_t>& attributes) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kFindObjectsInitMethod, AsVector(isolate_credential),
                         session_id, attributes);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::FindObjects(const SecureBlob& isolate_credential,
                                     uint64_t session_id,
                                     uint64_t max_object_count,
                                     vector<uint64_t>* object_list) {
  if (!object_list || object_list->size() > 0)
    LOG_CK_RV_AND_RETURN(CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kFindObjectsMethod, AsVector(isolate_credential),
                         session_id, max_object_count);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, object_list, &result);
  return result;
}

uint32_t ChapsProxyImpl::FindObjectsFinal(const SecureBlob& isolate_credential,
                                          uint64_t session_id) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kFindObjectsFinalMethod, AsVector(isolate_credential), session_id);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::EncryptInit(const SecureBlob& isolate_credential,
                                     uint64_t session_id,
                                     uint64_t mechanism_type,
                                     const vector<uint8_t>& mechanism_parameter,
                                     uint64_t key_handle) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kEncryptInitMethod, AsVector(isolate_credential), session_id,
      mechanism_type, mechanism_parameter, key_handle);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::Encrypt(const SecureBlob& isolate_credential,
                                 uint64_t session_id,
                                 const vector<uint8_t>& data_in,
                                 uint64_t max_out_length,
                                 uint64_t* actual_out_length,
                                 vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kEncryptMethod, AsVector(isolate_credential),
                         session_id, data_in, max_out_length);
  if (resp) {
    ExtractMethodCallResults(resp.get(), nullptr, actual_out_length, data_out,
                             &result);
  }
  return result;
}

uint32_t ChapsProxyImpl::EncryptUpdate(const SecureBlob& isolate_credential,
                                       uint64_t session_id,
                                       const vector<uint8_t>& data_in,
                                       uint64_t max_out_length,
                                       uint64_t* actual_out_length,
                                       vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kEncryptUpdateMethod, AsVector(isolate_credential),
                         session_id, data_in, max_out_length);
  if (resp) {
    ExtractMethodCallResults(resp.get(), nullptr, actual_out_length, data_out,
                             &result);
  }
  return result;
}

uint32_t ChapsProxyImpl::EncryptFinal(const SecureBlob& isolate_credential,
                                      uint64_t session_id,
                                      uint64_t max_out_length,
                                      uint64_t* actual_out_length,
                                      vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kEncryptFinalMethod, AsVector(isolate_credential),
                         session_id, max_out_length);
  if (resp) {
    ExtractMethodCallResults(resp.get(), nullptr, actual_out_length, data_out,
                             &result);
  }
  return result;
}

void ChapsProxyImpl::EncryptCancel(const SecureBlob& isolate_credential,
                                   uint64_t session_id) {
  proxy_->CallMethod(kEncryptCancelMethod, AsVector(isolate_credential),
                     session_id);
}

uint32_t ChapsProxyImpl::DecryptInit(const SecureBlob& isolate_credential,
                                     uint64_t session_id,
                                     uint64_t mechanism_type,
                                     const vector<uint8_t>& mechanism_parameter,
                                     uint64_t key_handle) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kDecryptInitMethod, AsVector(isolate_credential), session_id,
      mechanism_type, mechanism_parameter, key_handle);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::Decrypt(const SecureBlob& isolate_credential,
                                 uint64_t session_id,
                                 const vector<uint8_t>& data_in,
                                 uint64_t max_out_length,
                                 uint64_t* actual_out_length,
                                 vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kDecryptMethod, AsVector(isolate_credential),
                         session_id, data_in, max_out_length);
  if (resp) {
    ExtractMethodCallResults(resp.get(), nullptr, actual_out_length, data_out,
                             &result);
  }
  return result;
}

uint32_t ChapsProxyImpl::DecryptUpdate(const SecureBlob& isolate_credential,
                                       uint64_t session_id,
                                       const vector<uint8_t>& data_in,
                                       uint64_t max_out_length,
                                       uint64_t* actual_out_length,
                                       vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kDecryptUpdateMethod, AsVector(isolate_credential),
                         session_id, data_in, max_out_length);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, actual_out_length, data_out,
                             &result);
  return result;
}

uint32_t ChapsProxyImpl::DecryptFinal(const SecureBlob& isolate_credential,
                                      uint64_t session_id,
                                      uint64_t max_out_length,
                                      uint64_t* actual_out_length,
                                      vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kDecryptFinalMethod, AsVector(isolate_credential),
                         session_id, max_out_length);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, actual_out_length, data_out,
                             &result);
  return result;
}

void ChapsProxyImpl::DecryptCancel(const SecureBlob& isolate_credential,
                                   uint64_t session_id) {
  proxy_->CallMethod(kDecryptCancelMethod, AsVector(isolate_credential),
                     session_id);
}

uint32_t ChapsProxyImpl::DigestInit(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    uint64_t mechanism_type,
    const vector<uint8_t>& mechanism_parameter) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kDigestInitMethod, AsVector(isolate_credential),
                         session_id, mechanism_type, mechanism_parameter);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::Digest(const SecureBlob& isolate_credential,
                                uint64_t session_id,
                                const vector<uint8_t>& data_in,
                                uint64_t max_out_length,
                                uint64_t* actual_out_length,
                                vector<uint8_t>* digest) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kDigestMethod, AsVector(isolate_credential),
                         session_id, data_in, max_out_length);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, actual_out_length, digest,
                             &result);
  return result;
}

uint32_t ChapsProxyImpl::DigestUpdate(const SecureBlob& isolate_credential,
                                      uint64_t session_id,
                                      const vector<uint8_t>& data_in) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kDigestUpdateMethod, AsVector(isolate_credential), session_id, data_in);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::DigestKey(const SecureBlob& isolate_credential,
                                   uint64_t session_id,
                                   uint64_t key_handle) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kDigestKeyMethod, AsVector(isolate_credential), session_id, key_handle);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::DigestFinal(const SecureBlob& isolate_credential,
                                     uint64_t session_id,
                                     uint64_t max_out_length,
                                     uint64_t* actual_out_length,
                                     vector<uint8_t>* digest) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kDigestFinalMethod, AsVector(isolate_credential),
                         session_id, max_out_length);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, actual_out_length, digest,
                             &result);
  return result;
}

void ChapsProxyImpl::DigestCancel(const SecureBlob& isolate_credential,
                                  uint64_t session_id) {
  proxy_->CallMethod(kDigestCancelMethod, AsVector(isolate_credential),
                     session_id);
}

uint32_t ChapsProxyImpl::SignInit(const SecureBlob& isolate_credential,
                                  uint64_t session_id,
                                  uint64_t mechanism_type,
                                  const vector<uint8_t>& mechanism_parameter,
                                  uint64_t key_handle) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kSignInitMethod, AsVector(isolate_credential), session_id, mechanism_type,
      mechanism_parameter, key_handle);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::Sign(const SecureBlob& isolate_credential,
                              uint64_t session_id,
                              const vector<uint8_t>& data,
                              uint64_t max_out_length,
                              uint64_t* actual_out_length,
                              vector<uint8_t>* signature) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kSignMethod, AsVector(isolate_credential), session_id,
                         data, max_out_length);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, actual_out_length, signature,
                             &result);
  return result;
}

uint32_t ChapsProxyImpl::SignUpdate(const SecureBlob& isolate_credential,
                                    uint64_t session_id,
                                    const vector<uint8_t>& data_part) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kSignUpdateMethod, AsVector(isolate_credential), session_id, data_part);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::SignFinal(const SecureBlob& isolate_credential,
                                   uint64_t session_id,
                                   uint64_t max_out_length,
                                   uint64_t* actual_out_length,
                                   vector<uint8_t>* signature) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kSignFinalMethod, AsVector(isolate_credential),
                         session_id, max_out_length);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, actual_out_length, signature,
                             &result);
  return result;
}

void ChapsProxyImpl::SignCancel(const SecureBlob& isolate_credential,
                                uint64_t session_id) {
  proxy_->CallMethod(kSignCancelMethod, AsVector(isolate_credential),
                     session_id);
}

uint32_t ChapsProxyImpl::SignRecoverInit(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    uint64_t mechanism_type,
    const vector<uint8_t>& mechanism_parameter,
    uint64_t key_handle) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kSignRecoverInitMethod, AsVector(isolate_credential), session_id,
      mechanism_type, mechanism_parameter, key_handle);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::SignRecover(const SecureBlob& isolate_credential,
                                     uint64_t session_id,
                                     const vector<uint8_t>& data,
                                     uint64_t max_out_length,
                                     uint64_t* actual_out_length,
                                     vector<uint8_t>* signature) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kSignRecoverMethod, AsVector(isolate_credential),
                         session_id, data, max_out_length);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, actual_out_length, signature,
                             &result);
  return result;
}

uint32_t ChapsProxyImpl::VerifyInit(const SecureBlob& isolate_credential,
                                    uint64_t session_id,
                                    uint64_t mechanism_type,
                                    const vector<uint8_t>& mechanism_parameter,
                                    uint64_t key_handle) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kVerifyInitMethod, AsVector(isolate_credential), session_id,
      mechanism_type, mechanism_parameter, key_handle);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::Verify(const SecureBlob& isolate_credential,
                                uint64_t session_id,
                                const vector<uint8_t>& data,
                                const vector<uint8_t>& signature) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kVerifyMethod, AsVector(isolate_credential), session_id, data, signature);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::VerifyUpdate(const SecureBlob& isolate_credential,
                                      uint64_t session_id,
                                      const vector<uint8_t>& data_part) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kVerifyUpdateMethod, AsVector(isolate_credential), session_id, data_part);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::VerifyFinal(const SecureBlob& isolate_credential,
                                     uint64_t session_id,
                                     const vector<uint8_t>& signature) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kVerifyUpdateMethod, AsVector(isolate_credential), session_id, signature);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

void ChapsProxyImpl::VerifyCancel(const SecureBlob& isolate_credential,
                                  uint64_t session_id) {
  proxy_->CallMethod(kVerifyCancelMethod, AsVector(isolate_credential),
                     session_id);
}

uint32_t ChapsProxyImpl::VerifyRecoverInit(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    uint64_t mechanism_type,
    const vector<uint8_t>& mechanism_parameter,
    uint64_t key_handle) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kVerifyRecoverInitMethod, AsVector(isolate_credential), session_id,
      mechanism_type, mechanism_parameter, key_handle);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::VerifyRecover(const SecureBlob& isolate_credential,
                                       uint64_t session_id,
                                       const vector<uint8_t>& signature,
                                       uint64_t max_out_length,
                                       uint64_t* actual_out_length,
                                       vector<uint8_t>* data) {
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kVerifyRecoverMethod, AsVector(isolate_credential),
                         session_id, signature, max_out_length);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, actual_out_length, data,
                             &result);
  return result;
}

uint32_t ChapsProxyImpl::DigestEncryptUpdate(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    const vector<uint8_t>& data_in,
    uint64_t max_out_length,
    uint64_t* actual_out_length,
    vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kDigestEncryptUpdateMethod, AsVector(isolate_credential), session_id,
      data_in, max_out_length);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, actual_out_length, data_out,
                             &result);
  return result;
}

uint32_t ChapsProxyImpl::DecryptDigestUpdate(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    const vector<uint8_t>& data_in,
    uint64_t max_out_length,
    uint64_t* actual_out_length,
    vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kDecryptDigestUpdateMethod, AsVector(isolate_credential), session_id,
      data_in, max_out_length);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, actual_out_length, data_out,
                             &result);
  return result;
}

uint32_t ChapsProxyImpl::SignEncryptUpdate(const SecureBlob& isolate_credential,
                                           uint64_t session_id,
                                           const vector<uint8_t>& data_in,
                                           uint64_t max_out_length,
                                           uint64_t* actual_out_length,
                                           vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kSignEncryptUpdateMethod, AsVector(isolate_credential),
                         session_id, data_in, max_out_length);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, actual_out_length, data_out,
                             &result);
  return result;
}

uint32_t ChapsProxyImpl::DecryptVerifyUpdate(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    const vector<uint8_t>& data_in,
    uint64_t max_out_length,
    uint64_t* actual_out_length,
    vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kDecryptVerifyUpdateMethod, AsVector(isolate_credential), session_id,
      data_in, max_out_length);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, actual_out_length, data_out,
                             &result);
  return result;
}

uint32_t ChapsProxyImpl::GenerateKey(const SecureBlob& isolate_credential,
                                     uint64_t session_id,
                                     uint64_t mechanism_type,
                                     const vector<uint8_t>& mechanism_parameter,
                                     const vector<uint8_t>& attributes,
                                     uint64_t* key_handle) {
  LOG_CK_RV_AND_RETURN_IF(!key_handle, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kGenerateKeyMethod, AsVector(isolate_credential), session_id,
      mechanism_type, mechanism_parameter, attributes);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, key_handle, &result);
  return result;
}

uint32_t ChapsProxyImpl::GenerateKeyPair(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    uint64_t mechanism_type,
    const vector<uint8_t>& mechanism_parameter,
    const vector<uint8_t>& public_attributes,
    const vector<uint8_t>& private_attributes,
    uint64_t* public_key_handle,
    uint64_t* private_key_handle) {
  LOG_CK_RV_AND_RETURN_IF(!public_key_handle || !private_key_handle,
                          CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kGenerateKeyPairMethod, AsVector(isolate_credential),
                         session_id, mechanism_type, mechanism_parameter,
                         public_attributes, private_attributes);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, public_key_handle,
                             private_key_handle, &result);
  return result;
}

uint32_t ChapsProxyImpl::WrapKey(const SecureBlob& isolate_credential,
                                 uint64_t session_id,
                                 uint64_t mechanism_type,
                                 const vector<uint8_t>& mechanism_parameter,
                                 uint64_t wrapping_key_handle,
                                 uint64_t key_handle,
                                 uint64_t max_out_length,
                                 uint64_t* actual_out_length,
                                 vector<uint8_t>* wrapped_key) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !wrapped_key,
                          CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kWrapKeyMethod, AsVector(isolate_credential), session_id, mechanism_type,
      mechanism_parameter, wrapping_key_handle, key_handle, max_out_length);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, actual_out_length,
                             wrapped_key, &result);
  return result;
}

uint32_t ChapsProxyImpl::UnwrapKey(const SecureBlob& isolate_credential,
                                   uint64_t session_id,
                                   uint64_t mechanism_type,
                                   const vector<uint8_t>& mechanism_parameter,
                                   uint64_t wrapping_key_handle,
                                   const vector<uint8_t>& wrapped_key,
                                   const vector<uint8_t>& attributes,
                                   uint64_t* key_handle) {
  LOG_CK_RV_AND_RETURN_IF(!key_handle, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kUnwrapKeyMethod, AsVector(isolate_credential),
                         session_id, mechanism_type, mechanism_parameter,
                         wrapping_key_handle, wrapped_key, attributes);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, key_handle, &result);
  return result;
}

uint32_t ChapsProxyImpl::DeriveKey(const SecureBlob& isolate_credential,
                                   uint64_t session_id,
                                   uint64_t mechanism_type,
                                   const vector<uint8_t>& mechanism_parameter,
                                   uint64_t base_key_handle,
                                   const vector<uint8_t>& attributes,
                                   uint64_t* key_handle) {
  LOG_CK_RV_AND_RETURN_IF(!key_handle, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kDeriveKeyMethod, AsVector(isolate_credential), session_id,
      mechanism_type, mechanism_parameter, base_key_handle, attributes);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, key_handle, &result);
  return result;
}

uint32_t ChapsProxyImpl::SeedRandom(const SecureBlob& isolate_credential,
                                    uint64_t session_id,
                                    const vector<uint8_t>& seed) {
  LOG_CK_RV_AND_RETURN_IF(seed.size() == 0, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp = proxy_->CallMethod(
      kSeedRandomMethod, AsVector(isolate_credential), session_id, seed);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, &result);
  return result;
}

uint32_t ChapsProxyImpl::GenerateRandom(const SecureBlob& isolate_credential,
                                        uint64_t session_id,
                                        uint64_t num_bytes,
                                        vector<uint8_t>* random_data) {
  LOG_CK_RV_AND_RETURN_IF(!random_data || num_bytes == 0, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  std::unique_ptr<dbus::Response> resp =
      proxy_->CallMethod(kGenerateRandomMethod, AsVector(isolate_credential),
                         session_id, num_bytes);
  if (resp)
    ExtractMethodCallResults(resp.get(), nullptr, random_data, &result);
  return result;
}

}  // namespace chaps
