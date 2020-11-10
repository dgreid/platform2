// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_HTTP_REQUEST_H_
#define SHILL_MOCK_HTTP_REQUEST_H_

#include <memory>
#include <string>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/http_request.h"
#include "shill/http_url.h"  // MOCK_METHOD3() call below needs sizeof(HttpUrl).

namespace shill {

class MockHttpRequest : public HttpRequest {
 public:
  MockHttpRequest();
  MockHttpRequest(const MockHttpRequest&) = delete;
  MockHttpRequest& operator=(const MockHttpRequest&) = delete;

  ~MockHttpRequest() override;

  MOCK_METHOD(
      HttpRequest::Result,
      Start,
      (const std::string&,
       const brillo::http::HeaderList&,
       const base::Callback<void(std::shared_ptr<brillo::http::Response>)>&,
       const base::Callback<void(Result)>&),
      (override));
  MOCK_METHOD(void, Stop, (), (override));
};

}  // namespace shill

#endif  // SHILL_MOCK_HTTP_REQUEST_H_
