// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBSERVER_LIBWEBSERV_REQUEST_FAKE_H_
#define WEBSERVER_LIBWEBSERV_REQUEST_FAKE_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/callback_forward.h>
#include <base/files/file.h>
#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <brillo/errors/error.h>
#include <brillo/streams/stream.h>
#include <libwebserv/export.h>
#include <libwebserv/request.h>

namespace libwebserv {

// Implementation of Request that allows custom data for testing.
class RequestFake : public Request {
 public:
  RequestFake(const std::string& url, const std::string& method)
      : Request(url, method) {}
  RequestFake(const RequestFake&) = delete;
  RequestFake& operator=(const RequestFake&) = delete;

  void SetDataStream(brillo::StreamPtr data_stream) {
    data_stream_ = std::move(data_stream);
  }

  void SetFormDataPost(std::multimap<std::string, std::string> post_data) {
    post_data_ = std::move(post_data);
  }

  void SetFormDataGet(std::multimap<std::string, std::string> get_data) {
    get_data_ = std::move(get_data);
  }

  void SetFileInfo(
      std::multimap<std::string, std::unique_ptr<FileInfo>> file_info) {
    file_info_ = std::move(file_info);
  }

  void SetHeaders(std::multimap<std::string, std::string> headers) {
    headers_ = std::move(headers);
  }

  // Overrides from Request.
  brillo::StreamPtr GetDataStream() override { return std::move(data_stream_); }

 private:
  brillo::StreamPtr data_stream_;
};

}  // namespace libwebserv

#endif  // WEBSERVER_LIBWEBSERV_REQUEST_FAKE_H_
