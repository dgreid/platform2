// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/http_request.h"

#include <curl/curl.h>
#include <utility>

#include <base/bind.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <brillo/http/http_utils.h>

#include "shill/dns_client.h"
#include "shill/error.h"
#include "shill/event_dispatcher.h"
#include "shill/http_url.h"
#include "shill/logging.h"
#include "shill/net/sockets.h"

using base::Bind;
using base::Callback;
using base::StringToInt;
using std::string;

namespace {

// The curl error domain for http requests
const char kCurlEasyError[] = "curl_easy_error";

}  // namespace

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kHTTP;
static string ObjectID(const HttpRequest* r) {
  return r->interface_name();
}
}  // namespace Logging

const int HttpRequest::kRequestTimeoutSeconds = 10;

HttpRequest::HttpRequest(EventDispatcher* dispatcher,
                         const std::string& interface_name,
                         IPAddress::Family ip_family,
                         const std::vector<std::string>& dns_list,
                         bool allow_non_google_https)
    : interface_name_(interface_name),
      ip_family_(ip_family),
      weak_ptr_factory_(this),
      dns_client_callback_(
          Bind(&HttpRequest::GetDNSResult, weak_ptr_factory_.GetWeakPtr())),
      success_callback_(
          Bind(&HttpRequest::SuccessCallback, weak_ptr_factory_.GetWeakPtr())),
      error_callback_(
          Bind(&HttpRequest::ErrorCallback, weak_ptr_factory_.GetWeakPtr())),
      dns_client_(new DnsClient(ip_family_,
                                interface_name_,
                                dns_list,
                                DnsClient::kDnsTimeoutMilliseconds,
                                dispatcher,
                                dns_client_callback_)),
      transport_(brillo::http::Transport::CreateDefault()),
      request_id_(-1),
      server_port_(-1),
      is_running_(false) {
  if (allow_non_google_https) {
    transport_->UseCustomCertificate(
        brillo::http::Transport::Certificate::kNss);
  }
}

HttpRequest::~HttpRequest() {
  Stop();
}

HttpRequest::Result HttpRequest::Start(
    const string& url_string,
    const brillo::http::HeaderList& headers,
    const Callback<void(std::shared_ptr<brillo::http::Response>)>&
        request_success_callback,
    const Callback<void(Result)>& request_error_callback) {
  SLOG(this, 3) << "In " << __func__;

  DCHECK(!is_running_);

  HttpUrl url;
  if (!url.ParseFromString(url_string)) {
    LOG(ERROR) << interface_name_
               << ": Failed to parse URL string: " << url_string;
    return kResultInvalidInput;
  }
  url_string_ = url_string;
  headers_ = headers;
  is_running_ = true;
  server_hostname_ = url.host();
  server_port_ = url.port();
  server_path_ = url.path();
  transport_->SetDefaultTimeout(
      base::TimeDelta::FromSeconds(kRequestTimeoutSeconds));

  IPAddress addr(ip_family_);

  request_success_callback_ = request_success_callback;
  request_error_callback_ = request_error_callback;

  if (addr.SetAddressFromString(server_hostname_)) {
    StartRequest();
  } else {
    SLOG(this, 3) << "Looking up host: " << server_hostname_;
    Error error;
    if (!dns_client_->Start(server_hostname_, &error)) {
      LOG(ERROR) << interface_name_
                 << ": Failed to start DNS client: " << error.message();
      Stop();
      return kResultDNSFailure;
    }
  }

  return kResultInProgress;
}

void HttpRequest::StartRequest() {
  request_id_ = brillo::http::Get(url_string_, headers_, transport_,
                                  success_callback_, error_callback_);
}

void HttpRequest::SuccessCallback(
    brillo::http::RequestID request_id,
    std::unique_ptr<brillo::http::Response> response) {
  if (request_id != request_id_) {
    LOG(ERROR) << interface_name_ << ": Expected request ID " << request_id_
               << " but got " << request_id;
    SendStatus(kResultUnknown);
    return;
  }

  Callback<void(std::shared_ptr<brillo::http::Response>)>
      request_success_callback = request_success_callback_;
  Stop();

  if (!request_success_callback.is_null()) {
    request_success_callback.Run(std::move(response));
  }
}

void HttpRequest::ErrorCallback(brillo::http::RequestID request_id,
                                const brillo::Error* error) {
  int error_code;
  if (error->GetDomain() != kCurlEasyError) {
    LOG(ERROR) << interface_name_ << ": Expected error domain "
               << kCurlEasyError << " but got " << error->GetDomain();
    SendStatus(kResultUnknown);
    return;
  }
  if (request_id != request_id_) {
    LOG(ERROR) << interface_name_ << ": Expected request ID " << request_id_
               << " but got " << request_id;
    SendStatus(kResultUnknown);
    return;
  }
  if (!StringToInt(error->GetCode(), &error_code)) {
    LOG(ERROR) << interface_name_ << ": Unable to convert error code "
               << error->GetCode() << " to Int";
    SendStatus(kResultUnknown);
    return;
  }

  // TODO(matthewmwang): This breaks abstraction. Modify brillo::http::Transport
  // to provide an implementation agnostic error code.
  switch (error_code) {
    case CURLE_COULDNT_CONNECT:
      SendStatus(kResultConnectionFailure);
      break;
    case CURLE_WRITE_ERROR:
    case CURLE_READ_ERROR:
      SendStatus(kResultHTTPFailure);
      break;
    case CURLE_OPERATION_TIMEDOUT:
      SendStatus(kResultHTTPTimeout);
      break;
    default:
      SendStatus(kResultUnknown);
  }
}

void HttpRequest::Stop() {
  SLOG(this, 3) << "In " << __func__ << "; running is " << is_running_;

  if (!is_running_) {
    return;
  }

  // Clear IO handlers first so that closing the socket doesn't cause
  // events to fire.
  dns_client_->Stop();
  is_running_ = false;
  request_id_ = -1;
  server_hostname_.clear();
  server_path_.clear();
  server_port_ = -1;
  request_error_callback_.Reset();
  request_success_callback_.Reset();
}

// DnsClient callback that fires when the DNS request completes.
void HttpRequest::GetDNSResult(const Error& error, const IPAddress& address) {
  SLOG(this, 3) << "In " << __func__;
  if (!error.IsSuccess()) {
    LOG(ERROR) << interface_name_ << ": Could not resolve hostname "
               << server_hostname_ << ": " << error.message();
    if (error.message() == DnsClient::kErrorTimedOut) {
      SendStatus(kResultDNSTimeout);
    } else {
      SendStatus(kResultDNSFailure);
    }
    return;
  }

  string addr_string;
  if (!address.IntoString(&addr_string)) {
    SendStatus(kResultDNSFailure);
    return;
  }

  // Add the host/port to IP mapping to the DNS cache to force curl to resolve
  // the URL to the given IP. Otherwise, will do its own DNS resolution and not
  // use the IP we provide to it.
  transport_->ResolveHostToIp(server_hostname_, server_port_, addr_string);
  StartRequest();
}

void HttpRequest::SendStatus(Result result) {
  // Save copies on the stack, since Stop() will remove them.
  Callback<void(Result)> request_error_callback = request_error_callback_;
  Stop();

  // Call the callback last, since it may delete us and |this| may no longer
  // be valid.
  if (!request_error_callback.is_null()) {
    request_error_callback.Run(result);
  }
}

}  // namespace shill
