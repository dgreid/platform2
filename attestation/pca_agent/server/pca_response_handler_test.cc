// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/pca_agent/server/pca_response_handler.h"

#include <memory>
#include <utility>

#include <base/time/time.h>
#include <brillo/errors/error.h>
#include <brillo/http/http_connection_fake.h>
#include <brillo/streams/memory_stream.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "attestation/pca_agent/server/response_with_verifier.h"

namespace {

using ::testing::Types;

constexpr brillo::http::RequestID kDummyReqeustID = 7533967;
constexpr char kFakeErrMessage[] = "a tactical error";
constexpr char kDummyHandlerName[] = "testing";
constexpr char kFakeResponse[] = "fake response";

class FakeConnection : public brillo::http::fake::Connection {
 public:
  FakeConnection(int status_code, const std::string& data)
      : brillo::http::fake::Connection(/*url=*/"",
                                       /*method=*/"",
                                       /*transport=*/nullptr),
        status_code_(status_code),
        data_(data) {}
  int GetResponseStatusCode() const override { return status_code_; }
  brillo::StreamPtr ExtractDataStream(brillo::ErrorPtr*) override {
    return brillo::MemoryStream::OpenCopyOf(data_.data(), data_.length(),
                                            nullptr);
  }

 private:
  const int status_code_;
  const std::string data_;
};

std::unique_ptr<brillo::http::Response> MakeHttpResponseWithFakeConnection(
    int status_code,
    const std::string& data) {
  return std::make_unique<brillo::http::Response>(
      std::make_shared<FakeConnection>(status_code, data));
}

}  // namespace

namespace attestation {
namespace pca_agent {

template <typename ReplyType>
class PcaResponseHandlerTest : public ::testing::Test {
 protected:
  template <typename Verifier>
  scoped_refptr<PcaResponseHandler<ReplyType>> MakePcaResponseHandler(
      Verifier&& v) {
    auto response = MakeResponseWithVerifier<ReplyType>(v);
    return new PcaResponseHandler<ReplyType>(kDummyHandlerName,
                                             std::move(response));
  }
};

using ReplyTypes = testing::Types<EnrollReply, GetCertificateReply>;
TYPED_TEST_SUITE(PcaResponseHandlerTest, ReplyTypes);

TYPED_TEST(PcaResponseHandlerTest, OnError) {
  auto v = [](const auto& reply) {
    EXPECT_EQ(reply.status(), STATUS_CA_NOT_AVAILABLE);
  };
  auto handler = this->MakePcaResponseHandler(v);
  brillo::ErrorPtr err =
      brillo::Error::Create(base::Location(), "", "", kFakeErrMessage);
  handler->OnError(kDummyReqeustID, err.get());
}

TYPED_TEST(PcaResponseHandlerTest, OnSuccess) {
  auto v = [](const auto& reply) {
    EXPECT_EQ(reply.status(), STATUS_SUCCESS);
    EXPECT_EQ(reply.response(), std::string(kFakeResponse));
  };
  auto handler = this->MakePcaResponseHandler(v);
  std::unique_ptr<brillo::http::Response> resp =
      MakeHttpResponseWithFakeConnection(200, kFakeResponse);
  handler->OnSuccess(kDummyReqeustID, std::move(resp));
}

TYPED_TEST(PcaResponseHandlerTest, OnSuccessNotSupported) {
  auto v = [](const auto& reply) {
    EXPECT_EQ(reply.status(), STATUS_NOT_SUPPORTED);
  };
  auto handler = this->MakePcaResponseHandler(v);
  std::unique_ptr<brillo::http::Response> resp =
      MakeHttpResponseWithFakeConnection(101, "");
  handler->OnSuccess(kDummyReqeustID, std::move(resp));
}

TYPED_TEST(PcaResponseHandlerTest, OnSuccessBadHttpStatus) {
  auto v = [](const auto& reply) {
    EXPECT_EQ(reply.status(), STATUS_REQUEST_DENIED_BY_CA);
  };
  auto handler = this->MakePcaResponseHandler(v);
  std::unique_ptr<brillo::http::Response> resp =
      MakeHttpResponseWithFakeConnection(404, "");
  handler->OnSuccess(kDummyReqeustID, std::move(resp));
}

}  // namespace pca_agent
}  // namespace attestation
