// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBSERVER_LIBWEBSERV_SERVER_H_
#define WEBSERVER_LIBWEBSERV_SERVER_H_

#include <memory>
#include <string>

#include <base/callback.h>
#include <base/macros.h>
#include <libwebserv/export.h>

#include <base/memory/ref_counted.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <dbus/bus.h>

namespace libwebserv {

class ProtocolHandler;

// Top-level wrapper class around HTTP server and provides an interface to
// the web server.
class LIBWEBSERV_EXPORT Server {
 public:
  Server() = default;
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  virtual ~Server() = default;

  // Establish a connection to the system webserver.
  //
  // |service_name| is the well known D-Bus name of the client's process, used
  // to expose a callback D-Bus object the web server calls back with incoming
  // requests.
  // |on_server_online| and |on_server_offline| will notify the caller when the
  // server comes up and down.
  //
  // Note that you can use the returned Server instance as if the webserver
  // process is actually running (ignoring webserver crashes and restarts).
  // All registered request handlers will simply be re-registered when the
  // webserver appears again.
  static std::unique_ptr<Server> ConnectToServerViaDBus(
      const scoped_refptr<dbus::Bus>& bus,
      const std::string& service_name,
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb,
      const base::Closure& on_server_online,
      const base::Closure& on_server_offline);

  // A helper method that returns the default handler for "http".
  virtual ProtocolHandler* GetDefaultHttpHandler() = 0;

  // A helper method that returns the default handler for "https".
  virtual ProtocolHandler* GetDefaultHttpsHandler() = 0;

  // Returns an existing protocol handler by name.  If the handler with the
  // requested |name| does not exist, a new one will be created.
  //
  // The created handler is purely client side, and depends on the server
  // being configured to open a corresponding handler with the given name.
  // Because clients and the server come up asynchronously, we allow clients
  // to register anticipated handlers before server starts up.
  virtual ProtocolHandler* GetProtocolHandler(const std::string& name) = 0;

  // Returns true if |this| is connected to the web server daemon via IPC.
  virtual bool IsConnected() const = 0;

  // Set a user-callback to be invoked when a protocol handler is connect to the
  // server daemon.  Multiple calls to this method will overwrite previously set
  // callbacks.
  virtual void OnProtocolHandlerConnected(
      const base::Callback<void(ProtocolHandler*)>& callback) = 0;

  // Set a user-callback to be invoked when a protocol handler is disconnected
  // from the server daemon (e.g. on shutdown).  Multiple calls to this method
  // will overwrite previously set callbacks.
  virtual void OnProtocolHandlerDisconnected(
      const base::Callback<void(ProtocolHandler*)>& callback) = 0;

  // Returns the default request timeout used to process incoming requests.
  // The reply to an incoming request should be sent within this timeout or
  // else the web server will automatically abort the connection. If the timeout
  // is not set, the returned value will be base::TimeDelta::Max().
  virtual base::TimeDelta GetDefaultRequestTimeout() const = 0;
};

}  // namespace libwebserv

#endif  // WEBSERVER_LIBWEBSERV_SERVER_H_
