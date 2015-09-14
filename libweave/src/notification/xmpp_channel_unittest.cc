// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libweave/src/notification/xmpp_channel.h"

#include <algorithm>
#include <queue>

#include <gtest/gtest.h>
#include <weave/test/mock_task_runner.h>

#include "libweave/src/bind_lambda.h"

using testing::StrictMock;

namespace weave {

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
constexpr char kTlsStreamResponse[] =
    "<stream:features><mechanisms xmlns=\"urn:ietf:params:xml:ns:xmpp-sasl\">"
    "<mechanism>X-OAUTH2</mechanism>"
    "<mechanism>X-GOOGLE-TOKEN</mechanism></mechanisms></stream:features>";
constexpr char kAuthenticationSucceededResponse[] =
    "<success xmlns=\"urn:ietf:params:xml:ns:xmpp-sasl\"/>";
constexpr char kAuthenticationFailedResponse[] =
    "<failure xmlns=\"urn:ietf:params:xml:ns:xmpp-sasl\"><not-authorized/>"
    "</failure>";
constexpr char kRestartStreamResponse[] =
    "<stream:features><bind xmlns=\"urn:ietf:params:xml:ns:xmpp-bind\"/>"
    "<session xmlns=\"urn:ietf:params:xml:ns:xmpp-session\"/>"
    "</stream:features>";
constexpr char kBindResponse[] =
    "<iq id=\"1\" type=\"result\">"
    "<bind xmlns=\"urn:ietf:params:xml:ns:xmpp-bind\">"
    "<jid>110cc78f78d7032cc7bf2c6e14c1fa7d@clouddevices.gserviceaccount.com"
    "/19853128</jid></bind></iq>";
constexpr char kSessionResponse[] = "<iq type=\"result\" id=\"2\"/>";
constexpr char kSubscribedResponse[] =
    "<iq to=\""
    "110cc78f78d7032cc7bf2c6e14c1fa7d@clouddevices.gserviceaccount.com/"
    "19853128\" from=\""
    "110cc78f78d7032cc7bf2c6e14c1fa7d@clouddevices.gserviceaccount.com\" "
    "id=\"3\" type=\"result\"/>";
constexpr char kStartStreamMessage[] =
    "<stream:stream to='clouddevices.gserviceaccount.com' "
    "xmlns:stream='http://etherx.jabber.org/streams' xml:lang='*' "
    "version='1.0' xmlns='jabber:client'>";
constexpr char kStartTlsMessage[] =
    "<starttls xmlns='urn:ietf:params:xml:ns:xmpp-tls'/>";
constexpr char kAuthenticationMessage[] =
    "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='X-OAUTH2' "
    "auth:service='oauth2' auth:allow-non-google-login='true' "
    "auth:client-uses-full-bind-result='true' "
    "xmlns:auth='http://www.google.com/talk/protocol/auth'>"
    "AEFjY291bnRATmFtZQBBY2Nlc3NUb2tlbg==</auth>";
constexpr char kBindMessage[] =
    "<iq id='1' type='set'><bind "
    "xmlns='urn:ietf:params:xml:ns:xmpp-bind'/></iq>";
constexpr char kSessionMessage[] =
    "<iq id='2' type='set'><session "
    "xmlns='urn:ietf:params:xml:ns:xmpp-session'/></iq>";
constexpr char kSubscribeMessage[] =
    "<iq id='3' type='set' to='Account@Name'>"
    "<subscribe xmlns='google:push'><item channel='cloud_devices' from=''/>"
    "</subscribe></iq>";

}  // namespace

class FakeStream : public Stream {
 public:
  explicit FakeStream(TaskRunner* task_runner) : task_runner_{task_runner} {}

  bool FlushBlocking(ErrorPtr* error) override { return true; }

  bool CloseBlocking(ErrorPtr* error) override { return true; }

  void CancelPendingAsyncOperations() override {}

  void ExpectWritePacketString(base::TimeDelta, const std::string& data) {
    write_data_ += data;
  }

  void AddReadPacketString(base::TimeDelta, const std::string& data) {
    read_data_ += data;
  }

  bool ReadAsync(void* buffer,
                 size_t size_to_read,
                 const base::Callback<void(size_t)>& success_callback,
                 const base::Callback<void(const Error*)>& error_callback,
                 ErrorPtr* error) override {
    if (read_data_.empty()) {
      task_runner_->PostDelayedTask(
          FROM_HERE, base::Bind(base::IgnoreResult(&FakeStream::ReadAsync),
                                base::Unretained(this), buffer, size_to_read,
                                success_callback, error_callback, nullptr),
          base::TimeDelta::FromSeconds(0));
      return true;
    }
    size_t size = std::min(size_to_read, read_data_.size());
    memcpy(buffer, read_data_.data(), size);
    read_data_ = read_data_.substr(size);
    task_runner_->PostDelayedTask(FROM_HERE, base::Bind(success_callback, size),
                                  base::TimeDelta::FromSeconds(0));
    return true;
  }

  bool WriteAllAsync(const void* buffer,
                     size_t size_to_write,
                     const base::Closure& success_callback,
                     const base::Callback<void(const Error*)>& error_callback,
                     ErrorPtr* error) override {
    size_t size = std::min(size_to_write, write_data_.size());
    EXPECT_EQ(
        write_data_.substr(0, size),
        std::string(reinterpret_cast<const char*>(buffer), size_to_write));
    write_data_ = write_data_.substr(size);
    task_runner_->PostDelayedTask(FROM_HERE, success_callback,
                                  base::TimeDelta::FromSeconds(0));
    return true;
  }

 private:
  TaskRunner* task_runner_{nullptr};
  std::string write_data_;
  std::string read_data_;
};

class FakeXmppChannel : public XmppChannel {
 public:
  explicit FakeXmppChannel(TaskRunner* task_runner)
      : XmppChannel{kAccountName, kAccessToken, task_runner, nullptr},
        fake_stream_{task_runner} {}

  XmppState state() const { return state_; }
  void set_state(XmppState state) { state_ = state; }

  void Connect(const std::string& host,
               uint16_t port,
               const base::Closure& callback) override {
    set_state(XmppState::kConnecting);
    stream_ = &fake_stream_;
    callback.Run();
  }

  void SchedulePing(base::TimeDelta interval,
                    base::TimeDelta timeout) override {}

  FakeStream fake_stream_;
};

class XmppChannelTest : public ::testing::Test {
 protected:
  void StartStream() {
    xmpp_client_.fake_stream_.ExpectWritePacketString({}, kStartStreamMessage);
    xmpp_client_.fake_stream_.AddReadPacketString({}, kStartStreamResponse);
    xmpp_client_.fake_stream_.ExpectWritePacketString({}, kStartTlsMessage);
    xmpp_client_.Start(nullptr);
    RunUntil(XmppChannel::XmppState::kTlsStarted);
  }

  void StartWithState(XmppChannel::XmppState state) {
    StartStream();
    xmpp_client_.set_state(state);
  }

  void RunUntil(XmppChannel::XmppState st) {
    for (size_t n = 15; n && xmpp_client_.state() != st; --n)
      task_runner_.RunOnce();
    EXPECT_EQ(st, xmpp_client_.state());
  }

  StrictMock<test::MockTaskRunner> task_runner_;
  FakeXmppChannel xmpp_client_{&task_runner_};
};

TEST_F(XmppChannelTest, StartStream) {
  EXPECT_EQ(XmppChannel::XmppState::kNotStarted, xmpp_client_.state());
  xmpp_client_.fake_stream_.ExpectWritePacketString({}, kStartStreamMessage);
  xmpp_client_.Start(nullptr);
  RunUntil(XmppChannel::XmppState::kConnected);
}

TEST_F(XmppChannelTest, HandleStartedResponse) {
  StartStream();
}

TEST_F(XmppChannelTest, HandleTLSCompleted) {
  StartWithState(XmppChannel::XmppState::kTlsCompleted);
  xmpp_client_.fake_stream_.AddReadPacketString({}, kTlsStreamResponse);
  xmpp_client_.fake_stream_.ExpectWritePacketString({}, kAuthenticationMessage);
  RunUntil(XmppChannel::XmppState::kAuthenticationStarted);
}

TEST_F(XmppChannelTest, HandleAuthenticationSucceededResponse) {
  StartWithState(XmppChannel::XmppState::kAuthenticationStarted);
  xmpp_client_.fake_stream_.AddReadPacketString(
      {}, kAuthenticationSucceededResponse);
  xmpp_client_.fake_stream_.ExpectWritePacketString({}, kStartStreamMessage);
  RunUntil(XmppChannel::XmppState::kStreamRestartedPostAuthentication);
}

TEST_F(XmppChannelTest, HandleAuthenticationFailedResponse) {
  StartWithState(XmppChannel::XmppState::kAuthenticationStarted);
  xmpp_client_.fake_stream_.AddReadPacketString({},
                                                kAuthenticationFailedResponse);
  RunUntil(XmppChannel::XmppState::kAuthenticationFailed);
}

TEST_F(XmppChannelTest, HandleStreamRestartedResponse) {
  StartWithState(XmppChannel::XmppState::kStreamRestartedPostAuthentication);
  xmpp_client_.fake_stream_.AddReadPacketString({}, kRestartStreamResponse);
  xmpp_client_.fake_stream_.ExpectWritePacketString({}, kBindMessage);
  RunUntil(XmppChannel::XmppState::kBindSent);
  EXPECT_TRUE(xmpp_client_.jid().empty());

  xmpp_client_.fake_stream_.AddReadPacketString({}, kBindResponse);
  xmpp_client_.fake_stream_.ExpectWritePacketString({}, kSessionMessage);
  RunUntil(XmppChannel::XmppState::kSessionStarted);
  EXPECT_EQ(
      "110cc78f78d7032cc7bf2c6e14c1fa7d@clouddevices.gserviceaccount.com"
      "/19853128",
      xmpp_client_.jid());

  xmpp_client_.fake_stream_.AddReadPacketString({}, kSessionResponse);
  xmpp_client_.fake_stream_.ExpectWritePacketString({}, kSubscribeMessage);
  RunUntil(XmppChannel::XmppState::kSubscribeStarted);

  xmpp_client_.fake_stream_.AddReadPacketString({}, kSubscribedResponse);
  RunUntil(XmppChannel::XmppState::kSubscribed);
}

}  // namespace weave
