// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/notification/xmpp_channel.h"

#include <string>

#include <base/bind.h>
#include <chromeos/backoff_entry.h>
#include <chromeos/data_encoding.h>
#include <chromeos/streams/file_stream.h>
#include <chromeos/streams/tls_stream.h>

#include "buffet/notification/notification_delegate.h"
#include "buffet/notification/notification_parser.h"
#include "buffet/notification/xml_node.h"
#include "buffet/utils.h"

namespace buffet {

namespace {

std::string BuildXmppStartStreamCommand() {
  return "<stream:stream to='clouddevices.gserviceaccount.com' "
      "xmlns:stream='http://etherx.jabber.org/streams' "
      "xml:lang='*' version='1.0' xmlns='jabber:client'>";
}

std::string BuildXmppAuthenticateCommand(
    const std::string& account, const std::string& token) {
  chromeos::Blob credentials;
  credentials.push_back(0);
  credentials.insert(credentials.end(), account.begin(), account.end());
  credentials.push_back(0);
  credentials.insert(credentials.end(), token.begin(), token.end());
  std::string msg = "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' "
      "mechanism='X-OAUTH2' auth:service='oauth2' "
      "auth:allow-non-google-login='true' "
      "auth:client-uses-full-bind-result='true' "
      "xmlns:auth='http://www.google.com/talk/protocol/auth'>" +
      chromeos::data_encoding::Base64Encode(credentials) +
      "</auth>";
  return msg;
}

std::string BuildXmppBindCommand() {
  return "<iq type='set' id='0'>"
      "<bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'/></iq>";
}

std::string BuildXmppStartSessionCommand() {
  return "<iq type='set' id='1'>"
      "<session xmlns='urn:ietf:params:xml:ns:xmpp-session'/></iq>";
}

std::string BuildXmppSubscribeCommand(const std::string& account) {
  return "<iq type='set' to='" + account + "' "
      "id='pushsubscribe1'><subscribe xmlns='google:push'>"
      "<item channel='cloud_devices' from=''/>"
      "</subscribe></iq>";
}

// Backoff policy.
// Note: In order to ensure a minimum of 20 seconds between server errors,
// we have a 30s +- 10s (33%) jitter initial backoff.
const chromeos::BackoffEntry::Policy kDefaultBackoffPolicy = {
  // Number of initial errors (in sequence) to ignore before applying
  // exponential back-off rules.
  0,

  // Initial delay for exponential back-off in ms.
  30 * 1000,  // 30 seconds.

  // Factor by which the waiting time will be multiplied.
  2,

  // Fuzzing percentage. ex: 10% will spread requests randomly
  // between 90%-100% of the calculated time.
  0.33,  // 33%.

  // Maximum amount of time we are willing to delay our request in ms.
  10 * 60 * 1000,  // 10 minutes.

  // Time to keep an entry from being discarded even when it
  // has no significant state, -1 to never discard.
  -1,

  // Don't use initial delay unless the last request was an error.
  false,
};

const char kDefaultXmppHost[] = "talk.google.com";
const uint16_t kDefaultXmppPort = 5222;

}  // namespace

XmppChannel::XmppChannel(const std::string& account,
                         const std::string& access_token,
                         const scoped_refptr<base::TaskRunner>& task_runner)
    : account_{account},
      access_token_{access_token},
      backoff_entry_{&kDefaultBackoffPolicy},
      task_runner_{task_runner} {
  read_socket_data_.resize(4096);
}

void XmppChannel::OnMessageRead(size_t size) {
  std::string msg(read_socket_data_.data(), size);
  VLOG(2) << "Received XMPP packet: " << msg;
  read_pending_ = false;
  stream_parser_.ParseData(msg);
  WaitForMessage();
}

void XmppChannel::OnStreamStart(const std::string& node_name,
                                std::map<std::string, std::string> attributes) {
  VLOG(2) << "XMPP stream start: " << node_name;
}

void XmppChannel::OnStreamEnd(const std::string& node_name) {
  VLOG(2) << "XMPP stream ended: " << node_name << ". Restarting XMPP";
  task_runner_->PostTask(FROM_HERE,
                         base::Bind(&XmppChannel::Restart,
                                    weak_ptr_factory_.GetWeakPtr()));
}

void XmppChannel::OnStanza(std::unique_ptr<XmlNode> stanza) {
  // Handle stanza asynchronously, since XmppChannel::OnStanza() is a callback
  // from expat XML parser and some stanza could cause the XMPP stream to be
  // reset and the parser to be re-initialized. We don't want to destroy the
  // parser while it is performing a callback invocation.
  task_runner_->PostTask(FROM_HERE,
                         base::Bind(&XmppChannel::HandleStanza,
                                    weak_ptr_factory_.GetWeakPtr(),
                                    base::Passed(std::move(stanza))));
}

void XmppChannel::HandleStanza(std::unique_ptr<XmlNode> stanza) {
  VLOG(2) << "XMPP stanza received: " << stanza->ToString();

  switch (state_) {
    case XmppState::kStarted:
      if (stanza->name() == "stream:features" &&
          stanza->FindFirstChild("starttls/required", false)) {
        state_ = XmppState::kTlsStarted;
        SendMessage("<starttls xmlns='urn:ietf:params:xml:ns:xmpp-tls'/>");
        return;
      }
      break;
    case XmppState::kTlsStarted:
      if (stanza->name() == "proceed") {
        StartTlsHandshake();
        return;
      }
      break;
    case XmppState::kTlsCompleted:
      if (stanza->name() == "stream:features") {
        auto children = stanza->FindChildren("mechanisms/mechanism", false);
        for (const auto& child : children) {
          if (child->text() == "X-GOOGLE-TOKEN") {
            state_ = XmppState::kAuthenticationStarted;
            SendMessage(BuildXmppAuthenticateCommand(account_, access_token_));
            return;
          }
        }
      }
      break;
    case XmppState::kAuthenticationStarted:
      if (stanza->name() == "success") {
        state_ = XmppState::kStreamRestartedPostAuthentication;
        RestartXmppStream();
        return;
      } else if (stanza->name() == "failure") {
        if (stanza->FindFirstChild("not-authorized", false)) {
          state_ = XmppState::kAuthenticationFailed;
          if (delegate_)
            delegate_->OnPermanentFailure();
          return;
        }
      }
      break;
    case XmppState::kStreamRestartedPostAuthentication:
      if (stanza->name() == "stream:features" &&
          stanza->FindFirstChild("bind", false)) {
        state_ = XmppState::kBindSent;
        SendMessage(BuildXmppBindCommand());
        return;
      }
      break;
    case XmppState::kBindSent:
      if (stanza->name() == "iq" &&
          stanza->GetAttributeOrEmpty("type") == "result") {
        state_ = XmppState::kSessionStarted;
        SendMessage(BuildXmppStartSessionCommand());
        return;
      }
      break;
    case XmppState::kSessionStarted:
      if (stanza->name() == "iq" &&
          stanza->GetAttributeOrEmpty("type") == "result") {
        state_ = XmppState::kSubscribeStarted;
        SendMessage(BuildXmppSubscribeCommand(account_));
        return;
      }
      break;
    case XmppState::kSubscribeStarted:
      if (stanza->name() == "iq" &&
          stanza->GetAttributeOrEmpty("type") == "result") {
        state_ = XmppState::kSubscribed;
        if (delegate_)
          delegate_->OnConnected(GetName());
        return;
      }
      break;
    default:
      if (stanza->name() == "message") {
        HandleMessageStanza(std::move(stanza));
        return;
      }
      LOG(INFO) << "Unexpected XMPP stanza ignored: " << stanza->ToString();
      return;
  }
  // Something bad happened. Close the stream and start over.
  LOG(ERROR) << "Error condition occurred handling stanza: "
             << stanza->ToString();
  SendMessage("</stream:stream>");
}

void XmppChannel::HandleMessageStanza(std::unique_ptr<XmlNode> stanza) {
  const XmlNode* node = stanza->FindFirstChild("push:push/push:data", true);
  if (!node) {
    LOG(WARNING) << "XMPP message stanza is missing <push:data> element";
    return;
  }
  std::string data = node->text();
  std::string json_data;
  if (!chromeos::data_encoding::Base64Decode(data, &json_data)) {
    LOG(WARNING) << "Failed to decode base64-encoded message payload: " << data;
    return;
  }

  VLOG(2) << "XMPP push notification data: " << json_data;
  auto json_dict = LoadJsonDict(json_data, nullptr);
  if (json_dict && delegate_)
    ParseNotificationJson(*json_dict, delegate_);
}

void XmppChannel::StartTlsHandshake() {
  stream_->CancelPendingAsyncOperations();
  chromeos::TlsStream::Connect(
      std::move(raw_socket_), host_,
      base::Bind(&XmppChannel::OnTlsHandshakeComplete,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&XmppChannel::OnTlsError,
                 weak_ptr_factory_.GetWeakPtr()));
}

void XmppChannel::OnTlsHandshakeComplete(chromeos::StreamPtr tls_stream) {
  tls_stream_ = std::move(tls_stream);
  stream_ = tls_stream_.get();
  state_ = XmppState::kTlsCompleted;
  RestartXmppStream();
}

void XmppChannel::OnTlsError(const chromeos::Error* error) {
  LOG(ERROR) << "TLS handshake failed. Restarting XMPP connection";
  Restart();
}

void XmppChannel::SendMessage(const std::string& message) {
  if (write_pending_) {
    queued_write_data_ += message;
    return;
  }
  write_socket_data_ = queued_write_data_ + message;
  queued_write_data_.clear();
  chromeos::ErrorPtr error;
  VLOG(2) << "Sending XMPP message: " << message;

  write_pending_ = true;
  bool ok = stream_->WriteAllAsync(
      write_socket_data_.data(),
      write_socket_data_.size(),
      base::Bind(&XmppChannel::OnMessageSent, weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&XmppChannel::OnWriteError, weak_ptr_factory_.GetWeakPtr()),
      &error);

  if (!ok)
    OnWriteError(error.get());
}

void XmppChannel::OnMessageSent() {
  chromeos::ErrorPtr error;
  write_pending_ = false;
  if (!stream_->FlushBlocking(&error)) {
    OnWriteError(error.get());
    return;
  }
  if (queued_write_data_.empty()) {
    WaitForMessage();
  } else {
    SendMessage(std::string{});
  }
}

void XmppChannel::WaitForMessage() {
  if (read_pending_)
    return;

  chromeos::ErrorPtr error;
  read_pending_ = true;
  bool ok = stream_->ReadAsync(
      read_socket_data_.data(),
      read_socket_data_.size(),
      base::Bind(&XmppChannel::OnMessageRead, weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&XmppChannel::OnReadError, weak_ptr_factory_.GetWeakPtr()),
      &error);

  if (!ok)
    OnReadError(error.get());
}

void XmppChannel::OnReadError(const chromeos::Error* error) {
  read_pending_ = false;
  Restart();
}

void XmppChannel::OnWriteError(const chromeos::Error* error) {
  write_pending_ = false;
  Restart();
}

void XmppChannel::Connect(const std::string& host, uint16_t port,
                          const base::Closure& callback) {
  int socket_fd = ConnectSocket(host, port);
  if (socket_fd >= 0) {
    raw_socket_ =
      chromeos::FileStream::FromFileDescriptor(socket_fd, true, nullptr);
    if (!raw_socket_) {
      close(socket_fd);
      socket_fd = -1;
    }
  }

  backoff_entry_.InformOfRequest(raw_socket_ != nullptr);
  if (raw_socket_) {
    host_ = host;
    port_ = port;
    stream_ = raw_socket_.get();
    callback.Run();
  } else {
    VLOG(2) << "Delaying connection to XMPP server " << host << " for "
            << backoff_entry_.GetTimeUntilRelease().InMilliseconds()
            << " milliseconds.";
    task_runner_->PostDelayedTask(
        FROM_HERE,
        base::Bind(&XmppChannel::Connect, weak_ptr_factory_.GetWeakPtr(),
                    host, port, callback),
        backoff_entry_.GetTimeUntilRelease());
  }
}

std::string XmppChannel::GetName() const {
  return "xmpp";
}

void XmppChannel::AddChannelParameters(base::DictionaryValue* channel_json) {
  // No extra parameters needed for XMPP.
}

void XmppChannel::Restart() {
  Stop();
  Start(delegate_);
}

void XmppChannel::Start(NotificationDelegate* delegate) {
  CHECK(state_ == XmppState::kNotStarted);
  delegate_ = delegate;
  Connect(kDefaultXmppHost, kDefaultXmppPort,
          base::Bind(&XmppChannel::OnConnected,
                     weak_ptr_factory_.GetWeakPtr()));
}

void XmppChannel::Stop() {
  if (state_ == XmppState::kSubscribed && delegate_)
    delegate_->OnDisconnected();

  weak_ptr_factory_.InvalidateWeakPtrs();
  if (tls_stream_) {
    tls_stream_->CloseBlocking(nullptr);
    tls_stream_.reset();
  }
  if (raw_socket_) {
    raw_socket_->CloseBlocking(nullptr);
    raw_socket_.reset();
  }
  stream_ = nullptr;
  state_ = XmppState::kNotStarted;
}

void XmppChannel::OnConnected() {
  state_ = XmppState::kStarted;
  RestartXmppStream();
}

void XmppChannel::RestartXmppStream() {
  stream_parser_.Reset();
  stream_->CancelPendingAsyncOperations();
  read_pending_ = false;
  write_pending_ = false;
  SendMessage(BuildXmppStartStreamCommand());
}

}  // namespace buffet
