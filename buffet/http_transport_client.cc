// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/http_transport_client.h"

#include <utility>

#include <base/bind.h>
#include <brillo/errors/error.h>
#include <brillo/http/http_utils.h>
#include <brillo/streams/memory_stream.h>
#include <weave/enum_to_string.h>

#include "buffet/weave_error_conversion.h"

namespace buffet {

namespace {

using weave::provider::HttpClient;

// The number of seconds each HTTP request will be allowed before timing out.
const int kRequestTimeoutSeconds = 30;

const char kErrorDomain[] = "buffet";

class ResponseImpl : public HttpClient::Response {
 public:
  explicit ResponseImpl(std::unique_ptr<brillo::http::Response> response)
      : response_{std::move(response)},
        data_{response_->ExtractDataAsString()} {}
  ResponseImpl(const ResponseImpl&) = delete;
  ResponseImpl& operator=(const ResponseImpl&) = delete;
  ~ResponseImpl() override = default;

  // HttpClient::Response implementation
  int GetStatusCode() const override { return response_->GetStatusCode(); }

  std::string GetContentType() const override {
    return response_->GetContentType();
  }

  std::string GetData() const override { return data_; }

 private:
  std::unique_ptr<brillo::http::Response> response_;
  std::string data_;
};

}  // anonymous namespace

HttpTransportClient::HttpTransportClient()
    : transport_{brillo::http::Transport::CreateDefault()} {
  transport_->SetDefaultTimeout(
      base::TimeDelta::FromSeconds(kRequestTimeoutSeconds));
}

HttpTransportClient::~HttpTransportClient() {}

void HttpTransportClient::SetLocalIpAddress(const std::string& ip_address) {
  transport_->SetLocalIpAddress(ip_address);
}

void HttpTransportClient::SendRequest(Method method,
                                      const std::string& url,
                                      const Headers& headers,
                                      const std::string& data,
                                      const SendRequestCallback& callback) {
  brillo::http::Request request(url, weave::EnumToString(method), transport_);
  request.AddHeaders(headers);
  if (!data.empty()) {
    auto stream = brillo::MemoryStream::OpenCopyOf(data, nullptr);
    CHECK(stream->GetRemainingSize());
    brillo::ErrorPtr cromeos_error;
    if (!request.AddRequestBody(std::move(stream), &cromeos_error)) {
      weave::ErrorPtr error;
      ConvertError(*cromeos_error, &error);
      transport_->RunCallbackAsync(
          FROM_HERE, base::Bind(callback, nullptr, base::Passed(&error)));
      return;
    }
  }

  callbacks_.emplace(
      request.GetResponse(base::Bind(&HttpTransportClient::OnSuccessCallback,
                                     weak_ptr_factory_.GetWeakPtr()),
                          base::Bind(&HttpTransportClient::OnErrorCallback,
                                     weak_ptr_factory_.GetWeakPtr())),
      callback);
}

void HttpTransportClient::OnSuccessCallback(
    int id, std::unique_ptr<brillo::http::Response> response) {
  auto it = callbacks_.find(id);
  if (it == callbacks_.end()) {
    LOG(INFO) << "Request has already been cancelled: " << id;
    return;
  }

  it->second.Run(std::unique_ptr<HttpClient::Response>{new ResponseImpl{
                     std::move(response)}},
                 nullptr);
  callbacks_.erase(it);
}

void HttpTransportClient::OnErrorCallback(int id,
                                          const brillo::Error* brillo_error) {
  auto it = callbacks_.find(id);
  if (it == callbacks_.end()) {
    LOG(INFO) << "Request has already been cancelled: " << id;
    return;
  }

  weave::ErrorPtr error;
  ConvertError(*brillo_error, &error);
  it->second.Run(nullptr, std::move(error));
  callbacks_.erase(it);
}

void HttpTransportClient::SetOnline(bool online) {
  if (!online) {
    for (const auto& pair : callbacks_) {
      weave::ErrorPtr error;
      weave::Error::AddTo(&error, FROM_HERE, kErrorDomain, "offline",
                          "offline");
      transport_->RunCallbackAsync(
          FROM_HERE, base::Bind(pair.second, nullptr, base::Passed(&error)));
    }

    callbacks_.clear();
  }
}

}  // namespace buffet
