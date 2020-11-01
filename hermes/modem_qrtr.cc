// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/modem_qrtr.h"

#include <algorithm>
#include <array>
#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <libqrtr.h>

#include "hermes/apdu.h"
#include "hermes/euicc_manager_interface.h"
#include "hermes/qmi_uim.h"
#include "hermes/sgp_22.h"
#include "hermes/socket_qrtr.h"

namespace {

// As per QMI UIM spec section 2.2
constexpr uint8_t kQmiUimService = 0xB;
// This represents the default logical slot that we want our eSIM to be
// assigned. For dual sim - single standby modems, this will always work. For
// other multi-sim modems, get the first active slot and store it as a ModemQrtr
// field.
constexpr uint8_t kDefaultLogicalSlot = 0x01;
constexpr uint8_t kInvalidChannel = -1;

// Delay between SwitchSlot and the next QMI message
constexpr auto kSwitchSlotDelay = base::TimeDelta::FromSeconds(1);

constexpr auto kInitRetryDelay = base::TimeDelta::FromSeconds(10);

bool CheckMessageSuccess(QmiUimCommand cmd, const uim_qmi_result& qmi_result) {
  if (qmi_result.result == 0) {
    return true;
  }

  LOG(ERROR) << cmd.ToString()
             << " response contained error: " << qmi_result.error;
  return false;
}

}  // namespace

namespace hermes {

struct ApduTxInfo : public ModemQrtr::TxInfo {
  explicit ApduTxInfo(CommandApdu apdu,
                      ModemQrtr::ResponseCallback cb = nullptr)
      : apdu_(std::move(apdu)), callback_(cb) {}
  CommandApdu apdu_;
  ModemQrtr::ResponseCallback callback_;
};

struct SwitchSlotTxInfo : public ModemQrtr::TxInfo {
  explicit SwitchSlotTxInfo(const uint32_t physical_slot,
                            const uint8_t logical_slot)
      : physical_slot_(physical_slot), logical_slot_(logical_slot) {}
  const uint32_t physical_slot_;
  const uint8_t logical_slot_;
};

std::unique_ptr<ModemQrtr> ModemQrtr::Create(
    std::unique_ptr<SocketInterface> socket,
    Logger* logger,
    Executor* executor) {
  // Open the socket prior to passing to ModemQrtr, such that it always has a
  // valid socket to write to.
  if (!socket || !socket->Open()) {
    LOG(ERROR) << "Failed to open socket";
    return nullptr;
  }
  return std::unique_ptr<ModemQrtr>(
      new ModemQrtr(std::move(socket), logger, executor));
}

ModemQrtr::ModemQrtr(std::unique_ptr<SocketInterface> socket,
                     Logger* logger,
                     Executor* executor)
    : qmi_disabled_(false),
      extended_apdu_supported_(false),
      current_transaction_id_(static_cast<uint16_t>(-1)),
      channel_(kInvalidChannel),
      logical_slot_(kDefaultLogicalSlot),
      socket_(std::move(socket)),
      buffer_(4096),
      euicc_manager_(nullptr),
      logger_(logger),
      executor_(executor) {
  CHECK(socket_);
  CHECK(socket_->IsValid());
  socket_->SetDataAvailableCallback(
      base::Bind(&ModemQrtr::OnDataAvailable, base::Unretained(this)));

  // Set SGP.22 specification version supported by this implementation (this is
  // not currently constrained by the eUICC we use).
  spec_version_.set_major(2);
  spec_version_.set_minor(2);
  spec_version_.set_revision(0);
}

ModemQrtr::~ModemQrtr() {
  Shutdown();
  socket_->Close();
}

void ModemQrtr::SetActiveSlot(const uint32_t physical_slot) {
  tx_queue_.push_back(
      {std::make_unique<SwitchSlotTxInfo>(physical_slot, logical_slot_),
       AllocateId(), QmiUimCommand::kSwitchSlot});
  current_state_.Transition(State::kUimStarted);
  channel_ = kInvalidChannel;
  tx_queue_.push_back(
      {std::unique_ptr<TxInfo>(), AllocateId(), QmiUimCommand::kReset});
  tx_queue_.push_back({std::unique_ptr<TxInfo>(), AllocateId(),
                       QmiUimCommand::kOpenLogicalChannel});
}

void ModemQrtr::StoreAndSetActiveSlot(const uint32_t physical_slot) {
  tx_queue_.push_back(
      {std::make_unique<TxInfo>(), AllocateId(), QmiUimCommand::kGetSlots});
  SetActiveSlot(physical_slot);
}

void ModemQrtr::RestoreActiveSlot() {
  if (stored_active_slot_) {
    tx_queue_.push_back({std::make_unique<SwitchSlotTxInfo>(
                             stored_active_slot_.value(), logical_slot_),
                         AllocateId(), QmiUimCommand::kSwitchSlot});
    stored_active_slot_.reset();
  } else {
    LOG(ERROR) << "Attempted to restore active slot when none was stored";
  }
}

void ModemQrtr::SendApdus(std::vector<lpa::card::Apdu> apdus,
                          ResponseCallback cb) {
  for (size_t i = 0; i < apdus.size(); ++i) {
    ResponseCallback callback =
        (i == apdus.size() - 1 ? std::move(cb) : nullptr);
    CommandApdu apdu(static_cast<ApduClass>(apdus[i].cla()),
                     static_cast<ApduInstruction>(apdus[i].ins()),
                     extended_apdu_supported_);
    apdu.AddData(apdus[i].data());
    tx_queue_.push_back(
        {std::make_unique<ApduTxInfo>(std::move(apdu), std::move(callback)),
         AllocateId(), QmiUimCommand::kSendApdu});
  }
  // Begin transmitting if we are not already processing a transaction.
  if (!pending_response_type) {
    TransmitFromQueue();
  }
}

bool ModemQrtr::IsSimValidAfterEnable() {
  // This function is called by the lpa after profile enable.
  ReacquireChannel();
  return true;
}

bool ModemQrtr::IsSimValidAfterDisable() {
  // This function is called by the lpa after profile disable.
  ReacquireChannel();
  return true;
}

void ModemQrtr::Initialize(EuiccManagerInterface* euicc_manager) {
  CHECK(current_state_ == State::kUninitialized);
  // Initialization succeeds only if our active sim slot has an esim
  VLOG(1) << "Trying to initialize channel to eSIM";
  euicc_manager_ = euicc_manager;

  // StartService should result in a received QRTR_TYPE_NEW_SERVER
  // packet. Don't send other packets until that occurs.
  if (!socket_->StartService(kQmiUimService, 1, 0)) {
    LOG(ERROR) << "Failed starting UIM service during ModemQrtr initialization";
    RetryInitialization();
    return;
  }

  current_state_.Transition(State::kInitializeStarted);

  // Note: we use push_front so that SendApdus could be called prior to a
  // successful initialization.
  tx_queue_.push_front({std::unique_ptr<TxInfo>(), AllocateId(),
                        QmiUimCommand::kOpenLogicalChannel});
  // Request initial info about SIM slots.
  // TODO(crbug.com/1085825) Add support for getting indications so that this
  // info can get updated.
  tx_queue_.push_front(
      {std::make_unique<TxInfo>(), AllocateId(), QmiUimCommand::kGetSlots});
  tx_queue_.push_front(
      {std::unique_ptr<TxInfo>(), AllocateId(), QmiUimCommand::kReset});
}

void ModemQrtr::ReacquireChannel() {
  if (current_state_ != State::kSendApduReady) {
    return;
  }

  LOG(INFO) << "Reacquiring Channel";
  current_state_.Transition(State::kUimStarted);
  channel_ = kInvalidChannel;
  tx_queue_.push_front({std::unique_ptr<TxInfo>(), AllocateId(),
                        QmiUimCommand::kOpenLogicalChannel});
  tx_queue_.push_front(
      {std::unique_ptr<TxInfo>(), AllocateId(), QmiUimCommand::kReset});
}

void ModemQrtr::RetryInitialization() {
  VLOG(1) << "Reprobing for eSIM in " << kInitRetryDelay.InSeconds()
          << " seconds";
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&ModemQrtr::Initialize, base::Unretained(this),
                 euicc_manager_),
      kInitRetryDelay);
}

void ModemQrtr::FinalizeInitialization() {
  if (current_state_ != State::kLogicalChannelOpened) {
    VLOG(1) << "Could not open logical channel to eSIM";
    Shutdown();
    RetryInitialization();
    return;
  }
  LOG(INFO) << "ModemQrtr initialization successful. eSIM found.";
  current_state_.Transition(State::kSendApduReady);
  // TODO(crbug.com/1117582) Set this based on whether or not Extended Length
  // APDU is supported.
  extended_apdu_supported_ = false;
}

void ModemQrtr::Shutdown() {
  if (current_state_ != State::kUninitialized &&
      current_state_ != State::kInitializeStarted) {
    socket_->StopService(kQmiUimService, 1, 0);
  }
  current_state_.Transition(State::kUninitialized);
}

uint16_t ModemQrtr::AllocateId() {
  // transaction id cannot be 0, but when incrementing by 1, an overflow will
  // result in this method at some point returning 0. Incrementing by 2 when
  // transaction_id is initialized as an odd number guarantees us that this
  // method will never return 0 without special-casing the overflow.
  current_transaction_id_ += 2;
  return current_transaction_id_;
}

/////////////////////////////////////
// Transmit method implementations //
/////////////////////////////////////

void ModemQrtr::TransmitFromQueue() {
  if (tx_queue_.empty() || pending_response_type || qmi_disabled_) {
    return;
  }

  bool should_pop = true;
  switch (tx_queue_[0].uim_type_) {
    case QmiUimCommand::kReset:
      uim_reset_req reset_request;
      SendCommand(QmiUimCommand::kReset, tx_queue_[0].id_, &reset_request,
                  uim_reset_req_ei);
      break;
    case QmiUimCommand::kSwitchSlot:
      // Don't pop since we need to update the inactive euicc if SwitchSlot
      // succeeds
      should_pop = false;
      TransmitQmiSwitchSlot(&tx_queue_[0]);
      break;
    case QmiUimCommand::kGetSlots:
      uim_get_slots_req slots_request;
      SendCommand(QmiUimCommand::kGetSlots, tx_queue_[0].id_, &slots_request,
                  uim_get_slots_req_ei);
      break;
    case QmiUimCommand::kOpenLogicalChannel:
      TransmitQmiOpenLogicalChannel(&tx_queue_[0]);
      current_state_.Transition(State::kLogicalChannelPending);
      break;
    case QmiUimCommand::kSendApdu:
      // kSendApdu element will be popped off the queue after the response has
      // been entirely received. This occurs within |ReceiveQmiSendApdu|.
      should_pop = false;
      TransmitQmiSendApdu(&tx_queue_[0]);
      break;
    default:
      LOG(ERROR) << "Unexpected QMI UIM type in ModemQrtr tx queue";
  }
  if (should_pop) {
    tx_queue_.pop_front();
  }
}

void ModemQrtr::TransmitQmiSwitchSlot(TxElement* tx_element) {
  auto switch_slot_tx_info =
      dynamic_cast<SwitchSlotTxInfo*>(tx_queue_[0].info_.get());
  // Slot switching takes time, thus switch slots only when absolutely necessary
  if (!stored_active_slot_ ||
      stored_active_slot_.value() != switch_slot_tx_info->physical_slot_) {
    uim_switch_slot_req switch_slot_request;
    switch_slot_request.physical_slot = switch_slot_tx_info->physical_slot_;
    switch_slot_request.logical_slot = switch_slot_tx_info->logical_slot_;
    SendCommand(QmiUimCommand::kSwitchSlot, tx_queue_[0].id_,
                &switch_slot_request, uim_switch_slot_req_ei);
  } else {
    LOG(INFO) << "Requested slot is already active";
    tx_queue_.pop_front();
    TransmitFromQueue();
  }
}

void ModemQrtr::TransmitQmiOpenLogicalChannel(TxElement* tx_element) {
  DCHECK(tx_element &&
         tx_element->uim_type_ == QmiUimCommand::kOpenLogicalChannel);

  uim_open_logical_channel_req request;
  request.slot = logical_slot_;
  request.aid_valid = true;
  request.aid_len = kAidIsdr.size();
  std::copy(kAidIsdr.begin(), kAidIsdr.end(), request.aid);

  SendCommand(QmiUimCommand::kOpenLogicalChannel, tx_element->id_, &request,
              uim_open_logical_channel_req_ei);
}

void ModemQrtr::TransmitQmiSendApdu(TxElement* tx_element) {
  DCHECK(tx_element && tx_element->uim_type_ == QmiUimCommand::kSendApdu);

  uim_send_apdu_req request;
  request.slot = logical_slot_;
  request.channel_id_valid = true;
  request.channel_id = channel_;

  uint8_t* fragment;
  ApduTxInfo* apdu = static_cast<ApduTxInfo*>(tx_element->info_.get());
  size_t fragment_size = apdu->apdu_.GetNextFragment(&fragment);
  request.apdu_len = fragment_size;
  std::copy(fragment, fragment + fragment_size, request.apdu);

  SendCommand(QmiUimCommand::kSendApdu, tx_element->id_, &request,
              uim_send_apdu_req_ei);
}

bool ModemQrtr::SendCommand(QmiUimCommand type,
                            uint16_t id,
                            void* c_struct,
                            qmi_elem_info* ei) {
  if (!socket_->IsValid()) {
    LOG(ERROR) << "ModemQrtr socket is invalid!";
    return false;
  }
  if (pending_response_type) {
    LOG(ERROR) << "QRTR tried to send buffer while awaiting a qmi response";
    return false;
  }
  if (!current_state_.CanSend()) {
    LOG(ERROR) << "QRTR tried to send buffer in a non-sending state: "
               << current_state_;
    return false;
  }
  if (type == QmiUimCommand::kSendApdu &&
      current_state_ != State::kSendApduReady) {
    LOG(ERROR) << "QRTR tried to send apdu in state: " << current_state_;
    return false;
  }

  // All hermes initiated qmi messages expect a response
  pending_response_type = type;

  std::vector<uint8_t> encoded_buffer(kBufferDataSize * 2, 0);
  qrtr_packet packet;
  packet.data = encoded_buffer.data();
  packet.data_len = encoded_buffer.size();

  size_t len = qmi_encode_message(
      &packet, QMI_REQUEST, static_cast<uint16_t>(type), id, c_struct, ei);
  if (len < 0) {
    LOG(ERROR) << "Failed to encode QMI UIM request: "
               << static_cast<uint16_t>(type);
    return false;
  }

  LOG(INFO) << "ModemQrtr sending transaction type "
            << static_cast<uint16_t>(type)
            << " with data (size : " << packet.data_len
            << ") : " << base::HexEncode(packet.data, packet.data_len);
  int success = socket_->Send(packet.data, packet.data_len,
                              reinterpret_cast<void*>(&metadata_));
  if (success < 0) {
    LOG(ERROR) << "qrtr_sendto failed";
    return false;
  }
  return true;
}

////////////////////////////////////
// Receive method implementations //
////////////////////////////////////

void ModemQrtr::ProcessQrtrPacket(uint32_t node, uint32_t port, int size) {
  sockaddr_qrtr qrtr_sock;
  qrtr_sock.sq_family = AF_QIPCRTR;
  qrtr_sock.sq_node = node;
  qrtr_sock.sq_port = port;

  qrtr_packet pkt;
  int ret = qrtr_decode(&pkt, buffer_.data(), size, &qrtr_sock);
  if (ret < 0) {
    LOG(ERROR) << "qrtr_decode failed";
    return;
  }

  switch (pkt.type) {
    case QRTR_TYPE_NEW_SERVER:
      VLOG(1) << "Received NEW_SERVER QRTR packet";
      if (pkt.service == kQmiUimService && channel_ == kInvalidChannel) {
        current_state_.Transition(State::kUimStarted);
        metadata_.node = pkt.node;
        metadata_.port = pkt.port;
      }
      break;
    case QRTR_TYPE_DATA:
      VLOG(1) << "Received data QRTR packet";
      ProcessQmiPacket(pkt);
      break;
    case QRTR_TYPE_DEL_SERVER:
    case QRTR_TYPE_HELLO:
    case QRTR_TYPE_BYE:
    case QRTR_TYPE_DEL_CLIENT:
    case QRTR_TYPE_RESUME_TX:
    case QRTR_TYPE_EXIT:
    case QRTR_TYPE_PING:
    case QRTR_TYPE_NEW_LOOKUP:
    case QRTR_TYPE_DEL_LOOKUP:
      LOG(INFO) << "Received QRTR packet of type " << pkt.type << ". Ignoring.";
      break;
    default:
      LOG(WARNING) << "Received QRTR packet but did not recognize packet type "
                   << pkt.type << ".";
  }
  // If we cannot yet send another request, it is because we are waiting for a
  // response. After the response is received and processed, the next request
  // will be sent.
  if (!pending_response_type) {
    TransmitFromQueue();
  }
}

void ModemQrtr::ProcessQmiPacket(const qrtr_packet& packet) {
  uint32_t qmi_type;
  if (qmi_decode_header(&packet, &qmi_type) < 0) {
    LOG(ERROR) << "QRTR received invalid QMI packet";
    return;
  }

  VLOG(1) << "Received QMI message of type: " << qmi_type;
  switch (qmi_type) {
    case QmiUimCommand::kReset:
      VLOG(1) << "Ignoring received RESET packet";
      break;
    case QmiUimCommand::kSwitchSlot:
      ReceiveQmiSwitchSlot(packet);
      break;
    case QmiUimCommand::kGetSlots:
      ReceiveQmiGetSlots(packet);
      break;
    case QmiUimCommand::kOpenLogicalChannel:
      ReceiveQmiOpenLogicalChannel(packet);
      if (!current_state_.IsInitialized()) {
        FinalizeInitialization();
      }
      break;
    case QmiUimCommand::kSendApdu:
      ReceiveQmiSendApdu(packet);
      break;
    default:
      LOG(WARNING) << "Received QMI packet of unknown type: " << qmi_type;
      return;
  }

  if (!pending_response_type) {
    LOG(ERROR) << "Received unexpected QMI response. No pending response.";
    return;
  }

  if (pending_response_type != qmi_type)
    LOG(ERROR) << "Received unexpected QMI response. Expected: "
               << pending_response_type->ToString();
  pending_response_type.reset();
}

void ModemQrtr::ReceiveQmiGetSlots(const qrtr_packet& packet) {
  QmiUimCommand cmd(QmiUimCommand::kGetSlots);
  uim_get_slots_resp resp;
  unsigned int id;
  if (qmi_decode_message(&resp, &id, &packet, QMI_RESPONSE, cmd,
                         uim_get_slots_resp_ei) < 0) {
    LOG(ERROR) << "Failed to decode QMI UIM response: " << cmd.ToString();
    return;
  } else if (!CheckMessageSuccess(cmd, resp.result)) {
    return;
  }

  if (!resp.status_valid || !resp.info_valid) {
    LOG(ERROR) << "QMI UIM response for " << cmd.ToString()
               << " contained invalid slot info";
    return;
  }

  CHECK(euicc_manager_);
  bool logical_slot_found = false;
  uint8_t max_len = std::max(resp.status_len, resp.info_len);
  for (uint8_t i = 0; i < max_len; ++i) {
    bool is_present = (resp.status[i].physical_card_status ==
                       uim_physical_slot_status::kCardPresent);
    bool is_euicc = resp.info[i].is_euicc;

    bool is_active = (resp.status[i].physical_slot_state ==
                      uim_physical_slot_status::kSlotActive);

    if (is_active) {
      stored_active_slot_ = i + 1;
      if (!logical_slot_found) {
        // This is the logical slot we grab when we perform a switch slot
        logical_slot_ = resp.status[i].logical_slot;
        logical_slot_found = true;
      }
    }
    if (!is_present || !is_euicc)
      euicc_manager_->OnEuiccRemoved(i + 1);
    else
      euicc_manager_->OnEuiccUpdated(
          i + 1, is_active ? EuiccSlotInfo(resp.status[i].logical_slot)
                           : EuiccSlotInfo());
  }
}

void ModemQrtr::ReceiveQmiSwitchSlot(const qrtr_packet& packet) {
  QmiUimCommand cmd(QmiUimCommand::kSwitchSlot);
  uim_switch_slot_resp resp;
  unsigned int id;

  if (qmi_decode_message(&resp, &id, &packet, QMI_RESPONSE, cmd,
                         uim_switch_slot_resp_ei) < 0) {
    LOG(ERROR) << "Failed to decode QMI UIM response: " << cmd.ToString();
    return;
  }

  if (!CheckMessageSuccess(cmd, resp.result)) {
    return;
  }

  auto switch_slot_tx_info =
      dynamic_cast<SwitchSlotTxInfo*>(tx_queue_.front().info_.get());
  euicc_manager_->OnEuiccUpdated(
      switch_slot_tx_info->physical_slot_,
      EuiccSlotInfo(switch_slot_tx_info->logical_slot_));
  if (stored_active_slot_)
    euicc_manager_->OnEuiccUpdated(stored_active_slot_.value(),
                                   EuiccSlotInfo());

  tx_queue_.pop_front();
  // Sending QMI messages immediately after switch slot leads to QMI errors
  // since slot switching takes time. If channel reacquisition fails despite
  // this delay, we retry after kInitRetryDelay.
  DisableQmi(kSwitchSlotDelay);
}

void ModemQrtr::ReceiveQmiOpenLogicalChannel(const qrtr_packet& packet) {
  QmiUimCommand cmd(QmiUimCommand::kOpenLogicalChannel);
  if (current_state_ != State::kLogicalChannelPending) {
    LOG(ERROR) << "Received unexpected QMI UIM response: " << cmd.ToString()
               << " in state " << current_state_;
    return;
  }

  uim_open_logical_channel_resp resp;
  unsigned int id;
  if (qmi_decode_message(&resp, &id, &packet, QMI_RESPONSE, cmd,
                         uim_open_logical_channel_resp_ei) < 0) {
    LOG(ERROR) << "Failed to decode QMI UIM response: " << cmd.ToString();
    return;
  }

  if (resp.result.result != 0) {
    VLOG(1) << cmd.ToString()
            << " Could not open channel to eSIM. This is expected if the "
               "active sim slot is not an eSIM. QMI response contained error: "
            << resp.result.error;
    return;
  }

  if (!resp.channel_id_valid) {
    LOG(ERROR) << "QMI UIM response for " << cmd.ToString()
               << " contained an invalid channel id";
    return;
  }

  channel_ = resp.channel_id;
  current_state_.Transition(State::kLogicalChannelOpened);
}

void ModemQrtr::ReceiveQmiSendApdu(const qrtr_packet& packet) {
  QmiUimCommand cmd(QmiUimCommand::kSendApdu);
  CHECK(tx_queue_.size());
  // Ensure that the queued element is for a kSendApdu command
  TxInfo* base_info = tx_queue_[0].info_.get();
  CHECK(base_info);
  CHECK(dynamic_cast<ApduTxInfo*>(base_info));

  static ResponseApdu payload;
  uim_send_apdu_resp resp;
  unsigned int id;
  ApduTxInfo* info = static_cast<ApduTxInfo*>(base_info);
  if (!qmi_decode_message(&resp, &id, &packet, QMI_RESPONSE, cmd,
                          uim_send_apdu_resp_ei)) {
    LOG(ERROR) << "Failed to decode QMI UIM response: " << cmd.ToString();
    return;
  }
  if (!CheckMessageSuccess(cmd, resp.result)) {
    if (info->callback_) {
      info->callback_(responses_, lpa::card::EuiccCard::kSendApduError);
      // ResponseCallback interface does not indicate a change in ownership of
      // |responses_|, but all callbacks should transfer ownership. Check for
      // sanity.
      // TODO(pholla) : Make ResponseCallback interface accept const responses_&
      // and clear responses_.
      CHECK(responses_.empty());
    }
    // Pop the apdu that caused the error.
    tx_queue_.pop_front();
    ReacquireChannel();
    return;
  }

  VLOG(2) << "Adding to payload from APDU response ("
          << resp.apdu_response_len - 2 << " bytes): "
          << base::HexEncode(resp.apdu_response, resp.apdu_response_len - 2);
  payload.AddData(resp.apdu_response, resp.apdu_response_len);
  if (payload.MorePayloadIncoming()) {
    // Make the next transmit operation be a request for more APDU data
    info->apdu_ = payload.CreateGetMoreCommand(false);
    return;
  } else if (info->apdu_.HasMoreFragments()) {
    // Send next fragment of APDU
    VLOG(1) << "Sending next APDU fragment...";
    TransmitFromQueue();
    return;
  }

  if (tx_queue_.empty() || static_cast<uint16_t>(id) != tx_queue_[0].id_) {
    LOG(ERROR) << "ModemQrtr received APDU from modem with unrecognized "
               << "transaction ID";
    return;
  }

  VLOG(1) << "Finished transaction " << tx_queue_[0].id_ / 2
          << " (id: " << tx_queue_[0].id_ << ")";
  responses_.push_back(payload.Release());
  if (info->callback_) {
    info->callback_(responses_, lpa::card::EuiccCard::kNoError);
    // ResponseCallback interface does not indicate a change in ownership of
    // |responses_|, but all callbacks should transfer ownership. Check for
    // sanity.
    CHECK(responses_.empty());
  }
  tx_queue_.pop_front();
}

void ModemQrtr::OnDataAvailable(SocketInterface* socket) {
  CHECK(socket == socket_.get());

  void* metadata = nullptr;
  SocketQrtr::PacketMetadata data = {0, 0};
  if (socket->GetType() == SocketInterface::Type::kQrtr) {
    metadata = reinterpret_cast<void*>(&data);
  }

  int bytes_received = socket->Recv(buffer_.data(), buffer_.size(), metadata);
  if (bytes_received < 0) {
    LOG(ERROR) << "Socket recv failed";
    return;
  }
  LOG(INFO) << "ModemQrtr recevied raw data (" << bytes_received
            << " bytes): " << base::HexEncode(buffer_.data(), bytes_received);
  ProcessQrtrPacket(data.node, data.port, bytes_received);
}

const lpa::proto::EuiccSpecVersion& ModemQrtr::GetCardVersion() {
  return spec_version_;
}

bool ModemQrtr::State::Transition(ModemQrtr::State::Value value) {
  bool valid_transition = false;
  switch (value) {
    case kUninitialized:
      valid_transition = true;
      break;
    case kUimStarted:
      // we reacquire the channel from kSendApduReady after profile (en/dis)able
      valid_transition =
          (value_ == kSendApduReady || value_ == kInitializeStarted);
      break;
    default:
      // Most states can only transition from the previous state.
      valid_transition = (value == value_ + 1);
  }

  if (valid_transition) {
    value_ = value;
  } else {
    LOG(ERROR) << "Cannot transition from state " << *this << " to state "
               << State(value);
  }
  return valid_transition;
}

void ModemQrtr::DisableQmi(base::TimeDelta duration) {
  qmi_disabled_ = true;
  VLOG(1) << "Blocking QMI messages for " << duration << "seconds";
  executor_->PostDelayedTask(
      FROM_HERE, base::BindOnce(&ModemQrtr::EnableQmi, base::Unretained(this)),
      duration);
}

void ModemQrtr::EnableQmi() {
  qmi_disabled_ = false;
  TransmitFromQueue();
}

}  // namespace hermes
