// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBSERVER_LIBWEBSERV_MOCK_RESPONSE_H_
#define WEBSERVER_LIBWEBSERV_MOCK_RESPONSE_H_

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <base/macros.h>
#include <gmock/gmock.h>

#include <libwebserv/response.h>

namespace libwebserv {

// Mock Response implementation for testing.
class MockResponse : public Response {
 public:
  MockResponse() = default;
  MockResponse(const MockResponse&) = delete;
  MockResponse& operator=(const MockResponse&) = delete;

  MOCK_METHOD2(AddHeader, void(const std::string&, const std::string&));
  MOCK_METHOD1(AddHeaders,
               void(const std::vector<std::pair<std::string, std::string>>&));

  // Workaround for mocking with move-only StreamPtr.
  MOCK_METHOD3(MockReply, void(int, brillo::Stream*, const std::string&));

  MOCK_METHOD3(ReplyWithText,
               void(int, const std::string&, const std::string&));
  MOCK_METHOD2(ReplyWithJson, void(int, const base::Value*));
  MOCK_METHOD2(ReplyWithJson,
               void(int, const std::map<std::string, std::string>&));
  MOCK_METHOD2(Redirect, void(int, const std::string&));
  MOCK_METHOD2(ReplyWithError, void(int, const std::string&));
  MOCK_METHOD0(ReplyWithErrorNotFound, void());

 private:
  void Reply(int status_code,
             brillo::StreamPtr data_stream,
             const std::string& mime_type) override {
    return MockReply(status_code, data_stream.get(), mime_type);
  }
};

}  // namespace libwebserv

#endif  // WEBSERVER_LIBWEBSERV_MOCK_RESPONSE_H_
