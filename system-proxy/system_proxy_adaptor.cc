// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "system-proxy/system_proxy_adaptor.h"

#include <string>
#include <utility>
#include <vector>

#include <base/location.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/message_loops/message_loop.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/patchpanel/client.h>
#include <dbus/object_proxy.h>

#include "system-proxy/kerberos_client.h"
#include "system_proxy/proto_bindings/system_proxy_service.pb.h"
#include "system-proxy/sandboxed_worker.h"

namespace system_proxy {
namespace {

constexpr int kProxyPort = 3128;
constexpr char kNoCredentialsSpecifiedError[] =
    "No authentication credentials specified";
constexpr char kOnlySystemTrafficSupportedError[] =
    "Only system services traffic is currenly supported";
constexpr char kFailedToStartWorkerError[] = "Failed to start worker process";

// Serializes |proto| to a vector of bytes.
std::vector<uint8_t> SerializeProto(
    const google::protobuf::MessageLite& proto) {
  std::vector<uint8_t> proto_blob(proto.ByteSizeLong());
  DCHECK(proto.SerializeToArray(proto_blob.data(), proto_blob.size()));
  return proto_blob;
}

// Parses a proto from an array of bytes |proto_blob|. Returns
// ERROR_PARSE_REQUEST_FAILED on error.
std::string DeserializeProto(const base::Location& from_here,
                             google::protobuf::MessageLite* proto,
                             const std::vector<uint8_t>& proto_blob) {
  if (!proto->ParseFromArray(proto_blob.data(), proto_blob.size())) {
    const std::string error_message = "Failed to parse proto message.";
    LOG(ERROR) << from_here.ToString() << error_message;
    return error_message;
  }
  return "";
}
}  // namespace

SystemProxyAdaptor::SystemProxyAdaptor(
    std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object)
    : org::chromium::SystemProxyAdaptor(this),
      dbus_object_(std::move(dbus_object)),
      weak_ptr_factory_(this) {
  kerberos_client_ = std::make_unique<KerberosClient>(dbus_object_->GetBus());
}

SystemProxyAdaptor::~SystemProxyAdaptor() = default;

void SystemProxyAdaptor::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction&
        completion_callback) {
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(completion_callback);
}

std::vector<uint8_t> SystemProxyAdaptor::SetAuthenticationDetails(
    const std::vector<uint8_t>& request_blob) {
  LOG(INFO) << "Received set authentication details request.";

  SetAuthenticationDetailsRequest request;
  const std::string error_message =
      DeserializeProto(FROM_HERE, &request, request_blob);

  SetAuthenticationDetailsResponse response;
  if (!error_message.empty()) {
    response.set_error_message(error_message);
    return SerializeProto(response);
  }

  if (!request.has_credentials() && !request.has_kerberos_enabled()) {
    response.set_error_message(kNoCredentialsSpecifiedError);
    return SerializeProto(response);
  }

  if (request.traffic_type() != TrafficOrigin::SYSTEM) {
    response.set_error_message(kOnlySystemTrafficSupportedError);
    return SerializeProto(response);
  }

  if (!CreateWorkerIfNeeded(/* user_traffic */ false)) {
    response.set_error_message(kFailedToStartWorkerError);
    return SerializeProto(response);
  }

  if (request.has_credentials()) {
    if (!request.credentials().has_username() ||
        !request.credentials().has_password()) {
      response.set_error_message(kNoCredentialsSpecifiedError);
      return SerializeProto(response);
    }
    brillo::MessageLoop::current()->PostTask(
        FROM_HERE, base::Bind(&SystemProxyAdaptor::SetCredentialsTask,
                              weak_ptr_factory_.GetWeakPtr(),
                              system_services_worker_.get(),
                              request.credentials().username(),
                              request.credentials().password()));
  }

  if (request.has_kerberos_enabled()) {
    std::string principal_name = request.has_active_principal_name()
                                     ? request.active_principal_name()
                                     : std::string();

    brillo::MessageLoop::current()->PostTask(
        FROM_HERE, base::Bind(&SystemProxyAdaptor::SetKerberosEnabledTask,
                              weak_ptr_factory_.GetWeakPtr(),
                              system_services_worker_.get(),
                              request.kerberos_enabled(), principal_name));
  }

  return SerializeProto(response);
}

std::vector<uint8_t> SystemProxyAdaptor::SetSystemTrafficCredentials(
    const std::vector<uint8_t>& request_blob) {
  LOG(INFO) << "Received set credentials request.";

  SetSystemTrafficCredentialsRequest request;
  const std::string error_message =
      DeserializeProto(FROM_HERE, &request, request_blob);

  SetSystemTrafficCredentialsResponse response;
  if (!error_message.empty()) {
    response.set_error_message(error_message);
    return SerializeProto(response);
  }

  if (!request.has_system_services_username() ||
      !request.has_system_services_password()) {
    response.set_error_message(kNoCredentialsSpecifiedError);
    return SerializeProto(response);
  }

  if (!CreateWorkerIfNeeded(/* user_traffic */ false)) {
    response.set_error_message(kFailedToStartWorkerError);
    return SerializeProto(response);
  }

  brillo::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&SystemProxyAdaptor::SetCredentialsTask,
                 weak_ptr_factory_.GetWeakPtr(), system_services_worker_.get(),
                 request.system_services_username(),
                 request.system_services_password()));

  return SerializeProto(response);
}

std::vector<uint8_t> SystemProxyAdaptor::ShutDown() {
  LOG(INFO) << "Received shutdown request.";

  std::string error_message;
  if (system_services_worker_ && system_services_worker_->IsRunning()) {
    if (!system_services_worker_->Stop())
      error_message =
          "Failure to terminate worker process for system services traffic.";
  }

  if (arc_worker_ && arc_worker_->IsRunning()) {
    if (!arc_worker_->Stop())
      error_message += "Failure to terminate worker process for arc traffic.";
  }

  ShutDownResponse response;
  if (!error_message.empty())
    response.set_error_message(error_message);

  brillo::MessageLoop::current()->PostTask(
      FROM_HERE, base::Bind(&SystemProxyAdaptor::ShutDownTask,
                            weak_ptr_factory_.GetWeakPtr()));

  return SerializeProto(response);
}

void SystemProxyAdaptor::GetChromeProxyServersAsync(
    const std::string& target_url,
    const brillo::http::GetChromeProxyServersCallback& callback) {
  brillo::http::GetChromeProxyServersAsync(dbus_object_->GetBus(), target_url,
                                           move(callback));
}

std::unique_ptr<SandboxedWorker> SystemProxyAdaptor::CreateWorker() {
  return std::make_unique<SandboxedWorker>(weak_ptr_factory_.GetWeakPtr());
}

bool SystemProxyAdaptor::CreateWorkerIfNeeded(bool user_traffic) {
  if (user_traffic) {
    // Not supported at the moment.
    return false;
  }
  if (system_services_worker_) {
    return true;
  }

  system_services_worker_ = CreateWorker();
  if (!StartWorker(system_services_worker_.get(),
                   /* user_traffic= */ false)) {
    system_services_worker_.reset();
    return false;
  }
  // patchpanel_proxy is owned by |dbus_object_->bus_|.
  dbus::ObjectProxy* patchpanel_proxy = dbus_object_->GetBus()->GetObjectProxy(
      patchpanel::kPatchPanelServiceName,
      dbus::ObjectPath(patchpanel::kPatchPanelServicePath));
  patchpanel_proxy->WaitForServiceToBeAvailable(
      base::Bind(&SystemProxyAdaptor::OnPatchpanelServiceAvailable,
                 weak_ptr_factory_.GetWeakPtr()));
  return true;
}

void SystemProxyAdaptor::SetCredentialsTask(SandboxedWorker* worker,
                                            const std::string& username,
                                            const std::string& password) {
  DCHECK(worker);
  worker->SetUsernameAndPassword(username, password);
}

void SystemProxyAdaptor::SetKerberosEnabledTask(
    SandboxedWorker* worker,
    bool kerberos_enabled,
    const std::string& principal_name) {
  DCHECK(worker);

  worker->SetKerberosEnabled(kerberos_enabled,
                             kerberos_client_->krb5_conf_path(),
                             kerberos_client_->krb5_ccache_path());
  kerberos_client_->SetKerberosEnabled(kerberos_enabled);
  if (kerberos_enabled) {
    kerberos_client_->SetPrincipalName(principal_name);
  }
}

void SystemProxyAdaptor::ShutDownTask() {
  brillo::MessageLoop::current()->BreakLoop();
}

bool SystemProxyAdaptor::StartWorker(SandboxedWorker* worker,
                                     bool user_traffic) {
  DCHECK(worker);
  return worker->Start();
}

// Called when the patchpanel D-Bus service becomes available.
void SystemProxyAdaptor::OnPatchpanelServiceAvailable(bool is_available) {
  if (!is_available) {
    LOG(ERROR) << "Patchpanel service not available";
    return;
  }
  if (system_services_worker_) {
    DCHECK(system_services_worker_->IsRunning());
    ConnectNamespace(system_services_worker_.get(), /* user_traffic= */ false);
  }
}

bool SystemProxyAdaptor::ConnectNamespace(SandboxedWorker* worker,
                                          bool user_traffic) {
  std::unique_ptr<patchpanel::Client> patchpanel_client =
      patchpanel::Client::New();
  if (!patchpanel_client) {
    LOG(ERROR) << "Failed to open networking service client";
    return false;
  }

  std::pair<base::ScopedFD, patchpanel::ConnectNamespaceResponse> result =
      patchpanel_client->ConnectNamespace(
          worker->pid(), "" /* outbound_ifname */, user_traffic);

  if (!result.first.is_valid()) {
    LOG(ERROR) << "Failed to setup network namespace";
    return false;
  }

  worker->SetNetNamespaceLifelineFd(std::move(result.first));
  if (!worker->SetListeningAddress(result.second.host_ipv4_address(),
                                   kProxyPort)) {
    return false;
  }
  OnNamespaceConnected(worker, user_traffic);
  return true;
}

void SystemProxyAdaptor::OnNamespaceConnected(SandboxedWorker* worker,
                                              bool user_traffic) {
  WorkerActiveSignalDetails details;
  details.set_traffic_origin(user_traffic ? TrafficOrigin::USER
                                          : TrafficOrigin::SYSTEM);
  details.set_local_proxy_url(worker->local_proxy_host_and_port());
  SendWorkerActiveSignal(SerializeProto(details));
}

}  // namespace system_proxy
