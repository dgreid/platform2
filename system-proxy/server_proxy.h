// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SYSTEM_PROXY_SERVER_PROXY_H_
#define SYSTEM_PROXY_SERVER_PROXY_H_

#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/callback_forward.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <base/memory/weak_ptr.h>
#include <brillo/asynchronous_signal_handler.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

namespace patchpanel {
class Socket;
class SocketForwarder;
}  // namespace patchpanel

namespace system_proxy {

using OnProxyResolvedCallback =
    base::OnceCallback<void(const std::list<std::string>&)>;
using OnAuthAcquiredCallback = base::OnceCallback<void(const std::string&)>;

class ProxyConnectJob;

// ServerProxy listens for connections from the host (system services, ARC++
// apps) and sets-up connections to the remote server.
// Note: System-proxy only supports proxying over IPv4 networks.
class ServerProxy {
 public:
  explicit ServerProxy(base::OnceClosure quit_closure);
  ServerProxy(const ServerProxy&) = delete;
  ServerProxy& operator=(const ServerProxy&) = delete;
  virtual ~ServerProxy();

  void Init();

  // Creates a proxy resolution request that is forwarded to the parent process
  // trough the standard output. When the request is resolved, the parent
  // process will send the result trough the standard input.
  // |callback| will be called when the proxy is resolved, with the list of
  // proxy servers as parameter ,or in case of failure, with a list containing
  // only the direct proxy.
  void ResolveProxy(const std::string& target_url,
                    OnProxyResolvedCallback callback);
  // Creates an authentication required request that is forwarded to the parent
  // process trough the standard output. When the request is resolved, the
  // parent process will send the result trough the standard input. |callback|
  // will be called when the credentials associated to the protection space
  // given by the input parameters, or empty strings in case of failure or
  // missing credentials.
  void AuthenticationRequired(const std::string& proxy_url,
                              const std::string& scheme,
                              const std::string& realm,
                              OnAuthAcquiredCallback callback);

 protected:
  virtual int GetStdinPipe();
  virtual int GetStdoutPipe();
  virtual void HandleStdinReadable();

 private:
  friend class ServerProxyTest;
  FRIEND_TEST(ServerProxyTest, FetchCredentials);
  FRIEND_TEST(ServerProxyTest, FetchListeningAddress);
  FRIEND_TEST(ServerProxyTest, HandleConnectRequest);
  FRIEND_TEST(ServerProxyTest, HandlePendingJobs);
  FRIEND_TEST(ServerProxyTest, SetupConnection);
  FRIEND_TEST(ServerProxyTest, HandleCanceledJobWhilePendingProxyResolution);

  bool HandleSignal(const struct signalfd_siginfo& siginfo);

  void CreateListeningSocket();
  void OnConnectionAccept();

  // Called by |ProxyConnectJob| after setting up the connection with the remote
  // server via the remote proxy server. If the connection is successful, |fwd|
  // corresponds to the tunnel between the client and the server that has
  // started to forward data. In case of failure, |fwd| is empty.
  void OnConnectionSetupFinished(
      std::unique_ptr<patchpanel::SocketForwarder> fwd,
      ProxyConnectJob* connect_job);

  // Called when the proxy resolution result for |target_url| is received via
  // the standard input (see |ResolveProxy| method). |proxy_servers| will always
  // contain at least one entry, the direct proxy.
  void OnProxyResolved(const std::string& target_url,
                       const std::list<std::string>& proxy_servers);

  void OnCredentialsReceived();

  // Sets the environment variables for kerberos authentication.
  void SetKerberosEnv(bool kerberos_enabled);

  // The proxy listening address in network-byte order.
  uint32_t listening_addr_ = 0;
  int listening_port_;

  // The user name and password to use for proxy authentication in the format
  // compatible with libcurl's CURLOPT_USERPWD: both user name and password URL
  // encoded and separated by colon.
  std::string system_credentials_;

  std::unique_ptr<patchpanel::Socket> listening_fd_;

  // List of SocketForwarders that corresponds to the TCP tunnel between the
  // local client and the remote  proxy, forwarding data between the TCP
  // connection initiated by the local client to the local proxy and the TCP
  // connection initiated by the local proxy to the remote proxy.
  std::list<std::unique_ptr<patchpanel::SocketForwarder>> forwarders_;

  std::map<ProxyConnectJob*, std::unique_ptr<ProxyConnectJob>>
      pending_connect_jobs_;

  // Collection of ongoing proxy resolution requests. The key represents the
  // target url to be resolved and it's mapped to a list of callbaks to pending
  // connect jobs that are connecting to the same target url.
  std::map<std::string, std::list<OnProxyResolvedCallback>>
      pending_proxy_resolution_requests_;

  base::OnceClosure quit_closure_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> stdin_watcher_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> fd_watcher_;
  brillo::AsynchronousSignalHandler signal_handler_;

  base::WeakPtrFactory<ServerProxy> weak_ptr_factory_;
};
}  // namespace system_proxy

#endif  // SYSTEM_PROXY_SERVER_PROXY_H_
