// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBSERVER_WEBSERVD_SERVER_INTERFACE_H_
#define WEBSERVER_WEBSERVD_SERVER_INTERFACE_H_

#include <base/macros.h>

#include "webservd/config.h"

namespace webservd {

class ProtocolHandler;
class TempFileManager;

// An abstract interface to expose Server object to IPC transport layer such as
// D-Bus.
class ServerInterface {
 public:
  ServerInterface() = default;
  ServerInterface(const ServerInterface&) = delete;
  ServerInterface& operator=(const ServerInterface&) = delete;

  // Called by ProtocolHandler to notify the server that a new protocol handler
  // appears online or goes offline.
  virtual void ProtocolHandlerStarted(ProtocolHandler* handler) = 0;
  virtual void ProtocolHandlerStopped(ProtocolHandler* handler) = 0;

  // Returns the server configuration data.
  virtual const Config& GetConfig() const = 0;

  // Returns the temp file manager used to track life-times of temporary files.
  // The returned pointer is still owned by the server, so it must not be
  // stored or deleted.
  virtual TempFileManager* GetTempFileManager() = 0;

 protected:
  // This interface should not be used to control the life-time of the class
  // that derives from this interface. This is especially important when a mock
  // server class is used. Since the life-time of the mock must be controlled
  // by the test itself, we can't let some business logic suddenly delete
  // the instance of this interface.
  // So, just declare the destructor as protected, so nobody can just call
  // delete on a pointer to ServerInterface.
  ~ServerInterface() = default;
};

}  // namespace webservd

#endif  // WEBSERVER_WEBSERVD_SERVER_INTERFACE_H_
