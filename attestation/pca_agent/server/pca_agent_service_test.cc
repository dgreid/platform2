// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/pca_agent/server/pca_agent_service.h"

#include <brillo/dbus/dbus_method_response.h>
#include <brillo/http/http_transport_fake.h>
#include <brillo/mime_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "attestation/pca_agent/server/response_with_verifier.h"

using testing::_;
using testing::Invoke;

namespace attestation {
namespace pca_agent {

namespace {

constexpr char kDefaultACAEnrollUrl[] =
    "https://chromeos-ca.gstatic.com/enroll";
constexpr char kTestACAEnrollUrl[] =
    "https://asbestos-qa.corp.google.com/enroll";

constexpr char kDefaultACASignUrl[] = "https://chromeos-ca.gstatic.com/sign";
constexpr char kTestACASignUrl[] = "https://asbestos-qa.corp.google.com/sign";

constexpr char kFakeResponse[] = "kFakeResponse";

void FakeMethodHandler(const brillo::http::fake::ServerRequest& request,
                       brillo::http::fake::ServerResponse* response) {
  response->ReplyText(brillo::http::status_code::Ok, kFakeResponse,
                      brillo::mime::text::kPlain);
}

}  // namespace

class PcaAgentServiceTest : public testing::Test {
 public:
  PcaAgentServiceTest() = default;
  ~PcaAgentServiceTest() override = default;

  void SetUp() override {
    fake_transport_ = std::make_shared<brillo::http::fake::Transport>();
    service_.transport_ = fake_transport_;
  }

  PcaAgentService service_;
  std::shared_ptr<brillo::http::fake::Transport> fake_transport_;
};

TEST_F(PcaAgentServiceTest, Enroll404NotFound) {
  auto v = [](const EnrollReply& r) {
    EXPECT_EQ(r.status(), STATUS_REQUEST_DENIED_BY_CA);
  };
  EnrollRequest request;
  service_.Enroll(MakeResponseWithVerifier<EnrollReply>(v), request);
}

TEST_F(PcaAgentServiceTest, GetCertificate404NotFound) {
  auto v = [](const GetCertificateReply& r) {
    EXPECT_EQ(r.status(), STATUS_REQUEST_DENIED_BY_CA);
  };
  GetCertificateRequest request;
  service_.GetCertificate(MakeResponseWithVerifier<GetCertificateReply>(v),
                          request);
}

TEST_F(PcaAgentServiceTest, EnrollDefaultACA) {
  auto v = [](const EnrollReply& r) {
    EXPECT_EQ(r.status(), STATUS_SUCCESS);
    EXPECT_EQ(r.response(), kFakeResponse);
  };
  fake_transport_->AddHandler(kDefaultACAEnrollUrl,
                              brillo::http::request_type::kPost,
                              base::Bind(FakeMethodHandler));
  EnrollRequest request;
  request.set_aca_type(DEFAULT_ACA);
  service_.Enroll(MakeResponseWithVerifier<EnrollReply>(v), request);
}

TEST_F(PcaAgentServiceTest, EnrollTestACA) {
  auto v = [](const EnrollReply& r) {
    EXPECT_EQ(r.status(), STATUS_SUCCESS);
    EXPECT_EQ(r.response(), kFakeResponse);
  };
  fake_transport_->AddHandler(kTestACAEnrollUrl,
                              brillo::http::request_type::kPost,
                              base::Bind(FakeMethodHandler));
  EnrollRequest request;
  request.set_aca_type(TEST_ACA);
  service_.Enroll(MakeResponseWithVerifier<EnrollReply>(v), request);
}

TEST_F(PcaAgentServiceTest, GetCertificateDefaultACA) {
  auto v = [](const GetCertificateReply& r) {
    EXPECT_EQ(r.status(), STATUS_SUCCESS);
    EXPECT_EQ(r.response(), kFakeResponse);
  };
  fake_transport_->AddHandler(kDefaultACASignUrl,
                              brillo::http::request_type::kPost,
                              base::Bind(FakeMethodHandler));
  GetCertificateRequest request;
  request.set_aca_type(DEFAULT_ACA);
  service_.GetCertificate(MakeResponseWithVerifier<GetCertificateReply>(v),
                          request);
}

TEST_F(PcaAgentServiceTest, GetCertificateTestACA) {
  auto v = [](const GetCertificateReply& r) {
    EXPECT_EQ(r.status(), STATUS_SUCCESS);
    EXPECT_EQ(r.response(), kFakeResponse);
  };
  fake_transport_->AddHandler(kTestACASignUrl,
                              brillo::http::request_type::kPost,
                              base::Bind(FakeMethodHandler));
  GetCertificateRequest request;
  request.set_aca_type(TEST_ACA);
  service_.GetCertificate(MakeResponseWithVerifier<GetCertificateReply>(v),
                          request);
}

}  // namespace pca_agent
}  // namespace attestation
