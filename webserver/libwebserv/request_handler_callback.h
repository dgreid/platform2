// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBSERVER_LIBWEBSERV_REQUEST_HANDLER_CALLBACK_H_
#define WEBSERVER_LIBWEBSERV_REQUEST_HANDLER_CALLBACK_H_

#include <memory>

#include <base/callback.h>
#include <base/macros.h>
#include <libwebserv/export.h>
#include <libwebserv/request_handler_interface.h>

namespace libwebserv {

// A simple request handler that wraps a callback function.
// Essentially, it redirects the RequestHandlerInterface::HandleRequest calls
// to the provided callback.
class LIBWEBSERV_EXPORT RequestHandlerCallback final
    : public RequestHandlerInterface {
 public:
  explicit RequestHandlerCallback(
      const base::Callback<HandlerSignature>& callback);
  RequestHandlerCallback(const RequestHandlerCallback&) = delete;
  RequestHandlerCallback& operator=(const RequestHandlerCallback&) = delete;

  void HandleRequest(std::unique_ptr<Request> request,
                     std::unique_ptr<Response> response) override;

 private:
  base::Callback<HandlerSignature> callback_;
};

}  // namespace libwebserv

#endif  // WEBSERVER_LIBWEBSERV_REQUEST_HANDLER_CALLBACK_H_
