// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBSERVER_LIBWEBSERV_DBUS_RESPONSE_H_
#define WEBSERVER_LIBWEBSERV_DBUS_RESPONSE_H_

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <base/macros.h>
#include <brillo/streams/stream.h>
#include <libwebserv/response.h>

namespace libwebserv {

class DBusProtocolHandler;

// Implementation of the Response interface.
class DBusResponse final : public Response {
 public:
  DBusResponse(const DBusResponse&) = delete;
  DBusResponse& operator=(const DBusResponse&) = delete;
  ~DBusResponse() override;

  // Overrides from Response.
  void AddHeaders(
      const std::vector<std::pair<std::string, std::string>>& headers) override;
  void Reply(int status_code,
             brillo::StreamPtr data_stream,
             const std::string& mime_type) override;

 private:
  friend class DBusProtocolHandler;

  LIBWEBSERV_PRIVATE DBusResponse(DBusProtocolHandler* handler,
                                  const std::string& request_id);

  DBusProtocolHandler* handler_{nullptr};
  std::string request_id_;
  int status_code_{0};
  brillo::StreamPtr data_stream_;
  std::multimap<std::string, std::string> headers_;
  bool reply_sent_{false};
};

}  // namespace libwebserv

#endif  // WEBSERVER_LIBWEBSERV_DBUS_RESPONSE_H_
