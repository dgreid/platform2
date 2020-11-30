// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <brillo/process/process_mock.h>
#include <brillo/syslog_logging.h>
#include <brillo/test_helpers.h>
#include <gtest/gtest.h>

#include "vpn-manager/l2tp_manager.h"

using ::base::FilePath;
using ::base::StringPrintf;
using ::brillo::FindLog;
using ::brillo::ProcessMock;
using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;

namespace vpn_manager {

class L2tpManagerTest : public ::testing::Test {
 public:
  void SetUp() override {
    CHECK(temp_dir_.CreateUniqueTempDir());
    test_path_ = temp_dir_.GetPath().Append("l2tp_manager_testdir");
    base::DeletePathRecursively(test_path_);
    base::CreateDirectory(test_path_);
    remote_address_text_ = "1.2.3.4";
    ServiceManager::ConvertIPStringToSockAddr(remote_address_text_,
                                              &remote_address_);
    control_path_ = test_path_.Append("control");
    pppd_config_path_ = test_path_.Append("pppd.config");
    ppp_interface_path_ = test_path_.Append("ppp0");
    l2tpd_ = new ProcessMock;
    l2tp_.reset(new L2tpManager(true,          // default_route
                                true,          // length_bit
                                true,          // require_chap
                                false,         // refuse_pap
                                true,          // require_authentication
                                "",            // password
                                true,          // ppp_lcp_echo
                                10,            // ppp_setup_timeout
                                "",            // pppd_plugin
                                true,          // usepeerdns
                                "",            // user
                                true,          // systemconfig
                                test_path_));  // temp_path
    l2tp_->l2tpd_.reset(l2tpd_);
    l2tp_->l2tpd_control_path_ = control_path_;
    l2tp_->ppp_interface_path_ = ppp_interface_path_;
    l2tp_->SetUserForTesting("me");
    EXPECT_TRUE(l2tp_->Initialize(remote_address_));
  }

 protected:
  std::string GetExpectedConfig(std::string remote_address_text, bool debug);

  std::string remote_address_text_;
  sockaddr_storage remote_address_;
  FilePath test_path_;
  FilePath control_path_;
  FilePath pppd_config_path_;
  FilePath ppp_interface_path_;
  std::unique_ptr<L2tpManager> l2tp_;
  ProcessMock* l2tpd_;
  base::ScopedTempDir temp_dir_;
};

std::string L2tpManagerTest::GetExpectedConfig(std::string remote_address_text,
                                               bool debug) {
  return StringPrintf(
      "[lac managed]\n"
      "lns = %s\n"
      "require chap = yes\n"
      "refuse pap = no\n"
      "require authentication = yes\n"
      "name = me\n"
      "%s"
      "pppoptfile = %s/pppd.config\n"
      "length bit = yes\n"
      "bps = 1000000\n"
      "redial = yes\n"
      "redial timeout = 2\n"
      "max redials = 30\n",
      remote_address_text.c_str(), debug ? "ppp debug = yes\n" : "",
      test_path_.value().c_str());
}

TEST_F(L2tpManagerTest, FormatL2tpdConfiguration) {
  logging::SetMinLogLevel(0);
  EXPECT_EQ(GetExpectedConfig(remote_address_text_, false),
            l2tp_->FormatL2tpdConfiguration(pppd_config_path_.value()));
  logging::SetMinLogLevel(-4);
  EXPECT_EQ(GetExpectedConfig(remote_address_text_, true),
            l2tp_->FormatL2tpdConfiguration(pppd_config_path_.value()));
}

TEST_F(L2tpManagerTest, FormatPppdConfiguration) {
  const char kBaseExpected[] =
      "ipcp-accept-local\n"
      "ipcp-accept-remote\n"
      "refuse-eap\n"
      "noccp\n"
      "noauth\n"
      "crtscts\n"
      "mtu 1410\n"
      "mru 1410\n"
      "lock\n"
      "connect-delay 5000\n";

  logging::SetMinLogLevel(0);
  l2tp_->SetPppLcpEchoForTesting(false);
  l2tp_->SetDefaultRouteForTesting(false);
  l2tp_->SetUsePeerDnsForTesting(false);
  std::string expected(kBaseExpected);
  expected.append("nodefaultroute\n");
  EXPECT_EQ(expected, l2tp_->FormatPppdConfiguration());
  expected = kBaseExpected;
  l2tp_->SetDefaultRouteForTesting(true);
  expected.append("defaultroute\n");
  EXPECT_EQ(expected, l2tp_->FormatPppdConfiguration());
  l2tp_->SetPppLcpEchoForTesting(true);
  expected.append(
      "lcp-echo-failure 4\n"
      "lcp-echo-interval 30\n");
  EXPECT_EQ(expected, l2tp_->FormatPppdConfiguration());
  l2tp_->ppp_output_fd_ = 4;
  l2tp_->ppp_output_path_ = FilePath("/tmp/pppd.log");
  expected.append("logfile /tmp/pppd.log\n");
  EXPECT_EQ(expected, l2tp_->FormatPppdConfiguration());
  l2tp_->SetUsePeerDnsForTesting(true);
  expected.append("usepeerdns\n");
  EXPECT_EQ(expected, l2tp_->FormatPppdConfiguration());
  l2tp_->SetSystemConfigForTesting(false);
  expected.append("nosystemconfig\n");
  EXPECT_EQ(expected, l2tp_->FormatPppdConfiguration());
  l2tp_->SetPppdPluginForTesting("myplugin");
  expected.append("plugin myplugin\n");
  EXPECT_EQ(expected, l2tp_->FormatPppdConfiguration());
  logging::SetMinLogLevel(-2);
  expected.append("debug\n");
  EXPECT_EQ(expected, l2tp_->FormatPppdConfiguration());
}

TEST_F(L2tpManagerTest, Initiate) {
  l2tp_->SetUserForTesting("me");
  l2tp_->SetPasswordForTesting("password");
  EXPECT_TRUE(l2tp_->Initiate());
  ExpectFileEquals("c managed me password\n", control_path_.value().c_str());
  l2tp_->SetPppdPluginForTesting("set");
  EXPECT_TRUE(l2tp_->Initiate());
  ExpectFileEquals("c managed\n", control_path_.value().c_str());
}

TEST_F(L2tpManagerTest, Terminate) {
  EXPECT_TRUE(l2tp_->Terminate());
  ExpectFileEquals("d managed\n", control_path_.value().c_str());
}

TEST_F(L2tpManagerTest, Start) {
  InSequence unused;
  const int kMockFd = 567;

  EXPECT_CALL(*l2tpd_, Reset(0));
  EXPECT_CALL(*l2tpd_, AddArg(L2TPD));
  EXPECT_CALL(*l2tpd_, AddArg("-c"));
  EXPECT_CALL(*l2tpd_, AddArg(test_path_.value() + "/l2tpd.conf"));
  EXPECT_CALL(*l2tpd_, AddArg("-C"));
  EXPECT_CALL(*l2tpd_, AddArg(test_path_.value() + "/l2tpd.control"));
  EXPECT_CALL(*l2tpd_, AddArg("-D"));
  EXPECT_CALL(*l2tpd_, AddArg("-p"));
  EXPECT_CALL(*l2tpd_, AddArg("/run/l2tpipsec_vpn/xl2tpd.pid"));
  EXPECT_CALL(*l2tpd_, RedirectUsingPipe(STDERR_FILENO, false));
  EXPECT_CALL(*l2tpd_, Start());
  EXPECT_CALL(*l2tpd_, GetPipe(STDERR_FILENO)).WillOnce(Return(kMockFd));

  EXPECT_TRUE(l2tp_->Start());
  EXPECT_EQ(kMockFd, l2tp_->output_fd());
  EXPECT_FALSE(l2tp_->start_ticks_.is_null());
  EXPECT_FALSE(l2tp_->was_initiated_);
}

TEST_F(L2tpManagerTest, PollWaitIfNotUpYet) {
  l2tp_->start_ticks_ = base::TimeTicks::Now();
  EXPECT_EQ(1000, l2tp_->Poll());
}

TEST_F(L2tpManagerTest, PollTimeoutWaitingForControl) {
  l2tp_->start_ticks_ =
      base::TimeTicks::Now() -
      base::TimeDelta::FromSeconds(l2tp_->GetPppSetupTimeoutForTesting() + 1);
  EXPECT_EQ(1000, l2tp_->Poll());
  EXPECT_TRUE(FindLog("PPP setup timed out"));
  EXPECT_TRUE(l2tp_->was_stopped());
  EXPECT_FALSE(base::PathExists(control_path_));
}

TEST_F(L2tpManagerTest, PollTimeoutWaitingForUp) {
  l2tp_->start_ticks_ =
      base::TimeTicks::Now() -
      base::TimeDelta::FromSeconds(l2tp_->GetPppSetupTimeoutForTesting() + 1);
  l2tp_->was_initiated_ = true;
  EXPECT_EQ(1000, l2tp_->Poll());
  EXPECT_TRUE(FindLog("PPP setup timed out"));
  EXPECT_TRUE(l2tp_->was_stopped());
  ExpectFileEquals("d managed\n", control_path_.value().c_str());
}

TEST_F(L2tpManagerTest, PollInitiateConnection) {
  l2tp_->start_ticks_ = base::TimeTicks::Now();
  base::WriteFile(control_path_, "", 0);
  EXPECT_FALSE(l2tp_->was_initiated_);
  l2tp_->SetPppdPluginForTesting("set");
  EXPECT_EQ(1000, l2tp_->Poll());
  EXPECT_TRUE(l2tp_->was_initiated_);
  ExpectFileEquals("c managed\n", control_path_.value().c_str());
}

TEST_F(L2tpManagerTest, PollTransitionToUp) {
  l2tp_->start_ticks_ = base::TimeTicks::Now();
  l2tp_->was_initiated_ = true;
  EXPECT_FALSE(l2tp_->is_running());
  base::WriteFile(ppp_interface_path_, "", 0);
  EXPECT_EQ(-1, l2tp_->Poll());
  EXPECT_TRUE(FindLog("L2TP connection now up"));
  EXPECT_TRUE(l2tp_->is_running());
}

TEST_F(L2tpManagerTest, PollNothingIfRunning) {
  l2tp_->is_running_ = true;
  EXPECT_EQ(-1, l2tp_->Poll());
}

}  // namespace vpn_manager
