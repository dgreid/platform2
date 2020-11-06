// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webservd/dbus_request_handler.h"

#include <tuple>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/strings/string_util.h>
#include <brillo/http/http_request.h>
#include <brillo/mime_utils.h>

#include "libwebserv/dbus-proxies.h"
#include "webservd/request.h"
#include "webservd/server.h"

namespace webservd {

namespace {

constexpr int kDbusTimeoutInMsec = 50 * 1000;

void OnError(base::WeakPtr<webservd::Request> in_request,
             bool debug,
             brillo::Error* error) {
  auto* request = in_request.get();
  if (!request) {
    LOG(ERROR) << "Request Instance is expired";
    return;
  }

  std::string error_msg{"Internal Server Error"};
  if (debug) {
    error_msg += "\r\n" + error->GetMessage();
  }
  request->Complete(brillo::http::status_code::InternalServerError, {},
                    brillo::mime::text::kPlain, error_msg);
}

bool CompleteRequestIfInvalid(Request* request, const std::string& value) {
  if (base::IsStringUTF8(value))
    return false;

  request->Complete(brillo::http::status_code::BadRequest, {},
                    brillo::mime::text::kPlain, "Invalid Characters\n");
  return true;
}

}  // anonymous namespace

DBusRequestHandler::DBusRequestHandler(Server* server,
                                       RequestHandlerProxy* handler_proxy)
    : server_{server}, handler_proxy_{handler_proxy} {}

void DBusRequestHandler::HandleRequest(
    base::WeakPtr<webservd::Request> in_request, const std::string& src) {
  auto* request = in_request.get();
  if (!request) {
    LOG(INFO) << "Request Instance is not valid";
    return;
  }

  std::vector<std::tuple<std::string, std::string>> headers;
  for (const auto& pair : request->GetHeaders()) {
    if (CompleteRequestIfInvalid(request, pair.first) ||
        CompleteRequestIfInvalid(request, pair.second)) {
      return;
    }
    headers.emplace_back(pair.first, pair.second);
  }
  headers.emplace_back("Source-Host", src);

  std::vector<
      std::tuple<int32_t, std::string, std::string, std::string, std::string>>
      files;
  int32_t index = 0;
  for (const auto& file : request->GetFileInfo()) {
    if (CompleteRequestIfInvalid(request, file->field_name) ||
        CompleteRequestIfInvalid(request, file->file_name) ||
        CompleteRequestIfInvalid(request, file->content_type) ||
        CompleteRequestIfInvalid(request, file->transfer_encoding)) {
      return;
    }
    files.emplace_back(index++, file->field_name, file->file_name,
                       file->content_type, file->transfer_encoding);
  }

  std::vector<std::tuple<bool, std::string, std::string>> params;
  for (const auto& pair : request->GetDataGet()) {
    if (CompleteRequestIfInvalid(request, pair.first) ||
        CompleteRequestIfInvalid(request, pair.second)) {
      return;
    }
    params.emplace_back(false, pair.first, pair.second);
  }

  for (const auto& pair : request->GetDataPost()) {
    if (CompleteRequestIfInvalid(request, pair.first) ||
        CompleteRequestIfInvalid(request, pair.second)) {
      return;
    }
    params.emplace_back(true, pair.first, pair.second);
  }

  if (CompleteRequestIfInvalid(request, request->GetProtocolHandlerID()) ||
      CompleteRequestIfInvalid(request, request->GetRequestHandlerID()) ||
      CompleteRequestIfInvalid(request, request->GetID()) ||
      CompleteRequestIfInvalid(request, request->GetURL()) ||
      CompleteRequestIfInvalid(request, request->GetMethod())) {
    return;
  }

  auto error_callback =
      base::Bind(&OnError, in_request, server_->GetConfig().use_debug);

  auto request_id = std::make_tuple(
      request->GetProtocolHandlerID(), request->GetRequestHandlerID(),
      request->GetID(), request->GetURL(), request->GetMethod());

  base::ScopedFD body_data_pipe(request->GetBodyDataFileDescriptor());
  handler_proxy_->ProcessRequestAsync(request_id, headers, params, files,
                                      body_data_pipe.get(), base::DoNothing(),
                                      error_callback, kDbusTimeoutInMsec);
}

}  // namespace webservd
