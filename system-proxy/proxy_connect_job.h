// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SYSTEM_PROXY_PROXY_CONNECT_JOB_H_
#define SYSTEM_PROXY_PROXY_CONNECT_JOB_H_

#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <base/callback_forward.h>
#include <base/cancelable_callback.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

namespace patchpanel {
class SocketForwarder;
class Socket;
}  // namespace patchpanel

namespace system_proxy {
// ProxyConnectJob asynchronously sets up a connection to a remote target on
// behalf of a client. Internally, it performs the following steps:
// - waits for the client to send a HTTP connect request;
// - extracts the target url from the connect request;
// - requests proxy resolution for the target url and waits for the result;
// - performs the proxy authentication and connection setup to the remote
// target.
class ProxyConnectJob {
 public:
  using OnConnectionSetupFinishedCallback = base::OnceCallback<void(
      std::unique_ptr<patchpanel::SocketForwarder>, ProxyConnectJob*)>;

  // Will be invoked by ProxyConnectJob to resolve the proxy for |target_url_|.
  // The passed |callback| is expected to be called with the list of proxy
  // servers, which will always contain at least one entry, the default proxy.
  using ResolveProxyCallback = base::OnceCallback<void(
      const std::string& url,
      base::OnceCallback<void(const std::list<std::string>&)> callback)>;

  ProxyConnectJob(std::unique_ptr<patchpanel::Socket> socket,
                  const std::string& credentials,
                  ResolveProxyCallback resolve_proxy_callback,
                  OnConnectionSetupFinishedCallback setup_finished_callback);
  ProxyConnectJob(const ProxyConnectJob&) = delete;
  ProxyConnectJob& operator=(const ProxyConnectJob&) = delete;
  virtual ~ProxyConnectJob();

  // Marks |client_socket_| as non-blocking and adds a watcher that calls
  // |OnClientReadReady| when the socket is read ready.
  virtual bool Start();
  void OnProxyResolution(const std::list<std::string>& proxy_servers);

  friend std::ostream& operator<<(std::ostream& stream,
                                  const ProxyConnectJob& job);

 private:
  friend class ServerProxyTest;
  friend class ProxyConnectJobTest;
  FRIEND_TEST(ServerProxyTest, HandlePendingJobs);
  FRIEND_TEST(ServerProxyTest, HandleConnectRequest);
  FRIEND_TEST(ProxyConnectJobTest, SuccessfulConnection);
  FRIEND_TEST(ProxyConnectJobTest, SuccessfulConnectionAltEnding);
  FRIEND_TEST(ProxyConnectJobTest, BadHttpRequestWrongMethod);
  FRIEND_TEST(ProxyConnectJobTest, BadHttpRequestNoEmptyLine);

  // Reads data from the socket into |raw_request| until the first empty line,
  // which would mark the end of the HTTP request header.
  // This method does not validate the headers.
  bool TryReadHttpHeader(std::vector<char>* raw_request);

  // Called when the client socket is ready for reading.
  void OnClientReadReady();

  // Called from |OnProxyResolution|, after the proxy for |target_url_| is
  // resolved.
  void DoCurlServerConnection(const std::string& proxy_url);

  void OnError(const std::string_view& http_error_message);

  void OnClientConnectTimeout();

  std::string target_url_;
  const std::string credentials_;
  std::list<std::string> proxy_servers_;
  ResolveProxyCallback resolve_proxy_callback_;
  OnConnectionSetupFinishedCallback setup_finished_callback_;
  base::CancelableClosure client_connect_timeout_callback_;

  std::unique_ptr<patchpanel::Socket> client_socket_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> read_watcher_;
};
}  // namespace system_proxy

#endif  // SYSTEM_PROXY_PROXY_CONNECT_JOB_H_
