// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/portal_detector.h"

#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <brillo/http/http_request.h>
#include <brillo/http/mock_connection.h>
#include <brillo/http/mock_transport.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_connection.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_http_request.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/net/mock_time.h"

using base::Bind;
using base::Callback;
using base::Unretained;
using std::string;
using std::vector;
using testing::_;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;
using testing::Test;

namespace shill {

namespace {
const char kBadURL[] = "badurl";
const char kInterfaceName[] = "int0";
const char kHttpUrl[] = "http://www.chromium.org";
const char kHttpsUrl[] = "https://www.google.com";
const vector<string> kFallbackHttpUrls{
    "http://www.google.com/gen_204",
    "http://play.googleapis.com/generate_204",
};
const char kDNSServer0[] = "8.8.8.8";
const char kDNSServer1[] = "8.8.4.4";
const char* const kDNSServers[] = {kDNSServer0, kDNSServer1};
}  // namespace

MATCHER_P(IsResult, result, "") {
  return (result.phase == arg.phase && result.status == arg.status &&
          result.redirect_url_string == arg.redirect_url_string &&
          result.probe_url_string == arg.probe_url_string);
}

class PortalDetectorTest : public Test {
 public:
  PortalDetectorTest()
      : manager_(&control_, &dispatcher_, nullptr),
        device_info_(new NiceMock<MockDeviceInfo>(&manager_)),
        connection_(new StrictMock<MockConnection>(device_info_.get())),
        transport_(std::make_shared<brillo::http::MockTransport>()),
        brillo_connection_(
            std::make_shared<brillo::http::MockConnection>(transport_)),
        portal_detector_(
            new PortalDetector(connection_.get(),
                               &dispatcher_,
                               &metrics_,
                               callback_target_.result_callback())),
        interface_name_(kInterfaceName),
        dns_servers_(kDNSServers, kDNSServers + 2),
        http_request_(nullptr),
        https_request_(nullptr) {
    current_time_.tv_sec = current_time_.tv_usec = 0;
  }

  void SetUp() override {
    EXPECT_CALL(*connection_, IsIPv6()).WillRepeatedly(Return(false));
    EXPECT_CALL(*connection_, interface_name())
        .WillRepeatedly(ReturnRef(interface_name_));
    portal_detector_->time_ = &time_;
    EXPECT_CALL(time_, GetTimeMonotonic(_))
        .WillRepeatedly(Invoke(this, &PortalDetectorTest::GetTimeMonotonic));
    EXPECT_CALL(*connection_, dns_servers())
        .WillRepeatedly(ReturnRef(dns_servers_));
    EXPECT_EQ(nullptr, portal_detector_->http_request_);
  }

  void TearDown() override {
    Mock::VerifyAndClearExpectations(&http_request_);
    if (portal_detector()->http_request_) {
      EXPECT_CALL(*http_request(), Stop());
      EXPECT_CALL(*https_request(), Stop());

      // Delete the portal detector while expectations still exist.
      portal_detector_.reset();
    }
    testing::Mock::VerifyAndClearExpectations(brillo_connection_.get());
    brillo_connection_.reset();
    testing::Mock::VerifyAndClearExpectations(transport_.get());
    transport_.reset();
  }

 protected:
  static const int kNumAttempts;

  class CallbackTarget {
   public:
    CallbackTarget()
        : result_callback_(
              Bind(&CallbackTarget::ResultCallback, Unretained(this))) {}

    MOCK_METHOD(void,
                ResultCallback,
                (const PortalDetector::Result&, const PortalDetector::Result&));

    Callback<void(const PortalDetector::Result&,
                  const PortalDetector::Result&)>&
    result_callback() {
      return result_callback_;
    }

   private:
    Callback<void(const PortalDetector::Result&, const PortalDetector::Result&)>
        result_callback_;
  };

  void AssignHttpRequest() {
    http_request_ = new StrictMock<MockHttpRequest>();
    https_request_ = new StrictMock<MockHttpRequest>();
    portal_detector_->http_request_.reset(http_request_);
    portal_detector_->https_request_.reset(
        https_request_);  // Passes ownership.
  }

  bool StartPortalRequest(const PortalDetector::Properties& props, int delay) {
    bool ret = portal_detector_->StartAfterDelay(props, delay);
    if (ret) {
      AssignHttpRequest();
    }
    return ret;
  }

  void StartTrialTask() {
    EXPECT_CALL(*http_request(), Start(_, _, _, _))
        .WillOnce(Return(HttpRequest::kResultInProgress));
    EXPECT_CALL(*https_request(), Start(_, _, _, _))
        .WillOnce(Return(HttpRequest::kResultInProgress));
    portal_detector()->StartTrialTask();
  }

  MockHttpRequest* http_request() { return http_request_; }
  MockHttpRequest* https_request() { return https_request_; }
  PortalDetector* portal_detector() { return portal_detector_.get(); }
  MockEventDispatcher& dispatcher() { return dispatcher_; }
  CallbackTarget& callback_target() { return callback_target_; }
  MockMetrics& metrics() { return metrics_; }
  brillo::http::MockConnection* brillo_connection() {
    return brillo_connection_.get();
  }

  void ExpectReset() {
    EXPECT_FALSE(portal_detector_->attempt_count_);
    EXPECT_TRUE(callback_target_.result_callback() ==
                portal_detector_->portal_result_callback_);
    EXPECT_EQ(nullptr, portal_detector_->http_request_);
    EXPECT_EQ(nullptr, portal_detector_->https_request_);
  }

  void AdvanceTime(int milliseconds) {
    struct timeval tv = {milliseconds / 1000, (milliseconds % 1000) * 1000};
    timeradd(&current_time_, &tv, &current_time_);
  }

  void StartAttempt() {
    EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, 0));
    PortalDetector::Properties props =
        PortalDetector::Properties(kHttpUrl, kHttpsUrl, kFallbackHttpUrls);
    EXPECT_TRUE(StartPortalRequest(props, 0));
    StartTrialTask();
  }

  void ExpectRequestSuccessWithStatus(int status_code, bool is_http) {
    EXPECT_CALL(*brillo_connection_, GetResponseStatusCode())
        .WillOnce(Return(status_code));

    auto response =
        std::make_shared<brillo::http::Response>(brillo_connection_);
    if (is_http)
      portal_detector_->HttpRequestSuccessCallback(response);
    else
      portal_detector_->HttpsRequestSuccessCallback(response);
  }

 private:
  int GetTimeMonotonic(struct timeval* tv) {
    *tv = current_time_;
    return 0;
  }

  StrictMock<MockEventDispatcher> dispatcher_;
  MockControl control_;
  MockManager manager_;
  std::unique_ptr<MockDeviceInfo> device_info_;
  scoped_refptr<MockConnection> connection_;
  std::shared_ptr<brillo::http::MockTransport> transport_;
  NiceMock<MockMetrics> metrics_;
  std::shared_ptr<brillo::http::MockConnection> brillo_connection_;
  CallbackTarget callback_target_;
  std::unique_ptr<PortalDetector> portal_detector_;
  StrictMock<MockTime> time_;
  struct timeval current_time_;
  const string interface_name_;
  vector<string> dns_servers_;
  MockHttpRequest* http_request_;
  MockHttpRequest* https_request_;
};

// static
const int PortalDetectorTest::kNumAttempts = 0;

TEST_F(PortalDetectorTest, Constructor) {
  ExpectReset();
}

TEST_F(PortalDetectorTest, InvalidURL) {
  EXPECT_FALSE(portal_detector()->IsActive());
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, 0)).Times(0);
  PortalDetector::Properties props =
      PortalDetector::Properties(kBadURL, kHttpsUrl, kFallbackHttpUrls);
  EXPECT_FALSE(StartPortalRequest(props, 0));
  ExpectReset();

  EXPECT_FALSE(portal_detector()->IsActive());
}

TEST_F(PortalDetectorTest, IsActive) {
  // Before the trial is started, should not be active.
  EXPECT_FALSE(portal_detector()->IsActive());

  // Once the trial is started, IsActive should return true.
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, 0));
  PortalDetector::Properties props =
      PortalDetector::Properties(kHttpUrl, kHttpsUrl, kFallbackHttpUrls);
  EXPECT_TRUE(StartPortalRequest(props, 0));

  StartTrialTask();
  EXPECT_TRUE(portal_detector()->IsActive());

  // Finish the trial, IsActive should return false.
  EXPECT_CALL(*http_request(), Stop()).Times(1);
  EXPECT_CALL(*https_request(), Stop()).Times(1);
  portal_detector()->CompleteTrial(
      PortalDetector::Result(PortalDetector::Phase::kContent,
                             PortalDetector::Status::kFailure),
      PortalDetector::Result(PortalDetector::Phase::kContent,
                             PortalDetector::Status::kSuccess));
  EXPECT_FALSE(portal_detector()->IsActive());
}

TEST_F(PortalDetectorTest, StartAttemptFailed) {
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, 0));
  PortalDetector::Properties props =
      PortalDetector::Properties(kHttpUrl, kHttpsUrl, kFallbackHttpUrls);
  EXPECT_TRUE(StartPortalRequest(props, 0));

  // Expect that the request will be started -- return failure.
  EXPECT_CALL(*http_request(), Start(_, _, _, _))
      .WillOnce(Return(HttpRequest::kResultDNSFailure));

  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, 0)).Times(0);
  EXPECT_CALL(*http_request(), Stop()).Times(1);
  EXPECT_CALL(*https_request(), Stop()).Times(1);

  // Expect a non-final failure to be relayed to the caller.
  EXPECT_CALL(
      callback_target(),
      ResultCallback(
          IsResult(PortalDetector::Result(PortalDetector::Phase::kDNS,
                                          PortalDetector::Status::kFailure,
                                          kNumAttempts)),
          IsResult(PortalDetector::Result(PortalDetector::Phase::kContent,
                                          PortalDetector::Status::kFailure))));

  portal_detector()->StartTrialTask();
}

TEST_F(PortalDetectorTest, AdjustStartDelayImmediate) {
  PortalDetector::Properties props =
      PortalDetector::Properties(kHttpUrl, kHttpsUrl, kFallbackHttpUrls);
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, 0));
  EXPECT_TRUE(StartPortalRequest(props, 0));

  EXPECT_EQ(portal_detector()->AdjustStartDelay(1), 1);
}

TEST_F(PortalDetectorTest, AdjustStartDelayAfterDelay) {
  const int kDelaySeconds = 123;
  // The first attempt should be delayed by kDelaySeconds.
  PortalDetector::Properties props =
      PortalDetector::Properties(kHttpUrl, kHttpsUrl, kFallbackHttpUrls);
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, kDelaySeconds * 1000));

  EXPECT_TRUE(StartPortalRequest(props, kDelaySeconds));

  AdvanceTime(kDelaySeconds * 1000);

  EXPECT_EQ(portal_detector()->AdjustStartDelay(1), 1);
}

TEST_F(PortalDetectorTest, StartRepeated) {
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, 0)).Times(1);
  PortalDetector::Properties props =
      PortalDetector::Properties(kHttpUrl, kHttpsUrl, kFallbackHttpUrls);
  EXPECT_TRUE(StartPortalRequest(props, 0));

  // A second  should cancel the existing trial and set up the new one.
  EXPECT_CALL(*http_request(), Stop());
  EXPECT_CALL(*https_request(), Stop());
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, 10 * 1000)).Times(1);
  EXPECT_TRUE(portal_detector()->StartAfterDelay(props, 10));
}

TEST_F(PortalDetectorTest, AttemptCount) {
  EXPECT_FALSE(portal_detector()->IsInProgress());
  // Expect the PortalDetector to immediately post a task for the each attempt.
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, _)).Times(4);
  PortalDetector::Properties props =
      PortalDetector::Properties(kHttpUrl, kHttpsUrl, kFallbackHttpUrls);
  EXPECT_TRUE(StartPortalRequest(props, 0));
  EXPECT_EQ(portal_detector()->http_url_string_, kHttpUrl);

  EXPECT_CALL(
      callback_target(),
      ResultCallback(
          IsResult(PortalDetector::Result(PortalDetector::Phase::kDNS,
                                          PortalDetector::Status::kFailure,
                                          kNumAttempts)),
          IsResult(PortalDetector::Result(PortalDetector::Phase::kContent,
                                          PortalDetector::Status::kFailure))))
      .Times(3);

  // Expect the PortalDetector to stop the trial after
  // the final attempt.
  EXPECT_CALL(*http_request(), Stop()).Times(7);
  EXPECT_CALL(*https_request(), Stop()).Times(7);

  int init_delay = 3;
  for (int i = 0; i < 3; i++) {
    int delay = portal_detector()->AdjustStartDelay(init_delay);
    EXPECT_EQ(delay, init_delay);
    portal_detector()->StartAfterDelay(props, delay);
    EXPECT_NE(portal_detector()->http_url_string_, kHttpUrl);
    AdvanceTime(delay * 1000);
    PortalDetector::Result r = PortalDetector::GetPortalResultForRequestResult(
        HttpRequest::kResultDNSFailure);
    PortalDetector::Result https_result = PortalDetector::Result(
        PortalDetector::Phase::kContent, PortalDetector::Status::kFailure);
    portal_detector()->CompleteAttempt(r, https_result);
    init_delay *= 2;
  }
  portal_detector()->Stop();
  ExpectReset();
}

TEST_F(PortalDetectorTest, RequestSuccess) {
  StartAttempt();

  // HTTPS probe does not trigger anything (for now)
  PortalDetector::Result success_result =
      PortalDetector::Result(PortalDetector::Phase::kContent,
                             PortalDetector::Status::kSuccess, kNumAttempts);
  EXPECT_CALL(callback_target(), ResultCallback(IsResult(success_result),
                                                IsResult(success_result)))
      .Times(0);
  EXPECT_CALL(*http_request(), Stop()).Times(0);
  EXPECT_CALL(*https_request(), Stop()).Times(0);
  ExpectRequestSuccessWithStatus(204, false);

  EXPECT_CALL(callback_target(), ResultCallback(IsResult(success_result),
                                                IsResult(success_result)));
  EXPECT_CALL(*http_request(), Stop()).Times(1);
  EXPECT_CALL(*https_request(), Stop()).Times(1);
  EXPECT_CALL(metrics(), NotifyPortalDetectionMultiProbeResult(_, _));
  ExpectRequestSuccessWithStatus(204, true);
}

TEST_F(PortalDetectorTest, RequestHTTPFailureHTTPSSuccess) {
  StartAttempt();

  // HTTPS probe does not trigger anything (for now)
  PortalDetector::Result failure_result =
      PortalDetector::Result(PortalDetector::Phase::kContent,
                             PortalDetector::Status::kFailure, kNumAttempts);
  PortalDetector::Result success_result =
      PortalDetector::Result(PortalDetector::Phase::kContent,
                             PortalDetector::Status::kSuccess, kNumAttempts);
  EXPECT_CALL(callback_target(), ResultCallback(IsResult(failure_result),
                                                IsResult(success_result)))
      .Times(0);
  EXPECT_CALL(*http_request(), Stop()).Times(0);
  EXPECT_CALL(*https_request(), Stop()).Times(0);
  ExpectRequestSuccessWithStatus(123, true);

  EXPECT_CALL(callback_target(), ResultCallback(IsResult(failure_result),
                                                IsResult(success_result)));
  EXPECT_CALL(*http_request(), Stop()).Times(1);
  EXPECT_CALL(*https_request(), Stop()).Times(1);
  EXPECT_CALL(metrics(), NotifyPortalDetectionMultiProbeResult(_, _));
  ExpectRequestSuccessWithStatus(204, false);
}

TEST_F(PortalDetectorTest, RequestFail) {
  StartAttempt();

  // HTTPS probe does not trigger anything (for now)
  PortalDetector::Result failure_result =
      PortalDetector::Result(PortalDetector::Phase::kContent,
                             PortalDetector::Status::kFailure, kNumAttempts);

  EXPECT_CALL(callback_target(), ResultCallback(IsResult(failure_result),
                                                IsResult(failure_result)))
      .Times(0);
  EXPECT_CALL(*http_request(), Stop()).Times(0);
  EXPECT_CALL(*https_request(), Stop()).Times(0);
  ExpectRequestSuccessWithStatus(123, false);

  EXPECT_CALL(callback_target(), ResultCallback(IsResult(failure_result),
                                                IsResult(failure_result)));
  EXPECT_CALL(*http_request(), Stop()).Times(1);
  EXPECT_CALL(*https_request(), Stop()).Times(1);
  EXPECT_CALL(metrics(), NotifyPortalDetectionMultiProbeResult(_, _));
  ExpectRequestSuccessWithStatus(123, true);
}

TEST_F(PortalDetectorTest, RequestRedirect) {
  StartAttempt();

  PortalDetector::Result redirect_result = PortalDetector::Result(
      PortalDetector::Phase::kContent, PortalDetector::Status::kRedirect);
  redirect_result.redirect_url_string = kHttpUrl;
  redirect_result.probe_url_string = kHttpUrl;
  PortalDetector::Result failure_result = PortalDetector::Result(
      PortalDetector::Phase::kContent, PortalDetector::Status::kFailure);
  EXPECT_CALL(callback_target(), ResultCallback(IsResult(redirect_result),
                                                IsResult(failure_result)))
      .Times(0);
  EXPECT_CALL(*http_request(), Stop()).Times(0);
  EXPECT_CALL(*https_request(), Stop()).Times(0);
  ExpectRequestSuccessWithStatus(123, false);

  EXPECT_CALL(callback_target(), ResultCallback(IsResult(redirect_result),
                                                IsResult(failure_result)));
  EXPECT_CALL(*http_request(), Stop()).Times(1);
  EXPECT_CALL(*https_request(), Stop()).Times(1);
  EXPECT_CALL(*brillo_connection(), GetResponseHeader("Location"))
      .WillOnce(Return(kHttpUrl));
  EXPECT_CALL(metrics(), NotifyPortalDetectionMultiProbeResult(_, _));
  ExpectRequestSuccessWithStatus(302, true);
}

struct ResultMapping {
  ResultMapping() : http_result(HttpRequest::kResultUnknown), portal_result() {}
  ResultMapping(HttpRequest::Result in_http_result,
                const PortalDetector::Result& in_portal_result)
      : http_result(in_http_result), portal_result(in_portal_result) {}
  HttpRequest::Result http_result;
  PortalDetector::Result portal_result;
};

class PortalDetectorResultMappingTest
    : public testing::TestWithParam<ResultMapping> {};

TEST_P(PortalDetectorResultMappingTest, MapResult) {
  PortalDetector::Result trial_result =
      PortalDetector::GetPortalResultForRequestResult(GetParam().http_result);
  EXPECT_EQ(trial_result.phase, GetParam().portal_result.phase);
  EXPECT_EQ(trial_result.status, GetParam().portal_result.status);
}

INSTANTIATE_TEST_SUITE_P(
    TrialResultMappingTest,
    PortalDetectorResultMappingTest,
    ::testing::Values(
        ResultMapping(HttpRequest::kResultUnknown,
                      PortalDetector::Result(PortalDetector::Phase::kUnknown,
                                             PortalDetector::Status::kFailure)),
        ResultMapping(HttpRequest::kResultInvalidInput,
                      PortalDetector::Result(PortalDetector::Phase::kUnknown,
                                             PortalDetector::Status::kFailure)),
        ResultMapping(HttpRequest::kResultInProgress,
                      PortalDetector::Result(PortalDetector::Phase::kUnknown,
                                             PortalDetector::Status::kFailure)),
        ResultMapping(HttpRequest::kResultDNSFailure,
                      PortalDetector::Result(PortalDetector::Phase::kDNS,
                                             PortalDetector::Status::kFailure)),
        ResultMapping(HttpRequest::kResultDNSTimeout,
                      PortalDetector::Result(PortalDetector::Phase::kDNS,
                                             PortalDetector::Status::kTimeout)),
        ResultMapping(HttpRequest::kResultConnectionFailure,
                      PortalDetector::Result(PortalDetector::Phase::kConnection,
                                             PortalDetector::Status::kFailure)),
        ResultMapping(HttpRequest::kResultHTTPFailure,
                      PortalDetector::Result(PortalDetector::Phase::kHTTP,
                                             PortalDetector::Status::kFailure)),
        ResultMapping(HttpRequest::kResultHTTPTimeout,
                      PortalDetector::Result(PortalDetector::Phase::kHTTP,
                                             PortalDetector::Status::kTimeout)),
        ResultMapping(
            HttpRequest::kResultSuccess,
            PortalDetector::Result(PortalDetector::Phase::kContent,
                                   PortalDetector::Status::kFailure))));

}  // namespace shill
