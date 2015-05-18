// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/notification/xmpp_channel.h"

#include <queue>

#include <base/test/simple_test_clock.h>
#include <chromeos/bind_lambda.h>
#include <chromeos/streams/fake_stream.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace buffet {

using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::_;

namespace {

constexpr char kAccountName[] = "Account@Name";
constexpr char kAccessToken[] = "AccessToken";

constexpr char kStartStreamResponse[] =
    "<stream:stream from=\"clouddevices.gserviceaccount.com\" "
    "id=\"0CCF520913ABA04B\" version=\"1.0\" "
    "xmlns:stream=\"http://etherx.jabber.org/streams\" "
    "xmlns=\"jabber:client\">"
    "<stream:features><starttls xmlns=\"urn:ietf:params:xml:ns:xmpp-tls\">"
    "<required/></starttls><mechanisms "
    "xmlns=\"urn:ietf:params:xml:ns:xmpp-sasl\"><mechanism>X-OAUTH2</mechanism>"
    "<mechanism>X-GOOGLE-TOKEN</mechanism></mechanisms></stream:features>";
constexpr char kAuthenticationSucceededResponse[] =
    "<success xmlns=\"urn:ietf:params:xml:ns:xmpp-sasl\"/>";
constexpr char kAuthenticationFailedResponse[] =
    "<failure xmlns=\"urn:ietf:params:xml:ns:xmpp-sasl\"><not-authorized/>"
    "</failure></stream:stream>";
constexpr char kRestartStreamResponse[] =
    "<stream:features><bind xmlns=\"urn:ietf:params:xml:ns:xmpp-bind\"/>"
    "<session xmlns=\"urn:ietf:params:xml:ns:xmpp-session\"/>"
    "</stream:features>";
constexpr char kBindResponse[] =
    "<iq id=\"0\" type=\"result\">"
    "<bind xmlns=\"urn:ietf:params:xml:ns:xmpp-bind\">"
    "<jid>110cc78f78d7032cc7bf2c6e14c1fa7d@clouddevices.gserviceaccount.com"
    "/19853128</jid></bind></iq>";
constexpr char kSessionResponse[] =
    "<iq type=\"result\" id=\"1\"/>";
constexpr char kSubscribedResponse[] =
    "<iq to=\""
    "110cc78f78d7032cc7bf2c6e14c1fa7d@clouddevices.gserviceaccount.com/"
    "19853128\" from=\""
    "110cc78f78d7032cc7bf2c6e14c1fa7d@clouddevices.gserviceaccount.com\" "
    "id=\"pushsubscribe1\" type=\"result\"/>";
constexpr char kStartStreamMessage[] =
    "<stream:stream to='clouddevices.gserviceaccount.com' "
    "xmlns:stream='http://etherx.jabber.org/streams' xml:lang='*' "
    "version='1.0' xmlns='jabber:client'>";
constexpr char kAuthenticationMessage[] =
    "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='X-OAUTH2' "
    "auth:service='oauth2' auth:allow-non-google-login='true' "
    "auth:client-uses-full-bind-result='true' "
    "xmlns:auth='http://www.google.com/talk/protocol/auth'>"
    "AEFjY291bnRATmFtZQBBY2Nlc3NUb2tlbg==</auth>";
constexpr char kBindMessage[] =
    "<iq type='set' id='0'><bind "
    "xmlns='urn:ietf:params:xml:ns:xmpp-bind'/></iq>";
constexpr char kSessionMessage[] =
    "<iq type='set' id='1'><session "
    "xmlns='urn:ietf:params:xml:ns:xmpp-session'/></iq>";
constexpr char kSubscribeMessage[] =
    "<iq type='set' to='Account@Name' id='pushsubscribe1'>"
    "<subscribe xmlns='google:push'><item channel='cloud_devices' from=''/>"
    "</subscribe></iq>";
}  // namespace

// Mock-like task runner that allow the tests to inspect the calls to
// TaskRunner::PostDelayedTask and verify the delays.
class TestTaskRunner : public base::TaskRunner {
 public:
  MOCK_METHOD3(PostDelayedTask, bool(const tracked_objects::Location&,
                                     const base::Closure&,
                                     base::TimeDelta));
  bool RunsTasksOnCurrentThread() const { return true; }
};

class FakeXmppChannel : public XmppChannel {
 public:
  FakeXmppChannel(const scoped_refptr<base::TaskRunner>& task_runner,
                  base::Clock* clock)
      : XmppChannel{kAccountName, kAccessToken, task_runner},
        fake_stream_{chromeos::Stream::AccessMode::READ_WRITE, task_runner,
                     clock} {}

  XmppState state() const { return state_; }
  void set_state(XmppState state) { state_ = state; }

  void Connect(const std::string& host, uint16_t port,
               const base::Closure& callback) override {
    stream_ = &fake_stream_;
    callback.Run();
  }

  chromeos::FakeStream fake_stream_;
};

class XmppChannelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    task_runner_ = new TestTaskRunner;

    auto callback = [this](const tracked_objects::Location& from_here,
                           const base::Closure& task,
                           base::TimeDelta delay) -> bool {
      clock_.Advance(delay);
      message_queue_.push(task);
      return true;
    };

    EXPECT_CALL(*task_runner_, PostDelayedTask(_, _, _))
        .WillRepeatedly(testing::Invoke(callback));

    xmpp_client_.reset(new FakeXmppChannel{task_runner_, &clock_});
    clock_.SetNow(base::Time::Now());
  }

  void StartStream() {
    xmpp_client_->fake_stream_.ExpectWritePacketString({}, kStartStreamMessage);
    xmpp_client_->fake_stream_.AddReadPacketString({}, kStartStreamResponse);
    xmpp_client_->fake_stream_.ExpectWritePacketString({},
                                                       kAuthenticationMessage);
    xmpp_client_->Start(nullptr);
    RunTasks(4);
  }

  void StartWithState(XmppChannel::XmppState state) {
    StartStream();
    xmpp_client_->set_state(state);
  }

  void RunTasks(size_t count) {
    while (count > 0) {
      ASSERT_FALSE(message_queue_.empty());
      base::Closure task = message_queue_.front();
      message_queue_.pop();
      task.Run();
      count--;
    }
  }
  std::unique_ptr<FakeXmppChannel> xmpp_client_;
  base::SimpleTestClock clock_;
  scoped_refptr<TestTaskRunner> task_runner_;
  std::queue<base::Closure> message_queue_;
};

TEST_F(XmppChannelTest, StartStream) {
  EXPECT_EQ(XmppChannel::XmppState::kNotStarted, xmpp_client_->state());
  xmpp_client_->fake_stream_.ExpectWritePacketString({}, kStartStreamMessage);
  xmpp_client_->Start(nullptr);
  RunTasks(1);
  EXPECT_EQ(XmppChannel::XmppState::kStarted, xmpp_client_->state());
}

TEST_F(XmppChannelTest, HandleStartedResponse) {
  StartStream();
  EXPECT_EQ(XmppChannel::XmppState::kAuthenticationStarted,
            xmpp_client_->state());
}

TEST_F(XmppChannelTest, HandleAuthenticationSucceededResponse) {
  StartWithState(XmppChannel::XmppState::kAuthenticationStarted);
  xmpp_client_->fake_stream_.AddReadPacketString(
      {}, kAuthenticationSucceededResponse);
  xmpp_client_->fake_stream_.ExpectWritePacketString({}, kStartStreamMessage);
  RunTasks(4);
  EXPECT_EQ(XmppChannel::XmppState::kStreamRestartedPostAuthentication,
            xmpp_client_->state());
}

TEST_F(XmppChannelTest, HandleAuthenticationFailedResponse) {
  StartWithState(XmppChannel::XmppState::kAuthenticationStarted);
  xmpp_client_->fake_stream_.AddReadPacketString(
      {}, kAuthenticationFailedResponse);
  RunTasks(3);
  EXPECT_EQ(XmppChannel::XmppState::kAuthenticationFailed,
            xmpp_client_->state());
}

TEST_F(XmppChannelTest, HandleStreamRestartedResponse) {
  StartWithState(XmppChannel::XmppState::kStreamRestartedPostAuthentication);
  xmpp_client_->fake_stream_.AddReadPacketString({}, kRestartStreamResponse);
  xmpp_client_->fake_stream_.ExpectWritePacketString({}, kBindMessage);
  RunTasks(4);
  EXPECT_EQ(XmppChannel::XmppState::kBindSent,
            xmpp_client_->state());
}

TEST_F(XmppChannelTest, HandleBindResponse) {
  StartWithState(XmppChannel::XmppState::kBindSent);
  xmpp_client_->fake_stream_.AddReadPacketString({}, kBindResponse);
  xmpp_client_->fake_stream_.ExpectWritePacketString({}, kSessionMessage);
  RunTasks(4);
  EXPECT_EQ(XmppChannel::XmppState::kSessionStarted,
            xmpp_client_->state());
}

TEST_F(XmppChannelTest, HandleSessionResponse) {
  StartWithState(XmppChannel::XmppState::kSessionStarted);
  xmpp_client_->fake_stream_.AddReadPacketString({}, kSessionResponse);
  xmpp_client_->fake_stream_.ExpectWritePacketString({}, kSubscribeMessage);
  RunTasks(4);
  EXPECT_EQ(XmppChannel::XmppState::kSubscribeStarted,
            xmpp_client_->state());
}

TEST_F(XmppChannelTest, HandleSubscribeResponse) {
  StartWithState(XmppChannel::XmppState::kSubscribeStarted);
  xmpp_client_->fake_stream_.AddReadPacketString({}, kSubscribedResponse);
  RunTasks(3);
  EXPECT_EQ(XmppChannel::XmppState::kSubscribed,
            xmpp_client_->state());
}

}  // namespace buffet
