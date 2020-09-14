// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/traffic_monitor.h"

#include <utility>

#include <base/bind.h>
#include <base/strings/stringprintf.h>
#include <netinet/in.h>

#include "shill/device.h"
#include "shill/device_info.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/socket_info_reader.h"

using base::StringPrintf;
using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kLink;
static string ObjectID(const Device* d) {
  return d->link_name();
}
}  // namespace Logging

// static
const uint16_t TrafficMonitor::kDnsPort = 53;
const int64_t TrafficMonitor::kDnsTimedOutThresholdSeconds = 15;
const int TrafficMonitor::kMinimumFailedSamplesToTrigger = 2;
const int64_t TrafficMonitor::kSamplingIntervalMilliseconds = 5000;

TrafficMonitor::TrafficMonitor(
    const DeviceRefPtr& device,
    EventDispatcher* dispatcher,
    NetworkProblemDetectedCallback network_problem_detected_callback)
    : device_(device),
      dispatcher_(dispatcher),
      network_problem_detected_callback_(
          std::move(network_problem_detected_callback)),
      socket_info_reader_(new SocketInfoReader),
      accummulated_congested_tx_queues_samples_(0),
      connection_info_reader_(new ConnectionInfoReader),
      accummulated_dns_failures_samples_(0) {}

TrafficMonitor::~TrafficMonitor() {
  Stop();
}

void TrafficMonitor::Start() {
  SLOG(device_.get(), 2) << __func__;
  Stop();

  sample_traffic_callback_.Reset(
      base::Bind(&TrafficMonitor::SampleTraffic, base::Unretained(this)));
  dispatcher_->PostDelayedTask(FROM_HERE, sample_traffic_callback_.callback(),
                               kSamplingIntervalMilliseconds);
}

void TrafficMonitor::Stop() {
  SLOG(device_.get(), 2) << __func__;
  sample_traffic_callback_.Cancel();
  ResetCongestedTxQueuesStats();
  ResetDnsFailingStats();
}

void TrafficMonitor::ResetCongestedTxQueuesStats() {
  accummulated_congested_tx_queues_samples_ = 0;
}

void TrafficMonitor::ResetCongestedTxQueuesStatsWithLogging() {
  SLOG(device_.get(), 2) << __func__ << ": Tx-queues decongested";
  ResetCongestedTxQueuesStats();
}

TrafficMonitor::IPPortToTxQueueLengthMap
TrafficMonitor::BuildIPPortToTxQueueLength(
    const vector<SocketInfo>& socket_infos) {
  SLOG(device_.get(), 3) << __func__;

  IPPortToTxQueueLengthMap tx_queue_lengths;
  string device_ipv4;
  if (device_->ipconfig())
    device_ipv4 = device_->ipconfig()->properties().address;
  // NB: we may have multiple IPv6 addresses; we do a best-effort filter using
  // the preferred address, even though this is imprecise.
  string device_ipv6;
  if (device_->ip6config())
    device_ipv6 = device_->ip6config()->properties().address;

  for (const auto& info : socket_infos) {
    string socket_ip = info.local_ip_address.ToString();
    SLOG(device_.get(), 4) << "SocketInfo(IP=" << socket_ip
                           << ", TX=" << info.transmit_queue_value
                           << ", State=" << info.connection_state
                           << ", TimerState=" << info.timer_state;

    if ((socket_ip != device_ipv4 && socket_ip != device_ipv6) ||
        info.transmit_queue_value == 0 ||
        info.connection_state != SocketInfo::kConnectionStateEstablished ||
        (info.timer_state != SocketInfo::kTimerStateRetransmitTimerPending &&
         info.timer_state !=
             SocketInfo::kTimerStateZeroWindowProbeTimerPending)) {
      SLOG(device_.get(), 4) << "Connection Filtered.";
      continue;
    }
    SLOG(device_.get(), 3) << "Monitoring connection: TX="
                           << info.transmit_queue_value
                           << " TimerState=" << info.timer_state;

    string local_ip_port = StringPrintf(
        "%s:%d", info.local_ip_address.ToString().c_str(), info.local_port);
    tx_queue_lengths[local_ip_port] = info.transmit_queue_value;
  }
  return tx_queue_lengths;
}

bool TrafficMonitor::IsCongestedTxQueues() {
  SLOG(device_.get(), 4) << __func__;
  vector<SocketInfo> socket_infos;
  if (!socket_info_reader_->LoadTcpSocketInfo(&socket_infos) ||
      socket_infos.empty()) {
    SLOG(device_.get(), 3) << __func__ << ": Empty socket info";
    ResetCongestedTxQueuesStatsWithLogging();
    return false;
  }
  bool congested_tx_queues = true;
  IPPortToTxQueueLengthMap curr_tx_queue_lengths =
      BuildIPPortToTxQueueLength(socket_infos);
  if (curr_tx_queue_lengths.empty()) {
    SLOG(device_.get(), 3) << __func__ << ": No interesting socket info";
    ResetCongestedTxQueuesStatsWithLogging();
  } else {
    for (const auto& length_entry : old_tx_queue_lengths_) {
      IPPortToTxQueueLengthMap::iterator curr_tx_queue_it =
          curr_tx_queue_lengths.find(length_entry.first);
      if (curr_tx_queue_it == curr_tx_queue_lengths.end() ||
          curr_tx_queue_it->second < length_entry.second) {
        congested_tx_queues = false;
        // TODO(armansito): If we had a false positive earlier, we may
        // want to correct it here by invoking a "connection back to normal
        // callback", so that the OutOfCredits property can be set to
        // false.
        break;
      }
    }
    if (congested_tx_queues) {
      ++accummulated_congested_tx_queues_samples_;
      SLOG(device_.get(), 2)
          << __func__ << ": Congested tx-queues detected ("
          << accummulated_congested_tx_queues_samples_ << ")";
    }
  }
  old_tx_queue_lengths_ = curr_tx_queue_lengths;

  return congested_tx_queues;
}

void TrafficMonitor::ResetDnsFailingStats() {
  accummulated_dns_failures_samples_ = 0;
}

void TrafficMonitor::ResetDnsFailingStatsWithLogging() {
  SLOG(device_.get(), 2) << __func__ << ": DNS queries restored";
  ResetDnsFailingStats();
}

bool TrafficMonitor::IsDnsFailing() {
  SLOG(device_.get(), 4) << __func__;
  vector<ConnectionInfo> connection_infos;
  if (!connection_info_reader_->LoadConnectionInfo(&connection_infos) ||
      connection_infos.empty()) {
    SLOG(device_.get(), 3) << __func__ << ": Empty connection info";
  } else {
    // The time-to-expire counter is used to determine when a DNS request
    // has timed out.  This counter is the number of seconds remaining until
    // the entry is removed from the system IP connection tracker.  The
    // default time is 30 seconds.  This is too long of a wait.  Instead, we
    // want to time out at |kDnsTimedOutThresholdSeconds|.  Unfortunately,
    // we cannot simply look for entries less than
    // |kDnsTimedOutThresholdSeconds| because we will count the entry
    // multiple times once its time-to-expire is less than
    // |kDnsTimedOutThresholdSeconds|.  To ensure that we only count an
    // entry once, we look for entries in this time window between
    // |kDnsTimedOutThresholdSeconds| and |kDnsTimedOutLowerThresholdSeconds|.
    const int64_t kDnsTimedOutLowerThresholdSeconds =
        kDnsTimedOutThresholdSeconds - kSamplingIntervalMilliseconds / 1000;
    string device_ipv4;
    if (device_->ipconfig())
      device_ipv4 = device_->ipconfig()->properties().address;
    // NB: we may have multiple IPv6 addresses; we do a best-effort filter using
    // the preferred address, even though this is imprecise.
    string device_ipv6;
    if (device_->ip6config())
      device_ipv6 = device_->ip6config()->properties().address;

    for (const auto& info : connection_infos) {
      const auto& source_ip = info.original_source_ip_address.ToString();

      if (info.protocol != IPPROTO_UDP ||
          info.time_to_expire_seconds > kDnsTimedOutThresholdSeconds ||
          info.time_to_expire_seconds <= kDnsTimedOutLowerThresholdSeconds ||
          !info.is_unreplied ||
          (source_ip != device_ipv4 && source_ip != device_ipv6) ||
          info.original_destination_port != kDnsPort)
        continue;

      ++accummulated_dns_failures_samples_;
      SLOG(device_.get(), 2) << __func__ << ": DNS failures detected ("
                             << accummulated_dns_failures_samples_ << ")";
      return true;
    }
  }
  ResetDnsFailingStatsWithLogging();
  return false;
}

void TrafficMonitor::SampleTraffic() {
  SLOG(device_.get(), 3) << __func__;

  // Schedule the sample callback first, so it is possible for the network
  // problem callback to stop the traffic monitor.
  dispatcher_->PostDelayedTask(FROM_HERE, sample_traffic_callback_.callback(),
                               kSamplingIntervalMilliseconds);

  if (IsCongestedTxQueues() && accummulated_congested_tx_queues_samples_ ==
                                   kMinimumFailedSamplesToTrigger) {
    LOG(WARNING) << "Congested tx queues detected, out-of-credits?";
    network_problem_detected_callback_.Run(kNetworkProblemCongestedTxQueue);
  } else if (IsDnsFailing() && accummulated_dns_failures_samples_ ==
                                   kMinimumFailedSamplesToTrigger) {
    LOG(WARNING) << "DNS queries failing, out-of-credits?";
    network_problem_detected_callback_.Run(kNetworkProblemDNSFailure);
  }
}

}  // namespace shill
