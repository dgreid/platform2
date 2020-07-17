// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-proxy/server_proxy.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback_helpers.h>
#include <base/posix/eintr_wrapper.h>
#include <base/files/file_util.h>
#include <base/strings/string_util.h>
#include <base/threading/thread.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/data_encoding.h>
#include <brillo/http/http_transport.h>
#include <chromeos/patchpanel/socket.h>
#include <chromeos/patchpanel/socket_forwarder.h>

#include "bindings/worker_common.pb.h"
#include "system-proxy/protobuf_util.h"
#include "system-proxy/proxy_connect_job.h"

namespace system_proxy {

namespace {

constexpr int kMaxConn = 100;
// Name of the environment variable that points to the location of the kerberos
// credentials (ticket) cache.
constexpr char kKrb5CCEnvKey[] = "KRB5CCNAME";
// Name of the environment variable that points to the kerberos configuration
// file which contains information regarding the locations of KDCs and admin
// servers for the Kerberos realms of interest, defaults for the current realm
// and for Kerberos applications, and mappings of hostnames onto Kerberos
// realms.
constexpr char kKrb5ConfEnvKey[] = "KRB5_CONFIG";
constexpr char kCredentialsColonSeparator[] = ":";

// Returns the URL encoded value of |text|.
std::string UrlEncode(const std::string& text) {
  return brillo::data_encoding::UrlEncode(text.c_str(),
                                          /* encodeSpaceAsPlus= */ false);
}

}  // namespace

ServerProxy::ServerProxy(base::OnceClosure quit_closure)
    : system_credentials_(kCredentialsColonSeparator),
      quit_closure_(std::move(quit_closure)),
      weak_ptr_factory_(this) {}
ServerProxy::~ServerProxy() = default;

void ServerProxy::Init() {
  // Start listening for input.
  stdin_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      GetStdinPipe(), base::Bind(&ServerProxy::HandleStdinReadable,
                                 weak_ptr_factory_.GetWeakPtr()));

  // Handle termination signals.
  signal_handler_.Init();
  for (int signal : {SIGINT, SIGTERM, SIGHUP, SIGQUIT}) {
    signal_handler_.RegisterHandler(
        signal, base::BindRepeating(&ServerProxy::HandleSignal,
                                    base::Unretained(this)));
  }
}

void ServerProxy::ResolveProxy(const std::string& target_url,
                               OnProxyResolvedCallback callback) {
  auto it = pending_proxy_resolution_requests_.find(target_url);
  if (it != pending_proxy_resolution_requests_.end()) {
    it->second.push_back(std::move(callback));
    return;
  }
  worker::ProxyResolutionRequest proxy_request;
  proxy_request.set_target_url(target_url);
  worker::WorkerRequest request;
  *request.mutable_proxy_resolution_request() = proxy_request;
  if (!WriteProtobuf(GetStdoutPipe(), request)) {
    LOG(ERROR) << "Failed to send proxy resolution request for url: "
               << target_url;
    std::move(callback).Run({brillo::http::kDirectProxy});
    return;
  }
  pending_proxy_resolution_requests_[target_url].push_back(std::move(callback));
}

void ServerProxy::AuthenticationRequired(const std::string& proxy_url,
                                         const std::string& scheme,
                                         const std::string& realm,
                                         OnAuthAcquiredCallback callback) {
  worker::ProtectionSpace protection_space;
  protection_space.set_origin(proxy_url);
  protection_space.set_realm(realm);
  protection_space.set_scheme(scheme);

  std::string auth_key = protection_space.SerializeAsString();
  // Check the local cache.
  auto it = auth_cache_.find(auth_key);
  if (it != auth_cache_.end()) {
    std::move(callback).Run(it->second);
    return;
  }

  // Request the credentials from the main process.
  worker::AuthRequiredRequest auth_request;
  *auth_request.mutable_protection_space() = protection_space;

  worker::WorkerRequest request;
  *request.mutable_auth_required_request() = auth_request;

  if (!WriteProtobuf(GetStdoutPipe(), request)) {
    LOG(ERROR) << "Failed to send authentication required request";
    std::move(callback).Run(/* credentials= */ std::string());
    return;
  }
  pending_auth_required_requests_[auth_key].push_back(std::move(callback));
}

void ServerProxy::AuthCredentialsProvided(
    const std::string& auth_credentials_key, const std::string& credentials) {
  auto it = pending_auth_required_requests_.find(auth_credentials_key);
  if (it == pending_auth_required_requests_.end()) {
    LOG(WARNING) << "No pending requests found for credentials";
    return;
  }
  for (auto& auth_acquired_callback : it->second) {
    std::move(auth_acquired_callback).Run(credentials);
  }
  pending_auth_required_requests_.erase(auth_credentials_key);
}

void ServerProxy::HandleStdinReadable() {
  worker::WorkerConfigs config;
  if (!ReadProtobuf(GetStdinPipe(), &config)) {
    LOG(ERROR) << "Error decoding protobuf configurations." << std::endl;
    return;
  }

  if (config.has_credentials()) {
    std::string credentials;
    const std::string username = UrlEncode(config.credentials().username());
    const std::string password = UrlEncode(config.credentials().password());
    credentials = base::JoinString({username.c_str(), password.c_str()},
                                   kCredentialsColonSeparator);
    if (config.credentials().has_protection_space()) {
      std::string auth_key =
          config.credentials().protection_space().SerializeAsString();
      if (!username.empty() && !password.empty()) {
        auth_cache_[auth_key] = credentials;
        AuthCredentialsProvided(auth_key, credentials);
      } else {
        AuthCredentialsProvided(auth_key, std::string());
      }
    } else {
      system_credentials_ = credentials;
    }
  }

  if (config.has_listening_address()) {
    if (listening_addr_ != 0) {
      LOG(ERROR)
          << "Failure to set configurations: listening port was already set."
          << std::endl;
      return;
    }
    listening_addr_ = config.listening_address().addr();
    listening_port_ = config.listening_address().port();
    CreateListeningSocket();
  }

  if (config.has_proxy_resolution_reply()) {
    std::list<std::string> proxies;
    const worker::ProxyResolutionReply& reply = config.proxy_resolution_reply();
    for (auto const& proxy : reply.proxy_servers())
      proxies.push_back(proxy);

    OnProxyResolved(reply.target_url(), proxies);
  }

  if (config.has_kerberos_config()) {
    if (config.kerberos_config().enabled()) {
      // Set the environment variables that allow libcurl to use the existing
      // kerberos ticket for proxy authentication. The files to which the env
      // variables point to are maintained by the parent process.
      setenv(kKrb5ConfEnvKey, config.kerberos_config().krb5conf_path().c_str(),
             /* overwrite = */ 1);
      setenv(kKrb5CCEnvKey, config.kerberos_config().krb5cc_path().c_str(),
             /* overwrite = */ 1);
    } else {
      unsetenv(kKrb5ConfEnvKey);
      unsetenv(kKrb5CCEnvKey);
    }
  }

  if (config.has_clear_user_credentials()) {
    auth_cache_.clear();
  }
}

bool ServerProxy::HandleSignal(const struct signalfd_siginfo& siginfo) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                std::move(quit_closure_));
  return true;
}

int ServerProxy::GetStdinPipe() {
  return STDIN_FILENO;
}

int ServerProxy::GetStdoutPipe() {
  return STDOUT_FILENO;
}

void ServerProxy::CreateListeningSocket() {
  listening_fd_ = std::make_unique<patchpanel::Socket>(
      AF_INET, SOCK_STREAM | SOCK_NONBLOCK);

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(listening_port_);
  addr.sin_addr.s_addr = listening_addr_;
  if (!listening_fd_->Bind((const struct sockaddr*)&addr, sizeof(addr))) {
    LOG(ERROR) << "Cannot bind source socket" << std::endl;
    return;
  }

  if (!listening_fd_->Listen(kMaxConn)) {
    LOG(ERROR) << "Cannot listen on source socket." << std::endl;
    return;
  }

  fd_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      listening_fd_->fd(), base::BindRepeating(&ServerProxy::OnConnectionAccept,
                                               weak_ptr_factory_.GetWeakPtr()));
}

void ServerProxy::OnConnectionAccept() {
  struct sockaddr_storage client_src = {};
  socklen_t sockaddr_len = sizeof(client_src);
  if (auto client_conn =
          listening_fd_->Accept((struct sockaddr*)&client_src, &sockaddr_len)) {
    auto connect_job = std::make_unique<ProxyConnectJob>(
        std::move(client_conn), system_credentials_,
        base::BindOnce(&ServerProxy::ResolveProxy, base::Unretained(this)),
        base::BindOnce(&ServerProxy::AuthenticationRequired,
                       base::Unretained(this)),
        base::BindOnce(&ServerProxy::OnConnectionSetupFinished,
                       base::Unretained(this)));
    if (connect_job->Start())
      pending_connect_jobs_[connect_job.get()] = std::move(connect_job);
  }
  // Cleanup any defunct forwarders.
  // TODO(acostinas, chromium:1064536) Monitor the client and server sockets
  // and remove the corresponding SocketForwarder when a socket closes.
  for (auto it = forwarders_.begin(); it != forwarders_.end(); ++it) {
    if (!(*it)->IsRunning() && (*it)->HasBeenStarted())
      it = forwarders_.erase(it);
  }
}

void ServerProxy::OnProxyResolved(const std::string& target_url,
                                  const std::list<std::string>& proxy_servers) {
  auto callbacks = std::move(pending_proxy_resolution_requests_[target_url]);
  pending_proxy_resolution_requests_.erase(target_url);

  for (auto& callback : callbacks)
    std::move(callback).Run(proxy_servers);
}

void ServerProxy::OnConnectionSetupFinished(
    std::unique_ptr<patchpanel::SocketForwarder> fwd,
    ProxyConnectJob* connect_job) {
  if (fwd) {
    // The connection was set up successfully.
    forwarders_.emplace_back(std::move(fwd));
  }
  pending_connect_jobs_.erase(connect_job);
}

}  // namespace system_proxy
