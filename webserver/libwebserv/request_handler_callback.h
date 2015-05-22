// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBSERVER_LIBWEBSERV_REQUEST_HANDLER_CALLBACK_H_
#define WEBSERVER_LIBWEBSERV_REQUEST_HANDLER_CALLBACK_H_

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

  void HandleRequest(std::unique_ptr<Request> request,
                     std::unique_ptr<Response> response) override;

 private:
  base::Callback<HandlerSignature> callback_;
  DISALLOW_COPY_AND_ASSIGN(RequestHandlerCallback);
};

}  // namespace libwebserv

#endif  // WEBSERVER_LIBWEBSERV_REQUEST_HANDLER_CALLBACK_H_
