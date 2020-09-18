// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-proxy/proxy_connect_job.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <curl/easy.h>

#include <base/base64.h>
#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback_helpers.h>
#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <base/threading/thread.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/http/http_transport.h>
#include <chromeos/patchpanel/net_util.h>
#include <chromeos/patchpanel/socket.h>
#include <chromeos/patchpanel/socket_forwarder.h>

#include "system-proxy/curl_socket.h"
#include "system-proxy/http_util.h"

// The libpatchpanel-util library overloads << for socket data structures.
// By C++'s argument-dependent lookup rules, operators defined in a
// different namespace are not visible. We need the using directive to make
// the overload available this namespace.
using patchpanel::operator<<;

namespace {
// There's no RFC recomandation for the max size of http request headers but
// popular http server implementations (Apache, IIS, Tomcat) set the lower limit
// to 8000.
constexpr int kMaxHttpRequestHeadersSize = 8000;
constexpr base::TimeDelta kCurlConnectTimeout =
    base::TimeDelta::FromSeconds(30);
constexpr base::TimeDelta kWaitClientConnectTimeout =
    base::TimeDelta::FromSeconds(15);
// Time to wait for proxy authentication credentials to be fetched from the
// browser. The credentials are retrieved either from the Network Service or, if
// the Network Service doesn't have them, directly from the user via a login
// dialogue.
constexpr base::TimeDelta kCredentialsRequestTimeout =
    base::TimeDelta::FromMinutes(1);
constexpr size_t kMaxBadRequestPrintSize = 120;

constexpr int64_t kHttpCodeProxyAuthRequired = 407;

// HTTP error codes and messages with origin information for debugging (RFC723,
// section 6.1).
const std::string_view kHttpBadRequest =
    "HTTP/1.1 400 Bad Request - Origin: local proxy\r\n\r\n";
const std::string_view kHttpConnectionTimeout =
    "HTTP/1.1 408 Request Timeout - Origin: local proxy\r\n\r\n";
const std::string_view kHttpInternalServerError =
    "HTTP/1.1 500 Internal Server Error - Origin: local proxy\r\n\r\n";
const std::string_view kHttpBadGateway =
    "HTTP/1.1 502 Bad Gateway - Origin: local proxy\r\n\r\n";
const std::string_view kHttpProxyAuthRequired =
    "HTTP/1.1 407 Credentials required - Origin: local proxy\r\n\r\n";
constexpr char kHttpErrorTunnelFailed[] =
    "HTTP/1.1 %s Error creating tunnel - Origin: local proxy\r\n\r\n";
}  // namespace

namespace system_proxy {
// CURLOPT_HEADERFUNCTION callback implementation that only returns the headers
// from the last response sent by the sever. This is to make sure that we
// send back valid HTTP replies and auhentication data from the HTTP messages is
// not being leaked to the client. |userdata| is set on the libcurl CURL handle
// used to configure the request, using the the CURLOPT_HEADERDATA option. Note,
// from the libcurl documentation: This callback is being called for all the
// responses received from the proxy server after intiating the connection
// request. Multiple responses can be received in an authentication sequence.
// Only the last response's headers should be forwarded to the System-proxy
// client. The header callback will be called once for each header and only
// complete header lines are passed on to the callback.
static size_t WriteHeadersCallback(char* contents,
                                   size_t size,
                                   size_t nmemb,
                                   void* userdata) {
  std::vector<char>* vec = (std::vector<char>*)userdata;

  // Check if we are receiving a new HTTP message (after the last one was
  // terminated with an empty line).
  if (IsEndingWithHttpEmptyLine(base::StringPiece(vec->data(), vec->size()))) {
    VLOG(1) << "Removing the http reply headers from the server "
            << base::StringPiece(vec->data(), vec->size());
    vec->clear();
  }
  vec->insert(vec->end(), contents, contents + (nmemb * size));
  return size * nmemb;
}

// CONNECT requests may have a reply body. This method will capture the reply
// and save it in |userdata|. |userdata| is set on the libcurl CURL handle
// used to configure the request, using the the CURLOPT_WRITEDATA option.
static size_t WriteCallback(char* contents,
                            size_t size,
                            size_t nmemb,
                            void* userdata) {
  std::vector<char>* vec = (std::vector<char>*)userdata;
  vec->insert(vec->end(), contents, contents + (nmemb * size));
  return size * nmemb;
}

ProxyConnectJob::ProxyConnectJob(
    std::unique_ptr<patchpanel::Socket> socket,
    const std::string& credentials,
    ResolveProxyCallback resolve_proxy_callback,
    AuthenticationRequiredCallback auth_required_callback,
    OnConnectionSetupFinishedCallback setup_finished_callback)
    : credentials_(credentials),
      resolve_proxy_callback_(std::move(resolve_proxy_callback)),
      auth_required_callback_(std::move(auth_required_callback)),
      setup_finished_callback_(std::move(setup_finished_callback)),
      // Safe to use |base::Unretained| because the callback will be canceled
      // when it goes out of scope.
      client_connect_timeout_callback_(base::Bind(
          &ProxyConnectJob::OnClientConnectTimeout, base::Unretained(this))),
      credentials_request_timeout_callback_(base::Bind(
          &ProxyConnectJob::OnAuthenticationTimeout, base::Unretained(this))) {
  client_socket_ = std::move(socket);
}

ProxyConnectJob::~ProxyConnectJob() = default;

bool ProxyConnectJob::Start() {
  // Make the socket non-blocking.
  if (!base::SetNonBlocking(client_socket_->fd())) {
    PLOG(ERROR) << *this << " Failed to mark the socket as non-blocking.";
    client_socket_->SendTo(kHttpInternalServerError.data(),
                           kHttpInternalServerError.size());
    return false;
  }
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, client_connect_timeout_callback_.callback(),
      kWaitClientConnectTimeout);
  read_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      client_socket_->fd(), base::Bind(&ProxyConnectJob::OnClientReadReady,
                                       weak_ptr_factory_.GetWeakPtr()));
  return true;
}

void ProxyConnectJob::OnClientReadReady() {
  if (!read_watcher_) {
    // The connection has timed out while waiting for the client's HTTP CONNECT
    // request. See |OnClientConnectTimeout|.
    return;
  }
  client_connect_timeout_callback_.Cancel();
  // Stop watching.
  read_watcher_.reset();
  // The first message should be a HTTP CONNECT request.
  std::vector<char> connect_request;
  if (!TryReadHttpHeader(&connect_request)) {
    std::string encoded;
    base::Base64Encode(
        base::StringPiece(connect_request.data(), connect_request.size()),
        &encoded);
    LOG(ERROR) << *this
               << " Failure to read proxy CONNECT request. Base 64 encoded "
                  "request message from client: "
               << encoded;
    OnError(kHttpBadRequest);
    return;
  }
  base::StringPiece request(connect_request.data(), connect_request.size());
  target_url_ = GetUriAuthorityFromHttpHeader(request);
  if (target_url_.empty()) {
    LOG(ERROR)
        << *this
        << " Failed to extract target url from the HTTP CONNECT request.";
    OnError(kHttpBadRequest);
    return;
  }

  // The proxy resolution service in Chrome expects a proper URL, formatted as
  // scheme://host:port. It's safe to assume only https will be used for the
  // target url.
  std::move(resolve_proxy_callback_)
      .Run(base::StringPrintf("https://%s", target_url_.c_str()),
           base::Bind(&ProxyConnectJob::OnProxyResolution,
                      weak_ptr_factory_.GetWeakPtr()));
}

bool ProxyConnectJob::TryReadHttpHeader(std::vector<char>* raw_request) {
  size_t read_byte_count = 0;
  raw_request->resize(kMaxHttpRequestHeadersSize);

  // Read byte-by-byte and stop when reading an empty line (only CRLF) or when
  // exceeding the max buffer size.
  // TODO(acostinas, chromium:1064536) This may have some measurable performance
  // impact. We should read larger blocks of data, consume the HTTP headers,
  // cache the tunneled payload that may have already been included (e.g. TLS
  // ClientHello) and send it to server after the connection is established.
  while (read_byte_count < kMaxHttpRequestHeadersSize) {
    if (client_socket_->RecvFrom(raw_request->data() + read_byte_count, 1) <=
        0) {
      raw_request->resize(std::min(read_byte_count, kMaxBadRequestPrintSize));
      return false;
    }
    ++read_byte_count;

    if (IsEndingWithHttpEmptyLine(
            base::StringPiece(raw_request->data(), read_byte_count))) {
      raw_request->resize(read_byte_count);
      return true;
    }
  }
  return false;
}

void ProxyConnectJob::OnProxyResolution(
    const std::list<std::string>& proxy_servers) {
  proxy_servers_ = proxy_servers;
  DoCurlServerConnection();
}

void ProxyConnectJob::AuthenticationRequired(
    const std::vector<char>& http_response_headers) {
  DCHECK(!proxy_servers_.empty());
  SchemeRealmPairList scheme_realm_pairs = ParseAuthChallenge(base::StringPiece(
      http_response_headers.data(), http_response_headers.size()));
  if (scheme_realm_pairs.empty()) {
    LOG(ERROR) << "Failed to parse authentication challenge";
    OnError(kHttpBadGateway);
    return;
  }

  if (!authentication_timer_started_) {
    authentication_timer_started_ = true;
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE, credentials_request_timeout_callback_.callback(),
        kCredentialsRequestTimeout);
  }

  auth_required_callback_.Run(
      proxy_servers_.front(), scheme_realm_pairs.front().first,
      scheme_realm_pairs.front().second, credentials_,
      base::BindRepeating(&ProxyConnectJob::OnAuthCredentialsProvided,
                          weak_ptr_factory_.GetWeakPtr()));
}

void ProxyConnectJob::OnAuthCredentialsProvided(
    const std::string& credentials) {
  // If no credentials were returned or if the same bad credentials were
  // returned twice, quit the connection. This is to ensure that bad credentials
  // acquired from the Network Service won't trigger an authentication loop.
  if (credentials.empty() || credentials_ == credentials) {
    SendHttpResponseToClient(/* http_response_headers= */ {},
                             /* http_response_body= */ {});
    std::move(setup_finished_callback_).Run(nullptr, this);
    return;
  }
  credentials_ = credentials;
  VLOG(1) << "Connecting to the remote server with provided credentials";
  DoCurlServerConnection();
}

bool ProxyConnectJob::AreAuthCredentialsRequired(CURL* easyhandle) {
  if (http_response_code_ != kHttpCodeProxyAuthRequired) {
    return false;
  }

  CURLcode res;
  int64_t server_proxy_auth_scheme = 0;
  res = curl_easy_getinfo(easyhandle, CURLINFO_PROXYAUTH_AVAIL,
                          &server_proxy_auth_scheme);
  if (res != CURLE_OK || !server_proxy_auth_scheme) {
    return false;
  }

  // If kerberos is enabled, then we need to wait for the user to request a
  // kerberos ticket from Chrome.
  return !(server_proxy_auth_scheme & CURLAUTH_NEGOTIATE);
}

void ProxyConnectJob::DoCurlServerConnection() {
  DCHECK(!proxy_servers_.empty());
  CURL* easyhandle = curl_easy_init();
  CURLcode res;
  curl_socket_t newSocket = -1;

  if (!easyhandle) {
    // Unfortunately it's not possible to get the failure reason.
    LOG(ERROR) << *this << " Failure to create curl handle.";
    curl_easy_cleanup(easyhandle);
    OnError(kHttpInternalServerError);
    return;
  }
  curl_easy_setopt(easyhandle, CURLOPT_URL, target_url_.c_str());
  std::vector<char> http_response_headers;
  std::vector<char> http_response_body;

  if (proxy_servers_.front().c_str() != brillo::http::kDirectProxy) {
    curl_easy_setopt(easyhandle, CURLOPT_PROXY, proxy_servers_.front().c_str());
    curl_easy_setopt(easyhandle, CURLOPT_HTTPPROXYTUNNEL, 1L);
    curl_easy_setopt(easyhandle, CURLOPT_CONNECT_ONLY, 1);
    // Allow libcurl to pick authentication method. Curl will use the most
    // secure one the remote site claims to support.
    curl_easy_setopt(easyhandle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    curl_easy_setopt(easyhandle, CURLOPT_PROXYUSERPWD, credentials_.c_str());
  }
  curl_easy_setopt(easyhandle, CURLOPT_CONNECTTIMEOUT_MS,
                   kCurlConnectTimeout.InMilliseconds());
  curl_easy_setopt(easyhandle, CURLOPT_HEADERFUNCTION, WriteHeadersCallback);
  curl_easy_setopt(easyhandle, CURLOPT_HEADERDATA, &http_response_headers);
  curl_easy_setopt(easyhandle, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(easyhandle, CURLOPT_WRITEDATA, &http_response_body);

  res = curl_easy_perform(easyhandle);
  curl_easy_getinfo(easyhandle, CURLINFO_HTTP_CONNECTCODE,
                    &http_response_code_);

  if (res != CURLE_OK) {
    LOG(ERROR) << *this << " curl_easy_perform() failed with error: "
               << curl_easy_strerror(res);
    if (AreAuthCredentialsRequired(easyhandle)) {
      AuthenticationRequired(http_response_headers);
      curl_easy_cleanup(easyhandle);
      return;
    }
    credentials_request_timeout_callback_.Cancel();

    curl_easy_cleanup(easyhandle);

    SendHttpResponseToClient(/* http_response_headers= */ {},
                             /* http_response_body= */ {});
    std::move(setup_finished_callback_).Run(nullptr, this);
    return;
  }
  credentials_request_timeout_callback_.Cancel();
  // Extract the socket from the curl handle.
  res = curl_easy_getinfo(easyhandle, CURLINFO_ACTIVESOCKET, &newSocket);
  if (res != CURLE_OK) {
    LOG(ERROR) << *this << " Failed to get socket from curl with error: "
               << curl_easy_strerror(res);
    curl_easy_cleanup(easyhandle);
    OnError(kHttpBadGateway);
    return;
  }

  ScopedCurlEasyhandle scoped_handle(easyhandle, FreeCurlEasyhandle());
  auto server_conn = std::make_unique<CurlSocket>(base::ScopedFD(newSocket),
                                                  std::move(scoped_handle));

  // Send the server reply to the client. If the connection is successful, the
  // reply headers should be "HTTP/1.1 200 Connection Established".
  if (!SendHttpResponseToClient(http_response_headers, http_response_body)) {
    std::move(setup_finished_callback_).Run(nullptr, this);
    return;
  }

  auto fwd =
      CreateSocketForwarder(std::move(client_socket_), std::move(server_conn));
  // Start forwarding data between sockets.
  fwd->Start();
  std::move(setup_finished_callback_).Run(std::move(fwd), this);
}

std::unique_ptr<patchpanel::SocketForwarder>
ProxyConnectJob::CreateSocketForwarder(
    std::unique_ptr<patchpanel::Socket> peer0,
    std::unique_ptr<patchpanel::Socket> peer1) {
  return std::make_unique<patchpanel::SocketForwarder>(
      base::StringPrintf("%d-%d", peer0->fd(), peer1->fd()), std::move(peer0),
      std::move(peer1));
}

bool ProxyConnectJob::SendHttpResponseToClient(
    const std::vector<char>& http_response_headers,
    const std::vector<char>& http_response_body) {
  if (http_response_code_ == 0) {
    // No HTTP CONNECT response code is available.
    return client_socket_->SendTo(kHttpInternalServerError.data(),
                                  kHttpInternalServerError.size());
  }

  if (http_response_code_ == kHttpCodeProxyAuthRequired) {
    // This will be a hint for the user to authenticate via the Browser or
    // acquire a Kerberos ticket.
    return client_socket_->SendTo(kHttpProxyAuthRequired.data(),
                                  kHttpProxyAuthRequired.size());
  }

  if (http_response_code_ >= 400) {
    VLOG(1) << "Failed to set up HTTP tunnel with code " << http_response_code_;
    std::string http_error = base::StringPrintf(
        kHttpErrorTunnelFailed, std::to_string(http_response_code_).c_str());
    return client_socket_->SendTo(http_error.c_str(), http_error.size());
  }

  if (http_response_headers.empty()) {
    return client_socket_->SendTo(kHttpInternalServerError.data(),
                                  kHttpInternalServerError.size());
  }

  VLOG(1) << "Sending server reply to client";
  if (!client_socket_->SendTo(http_response_headers.data(),
                              http_response_headers.size())) {
    PLOG(ERROR) << "Failed to send HTTP server response headers to client";
    return false;
  }
  if (!http_response_body.empty()) {
    if (!client_socket_->SendTo(http_response_body.data(),
                                http_response_body.size())) {
      PLOG(ERROR) << "Failed to send HTTP server response payload to client";
      return false;
    }
  }
  return true;
}

void ProxyConnectJob::OnError(const std::string_view& http_error_message) {
  client_socket_->SendTo(http_error_message.data(), http_error_message.size());
  std::move(setup_finished_callback_).Run(nullptr, this);
}

void ProxyConnectJob::OnClientConnectTimeout() {
  // Stop listening for client connect requests.
  read_watcher_.reset();
  LOG(ERROR) << *this
             << " Connection timed out while waiting for the client to send a "
                "connect request.";
  OnError(kHttpConnectionTimeout);
}

void ProxyConnectJob::OnAuthenticationTimeout() {
  LOG(ERROR)
      << *this
      << "The connect job timed out while waiting for proxy authentication "
         "credentials";
  OnError(kHttpProxyAuthRequired);
}

std::ostream& operator<<(std::ostream& stream, const ProxyConnectJob& job) {
  stream << "{fd: " << job.client_socket_->fd();
  if (!job.target_url_.empty()) {
    stream << ", url: " << job.target_url_;
  }
  stream << "}";
  return stream;
}

}  // namespace system_proxy
