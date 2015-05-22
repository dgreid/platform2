// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libwebserv/server.h>

#include <tuple>
#include <vector>

#include <libwebserv/protocol_handler.h>
#include <libwebserv/request.h>

#include "libwebserv/org.chromium.WebServer.RequestHandler.h"
#include "webservd/dbus-proxies.h"

namespace libwebserv {

class Server::RequestHandler final
    : public org::chromium::WebServer::RequestHandlerInterface {
 public:
  explicit RequestHandler(Server* server) : server_{server} {}
  bool ProcessRequest(
      chromeos::ErrorPtr* error,
      const std::tuple<std::string, std::string, std::string, std::string,
                       std::string>& in_request_info,
      const std::vector<std::tuple<std::string, std::string>>& in_headers,
      const std::vector<std::tuple<bool, std::string, std::string>>& in_params,
      const std::vector<std::tuple<int32_t, std::string, std::string,
                                   std::string, std::string>>& in_files,
      const std::vector<uint8_t>& in_body) override;

 private:
  Server* server_{nullptr};
  DISALLOW_COPY_AND_ASSIGN(RequestHandler);
};

bool Server::RequestHandler::ProcessRequest(
    chromeos::ErrorPtr* error,
    const std::tuple<std::string, std::string, std::string, std::string,
                     std::string>& in_request_info,
    const std::vector<std::tuple<std::string, std::string>>& in_headers,
    const std::vector<std::tuple<bool, std::string, std::string>>& in_params,
    const std::vector<std::tuple<int32_t, std::string, std::string,
                                 std::string, std::string>>& in_files,
    const std::vector<uint8_t>& in_body) {
  std::string protocol_handler_id = std::get<0>(in_request_info);
  std::string request_handler_id = std::get<1>(in_request_info);
  std::string request_id = std::get<2>(in_request_info);
  std::string url = std::get<3>(in_request_info);
  std::string method = std::get<4>(in_request_info);
  ProtocolHandler* protocol_handler =
      server_->GetProtocolHandlerByID(protocol_handler_id);
  if (!protocol_handler) {
    chromeos::Error::AddToPrintf(error, FROM_HERE,
                                 chromeos::errors::dbus::kDomain,
                                 DBUS_ERROR_FAILED,
                                 "Unknown protocol handler '%s'",
                                 protocol_handler_id.c_str());
    return false;
  }
  std::unique_ptr<Request> request{new Request{protocol_handler, url, method}};
  // Convert request data into format required by the Request object.
  for (const auto& tuple : in_params) {
    if (std::get<0>(tuple))
      request->post_data_.emplace(std::get<1>(tuple), std::get<2>(tuple));
    else
      request->get_data_.emplace(std::get<1>(tuple), std::get<2>(tuple));
  }

  for (const auto& tuple : in_headers)
    request->headers_.emplace(std::get<0>(tuple), std::get<1>(tuple));

  for (const auto& tuple : in_files) {
    request->file_info_.emplace(
        std::get<1>(tuple),  // field_name
        std::unique_ptr<FileInfo>{new FileInfo{
            protocol_handler,
            std::get<0>(tuple),     // file_id
            request_id,
            std::get<2>(tuple),     // file_name
            std::get<3>(tuple),     // content_type
            std::get<4>(tuple)}});  // transfer_encoding
  }

  request->raw_data_ = in_body;

  return protocol_handler->ProcessRequest(protocol_handler_id,
                                          request_handler_id,
                                          request_id,
                                          std::move(request),
                                          error);
}

Server::Server()
    : request_handler_{new RequestHandler{this}},
      dbus_adaptor_{new org::chromium::WebServer::RequestHandlerAdaptor{
          request_handler_.get()}} {}

Server::~Server() {
}

void Server::Connect(
    const scoped_refptr<dbus::Bus>& bus,
    const std::string& service_name,
    const chromeos::dbus_utils::AsyncEventSequencer::CompletionAction& cb,
    const base::Closure& on_server_online,
    const base::Closure& on_server_offline) {
  service_name_ = service_name;
  dbus_object_.reset(new chromeos::dbus_utils::DBusObject{
      nullptr, bus, dbus_adaptor_->GetObjectPath()});
  dbus_adaptor_->RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(cb);
  on_server_online_ = on_server_online;
  on_server_offline_ = on_server_offline;
  object_manager_.reset(new org::chromium::WebServer::ObjectManagerProxy{bus});
  object_manager_->SetServerAddedCallback(
      base::Bind(&Server::Online, base::Unretained(this)));
  object_manager_->SetServerRemovedCallback(
      base::Bind(&Server::Offline, base::Unretained(this)));
  object_manager_->SetProtocolHandlerAddedCallback(
      base::Bind(&Server::ProtocolHandlerAdded, base::Unretained(this)));
  object_manager_->SetProtocolHandlerRemovedCallback(
      base::Bind(&Server::ProtocolHandlerRemoved, base::Unretained(this)));
}

void Server::Disconnect() {
  on_server_offline_.Reset();
  on_server_online_.Reset();
  protocol_handlers_ids_.clear();
  protocol_handlers_names_.clear();
  // Release D-Bus object manager proxy after all the dependent maps are freed
  // (e.g. |protocol_handlers_names_| contains pointers to ProtocolHandlerProxy,
  // instances of which are owned by the D-Bus object manager).
  object_manager_.reset();
  dbus_object_.reset();
}

void Server::Online(org::chromium::WebServer::ServerProxy* server) {
  VLOG(1) << "Web server is on-line.";
  proxy_ = server;
  if (!on_server_online_.is_null())
    on_server_online_.Run();
}

void Server::Offline(const dbus::ObjectPath& object_path) {
  if (!on_server_offline_.is_null())
    on_server_offline_.Run();
  proxy_ = nullptr;
  VLOG(1) << "Web server is off-line.";
}

void Server::ProtocolHandlerAdded(
    org::chromium::WebServer::ProtocolHandlerProxy* handler) {
  VLOG(1) << "Server-side protocol handler with ID '" << handler->id()
          << "' is on-line (" << handler->name() << ")";

  protocol_handler_id_map_.emplace(handler->GetObjectPath(), handler->id());
  ProtocolHandler* registered_handler = GetProtocolHandler(handler->name());
  if (registered_handler) {
    protocol_handlers_ids_.emplace(handler->id(), registered_handler);
    registered_handler->Connect(handler);
    if (!on_protocol_handler_connected_.is_null())
      on_protocol_handler_connected_.Run(registered_handler);
  }
}

void Server::ProtocolHandlerRemoved(const dbus::ObjectPath& object_path) {
  auto p = protocol_handler_id_map_.find(object_path);
  if (p == protocol_handler_id_map_.end())
    return;

  VLOG(1) << "Server-side protocol handler with ID '" << p->second
          << "' is off-line.";

  ProtocolHandler* registered_handler = GetProtocolHandlerByID(p->second);
  if (registered_handler) {
    if (!on_protocol_handler_disconnected_.is_null())
      on_protocol_handler_disconnected_.Run(registered_handler);
    registered_handler->Disconnect(object_path);
    protocol_handlers_ids_.erase(p->second);
  }

  protocol_handler_id_map_.erase(p);
}

ProtocolHandler* Server::GetProtocolHandler(const std::string& name) {
  auto p = protocol_handlers_names_.find(name);
  if (p == protocol_handlers_names_.end()) {
    VLOG(1) << "Creating a client-side instance of web server's protocol "
            << "handler with name '" << name << "'";
    p = protocol_handlers_names_.emplace(
        name,
        std::unique_ptr<ProtocolHandler>{new ProtocolHandler{name, this}})
            .first;
  }
  return p->second.get();
}

ProtocolHandler* Server::GetDefaultHttpHandler() {
  return GetProtocolHandler(ProtocolHandler::kHttp);
}

ProtocolHandler* Server::GetDefaultHttpsHandler() {
  return GetProtocolHandler(ProtocolHandler::kHttps);
}

ProtocolHandler* Server::GetProtocolHandlerByID(const std::string& id) const {
  auto p = protocol_handlers_ids_.find(id);
  if (p == protocol_handlers_ids_.end()) {
    LOG(ERROR) << "Unable to locate protocol handler with ID '" << id << "'";
    return nullptr;
  }
  return p->second;
}

void Server::OnProtocolHandlerConnected(
    const base::Callback<void(ProtocolHandler*)>& callback) {
  on_protocol_handler_connected_ = callback;
}

void Server::OnProtocolHandlerDisconnected(
    const base::Callback<void(ProtocolHandler*)>& callback) {
  on_protocol_handler_disconnected_ = callback;
}

}  // namespace libwebserv
