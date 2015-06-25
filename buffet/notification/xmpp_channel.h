// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_NOTIFICATION_XMPP_CHANNEL_H_
#define BUFFET_NOTIFICATION_XMPP_CHANNEL_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/callback_forward.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/single_thread_task_runner.h>
#include <chromeos/backoff_entry.h>
#include <chromeos/streams/stream.h>

#include "buffet/notification/notification_channel.h"
#include "buffet/notification/xmpp_iq_stanza_handler.h"
#include "buffet/notification/xmpp_stream_parser.h"

namespace privetd {
class ShillClient;
}

namespace buffet {

// Simple interface to abstract XmppChannel's SendMessage() method.
class XmppChannelInterface {
 public:
  virtual void SendMessage(const std::string& message) = 0;

 protected:
  virtual ~XmppChannelInterface() = default;
};

class XmppChannel : public NotificationChannel,
                    public XmppStreamParser::Delegate,
                    public XmppChannelInterface {
 public:
  // |account| is the robot account for buffet and |access_token|
  // it the OAuth token. Note that the OAuth token expires fairly frequently
  // so you will need to reset the XmppClient every time this happens.
  XmppChannel(const std::string& account,
              const std::string& access_token,
              const scoped_refptr<base::SingleThreadTaskRunner>& task_runner,
              privetd::ShillClient* shill);
  ~XmppChannel() override = default;

  // Overrides from NotificationChannel.
  std::string GetName() const override;
  bool IsConnected() const override;
  void AddChannelParameters(base::DictionaryValue* channel_json) override;
  void Start(NotificationDelegate* delegate) override;
  void Stop() override;

  const std::string& jid() const { return jid_; }

  // Internal states for the XMPP stream.
  enum class XmppState {
    kNotStarted,
    kConnecting,
    kConnected,
    kTlsStarted,
    kTlsCompleted,
    kAuthenticationStarted,
    kAuthenticationFailed,
    kStreamRestartedPostAuthentication,
    kBindSent,
    kSessionStarted,
    kSubscribeStarted,
    kSubscribed,
  };

 protected:
  // These methods are internal helpers that can be overloaded by unit tests
  // to help provide unit-test-specific functionality.
  virtual void Connect(const std::string& host, uint16_t port,
                       const base::Closure& callback);
  virtual void SchedulePing(base::TimeDelta interval, base::TimeDelta timeout);
  void ScheduleRegularPing();
  void ScheduleFastPing();

  XmppState state_{XmppState::kNotStarted};

  // The connection socket stream to the XMPP server.
  chromeos::Stream* stream_{nullptr};

 private:
  friend class IqStanzaHandler;

  // Overrides from XmppStreamParser::Delegate.
  void OnStreamStart(const std::string& node_name,
                     std::map<std::string, std::string> attributes) override;
  void OnStreamEnd(const std::string& node_name) override;
  void OnStanza(std::unique_ptr<XmlNode> stanza) override;

  // Overrides from XmppChannelInterface.
  void SendMessage(const std::string& message) override;

  void HandleStanza(std::unique_ptr<XmlNode> stanza);
  void HandleMessageStanza(std::unique_ptr<XmlNode> stanza);
  void RestartXmppStream();

  void StartTlsHandshake();
  void OnTlsHandshakeComplete(chromeos::StreamPtr tls_stream);
  void OnTlsError(const chromeos::Error* error);

  void WaitForMessage();

  void OnConnected();
  void OnMessageRead(size_t size);
  void OnMessageSent();
  void OnReadError(const chromeos::Error* error);
  void OnWriteError(const chromeos::Error* error);
  void Restart();
  void CloseStream();

  // XMPP connection state machine's state handlers.
  void OnBindCompleted(std::unique_ptr<XmlNode> reply);
  void OnSessionEstablished(std::unique_ptr<XmlNode> reply);
  void OnSubscribed(std::unique_ptr<XmlNode> reply);

  // Sends a ping request to the server to check if the connection is still
  // valid.
  void PingServer(base::TimeDelta timeout);
  void OnPingResponse(base::Time sent_time, std::unique_ptr<XmlNode> reply);
  void OnPingTimeout(base::Time sent_time);

  void OnConnectivityChanged(bool online);

  // Robot account name for the device.
  std::string account_;

  // Full JID of this device.
  std::string jid_;

  // OAuth access token for the account. Expires fairly frequently.
  std::string access_token_;

  chromeos::StreamPtr raw_socket_;
  chromeos::StreamPtr tls_stream_;  // Must follow |raw_socket_|.

  // Read buffer for incoming message packets.
  std::vector<char> read_socket_data_;
  // Write buffer for outgoing message packets.
  std::string write_socket_data_;
  std::string queued_write_data_;

  // XMPP server name and port used for connection.
  std::string host_;
  uint16_t port_{0};

  chromeos::BackoffEntry backoff_entry_;
  NotificationDelegate* delegate_{nullptr};
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  XmppStreamParser stream_parser_{this};
  bool read_pending_{false};
  bool write_pending_{false};
  std::unique_ptr<IqStanzaHandler> iq_stanza_handler_;

  base::WeakPtrFactory<XmppChannel> ping_ptr_factory_{this};
  base::WeakPtrFactory<XmppChannel> task_ptr_factory_{this};
  base::WeakPtrFactory<XmppChannel> weak_ptr_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(XmppChannel);
};

}  // namespace buffet

#endif  // BUFFET_NOTIFICATION_XMPP_CHANNEL_H_

