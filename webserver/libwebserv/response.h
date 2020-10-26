// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBSERVER_LIBWEBSERV_RESPONSE_H_
#define WEBSERVER_LIBWEBSERV_RESPONSE_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/macros.h>
#include <base/values.h>
#include <brillo/streams/stream.h>
#include <libwebserv/export.h>

namespace base {
class Value;
}  // namespace base

namespace libwebserv {

class ProtocolHandler;

// Response class is a proxy for HTTP response used by the request handler
// to provide response HTTP headers and data.
class LIBWEBSERV_EXPORT Response {
 public:
  virtual ~Response() = default;

  // Adds a single HTTP response header to the response.
  virtual void AddHeader(const std::string& header_name,
                         const std::string& value);

  // Adds number of HTTP response headers to the response.
  virtual void AddHeaders(
      const std::vector<std::pair<std::string, std::string>>& headers) = 0;

  // Generic reply method for sending arbitrary binary data response.
  virtual void Reply(int status_code,
                     brillo::StreamPtr data_stream,
                     const std::string& mime_type) = 0;

  // Reply with text body.
  virtual void ReplyWithText(int status_code,
                             const std::string& text,
                             const std::string& mime_type);

  // Reply with JSON object. The content type will be "application/json".
  virtual void ReplyWithJson(int status_code, const base::Value& json);
  // TODO(crbug.com/1099111): remove after all usages are migrated to above
  virtual void ReplyWithJson(int status_code, const base::Value* json);

  // Special form for JSON response for simple objects that have a flat
  // list of key-value pairs of string type.
  virtual void ReplyWithJson(int status_code,
                             const std::map<std::string, std::string>& json);

  // Issue a redirect response, so the client browser loads a page at
  // the URL specified in |redirect_url|. If this is not an external URL,
  // it must be an absolute path starting at the root "/...".
  virtual void Redirect(int status_code, const std::string& redirect_url);

  // Send a plain text response (with no Content-Type header).
  // Usually used with error responses. |error_text| must be plain text.
  virtual void ReplyWithError(int status_code, const std::string& error_text);

  // Send "404 Not Found" response.
  virtual void ReplyWithErrorNotFound();
};

}  // namespace libwebserv

#endif  // WEBSERVER_LIBWEBSERV_RESPONSE_H_
