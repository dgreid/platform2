// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_TRAFFIC_MONITOR_H_
#define SHILL_TRAFFIC_MONITOR_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/cancelable_callback.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/connection_info.h"
#include "shill/connection_info_reader.h"
#include "shill/refptr_types.h"
#include "shill/socket_info.h"

namespace shill {

class EventDispatcher;
class SocketInfoReader;

// TrafficMonitor detects certain abnormal scenarios on a network interface
// and notifies an observer of various scenarios via callbacks.
class TrafficMonitor {
 public:
  // Network problem detected by traffic monitor.
  enum NetworkProblem {
    kNetworkProblemCongestedTxQueue = 0,
    kNetworkProblemDNSFailure,
    kNetworkProblemMax
  };

  using NetworkProblemDetectedCallback = base::Callback<void(int)>;

  TrafficMonitor(
      const DeviceRefPtr& device,
      EventDispatcher* dispatcher,
      NetworkProblemDetectedCallback network_problem_detected_callback);
  virtual ~TrafficMonitor();

  // Starts traffic monitoring on the selected device.
  virtual void Start();

  // Stops traffic monitoring on the selected device.
  virtual void Stop();

 private:
  friend class TrafficMonitorTest;
  FRIEND_TEST(TrafficMonitorTest, SampleTrafficDnsFailureThenSuccess);
  FRIEND_TEST(TrafficMonitorTest, SampleTrafficDnsOutstanding);
  FRIEND_TEST(TrafficMonitorTest, SampleTrafficDnsStatsReset);
  FRIEND_TEST(TrafficMonitorTest, SampleTrafficDnsSuccessful);
  FRIEND_TEST(TrafficMonitorTest, SampleTrafficDnsTimedOut);
  FRIEND_TEST(TrafficMonitorTest, SampleTrafficDnsTimedOutInvalidProtocol);
  FRIEND_TEST(TrafficMonitorTest, SampleTrafficDnsTimedOutInvalidSourceIp);
  FRIEND_TEST(TrafficMonitorTest, SampleTrafficDnsTimedOutOutsideTimeWindow);
  FRIEND_TEST(TrafficMonitorTest, SampleTrafficNonDnsTimedOut);
  FRIEND_TEST(TrafficMonitorTest,
              SampleTrafficStuckTxQueueIncreasingQueueLength);
  FRIEND_TEST(TrafficMonitorTest, SampleTrafficStuckTxQueueSameQueueLength);
  FRIEND_TEST(TrafficMonitorTest, SampleTrafficStuckTxQueueVariousQueueLengths);
  FRIEND_TEST(TrafficMonitorTest, SampleTrafficUnstuckTxQueueNoConnection);
  FRIEND_TEST(TrafficMonitorTest, SampleTrafficUnstuckTxQueueStateChanged);
  FRIEND_TEST(TrafficMonitorTest, SampleTrafficUnstuckTxQueueZeroQueueLength);
  FRIEND_TEST(TrafficMonitorTest, StartAndStop);

  using IPPortToTxQueueLengthMap = std::map<std::string, uint64_t>;

  // The minimum number of samples that indicate an abnormal scenario
  // required to trigger the callback.
  static const int kMinimumFailedSamplesToTrigger;
  // The frequency at which to sample the TCP connections.
  static const int64_t kSamplingIntervalMilliseconds;
  // DNS port.
  static const uint16_t kDnsPort;
  // If a DNS "connection" time-to-expire falls below this threshold, then
  // it's considered a timed out DNS request.
  static const int64_t kDnsTimedOutThresholdSeconds;

  // Resets congested tx-queues tracking statistics.
  void ResetCongestedTxQueuesStats();
  void ResetCongestedTxQueuesStatsWithLogging();

  // Builds map of IP address/port to tx queue lengths from socket info vector.
  // Skips sockets not on device, tx queue length is 0, connection state is not
  // established or does not have a pending retransmit timer.
  IPPortToTxQueueLengthMap BuildIPPortToTxQueueLength(
      const std::vector<SocketInfo>& socket_infos);

  // Checks for congested tx-queue via network statistics.
  // Returns |true| if tx-queue is congested.
  bool IsCongestedTxQueues();

  // Resets failing DNS queries tracking statistics.
  void ResetDnsFailingStats();
  void ResetDnsFailingStatsWithLogging();

  // Checks to see for failed DNS queries.
  bool IsDnsFailing();

  // Samples traffic (e.g. receive and transmit byte counts) on the
  // selected device and invokes appropriate callbacks when certain
  // abnormal scenarios are detected.
  void SampleTraffic();

  // The device on which to perform traffic monitoring.
  DeviceRefPtr device_;

  // Dispatcher on which to create delayed tasks.
  EventDispatcher* dispatcher_;

  // Callback to invoke when TrafficMonitor needs to sample traffic
  // of the network interface.
  base::CancelableClosure sample_traffic_callback_;

  // Callback to invoke when we detect a network problem. Possible network
  // problems that can be detected are congested TCP TX queue and DNS failure.
  // Refer to enum NetworkProblem for all possible network problems that can be
  // detected by Traffic Monitor.
  NetworkProblemDetectedCallback network_problem_detected_callback_;

  // Reads and parses socket information from the system.
  std::unique_ptr<SocketInfoReader> socket_info_reader_;

  // Number of consecutive congested tx-queue cases sampled.
  int accummulated_congested_tx_queues_samples_;

  // Map of tx queue lengths from previous sampling pass.
  IPPortToTxQueueLengthMap old_tx_queue_lengths_;

  // Reads and parses connection information from the system.
  std::unique_ptr<ConnectionInfoReader> connection_info_reader_;

  // Number of consecutive sample intervals that contains failed DNS requests.
  int accummulated_dns_failures_samples_;

  DISALLOW_COPY_AND_ASSIGN(TrafficMonitor);
};

}  // namespace shill

#endif  // SHILL_TRAFFIC_MONITOR_H_
