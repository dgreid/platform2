// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPPUSB_MANAGER_SOCKET_MANAGER_H_
#define IPPUSB_MANAGER_SOCKET_MANAGER_H_

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <memory>
#include <string>

#include <base/files/scoped_file.h>
#include <base/macros.h>

namespace ippusb_manager {

class SocketManager {
 public:
  SocketManager(base::ScopedFD, struct sockaddr_un addr);

  virtual ~SocketManager();

  int GetFd() const { return socket_fd_.get(); }

  const struct sockaddr_un* GetAddr() const { return &addr_; }

  void CloseSocket();

  virtual bool OpenConnection() = 0;

  // Messages sent and received are expected to be in the following format:
  //
  //   1 byte    N  // The number of bytes in the message.
  //   N byte(s) M  // A message containing exactly N bytes.
  //
  // Note: The '\0' terminator character is expected to be included in all
  // messages sent and received.
  //
  // Note: Since the length of the message is expected to be stored in 1 byte,
  //       |msg| can only have a maximum size of 255.

  // Gets a message from |fd| and stores the received message in |msg|. Returns
  // true on success.
  bool GetMessage(int fd, std::string* msg);

  // Sends |msg| to the connection on |fd|. Returns true on success.
  bool SendMessage(int fd, const std::string& msg);

 private:
  // File descriptor of the socket.
  base::ScopedFD socket_fd_;
  struct sockaddr_un addr_;
};

class ServerSocketManager : public SocketManager {
 public:
  ServerSocketManager(base::ScopedFD fd, struct sockaddr_un addr);

  // Attempts to accept a client connection on the open socket. Returns true if
  // the connection is opened successfully, false otherwise.
  bool OpenConnection();

  // Closes the client connection.
  void CloseConnection();

  // Convenience functions which defer to the equivalent Get/Send functions in
  // SocketManager.
  bool GetMessage(std::string* msg);
  bool SendMessage(const std::string& msg);

  static std::unique_ptr<ServerSocketManager> Create(const char* socket_path,
                                                     base::ScopedFD fd);

 private:
  // File descriptor for the currently open connection.
  base::ScopedFD connection_fd_;
};

class ClientSocketManager : public SocketManager {
 public:
  ClientSocketManager(base::ScopedFD fd, struct sockaddr_un addr);

  // Connects to the server. Returns true on success.
  bool OpenConnection();

  // Convenience functions which defer to the equivalent Get/Send functions in
  // SocketManager.
  bool GetMessage(std::string* msg);
  bool SendMessage(const std::string& msg);

  static std::unique_ptr<ClientSocketManager> Create(const char* socket_path);
};

}  // namespace ippusb_manager

#endif  // IPPUSB_MANAGER_SOCKET_MANAGER_H_
