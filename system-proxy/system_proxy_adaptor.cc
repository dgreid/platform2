// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "system-proxy/system_proxy_adaptor.h"

#include <string>
#include <utility>
#include <vector>

#include <base/location.h>
#include <brillo/dbus/dbus_object.h>

#include "system_proxy/proto_bindings/system_proxy_service.pb.h"

namespace system_proxy {
namespace {
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
      dbus_object_(std::move(dbus_object)) {}

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
  if (!error_message.empty())
    response.set_error_message(error_message);
  return SerializeProto(response);
}

std::vector<uint8_t> SystemProxyAdaptor::ShutDown() {
  LOG(INFO) << "Received shutdown request.";
  ShutDownResponse response;
  return SerializeProto(response);
}

}  // namespace system_proxy
