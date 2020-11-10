// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBSERVER_LIBWEBSERV_REQUEST_IMPL_H_
#define WEBSERVER_LIBWEBSERV_REQUEST_IMPL_H_

#include <string>

#include <base/files/file.h>
#include <base/macros.h>
#include <brillo/streams/stream.h>
#include <libwebserv/request.h>

namespace libwebserv {

class DBusProtocolHandler;

// Implementation of the Request interface.
class RequestImpl final : public Request {
 public:
  RequestImpl(const RequestImpl&) = delete;
  RequestImpl& operator=(const RequestImpl&) = delete;
  // Overrides from Request.
  brillo::StreamPtr GetDataStream() override;

 private:
  friend class DBusServer;

  LIBWEBSERV_PRIVATE RequestImpl(DBusProtocolHandler* handler,
                                 const std::string& url,
                                 const std::string& method);
  DBusProtocolHandler* handler_{nullptr};
  base::File raw_data_fd_;
  bool last_posted_data_was_file_{false};
};

}  // namespace libwebserv

#endif  // WEBSERVER_LIBWEBSERV_REQUEST_IMPL_H_
