// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBSERVER_LIBWEBSERV_REQUEST_HANDLER_INTERFACE_H_
#define WEBSERVER_LIBWEBSERV_REQUEST_HANDLER_INTERFACE_H_

#include <memory>

#include <libwebserv/request.h>
#include <libwebserv/response.h>

namespace libwebserv {

// The base interface for HTTP request handlers. When registering a handler,
// the RequestHandlerInterface is provided, and when a request comes in,
// RequestHandlerInterface::HandleRequest() is called to process the data and
// send response.
class RequestHandlerInterface {
 public:
  using HandlerSignature =
      void(std::unique_ptr<Request>, std::unique_ptr<Response>);
  virtual ~RequestHandlerInterface() = default;

  virtual void HandleRequest(std::unique_ptr<Request> request,
                             std::unique_ptr<Response> response) = 0;
};

}  // namespace libwebserv

#endif  // WEBSERVER_LIBWEBSERV_REQUEST_HANDLER_INTERFACE_H_
