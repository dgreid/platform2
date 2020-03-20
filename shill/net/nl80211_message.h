// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NET_NL80211_MESSAGE_H_
#define SHILL_NET_NL80211_MESSAGE_H_

#include <map>
#include <memory>
#include <string>

#include <base/macros.h>
#include <base/no_destructor.h>

#include "shill/net/byte_string.h"
#include "shill/net/generic_netlink_message.h"
#include "shill/net/shill_export.h"

namespace shill {

class NetlinkPacket;

// Class for messages received from the mac80211 drivers by way of the
// cfg80211 kernel module.
class SHILL_EXPORT Nl80211Message : public GenericNetlinkMessage {
 public:
  static const char kMessageTypeString[];

  Nl80211Message(uint8_t command, const char* command_string)
      : GenericNetlinkMessage(nl80211_message_type_, command, command_string) {}
  ~Nl80211Message() override = default;

  // Gets the family_id / message_type for all Nl80211 messages.
  static uint16_t GetMessageType();

  // Sets the family_id / message_type for all Nl80211 messages.
  static void SetMessageType(uint16_t message_type);

  bool InitFromPacket(NetlinkPacket* packet, MessageContext context) override;

  uint8_t command() const { return command_; }
  const char* command_string() const { return command_string_; }
  uint16_t message_type() const { return message_type_; }
  uint32_t sequence_number() const { return sequence_number_; }
  void set_sequence_number(uint32_t seq) { sequence_number_ = seq; }

  // Message factory for all types of Nl80211 message.
  static std::unique_ptr<NetlinkMessage> CreateMessage(
      const NetlinkPacket& packet);

 private:
  static uint16_t nl80211_message_type_;

  DISALLOW_COPY_AND_ASSIGN(Nl80211Message);
};

class SHILL_EXPORT Nl80211Frame {
 public:
  enum Type {
    kAssocResponseFrameType = 0x10,
    kReassocResponseFrameType = 0x30,
    kAssocRequestFrameType = 0x00,
    kReassocRequestFrameType = 0x20,
    kAuthFrameType = 0xb0,
    kDisassocFrameType = 0xa0,
    kDeauthFrameType = 0xc0,
    kIllegalFrameType = 0xff
  };

  explicit Nl80211Frame(const ByteString& init);
  std::string ToString() const;
  bool IsEqual(const Nl80211Frame& other) const;
  uint16_t reason() const { return reason_; }
  uint16_t status() const { return status_; }
  uint8_t frame_type() const { return frame_type_; }

 private:
  static const uint8_t kFrameTypeMask;

  std::string mac_from_;
  std::string mac_to_;
  uint8_t frame_type_;
  uint16_t reason_;
  uint16_t status_;
  ByteString frame_;

  DISALLOW_COPY_AND_ASSIGN(Nl80211Frame);
};

//
// Specific Nl80211Message types.
//

class SHILL_EXPORT AssociateMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  AssociateMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(AssociateMessage);
};

class SHILL_EXPORT AuthenticateMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  AuthenticateMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(AuthenticateMessage);
};

class SHILL_EXPORT CancelRemainOnChannelMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  CancelRemainOnChannelMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(CancelRemainOnChannelMessage);
};

class SHILL_EXPORT ConnectMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  ConnectMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ConnectMessage);
};

class SHILL_EXPORT DeauthenticateMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  DeauthenticateMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(DeauthenticateMessage);
};

class SHILL_EXPORT DelInterfaceMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  DelInterfaceMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(DelInterfaceMessage);
};

class SHILL_EXPORT DeleteStationMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  DeleteStationMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(DeleteStationMessage);
};

class SHILL_EXPORT DisassociateMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  DisassociateMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(DisassociateMessage);
};

class SHILL_EXPORT DisconnectMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  DisconnectMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(DisconnectMessage);
};

class SHILL_EXPORT FrameTxStatusMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  FrameTxStatusMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(FrameTxStatusMessage);
};

class SHILL_EXPORT GetRegMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  GetRegMessage();

 private:
  DISALLOW_COPY_AND_ASSIGN(GetRegMessage);
};

class SHILL_EXPORT GetStationMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  GetStationMessage();

 private:
  DISALLOW_COPY_AND_ASSIGN(GetStationMessage);
};

class SHILL_EXPORT SetWakeOnPacketConnMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  SetWakeOnPacketConnMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(SetWakeOnPacketConnMessage);
};

class SHILL_EXPORT GetWakeOnPacketConnMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  GetWakeOnPacketConnMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(GetWakeOnPacketConnMessage);
};

class SHILL_EXPORT GetWiphyMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  GetWiphyMessage();

 private:
  DISALLOW_COPY_AND_ASSIGN(GetWiphyMessage);
};

class SHILL_EXPORT JoinIbssMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  JoinIbssMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(JoinIbssMessage);
};

class SHILL_EXPORT MichaelMicFailureMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  MichaelMicFailureMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(MichaelMicFailureMessage);
};

class SHILL_EXPORT NewScanResultsMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  NewScanResultsMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(NewScanResultsMessage);
};

class SHILL_EXPORT NewStationMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  NewStationMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(NewStationMessage);
};

class SHILL_EXPORT NewWiphyMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  NewWiphyMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(NewWiphyMessage);
};

class SHILL_EXPORT NotifyCqmMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  NotifyCqmMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(NotifyCqmMessage);
};

class SHILL_EXPORT PmksaCandidateMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  PmksaCandidateMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(PmksaCandidateMessage);
};

class SHILL_EXPORT ProbeMeshLinkMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  ProbeMeshLinkMessage();

 private:
  DISALLOW_COPY_AND_ASSIGN(ProbeMeshLinkMessage);
};

class SHILL_EXPORT RegBeaconHintMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  RegBeaconHintMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(RegBeaconHintMessage);
};

class SHILL_EXPORT RegChangeMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  RegChangeMessage();

 private:
  DISALLOW_COPY_AND_ASSIGN(RegChangeMessage);
};

class SHILL_EXPORT RemainOnChannelMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  RemainOnChannelMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(RemainOnChannelMessage);
};

class SHILL_EXPORT RoamMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  RoamMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(RoamMessage);
};

class SHILL_EXPORT ScanAbortedMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  ScanAbortedMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ScanAbortedMessage);
};

class SHILL_EXPORT GetScanMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  GetScanMessage();

 private:
  DISALLOW_COPY_AND_ASSIGN(GetScanMessage);
};

class SHILL_EXPORT TriggerScanMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  TriggerScanMessage();

 private:
  DISALLOW_COPY_AND_ASSIGN(TriggerScanMessage);
};

class SHILL_EXPORT UnknownNl80211Message : public Nl80211Message {
 public:
  explicit UnknownNl80211Message(uint8_t command)
      : Nl80211Message(command, "<UNKNOWN NL80211 MESSAGE>") {}

 private:
  DISALLOW_COPY_AND_ASSIGN(UnknownNl80211Message);
};

class SHILL_EXPORT UnprotDeauthenticateMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  UnprotDeauthenticateMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(UnprotDeauthenticateMessage);
};

class SHILL_EXPORT UnprotDisassociateMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  UnprotDisassociateMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(UnprotDisassociateMessage);
};

class SHILL_EXPORT WiphyRegChangeMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  WiphyRegChangeMessage();

 private:
  DISALLOW_COPY_AND_ASSIGN(WiphyRegChangeMessage);
};

class SHILL_EXPORT GetInterfaceMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  GetInterfaceMessage();

 private:
  DISALLOW_COPY_AND_ASSIGN(GetInterfaceMessage);
};

class SHILL_EXPORT NewInterfaceMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  NewInterfaceMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(NewInterfaceMessage);
};

class SHILL_EXPORT GetSurveyMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  GetSurveyMessage();

 private:
  DISALLOW_COPY_AND_ASSIGN(GetSurveyMessage);
};

class SHILL_EXPORT SurveyResultsMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  SurveyResultsMessage() : Nl80211Message(kCommand, kCommandString) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(SurveyResultsMessage);
};

class SHILL_EXPORT GetMeshPathInfoMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  GetMeshPathInfoMessage();

 private:
  DISALLOW_COPY_AND_ASSIGN(GetMeshPathInfoMessage);
};

class SHILL_EXPORT GetMeshProxyPathMessage : public Nl80211Message {
 public:
  static const uint8_t kCommand;
  static const char kCommandString[];

  GetMeshProxyPathMessage();

 private:
  DISALLOW_COPY_AND_ASSIGN(GetMeshProxyPathMessage);
};

}  // namespace shill

#endif  // SHILL_NET_NL80211_MESSAGE_H_
