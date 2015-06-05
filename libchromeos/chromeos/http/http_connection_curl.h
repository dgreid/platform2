// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBCHROMEOS_CHROMEOS_HTTP_HTTP_CONNECTION_CURL_H_
#define LIBCHROMEOS_CHROMEOS_HTTP_HTTP_CONNECTION_CURL_H_

#include <map>
#include <string>
#include <vector>

#include <base/macros.h>
#include <chromeos/chromeos_export.h>
#include <chromeos/http/http_connection.h>
#include <chromeos/http/http_transport_curl.h>
#include <curl/curl.h>

namespace chromeos {
namespace http {
namespace curl {

// This is a libcurl-based implementation of http::Connection.
class CHROMEOS_EXPORT Connection : public http::Connection {
 public:
  Connection(CURL* curl_handle,
             const std::string& method,
             const std::shared_ptr<CurlInterface>& curl_interface,
             const std::shared_ptr<http::Transport>& transport);
  ~Connection() override;

  // Overrides from http::Connection.
  // See http_connection.h for description of these methods.
  bool SendHeaders(const HeaderList& headers,
                   chromeos::ErrorPtr* error) override;
  bool SetRequestData(StreamPtr stream, chromeos::ErrorPtr* error) override;
  void SetResponseData(StreamPtr stream) override;
  bool FinishRequest(chromeos::ErrorPtr* error) override;
  RequestID FinishRequestAsync(
      const SuccessCallback& success_callback,
      const ErrorCallback& error_callback) override;

  int GetResponseStatusCode() const override;
  std::string GetResponseStatusText() const override;
  std::string GetProtocolVersion() const override;
  std::string GetResponseHeader(const std::string& header_name) const override;
  StreamPtr ExtractDataStream(chromeos::ErrorPtr* error) override;

 protected:
  // Write data callback. Used by CURL when receiving response data.
  CHROMEOS_PRIVATE static size_t write_callback(char* ptr,
                                                size_t size,
                                                size_t num,
                                                void* data);
  // Read data callback. Used by CURL when sending request body data.
  CHROMEOS_PRIVATE static size_t read_callback(char* ptr,
                                               size_t size,
                                               size_t num,
                                               void* data);
  // Write header data callback. Used by CURL when receiving response headers.
  CHROMEOS_PRIVATE static size_t header_callback(char* ptr,
                                                 size_t size,
                                                 size_t num,
                                                 void* data);

  // Helper method to set up the |curl_handle_| with all the parameters
  // pertaining to the current connection.
  CHROMEOS_PRIVATE void PrepareRequest();

  // HTTP request verb, such as "GET", "POST", "PUT", ...
  std::string method_;

  // Binary data for request body.
  StreamPtr request_data_stream_;

  // Received response data.
  StreamPtr response_data_stream_;

  // List of optional request headers provided by the caller.
  // After request has been sent, contains the received response headers.
  std::multimap<std::string, std::string> headers_;

  // HTTP protocol version, such as HTTP/1.1
  std::string protocol_version_;
  // Response status text, such as "OK" for 200, or "Forbidden" for 403
  std::string status_text_;
  // Flag used when parsing response headers to separate the response status
  // from the rest of response headers.
  bool status_text_set_{false};

  CURL* curl_handle_{nullptr};
  curl_slist* header_list_{nullptr};

  std::shared_ptr<CurlInterface> curl_interface_;

 private:
  friend class http::curl::Transport;
  DISALLOW_COPY_AND_ASSIGN(Connection);
};

}  // namespace curl
}  // namespace http
}  // namespace chromeos

#endif  // LIBCHROMEOS_CHROMEOS_HTTP_HTTP_CONNECTION_CURL_H_
