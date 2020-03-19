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

#include "system_proxy/proto_bindings/system_proxy_service.pb.h"
#include "system-proxy/sandboxed_worker.h"

namespace system_proxy {
namespace {

constexpr int kProxyPort = 3128;

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
      weak_ptr_factory_(this) {}

SystemProxyAdaptor::~SystemProxyAdaptor() = default;

void SystemProxyAdaptor::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction&
        completion_callback) {
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(completion_callback);
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
    response.set_error_message("No credentials specified");
    return SerializeProto(response);
  }

  if (!system_services_worker_) {
    system_services_worker_ = CreateWorker();
    StartWorker(system_services_worker_.get());
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
  brillo::http::GetChromeProxyServersAsync(
      dbus_object_->GetBus(), target_url,
      move(callback));
}

std::unique_ptr<SandboxedWorker> SystemProxyAdaptor::CreateWorker() {
  return std::make_unique<SandboxedWorker>(weak_ptr_factory_.GetWeakPtr());
}

void SystemProxyAdaptor::SetCredentialsTask(SandboxedWorker* worker,
                                            const std::string& username,
                                            const std::string& password) {
  DCHECK(worker);
  worker->SetUsernameAndPassword(username, password);
}

void SystemProxyAdaptor::ShutDownTask() {
  brillo::MessageLoop::current()->BreakLoop();
}

void SystemProxyAdaptor::StartWorker(SandboxedWorker* worker) {
  DCHECK(worker);
  worker->Start();
  if (!worker->IsRunning()) {
    LOG(ERROR) << "Failed to start worker process";
    return;
  }
  ConnectNamespace(worker);
}

void SystemProxyAdaptor::ConnectNamespace(SandboxedWorker* worker) {
  // TODO(acostinas,b/147712924) Call the datapath service to setup routing and
  // create a veth pair for the network namespace.
}

void SystemProxyAdaptor::OnConnectNamespace(
    SandboxedWorker* worker, const patchpanel::IPv4Subnet& ipv4_subnet) {
  worker->SetListeningAddress(ipv4_subnet.base_addr(), kProxyPort);
}

}  // namespace system_proxy
