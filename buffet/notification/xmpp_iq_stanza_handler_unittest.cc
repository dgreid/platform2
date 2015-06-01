// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/notification/xmpp_iq_stanza_handler.h"

#include <map>
#include <memory>

#include <chromeos/bind_lambda.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "buffet/notification/xml_node.h"
#include "buffet/notification/xmpp_channel.h"
#include "buffet/notification/xmpp_stream_parser.h"

using testing::Invoke;
using testing::Return;
using testing::_;

namespace buffet {
namespace {

// Mock-like task runner that allow the tests to inspect the calls to
// TaskRunner::PostDelayedTask and verify the delays.
class TestTaskRunner : public base::SingleThreadTaskRunner {
 public:
  TestTaskRunner() = default;

  MOCK_METHOD3(PostDelayedTask, bool(const tracked_objects::Location&,
                                     const base::Closure&,
                                     base::TimeDelta));
  MOCK_METHOD3(PostNonNestableDelayedTask,
               bool(const tracked_objects::Location&,
                    const base::Closure&,
                    base::TimeDelta));

  bool RunsTasksOnCurrentThread() const { return true; }

 private:
  DISALLOW_COPY_AND_ASSIGN(TestTaskRunner);
};

class MockXmppChannelInterface : public XmppChannelInterface {
 public:
  MockXmppChannelInterface() = default;

  MOCK_METHOD1(SendMessage, void(const std::string&));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockXmppChannelInterface);
};

// Simple class that allows to parse XML from string to XmlNode.
class XmlParser : public XmppStreamParser::Delegate {
 public:
  std::unique_ptr<XmlNode> Parse(const std::string& xml) {
    parser_.ParseData(xml);
    return std::move(node_);
  }

 private:
  // Overrides from XmppStreamParser::Delegate.
  void OnStreamStart(const std::string& node_name,
                     std::map<std::string, std::string> attributes) override {
    node_.reset(new XmlNode{node_name, std::move(attributes)});
  }

  void OnStreamEnd(const std::string& node_name) override {}

  void OnStanza(std::unique_ptr<XmlNode> stanza) override {
    node_->AddChild(std::move(stanza));
  }

  std::unique_ptr<XmlNode> node_;
  XmppStreamParser parser_{this};
};

class MockResponseReceiver {
 public:
  MOCK_METHOD2(OnResponse, void(int id, const std::string&));

  IqStanzaHandler::ResponseCallback callback(int id) {
    return base::Bind(&MockResponseReceiver::OnResponseCallback,
                      base::Unretained(this), id);
  }

 private:
  void OnResponseCallback(int id, std::unique_ptr<XmlNode> response) {
    OnResponse(id, response->children().front()->name());
  }
};

// Functor to call the |task| immediately.
struct TaskInvoker {
  bool operator()(const tracked_objects::Location& location,
                  const base::Closure& task,
                  base::TimeDelta delay) {
    task.Run();
    return true;
  }
};

}  // anonymous namespace

class IqStanzaHandlerTest : public testing::Test {
 public:
  void SetUp() override {
    task_runner_ = new TestTaskRunner;
    iq_stanza_handler_.reset(
        new IqStanzaHandler{&mock_xmpp_channel_, task_runner_});
  }

  testing::StrictMock<MockXmppChannelInterface> mock_xmpp_channel_;
  scoped_refptr<TestTaskRunner> task_runner_;
  std::unique_ptr<IqStanzaHandler> iq_stanza_handler_;
  MockResponseReceiver receiver_;
  TaskInvoker task_invoker_;
};

TEST_F(IqStanzaHandlerTest, SendRequest) {
  EXPECT_CALL(*task_runner_, PostDelayedTask(_, _, _))
      .WillRepeatedly(Return(true));

  std::string expected_msg = "<iq id='1' type='set'><body/></iq>";
  EXPECT_CALL(mock_xmpp_channel_, SendMessage(expected_msg)).Times(1);
  iq_stanza_handler_->SendRequest("set", "", "", "<body/>", {}, {});

  expected_msg = "<iq id='2' type='get'><body/></iq>";
  EXPECT_CALL(mock_xmpp_channel_, SendMessage(expected_msg)).Times(1);
  iq_stanza_handler_->SendRequest("get", "", "", "<body/>", {}, {});

  expected_msg = "<iq id='3' type='query' from='foo@bar'><body/></iq>";
  EXPECT_CALL(mock_xmpp_channel_, SendMessage(expected_msg)).Times(1);
  iq_stanza_handler_->SendRequest("query", "foo@bar", "", "<body/>", {}, {});

  expected_msg = "<iq id='4' type='query' to='foo@bar'><body/></iq>";
  EXPECT_CALL(mock_xmpp_channel_, SendMessage(expected_msg)).Times(1);
  iq_stanza_handler_->SendRequest("query", "", "foo@bar", "<body/>", {}, {});

  expected_msg = "<iq id='5' type='query' from='foo@bar' to='baz'><body/></iq>";
  EXPECT_CALL(mock_xmpp_channel_, SendMessage(expected_msg)).Times(1);
  iq_stanza_handler_->SendRequest("query", "foo@bar", "baz", "<body/>", {}, {});
}

TEST_F(IqStanzaHandlerTest, UnsupportedIqRequest) {
  // Server IQ requests are not supported for now. Expect an error response.
  std::string expected_msg =
      "<iq id='1' type='error'><error type='modify'>"
      "<feature-not-implemented xmlns='urn:ietf:params:xml:ns:xmpp-stanzas'/>"
      "</error></iq>";
  EXPECT_CALL(mock_xmpp_channel_, SendMessage(expected_msg)).Times(1);
  auto request = XmlParser{}.Parse("<iq id='1' type='set'><foo/></iq>");
  EXPECT_TRUE(iq_stanza_handler_->HandleIqStanza(std::move(request)));
}

TEST_F(IqStanzaHandlerTest, UnknownResponseId) {
  // No requests with ID=100 have been previously sent.
  auto request = XmlParser{}.Parse("<iq id='100' type='result'><foo/></iq>");
  EXPECT_TRUE(iq_stanza_handler_->HandleIqStanza(std::move(request)));
}

TEST_F(IqStanzaHandlerTest, SequentialResponses) {
  EXPECT_CALL(*task_runner_, PostDelayedTask(_, _, _))
      .Times(2).WillRepeatedly(Return(true));

  EXPECT_CALL(mock_xmpp_channel_, SendMessage(_)).Times(2);
  iq_stanza_handler_->SendRequest("set", "", "", "<body/>",
                                  receiver_.callback(1), {});
  iq_stanza_handler_->SendRequest("get", "", "", "<body/>",
                                  receiver_.callback(2), {});

  EXPECT_CALL(*task_runner_, PostDelayedTask(_, _, _))
      .Times(2).WillRepeatedly(Invoke(task_invoker_));

  EXPECT_CALL(receiver_, OnResponse(1, "foo"));
  auto request = XmlParser{}.Parse("<iq id='1' type='result'><foo/></iq>");
  EXPECT_TRUE(iq_stanza_handler_->HandleIqStanza(std::move(request)));

  EXPECT_CALL(receiver_, OnResponse(2, "bar"));
  request = XmlParser{}.Parse("<iq id='2' type='result'><bar/></iq>");
  EXPECT_TRUE(iq_stanza_handler_->HandleIqStanza(std::move(request)));
}

TEST_F(IqStanzaHandlerTest, OutOfOrderResponses) {
  EXPECT_CALL(*task_runner_, PostDelayedTask(_, _, _))
      .Times(2).WillRepeatedly(Return(true));

  EXPECT_CALL(mock_xmpp_channel_, SendMessage(_)).Times(2);
  iq_stanza_handler_->SendRequest("set", "", "", "<body/>",
                                  receiver_.callback(1), {});
  iq_stanza_handler_->SendRequest("get", "", "", "<body/>",
                                  receiver_.callback(2), {});

  EXPECT_CALL(*task_runner_, PostDelayedTask(_, _, _))
      .Times(2).WillRepeatedly(Invoke(task_invoker_));

  EXPECT_CALL(receiver_, OnResponse(2, "bar"));
  auto request = XmlParser{}.Parse("<iq id='2' type='result'><bar/></iq>");
  EXPECT_TRUE(iq_stanza_handler_->HandleIqStanza(std::move(request)));

  EXPECT_CALL(receiver_, OnResponse(1, "foo"));
  request = XmlParser{}.Parse("<iq id='1' type='result'><foo/></iq>");
  EXPECT_TRUE(iq_stanza_handler_->HandleIqStanza(std::move(request)));
}

TEST_F(IqStanzaHandlerTest, RequestTimeout) {
  EXPECT_CALL(*task_runner_, PostDelayedTask(_, _, _))
      .WillOnce(Invoke(task_invoker_));

  bool called = false;
  auto on_timeout = [&called]() { called = true; };

  EXPECT_CALL(mock_xmpp_channel_, SendMessage(_)).Times(1);
  EXPECT_FALSE(called);
  iq_stanza_handler_->SendRequest("set", "", "", "<body/>", {},
                                  base::Bind(on_timeout));
  EXPECT_TRUE(called);
}

}  // namespace buffet
