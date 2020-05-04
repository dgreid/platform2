// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VPN_MANAGER_L2TP_MANAGER_H_
#define VPN_MANAGER_L2TP_MANAGER_H_

#include <sys/socket.h>

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/optional.h>
#include <base/time/time.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "vpn-manager/service_manager.h"

namespace brillo {
class Process;
}

namespace vpn_manager {

// Manages the L2TP daemon.  This manager orchestrates configuring and
// launching the L2TP daemon, initiating the L2TP connection, and
// detecting when PPP has been set up.  It also sends user credentials
// to PPP through the L2TP control fifo unless the user has specified
// a PPP plugin should be used, which it will defer to.  Current
// implementation assumes a connection that has been stopped will not
// be started again with the same object.
class L2tpManager : public ServiceManager {
 public:
  L2tpManager(bool default_route,
              bool length_bit,
              bool require_chap,
              bool refuse_pap,
              bool require_authentication,
              const std::string& password,
              bool ppp_lcp_echo,
              int ppp_setup_timeout,
              const std::string& pppd_plugin,
              bool use_peer_dns,
              const std::string& user,
              bool system_config,
              const base::FilePath& temp_path);
  ~L2tpManager() override = default;

  // Getters for private values, needed for unit tests.
  int GetPppSetupTimeoutForTesting();

  // Setters for private values, needed for unit tests.
  void SetDefaultRouteForTesting(bool default_route);
  void SetPasswordForTesting(const std::string& password);
  void SetPppdPluginForTesting(const std::string& pppd_plugin);
  void SetPppLcpEchoForTesting(bool ppp_lcp_echo);
  void SetUsePeerDnsForTesting(bool use_peer_dns);
  void SetUserForTesting(const std::string& user);
  void SetSystemConfigForTesting(bool system_config);

  // Initialize the object using |remote_host|.  Returns false if
  // an illegal set of parameters has been given.  Has no side effects
  // other than setting up the object.
  bool Initialize(const sockaddr_storage& remote_address);

  bool Start() override;
  void Stop() override;
  int Poll() override;
  void ProcessOutput() override;
  void ProcessPppOutput();
  bool IsChild(pid_t pid) override;
  void OnSyslogOutput(const std::string& prefix,
                      const std::string& line) override;

  // Returns the stderr output file descriptor of our child process.
  int output_fd() const { return output_fd_; }

  // Returns the log output file descriptor of the ppp daemon.
  int ppp_output_fd() const { return ppp_output_fd_; }

 private:
  friend class L2tpManagerTest;
  FRIEND_TEST(L2tpManagerTest, FormatL2tpdConfiguration);
  FRIEND_TEST(L2tpManagerTest, FormatPppdConfiguration);
  FRIEND_TEST(L2tpManagerTest, Initiate);
  FRIEND_TEST(L2tpManagerTest, PollInitiateConnection);
  FRIEND_TEST(L2tpManagerTest, PollNothingIfRunning);
  FRIEND_TEST(L2tpManagerTest, PollTimeoutWaitingForControl);
  FRIEND_TEST(L2tpManagerTest, PollTimeoutWaitingForUp);
  FRIEND_TEST(L2tpManagerTest, PollTransitionToUp);
  FRIEND_TEST(L2tpManagerTest, PollWaitIfNotUpYet);
  FRIEND_TEST(L2tpManagerTest, Start);
  FRIEND_TEST(L2tpManagerTest, Terminate);

  bool CreatePppLogFifo();
  base::Optional<std::string> FormatL2tpdConfiguration(
      const std::string& ppp_config_path);
  std::string FormatPppdConfiguration();
  bool Initiate();
  bool Terminate();

  // Command line flags.
  bool default_route_;
  bool length_bit_;
  bool require_chap_;
  bool refuse_pap_;
  bool require_authentication_;
  std::string password_;
  bool ppp_lcp_echo_;
  int ppp_setup_timeout_;
  std::string pppd_plugin_;
  bool use_peer_dns_;
  std::string user_;
  bool system_config_;

  // Has the L2TP connection been initiated yet.
  bool was_initiated_;
  // l2tp daemon stderr pipe file descriptor.
  int output_fd_;
  // ppp daemon log pipe file descriptor.
  int ppp_output_fd_;
  // Start time of the l2tp daemon.
  base::TimeTicks start_ticks_;
  // Remote address for L2TP connection.
  sockaddr_storage remote_address_;
  // Remote address for L2TP connection (as a string).
  std::string remote_address_text_;
  // Last partial line read from output_fd_.
  std::string partial_output_line_;
  // Last partial line read from ppp_output_fd_.
  std::string partial_ppp_output_line_;
  // Path to a file whose existence indicates the ppp device is up.
  base::FilePath ppp_interface_path_;
  // Path to ppp daemon's log file.
  base::FilePath ppp_output_path_;
  // Path to l2tp daemon's control file.
  base::FilePath l2tpd_control_path_;
  // Running l2tp process.
  std::unique_ptr<brillo::Process> l2tpd_;
};

}  // namespace vpn_manager

#endif  // VPN_MANAGER_L2TP_MANAGER_H_
