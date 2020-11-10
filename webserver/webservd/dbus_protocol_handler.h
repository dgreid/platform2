// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBSERVER_WEBSERVD_DBUS_PROTOCOL_HANDLER_H_
#define WEBSERVER_WEBSERVD_DBUS_PROTOCOL_HANDLER_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <brillo/dbus/dbus_object.h>
#include <dbus/bus.h>

#include "dbus_bindings/org.chromium.WebServer.ProtocolHandler.h"
#include "libwebserv/dbus-proxies.h"

namespace brillo {
namespace dbus_utils {
class ExportedObjectManager;
}  // namespace dbus_utils
}  // namespace brillo

namespace webservd {

class ProtocolHandler;
class Request;
class Server;

// This is a D-Bus interface object for the internal ProtocolHandler class.
class DBusProtocolHandler final
    : public org::chromium::WebServer::ProtocolHandlerInterface {
 public:
  DBusProtocolHandler(brillo::dbus_utils::ExportedObjectManager* object_manager,
                      const dbus::ObjectPath& object_path,
                      ProtocolHandler* protocol_handler,
                      Server* server);
  DBusProtocolHandler(const DBusProtocolHandler&) = delete;
  DBusProtocolHandler& operator=(const DBusProtocolHandler&) = delete;

  ~DBusProtocolHandler();

  void RegisterAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb);

  // Returns the instance of D-Bus exported object manager.
  brillo::dbus_utils::ExportedObjectManager* GetObjectManager() const;

  // Overrides from org::chromium::WebServer::DBusProtocolHandlerInterface.
  bool AddRequestHandler(brillo::ErrorPtr* error,
                         dbus::Message* message,
                         const std::string& in_url,
                         const std::string& in_method,
                         const std::string& in_service_name,
                         std::string* out_request_handler_id) override;

  bool RemoveRequestHandler(brillo::ErrorPtr* error,
                            const std::string& in_request_handler_id) override;

  bool GetRequestFileData(
      brillo::ErrorPtr* error,
      const std::string& in_request_id,
      int32_t in_file_id,
      brillo::dbus_utils::FileDescriptor* out_contents) override;

  bool CompleteRequest(
      brillo::ErrorPtr* error,
      const std::string& in_request_id,
      int32_t in_status_code,
      const std::vector<std::tuple<std::string, std::string>>& in_headers,
      int64_t in_data_size,
      brillo::dbus_utils::FileDescriptor* out_response_stream) override;

 private:
  using RequestHandlerProxy = org::chromium::WebServer::RequestHandlerProxy;

  // Information about a request handler D-Bus back-end client.
  struct DBusServiceData {
    // D-Bus unique address of the process owning this service.
    std::string owner;
    // A D-Bus proxy to the client's request handler object that actually
    // processes requests registered for this client.
    std::unique_ptr<RequestHandlerProxy> handler_proxy;
    // A list of handler IDs registered by this client.
    std::set<std::string> handler_ids;
    // Called when the owner of the well known service name associated with this
    // client changes.  Since clients start up, this is called for the first
    // time when they die.
    dbus::Bus::ServiceOwnerChangeCallback on_client_disconnected_callback;
  };

  // Looks up a request with |request_id|.
  // Returns nullptr and sets additional |error| information, if not found.
  Request* GetRequest(const std::string& request_id, brillo::ErrorPtr* error);

  // Callback invoked when a client owning |service_name| is changed.
  void OnClientDisconnected(const std::string& service_name,
                            const std::string& service_owner);

  // D-Bus object adaptor for ProtocolHandler D-Bus object.
  org::chromium::WebServer::ProtocolHandlerAdaptor dbus_adaptor_{this};
  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;

  // Reference back to the real ProtocolHandler object.
  ProtocolHandler* protocol_handler_{nullptr};
  // Reference back to the Server class.
  Server* server_{nullptr};

  // Called when the owner of a service name changes.  We're only interested in
  // transitions to the empty string, indicating that a service name owner has
  // died.
  dbus::Bus::GetServiceOwnerCallback on_client_disconnected_callback_;

  // A map that holds information regarding a server back-end client processing
  // requests on the D-Bus service with the name used in the key of the map.
  std::map<std::string, DBusServiceData> dbus_service_data_;
  // Handler ID to service name map.
  std::map<std::string, std::string> handler_to_service_name_map_;

  base::WeakPtrFactory<DBusProtocolHandler> weak_ptr_factory_{this};
};

}  // namespace webservd

#endif  // WEBSERVER_WEBSERVD_DBUS_PROTOCOL_HANDLER_H_
