// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/session_manager_dbus_adaptor.h"

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/file_util.h>
#include <base/memory/scoped_ptr.h>
#include <base/stl_util.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/exported_object.h>
#include <dbus/file_descriptor.h>
#include <dbus/message.h>

#include "login_manager/dbus_error_types.h"
#include "login_manager/policy_service.h"
#include "login_manager/session_manager_impl.h"

namespace login_manager {
namespace {

const char kBindingsPath[] =
    "/usr/share/dbus-1/interfaces/org.chromium.SessionManagerInterface.xml";
const char kDBusIntrospectableInterface[] =
    "org.freedesktop.DBus.Introspectable";
const char kDBusIntrospectMethod[] = "Introspect";

// Passes |method_call| to |handler| and passes the response to
// |response_sender|. If |handler| returns NULL, an empty response is created
// and sent.
void HandleSynchronousDBusMethodCall(
    base::Callback<scoped_ptr<dbus::Response>(dbus::MethodCall*)> handler,
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  scoped_ptr<dbus::Response> response = handler.Run(method_call);
  if (!response)
    response = dbus::Response::FromMethodCall(method_call);
  response_sender.Run(std::move(response));
}

scoped_ptr<dbus::Response> CreateError(dbus::MethodCall* call,
                                       const std::string& name,
                                       const std::string& message) {
  // TODO(achuith): Remove debug logging once crbug.com/631640 is resolved.
  LOG(ERROR) << "CreateError name=" << name << ", message=" << message;
  return dbus::ErrorResponse::FromMethodCall(call, name, message);
}

// Creates a new "invalid args" reply to call.
scoped_ptr<dbus::Response> CreateInvalidArgsError(dbus::MethodCall* call,
                                                  std::string message) {
  return CreateError(call, DBUS_ERROR_INVALID_ARGS, "Signature is: " + message);
}

// Craft a Response to call that is appropriate, given the contents of error.
// If error is set, this will be an ErrorResponse. Otherwise, it will be a
// Response containing payload.
scoped_ptr<dbus::Response> CraftAppropriateResponseWithBool(
    dbus::MethodCall* call,
    const SessionManagerImpl::Error& error,
    bool payload) {
  scoped_ptr<dbus::Response> response;
  if (error.is_set()) {
    response = CreateError(call, error.name(), error.message());
  } else {
    response = dbus::Response::FromMethodCall(call);
    dbus::MessageWriter writer(response.get());
    writer.AppendBool(payload);
  }
  return response;
}

scoped_ptr<dbus::Response> CraftAppropriateResponseWithString(
    dbus::MethodCall* call,
    const SessionManagerImpl::Error& error,
    const std::string& payload) {
  scoped_ptr<dbus::Response> response;
  if (error.is_set()) {
    response = CreateError(call, error.name(), error.message());
  } else {
    response = dbus::Response::FromMethodCall(call);
    dbus::MessageWriter writer(response.get());
    writer.AppendString(payload);
  }
  return response;
}

scoped_ptr<dbus::Response> CraftAppropriateResponseWithBytes(
    dbus::MethodCall* call,
    const SessionManagerImpl::Error& error,
    const std::vector<uint8_t>& payload) {
  scoped_ptr<dbus::Response> response;
  if (error.is_set()) {
    response = CreateError(call, error.name(), error.message());
  } else {
    response = dbus::Response::FromMethodCall(call);
    dbus::MessageWriter writer(response.get());
    writer.AppendArrayOfBytes(payload.data(), payload.size());
  }
  return response;
}

// Handles completion of a server-backed state key retrieval operation and
// passes the response back to the waiting DBus invocation context.
void HandleGetServerBackedStateKeysCompletion(
    dbus::MethodCall* call,
    const dbus::ExportedObject::ResponseSender& sender,
    const std::vector<std::vector<uint8_t>>& state_keys) {
  scoped_ptr<dbus::Response> response(dbus::Response::FromMethodCall(call));
  dbus::MessageWriter writer(response.get());
  dbus::MessageWriter array_writer(NULL);
  writer.OpenArray("ay", &array_writer);
  for (std::vector<std::vector<uint8_t>>::const_iterator state_key(
           state_keys.begin());
       state_key != state_keys.end();
       ++state_key) {
    array_writer.AppendArrayOfBytes(state_key->data(), state_key->size());
  }
  writer.CloseContainer(&array_writer);
  sender.Run(std::move(response));
}

}  // namespace

// Callback that forwards a result to a DBus invocation context.
class DBusMethodCompletion {
 public:
  static PolicyService::Completion CreateCallback(
      dbus::MethodCall* call,
      const dbus::ExportedObject::ResponseSender& sender);
  virtual ~DBusMethodCompletion();

 private:
  DBusMethodCompletion() : call_(nullptr) {}
  DBusMethodCompletion(dbus::MethodCall* call,
                       const dbus::ExportedObject::ResponseSender& sender);
  void HandleResult(const PolicyService::Error& error);

  dbus::MethodCall* call_ = nullptr;
  dbus::ExportedObject::ResponseSender sender_;

  DISALLOW_COPY_AND_ASSIGN(DBusMethodCompletion);
};

// static
PolicyService::Completion DBusMethodCompletion::CreateCallback(
      dbus::MethodCall* call,
      const dbus::ExportedObject::ResponseSender& sender) {
  return base::Bind(&DBusMethodCompletion::HandleResult,
                    base::Owned(new DBusMethodCompletion(call, sender)));
}

// Apparently, call is owned by sender, so it's safe to hang on to it.
DBusMethodCompletion::DBusMethodCompletion(
    dbus::MethodCall* call,
    const dbus::ExportedObject::ResponseSender& sender)
    : call_(call), sender_(sender) {
}

DBusMethodCompletion::~DBusMethodCompletion() {
  if (call_) {
    NOTREACHED() << "Unfinished DBUS call!";
    sender_.Run(dbus::Response::FromMethodCall(call_));
  }
}

void DBusMethodCompletion::HandleResult(const PolicyService::Error& error) {
  if (error.code() == dbus_error::kNone) {
    scoped_ptr<dbus::Response> response(dbus::Response::FromMethodCall(call_));
    dbus::MessageWriter writer(response.get());
    writer.AppendBool(true);
    sender_.Run(std::move(response));
    call_ = nullptr;
  } else {
    sender_.Run(
        dbus::ErrorResponse::FromMethodCall(call_,
                                            error.code(), error.message()));
    call_ = nullptr;
  }
}

SessionManagerDBusAdaptor::SessionManagerDBusAdaptor(SessionManagerImpl* impl)
    : impl_(impl) {
  CHECK(impl_);
}

SessionManagerDBusAdaptor::~SessionManagerDBusAdaptor() {
}

void SessionManagerDBusAdaptor::ExportDBusMethods(
    dbus::ExportedObject* object) {
  ExportSyncDBusMethod(object,
                       kSessionManagerEmitLoginPromptVisible,
                       &SessionManagerDBusAdaptor::EmitLoginPromptVisible);
  ExportSyncDBusMethod(object,
                       "EnableChromeTesting",
                       &SessionManagerDBusAdaptor::EnableChromeTesting);
  ExportSyncDBusMethod(object,
                       kSessionManagerStartSession,
                       &SessionManagerDBusAdaptor::StartSession);
  ExportSyncDBusMethod(object,
                       kSessionManagerStopSession,
                       &SessionManagerDBusAdaptor::StopSession);

  ExportAsyncDBusMethod(object,
                        kSessionManagerStorePolicy,
                        &SessionManagerDBusAdaptor::StorePolicy);
  ExportSyncDBusMethod(object,
                       kSessionManagerRetrievePolicy,
                       &SessionManagerDBusAdaptor::RetrievePolicy);

  ExportAsyncDBusMethod(object,
                        kSessionManagerStorePolicyForUser,
                        &SessionManagerDBusAdaptor::StorePolicyForUser);
  ExportSyncDBusMethod(object,
                       kSessionManagerRetrievePolicyForUser,
                       &SessionManagerDBusAdaptor::RetrievePolicyForUser);

  ExportAsyncDBusMethod(
      object,
      kSessionManagerStoreDeviceLocalAccountPolicy,
      &SessionManagerDBusAdaptor::StoreDeviceLocalAccountPolicy);
  ExportSyncDBusMethod(
      object,
      kSessionManagerRetrieveDeviceLocalAccountPolicy,
      &SessionManagerDBusAdaptor::RetrieveDeviceLocalAccountPolicy);

  ExportSyncDBusMethod(object,
                       kSessionManagerRetrieveSessionState,
                       &SessionManagerDBusAdaptor::RetrieveSessionState);
  ExportSyncDBusMethod(object,
                       kSessionManagerRetrieveActiveSessions,
                       &SessionManagerDBusAdaptor::RetrieveActiveSessions);

  ExportSyncDBusMethod(
      object,
      kSessionManagerHandleSupervisedUserCreationStarting,
      &SessionManagerDBusAdaptor::HandleSupervisedUserCreationStarting);
  ExportSyncDBusMethod(
      object,
      kSessionManagerHandleSupervisedUserCreationFinished,
      &SessionManagerDBusAdaptor::HandleSupervisedUserCreationFinished);
  ExportSyncDBusMethod(object,
                       kSessionManagerLockScreen,
                       &SessionManagerDBusAdaptor::LockScreen);
  ExportSyncDBusMethod(object,
                       kSessionManagerHandleLockScreenShown,
                       &SessionManagerDBusAdaptor::HandleLockScreenShown);
  ExportSyncDBusMethod(object,
                       kSessionManagerHandleLockScreenDismissed,
                       &SessionManagerDBusAdaptor::HandleLockScreenDismissed);

  ExportSyncDBusMethod(object,
                       kSessionManagerRestartJob,
                       &SessionManagerDBusAdaptor::RestartJob);
  ExportSyncDBusMethod(object,
                       kSessionManagerStartDeviceWipe,
                       &SessionManagerDBusAdaptor::StartDeviceWipe);
  ExportSyncDBusMethod(object,
                       kSessionManagerSetFlagsForUser,
                       &SessionManagerDBusAdaptor::SetFlagsForUser);

  ExportAsyncDBusMethod(object,
                        kSessionManagerGetServerBackedStateKeys,
                        &SessionManagerDBusAdaptor::GetServerBackedStateKeys);
  ExportSyncDBusMethod(object,
                       kSessionManagerInitMachineInfo,
                       &SessionManagerDBusAdaptor::InitMachineInfo);

  ExportSyncDBusMethod(object,
                       kSessionManagerStartContainer,
                       &SessionManagerDBusAdaptor::StartContainer);
  ExportSyncDBusMethod(object,
                       kSessionManagerStopContainer,
                       &SessionManagerDBusAdaptor::StopContainer);
  ExportSyncDBusMethod(object,
                       kSessionManagerStartArcInstance,
                       &SessionManagerDBusAdaptor::StartArcInstance);
  ExportSyncDBusMethod(object,
                       kSessionManagerStopArcInstance,
                       &SessionManagerDBusAdaptor::StopArcInstance);
  ExportSyncDBusMethod(object,
                       kSessionManagerPrioritizeArcInstance,
                       &SessionManagerDBusAdaptor::PrioritizeArcInstance);
  ExportSyncDBusMethod(object,
                       kSessionManagerGetArcStartTimeTicks,
                       &SessionManagerDBusAdaptor::GetArcStartTimeTicks);
  ExportSyncDBusMethod(object,
                       kSessionManagerRemoveArcData,
                       &SessionManagerDBusAdaptor::RemoveArcData);

  CHECK(object->ExportMethodAndBlock(
      kDBusIntrospectableInterface,
      kDBusIntrospectMethod,
      base::Bind(&HandleSynchronousDBusMethodCall,
                 base::Bind(&SessionManagerDBusAdaptor::Introspect,
                            base::Unretained(this)))));
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::EmitLoginPromptVisible(
    dbus::MethodCall* call) {
  SessionManagerImpl::Error error;
  impl_->EmitLoginPromptVisible(&error);
  if (error.is_set())
    return CreateError(call, error.name(), error.message());
  return scoped_ptr<dbus::Response>(dbus::Response::FromMethodCall(call));
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::EnableChromeTesting(
    dbus::MethodCall* call) {
  dbus::MessageReader reader(call);
  bool relaunch;
  std::vector<std::string> extra_args;
  if (!reader.PopBool(&relaunch) || !reader.PopArrayOfStrings(&extra_args))
    return CreateInvalidArgsError(call, call->GetSignature());

  SessionManagerImpl::Error error;
  std::string testing_path =
      impl_->EnableChromeTesting(relaunch, extra_args, &error);
  return CraftAppropriateResponseWithString(call, error, testing_path);
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::StartSession(
    dbus::MethodCall* call) {
  dbus::MessageReader reader(call);
  std::string account_id, unique_id;
  if (!reader.PopString(&account_id) || !reader.PopString(&unique_id))
    return CreateInvalidArgsError(call, call->GetSignature());

  SessionManagerImpl::Error error;
  bool success = impl_->StartSession(account_id, unique_id, &error);
  return CraftAppropriateResponseWithBool(call, error, success);
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::StopSession(
    dbus::MethodCall* call) {
  // Though this takes a string (unique_id), it is ignored.
  bool success = impl_->StopSession();
  scoped_ptr<dbus::Response> response(dbus::Response::FromMethodCall(call));
  dbus::MessageWriter writer(response.get());
  writer.AppendBool(success);
  return response;
}

void SessionManagerDBusAdaptor::StorePolicy(
    dbus::MethodCall* call,
    dbus::ExportedObject::ResponseSender sender) {
  const uint8_t* policy_blob = NULL;
  size_t policy_blob_len = 0;
  dbus::MessageReader reader(call);
  // policy_blob points into reader after pop.
  if (!reader.PopArrayOfBytes(&policy_blob, &policy_blob_len)) {
    sender.Run(CreateInvalidArgsError(call, call->GetSignature()));
  } else {
    impl_->StorePolicy(policy_blob, policy_blob_len,
                       DBusMethodCompletion::CreateCallback(call, sender));
    // Response will be sent asynchronously.
  }
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::RetrievePolicy(
    dbus::MethodCall* call) {
  std::vector<uint8_t> policy_data;
  SessionManagerImpl::Error error;
  impl_->RetrievePolicy(&policy_data, &error);
  return CraftAppropriateResponseWithBytes(call, error, policy_data);
}

void SessionManagerDBusAdaptor::StorePolicyForUser(
    dbus::MethodCall* call,
    dbus::ExportedObject::ResponseSender sender) {
  std::string account_id;
  const uint8_t* policy_blob = NULL;
  size_t policy_blob_len = 0;
  dbus::MessageReader reader(call);
  // policy_blob points into reader after pop.
  if (!reader.PopString(&account_id) ||
      !reader.PopArrayOfBytes(&policy_blob, &policy_blob_len)) {
    sender.Run(CreateInvalidArgsError(call, call->GetSignature()));
  } else {
    impl_->StorePolicyForUser(
        account_id, policy_blob, policy_blob_len,
        DBusMethodCompletion::CreateCallback(call, sender));
    // Response will normally be sent asynchronously.
  }
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::RetrievePolicyForUser(
    dbus::MethodCall* call) {
  std::string account_id;
  dbus::MessageReader reader(call);

  if (!reader.PopString(&account_id))
    return CreateInvalidArgsError(call, call->GetSignature());

  std::vector<uint8_t> policy_data;
  SessionManagerImpl::Error error;
  impl_->RetrievePolicyForUser(account_id, &policy_data, &error);
  return CraftAppropriateResponseWithBytes(call, error, policy_data);
}

void SessionManagerDBusAdaptor::StoreDeviceLocalAccountPolicy(
    dbus::MethodCall* call,
    dbus::ExportedObject::ResponseSender sender) {
  std::string account_id;
  const uint8_t* policy_blob = NULL;
  size_t policy_blob_len = 0;
  dbus::MessageReader reader(call);
  // policy_blob points into reader after pop.
  if (!reader.PopString(&account_id) ||
      !reader.PopArrayOfBytes(&policy_blob, &policy_blob_len)) {
    sender.Run(CreateInvalidArgsError(call, call->GetSignature()));
  } else {
    impl_->StoreDeviceLocalAccountPolicy(
        account_id,
        policy_blob,
        policy_blob_len,
        DBusMethodCompletion::CreateCallback(call, sender));
    // Response will be sent asynchronously.
  }
}

scoped_ptr<dbus::Response>
SessionManagerDBusAdaptor::RetrieveDeviceLocalAccountPolicy(
    dbus::MethodCall* call) {
  std::string account_id;
  dbus::MessageReader reader(call);

  if (!reader.PopString(&account_id))
    return CreateInvalidArgsError(call, call->GetSignature());

  std::vector<uint8_t> policy_data;
  SessionManagerImpl::Error error;
  impl_->RetrieveDeviceLocalAccountPolicy(account_id, &policy_data, &error);
  return CraftAppropriateResponseWithBytes(call, error, policy_data);
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::RetrieveSessionState(
    dbus::MethodCall* call) {
  scoped_ptr<dbus::Response> response(dbus::Response::FromMethodCall(call));
  dbus::MessageWriter writer(response.get());
  writer.AppendString(impl_->RetrieveSessionState());
  return response;
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::RetrieveActiveSessions(
    dbus::MethodCall* call) {
  std::map<std::string, std::string> sessions;
  impl_->RetrieveActiveSessions(&sessions);

  scoped_ptr<dbus::Response> response(dbus::Response::FromMethodCall(call));
  dbus::MessageWriter writer(response.get());
  dbus::MessageWriter array_writer(NULL);
  writer.OpenArray("{ss}", &array_writer);
  for (std::map<std::string, std::string>::const_iterator it = sessions.begin();
       it != sessions.end();
       ++it) {
    dbus::MessageWriter entry_writer(NULL);
    array_writer.OpenDictEntry(&entry_writer);
    entry_writer.AppendString(it->first);
    entry_writer.AppendString(it->second);
    array_writer.CloseContainer(&entry_writer);
  }
  writer.CloseContainer(&array_writer);
  return response;
}

scoped_ptr<dbus::Response>
SessionManagerDBusAdaptor::HandleSupervisedUserCreationStarting(
    dbus::MethodCall* call) {
  impl_->HandleSupervisedUserCreationStarting();
  return scoped_ptr<dbus::Response>(dbus::Response::FromMethodCall(call));
}

scoped_ptr<dbus::Response>
SessionManagerDBusAdaptor::HandleSupervisedUserCreationFinished(
    dbus::MethodCall* call) {
  impl_->HandleSupervisedUserCreationFinished();
  return scoped_ptr<dbus::Response>(dbus::Response::FromMethodCall(call));
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::LockScreen(
    dbus::MethodCall* call) {
  SessionManagerImpl::Error error;
  impl_->LockScreen(&error);

  if (error.is_set())
    return CreateError(call, error.name(), error.message());
  return scoped_ptr<dbus::Response>(dbus::Response::FromMethodCall(call));
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::HandleLockScreenShown(
    dbus::MethodCall* call) {
  impl_->HandleLockScreenShown();
  return scoped_ptr<dbus::Response>(dbus::Response::FromMethodCall(call));
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::HandleLockScreenDismissed(
    dbus::MethodCall* call) {
  impl_->HandleLockScreenDismissed();
  return scoped_ptr<dbus::Response>(dbus::Response::FromMethodCall(call));
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::RestartJob(
    dbus::MethodCall* call) {
  // TODO(achuith): Remove debug logging once crbug.com/631640 is resolved.
  LOG(INFO) << "SessionManagerDBusAdaptor::RestartJob";
  dbus::FileDescriptor fd;
  std::vector<std::string> argv;
  dbus::MessageReader reader(call);
  if (!reader.PopFileDescriptor(&fd) || !reader.PopArrayOfStrings(&argv))
    return CreateInvalidArgsError(call, call->GetSignature());

  fd.CheckValidity();
  CHECK(fd.is_valid());

  SessionManagerImpl::Error error;
  if (impl_->RestartJob(fd.value(), argv, &error))
    return dbus::Response::FromMethodCall(call);
  return CreateError(call, error.name(), error.message());
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::StartDeviceWipe(
    dbus::MethodCall* call) {
  SessionManagerImpl::Error error;
  impl_->StartDeviceWipe("session_manager_dbus_request", &error);
  return CraftAppropriateResponseWithBool(call, error, true);
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::SetFlagsForUser(
    dbus::MethodCall* call) {
  dbus::MessageReader reader(call);
  std::string account_id;
  std::vector<std::string> session_user_flags;
  if (!reader.PopString(&account_id) ||
      !reader.PopArrayOfStrings(&session_user_flags)) {
    return CreateInvalidArgsError(call, call->GetSignature());
  }
  impl_->SetFlagsForUser(account_id, session_user_flags);
  return scoped_ptr<dbus::Response>(dbus::Response::FromMethodCall(call));
}

void SessionManagerDBusAdaptor::GetServerBackedStateKeys(
    dbus::MethodCall* call,
    dbus::ExportedObject::ResponseSender sender) {
  std::vector<std::vector<uint8_t>> state_keys;
  impl_->RequestServerBackedStateKeys(
      base::Bind(&HandleGetServerBackedStateKeysCompletion, call, sender));
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::InitMachineInfo(
    dbus::MethodCall* call) {
  dbus::MessageReader reader(call);
  std::string data;
  if (!reader.PopString(&data))
    return CreateInvalidArgsError(call, call->GetSignature());

  SessionManagerImpl::Error error;
  impl_->InitMachineInfo(data, &error);
  if (error.is_set())
    return CreateError(call, error.name(), error.message());
  return scoped_ptr<dbus::Response>(dbus::Response::FromMethodCall(call));
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::StartContainer(
    dbus::MethodCall* call) {
  dbus::MessageReader reader(call);
  std::string name;
  if (!reader.PopString(&name))
    return CreateInvalidArgsError(call, call->GetSignature());

  SessionManagerImpl::Error error;
  impl_->StartContainer(name, &error);
  if (error.is_set())
    return CreateError(call, error.name(), error.message());
  return scoped_ptr<dbus::Response>(dbus::Response::FromMethodCall(call));
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::StopContainer(
    dbus::MethodCall* call) {
  dbus::MessageReader reader(call);
  std::string name;
  if (!reader.PopString(&name))
    return CreateInvalidArgsError(call, call->GetSignature());

  SessionManagerImpl::Error error;
  impl_->StopContainer(name, &error);
  if (error.is_set())
    return CreateError(call, error.name(), error.message());
  return scoped_ptr<dbus::Response>(dbus::Response::FromMethodCall(call));
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::StartArcInstance(
    dbus::MethodCall* call) {
  dbus::MessageReader reader(call);
  std::string account_id;
  if (!reader.PopString(&account_id))
    return CreateInvalidArgsError(call, call->GetSignature());

  SessionManagerImpl::Error error;
  impl_->StartArcInstance(account_id, &error);
  if (error.is_set())
    return CreateError(call, error.name(), error.message());
  return scoped_ptr<dbus::Response>(dbus::Response::FromMethodCall(call));
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::StopArcInstance(
    dbus::MethodCall* call) {
  SessionManagerImpl::Error error;
  impl_->StopArcInstance(&error);
  if (error.is_set())
    return CreateError(call, error.name(), error.message());
  return scoped_ptr<dbus::Response>(dbus::Response::FromMethodCall(call));
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::PrioritizeArcInstance(
    dbus::MethodCall* call) {
  SessionManagerImpl::Error error;
  impl_->PrioritizeArcInstance(&error);
  if (error.is_set())
    return CreateError(call, error.name(), error.message());
  return scoped_ptr<dbus::Response>(dbus::Response::FromMethodCall(call));
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::GetArcStartTimeTicks(
    dbus::MethodCall* call) {
  SessionManagerImpl::Error error;
  base::TimeTicks start_time = impl_->GetArcStartTime(&error);
  if (error.is_set())
    return CreateError(call, error.name(), error.message());

  scoped_ptr<dbus::Response> response(dbus::Response::FromMethodCall(call));
  dbus::MessageWriter writer(response.get());
  writer.AppendInt64(start_time.ToInternalValue());
  return response;
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::RemoveArcData(
    dbus::MethodCall* call) {
  dbus::MessageReader reader(call);
  std::string account_id;
  if (!reader.PopString(&account_id))
    return CreateInvalidArgsError(call, call->GetSignature());

  SessionManagerImpl::Error error;
  impl_->RemoveArcData(account_id, &error);
  if (error.is_set())
    return CreateError(call, error.name(), error.message());

  return scoped_ptr<dbus::Response>(dbus::Response::FromMethodCall(call));
}

scoped_ptr<dbus::Response> SessionManagerDBusAdaptor::Introspect(
    dbus::MethodCall* call) {
  std::string output;
  if (!base::ReadFileToString(base::FilePath(kBindingsPath), &output)) {
    PLOG(ERROR) << "Can't read XML bindings from disk:";
    return CreateError(call, "Can't read XML bindings from disk.", "");
  }
  scoped_ptr<dbus::Response> response(dbus::Response::FromMethodCall(call));
  dbus::MessageWriter writer(response.get());
  writer.AppendString(output);
  return response;
}

void SessionManagerDBusAdaptor::ExportSyncDBusMethod(
    dbus::ExportedObject* object,
    const std::string& method_name,
    SyncDBusMethodCallMemberFunction member) {
  DCHECK(object);
  CHECK(object->ExportMethodAndBlock(
      kSessionManagerInterface,
      method_name,
      base::Bind(&HandleSynchronousDBusMethodCall,
                 base::Bind(member, base::Unretained(this)))));
}

void SessionManagerDBusAdaptor::ExportAsyncDBusMethod(
    dbus::ExportedObject* object,
    const std::string& method_name,
    AsyncDBusMethodCallMemberFunction member) {
  DCHECK(object);
  CHECK(
      object->ExportMethodAndBlock(kSessionManagerInterface,
                                   method_name,
                                   base::Bind(member, base::Unretained(this))));
}

}  // namespace login_manager
