// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/firewall.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "patchpanel/mock_firewall.h"

namespace patchpanel {

using testing::_;
using testing::Return;

class FirewallTest : public testing::Test {
 public:
  FirewallTest() = default;
  FirewallTest(const FirewallTest&) = delete;
  FirewallTest& operator=(const FirewallTest&) = delete;

  ~FirewallTest() override = default;

 protected:
  void SetMockExpectations(MockFirewall* firewall, bool success) {
    // Empty criterion matches all commands.
    firewall->SetRunInMinijailFailCriterion(std::vector<std::string>(), true,
                                            success);
  }

  void SetMockExpectationsPerExecutable(MockFirewall* firewall,
                                        bool ip4_success,
                                        bool ip6_success) {
    if (!ip4_success)
      firewall->SetRunInMinijailFailCriterion(
          std::vector<std::string>({kIpTablesPath}), true /* repeat */,
          false /* omit_failure */);
    if (!ip6_success)
      firewall->SetRunInMinijailFailCriterion(
          std::vector<std::string>({kIp6TablesPath}), true, false);
  }
};

TEST_F(FirewallTest, Port0Fails) {
  MockFirewall mock_firewall;
  // Don't fail on anything.
  int id = mock_firewall.SetRunInMinijailFailCriterion(
      std::vector<std::string>(), true, true);

  // Try to punch hole for TCP port 0, port 0 is not a valid port.
  EXPECT_FALSE(
      mock_firewall.AddAcceptRules(ModifyPortRuleRequest::TCP, 0, "iface"));
  // Try to punch hole for UDP port 0, port 0 is not a valid port.
  EXPECT_FALSE(
      mock_firewall.AddAcceptRules(ModifyPortRuleRequest::UDP, 0, "iface"));

  // Try to plug hole for TCP port 0, port 0 is not a valid port.
  EXPECT_FALSE(
      mock_firewall.DeleteAcceptRules(ModifyPortRuleRequest::TCP, 0, "iface"));
  // Try to plug hole for UDP port 0, port 0 is not a valid port.
  EXPECT_FALSE(
      mock_firewall.DeleteAcceptRules(ModifyPortRuleRequest::UDP, 0, "iface"));

  // We should not be adding/removing any rules for port 0.
  EXPECT_EQ(mock_firewall.GetRunInMinijailCriterionMatchCount(id), 0);
}

TEST_F(FirewallTest, ValidInterfaceName) {
  MockFirewall mock_firewall;
  SetMockExpectations(&mock_firewall, true /* success */);

  EXPECT_TRUE(mock_firewall.AddAcceptRules(ModifyPortRuleRequest::TCP, 80,
                                           "shortname"));
  EXPECT_TRUE(mock_firewall.AddAcceptRules(ModifyPortRuleRequest::UDP, 53,
                                           "shortname"));
  EXPECT_TRUE(mock_firewall.AddAcceptRules(ModifyPortRuleRequest::TCP, 80,
                                           "middle-dash"));
  EXPECT_TRUE(mock_firewall.AddAcceptRules(ModifyPortRuleRequest::UDP, 53,
                                           "middle-dash"));
  EXPECT_TRUE(mock_firewall.AddAcceptRules(ModifyPortRuleRequest::TCP, 80,
                                           "middle.dot"));
  EXPECT_TRUE(mock_firewall.AddAcceptRules(ModifyPortRuleRequest::UDP, 53,
                                           "middle.dot"));
}

TEST_F(FirewallTest, InvalidInterfaceName) {
  MockFirewall mock_firewall;
  int id = mock_firewall.SetRunInMinijailFailCriterion(
      std::vector<std::string>(), true, true);

  EXPECT_FALSE(mock_firewall.AddAcceptRules(ModifyPortRuleRequest::TCP, 80,
                                            "reallylonginterfacename"));
  EXPECT_FALSE(mock_firewall.AddAcceptRules(ModifyPortRuleRequest::TCP, 80,
                                            "with spaces"));
  EXPECT_FALSE(mock_firewall.AddAcceptRules(ModifyPortRuleRequest::TCP, 80,
                                            "with$ymbols"));
  EXPECT_FALSE(mock_firewall.AddAcceptRules(ModifyPortRuleRequest::TCP, 80,
                                            "-startdash"));
  EXPECT_FALSE(
      mock_firewall.AddAcceptRules(ModifyPortRuleRequest::TCP, 80, "enddash-"));
  EXPECT_FALSE(mock_firewall.AddAcceptRules(ModifyPortRuleRequest::TCP, 80,
                                            ".startdot"));
  EXPECT_FALSE(
      mock_firewall.AddAcceptRules(ModifyPortRuleRequest::TCP, 80, "enddot."));

  EXPECT_FALSE(mock_firewall.AddAcceptRules(ModifyPortRuleRequest::UDP, 53,
                                            "reallylonginterfacename"));
  EXPECT_FALSE(mock_firewall.AddAcceptRules(ModifyPortRuleRequest::UDP, 53,
                                            "with spaces"));
  EXPECT_FALSE(mock_firewall.AddAcceptRules(ModifyPortRuleRequest::UDP, 53,
                                            "with$ymbols"));
  EXPECT_FALSE(mock_firewall.AddAcceptRules(ModifyPortRuleRequest::UDP, 53,
                                            "-startdash"));
  EXPECT_FALSE(
      mock_firewall.AddAcceptRules(ModifyPortRuleRequest::UDP, 53, "enddash-"));
  EXPECT_FALSE(mock_firewall.AddAcceptRules(ModifyPortRuleRequest::UDP, 53,
                                            ".startdot"));
  EXPECT_FALSE(
      mock_firewall.AddAcceptRules(ModifyPortRuleRequest::UDP, 53, "enddot."));

  EXPECT_FALSE(mock_firewall.DeleteAcceptRules(ModifyPortRuleRequest::TCP, 80,
                                               "reallylonginterfacename"));
  EXPECT_FALSE(mock_firewall.DeleteAcceptRules(ModifyPortRuleRequest::TCP, 80,
                                               "with spaces"));
  EXPECT_FALSE(mock_firewall.DeleteAcceptRules(ModifyPortRuleRequest::TCP, 80,
                                               "with$ymbols"));
  EXPECT_FALSE(mock_firewall.DeleteAcceptRules(ModifyPortRuleRequest::TCP, 80,
                                               "-startdash"));
  EXPECT_FALSE(mock_firewall.DeleteAcceptRules(ModifyPortRuleRequest::TCP, 80,
                                               "enddash-"));
  EXPECT_FALSE(mock_firewall.DeleteAcceptRules(ModifyPortRuleRequest::TCP, 80,
                                               ".startdot"));
  EXPECT_FALSE(mock_firewall.DeleteAcceptRules(ModifyPortRuleRequest::TCP, 80,
                                               "enddot."));

  EXPECT_FALSE(mock_firewall.DeleteAcceptRules(ModifyPortRuleRequest::UDP, 53,
                                               "reallylonginterfacename"));
  EXPECT_FALSE(mock_firewall.DeleteAcceptRules(ModifyPortRuleRequest::UDP, 53,
                                               "with spaces"));
  EXPECT_FALSE(mock_firewall.DeleteAcceptRules(ModifyPortRuleRequest::UDP, 53,
                                               "with$ymbols"));
  EXPECT_FALSE(mock_firewall.DeleteAcceptRules(ModifyPortRuleRequest::UDP, 53,
                                               "-startdash"));
  EXPECT_FALSE(mock_firewall.DeleteAcceptRules(ModifyPortRuleRequest::UDP, 53,
                                               "enddash-"));
  EXPECT_FALSE(mock_firewall.DeleteAcceptRules(ModifyPortRuleRequest::UDP, 53,
                                               ".startdot"));
  EXPECT_FALSE(mock_firewall.DeleteAcceptRules(ModifyPortRuleRequest::UDP, 53,
                                               "enddot."));

  // We should not be adding/removing any rules for invalid interface names.
  EXPECT_EQ(mock_firewall.GetRunInMinijailCriterionMatchCount(id), 0);
}

TEST_F(FirewallTest, AddAcceptRules_Fails) {
  MockFirewall mock_firewall;
  SetMockExpectations(&mock_firewall, false /* success */);

  // Punch hole for TCP port 80, should fail.
  ASSERT_FALSE(
      mock_firewall.AddAcceptRules(ModifyPortRuleRequest::TCP, 80, "iface"));
  // Punch hole for UDP port 53, should fail.
  ASSERT_FALSE(
      mock_firewall.AddAcceptRules(ModifyPortRuleRequest::UDP, 53, "iface"));
}

TEST_F(FirewallTest, AddAcceptRules_Ipv6Fails) {
  MockFirewall mock_firewall;
  SetMockExpectationsPerExecutable(&mock_firewall, true /* ip4_success */,
                                   false /* ip6_success */);

  // Punch hole for TCP port 80, should fail because 'ip6tables' fails.
  ASSERT_FALSE(
      mock_firewall.AddAcceptRules(ModifyPortRuleRequest::TCP, 80, "iface"));
  // Punch hole for UDP port 53, should fail because 'ip6tables' fails.
  ASSERT_FALSE(
      mock_firewall.AddAcceptRules(ModifyPortRuleRequest::UDP, 53, "iface"));
}

TEST_F(FirewallTest, Port0LockdownFails) {
  MockFirewall mock_firewall;
  // Don't fail on anything.
  int id = mock_firewall.SetRunInMinijailFailCriterion(
      std::vector<std::string>(), true, true);

  // Try to lock down TCP port 0, port 0 is not a valid port.
  EXPECT_FALSE(
      mock_firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::TCP, 0));
  // Try to lock down UDP port 0, port 0 is not a valid port.
  EXPECT_FALSE(
      mock_firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::UDP, 0));

  // We should not be adding/removing any rules for port 0.
  EXPECT_EQ(mock_firewall.GetRunInMinijailCriterionMatchCount(id), 0);
}

TEST_F(FirewallTest, AddLoopbackLockdownRules_Success) {
  MockFirewall mock_firewall;
  SetMockExpectations(&mock_firewall, true /* success */);
  ASSERT_TRUE(
      mock_firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::TCP, 80));
  ASSERT_TRUE(
      mock_firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::UDP, 53));
  ASSERT_TRUE(
      mock_firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::TCP, 1234));
  ASSERT_TRUE(
      mock_firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::TCP, 8080));
}

TEST_F(FirewallTest, AddLoopbackLockdownRules_Fails) {
  MockFirewall mock_firewall;
  SetMockExpectations(&mock_firewall, false /* success */);

  // Lock down TCP port 80, should fail.
  ASSERT_FALSE(
      mock_firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::TCP, 80));
  // Lock down UDP port 53, should fail.
  ASSERT_FALSE(
      mock_firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::UDP, 53));
}

TEST_F(FirewallTest, AddLoopbackLockdownRules_Ipv6Fails) {
  MockFirewall mock_firewall;
  SetMockExpectationsPerExecutable(&mock_firewall, true /* ip4_success */,
                                   false /* ip6_success */);

  // Lock down TCP port 80, should fail because 'ip6tables' fails.
  ASSERT_FALSE(
      mock_firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::TCP, 80));
  // Lock down UDP port 53, should fail because 'ip6tables' fails.
  ASSERT_FALSE(
      mock_firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::UDP, 53));
}

TEST_F(FirewallTest, AddIpv4ForwardRules_InvalidArguments) {
  MockFirewall mock_firewall;
  // Don't fail on anything.
  mock_firewall.SetRunInMinijailFailCriterion({}, true, true);

  // Invalid input interface. No iptables commands are issued.
  ASSERT_FALSE(mock_firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, "", 80, "-startdash", "100.115.92.5", 8080));
  ASSERT_FALSE(mock_firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, "", 80, "enddash-", "100.115.92.5", 8080));
  ASSERT_FALSE(mock_firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, "", 80, ".startdot", "100.115.92.5", 8080));
  ASSERT_FALSE(mock_firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, "", 80, "enddot.", "100.115.92.5", 8080));
  ASSERT_TRUE(mock_firewall.GetAllCommands().empty());
  mock_firewall.ResetStoredCommands();

  // Empty interface. No iptables commands are issused.
  ASSERT_FALSE(mock_firewall.AddIpv4ForwardRule(ModifyPortRuleRequest::TCP, "",
                                                80, "", "100.115.92.5", 8080));
  ASSERT_FALSE(mock_firewall.AddIpv4ForwardRule(ModifyPortRuleRequest::UDP, "",
                                                80, "", "100.115.92.5", 8080));
  ASSERT_FALSE(mock_firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, "", 80, "", "100.115.92.5", 8080));
  ASSERT_FALSE(mock_firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, "", 80, "", "100.115.92.5", 8080));
  ASSERT_TRUE(mock_firewall.GetAllCommands().empty());
  mock_firewall.ResetStoredCommands();

  // Invalid input dst address. No iptables commands are issused.
  ASSERT_FALSE(mock_firewall.AddIpv4ForwardRule(ModifyPortRuleRequest::TCP,
                                                "256.256.256.256", 80, "iface",
                                                "100.115.92.5", 8080));
  ASSERT_FALSE(mock_firewall.AddIpv4ForwardRule(ModifyPortRuleRequest::UDP,
                                                "qodpjqwpod", 80, "iface",
                                                "100.115.92.5", 8080));
  // Trying to delete an IPv4 forward rule with an invalid input address will
  // still trigger an explicit iptables -D command for the associated FORWARD
  // ACCEPT rule. Two such commands are expected.
  ASSERT_FALSE(mock_firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, "1.1", 80, "iface", "100.115.92.5", 8080));
  ASSERT_FALSE(mock_firewall.DeleteIpv4ForwardRule(ModifyPortRuleRequest::UDP,
                                                   "2001:db8::1", 80, "iface",
                                                   "100.115.92.5", 8080));
  {
    std::vector<std::string> expected_commands = {
        "/sbin/iptables -t filter -D FORWARD -i iface -p tcp -d 100.115.92.5 "
        "--dport 8080 -j ACCEPT -w",
        "/sbin/iptables -t filter -D FORWARD -i iface -p udp -d 100.115.92.5 "
        "--dport 8080 -j ACCEPT -w",
    };
    std::vector<std::string> issued_commands = mock_firewall.GetAllCommands();
    EXPECT_EQ(expected_commands.size(), issued_commands.size());
    for (int i = 0; i < expected_commands.size(); i++) {
      ASSERT_EQ(expected_commands[i], issued_commands[i]);
    }
  }
  mock_firewall.ResetStoredCommands();

  // Invalid input dst port.
  ASSERT_FALSE(mock_firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, "", 0, "iface", "100.115.92.5", 8080));
  ASSERT_FALSE(mock_firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, "", 0, "iface", "100.115.92.5", 8080));
  // Trying to delete an IPv4 forward rule with an invalid input port will
  // still trigger an explicit iptables -D command for the associated FORWARD
  // ACCEPT rule. Two such commands are expected.
  ASSERT_FALSE(mock_firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, "", 0, "iface", "100.115.92.5", 8080));
  ASSERT_FALSE(mock_firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, "", 0, "iface", "100.115.92.5", 8080));
  {
    std::vector<std::string> expected_commands = {
        "/sbin/iptables -t filter -D FORWARD -i iface -p tcp -d 100.115.92.5 "
        "--dport 8080 -j ACCEPT -w",
        "/sbin/iptables -t filter -D FORWARD -i iface -p udp -d 100.115.92.5 "
        "--dport 8080 -j ACCEPT -w",
    };
    std::vector<std::string> issued_commands = mock_firewall.GetAllCommands();
    EXPECT_EQ(expected_commands.size(), issued_commands.size());
    for (int i = 0; i < expected_commands.size(); i++) {
      ASSERT_EQ(expected_commands[i], issued_commands[i]);
    }
  }
  mock_firewall.ResetStoredCommands();

  // Invalid output dst address. No iptables commands are issused.
  ASSERT_FALSE(mock_firewall.AddIpv4ForwardRule(ModifyPortRuleRequest::TCP, "",
                                                80, "iface", "", 8080));
  ASSERT_FALSE(mock_firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, "", 80, "iface", "qodpjqwpod", 8080));
  ASSERT_FALSE(mock_firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, "", 80, "iface", "1.1", 8080));
  ASSERT_FALSE(mock_firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, "", 80, "iface", "2001:db8::1", 8080));
  ASSERT_TRUE(mock_firewall.GetAllCommands().empty());
  mock_firewall.ResetStoredCommands();

  // Invalid output dst port. No iptables commands are issused.
  ASSERT_FALSE(mock_firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, "", 80, "iface", "100.115.92.5", 0));
  ASSERT_FALSE(mock_firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, "", 80, "iface", "100.115.92.5", 0));
  ASSERT_FALSE(mock_firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, "", 80, "iface", "100.115.92.5", 0));
  ASSERT_FALSE(mock_firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, "", 80, "iface", "100.115.92.5", 0));
  ASSERT_TRUE(mock_firewall.GetAllCommands().empty());
  mock_firewall.ResetStoredCommands();
}

TEST_F(FirewallTest, AddIpv4ForwardRules_IptablesFails) {
  MockFirewall mock_firewall;
  mock_firewall.SetRunInMinijailFailCriterion({}, true, false);

  ASSERT_FALSE(mock_firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, "", 80, "iface", "100.115.92.6", 8080));
  ASSERT_FALSE(mock_firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, "", 80, "iface", "100.115.92.6", 8080));
  ASSERT_FALSE(mock_firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, "", 80, "iface", "100.115.92.6", 8080));
  ASSERT_FALSE(mock_firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, "", 80, "iface", "100.115.92.6", 8080));

  // AddIpv4ForwardRule: Firewall should return at the first failure and issue
  // only a single command.
  // DeleteIpv4ForwardRule: Firewall should try to delete both the DNAT rule
  // and the FORWARD rule regardless of iptables failures.
  std::vector<std::string> expected_commands = {
      // Failed first AddIpv4ForwardRule call
      "/sbin/iptables -t nat -I PREROUTING -i iface -p tcp --dport 80 -j DNAT "
      "--to-destination 100.115.92.6:8080 -w",
      // Failed second AddIpv4ForwardRule call
      "/sbin/iptables -t nat -I PREROUTING -i iface -p udp --dport 80 -j DNAT "
      "--to-destination 100.115.92.6:8080 -w",
      // Failed first DeleteIpv4ForwardRule call
      "/sbin/iptables -t nat -D PREROUTING -i iface -p tcp --dport 80 -j DNAT "
      "--to-destination 100.115.92.6:8080 -w",
      "/sbin/iptables -t filter -D FORWARD -i iface -p tcp -d 100.115.92.6 "
      "--dport 8080 -j ACCEPT -w",
      // Failed second DeleteIpv4ForwardRule call
      "/sbin/iptables -t nat -D PREROUTING -i iface -p udp --dport 80 -j DNAT "
      "--to-destination 100.115.92.6:8080 -w",
      "/sbin/iptables -t filter -D FORWARD -i iface -p udp -d 100.115.92.6 "
      "--dport 8080 -j ACCEPT -w",
  };
  std::vector<std::string> issued_commands = mock_firewall.GetAllCommands();
  EXPECT_EQ(expected_commands.size(), issued_commands.size());
  for (int i = 0; i < expected_commands.size(); i++) {
    ASSERT_EQ(expected_commands[i], issued_commands[i]);
  }
}

TEST_F(FirewallTest, AddIpv4ForwardRules_ValidRules) {
  MockFirewall mock_firewall;
  mock_firewall.SetRunInMinijailFailCriterion({}, true, true);

  ASSERT_TRUE(mock_firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, "", 80, "wlan0", "100.115.92.2", 8080));
  ASSERT_TRUE(mock_firewall.AddIpv4ForwardRule(ModifyPortRuleRequest::TCP,
                                               "100.115.92.2", 5555, "vmtap0",
                                               "127.0.0.1", 5550));
  ASSERT_TRUE(mock_firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, "", 5353, "eth0", "192.168.1.1", 5353));
  ASSERT_TRUE(mock_firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, "", 5000, "mlan0", "10.0.0.24", 5001));
  ASSERT_TRUE(mock_firewall.DeleteIpv4ForwardRule(ModifyPortRuleRequest::TCP,
                                                  "100.115.92.2", 5555,
                                                  "vmtap0", "127.0.0.1", 5550));
  ASSERT_TRUE(mock_firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, "", 443, "eth1", "1.2.3.4", 443));

  std::vector<std::string> expected_commands = {
      "/sbin/iptables -t nat -I PREROUTING -i wlan0 -p tcp --dport 80 -j DNAT "
      "--to-destination 100.115.92.2:8080 -w",
      "/sbin/iptables -t filter -A FORWARD -i wlan0 -p tcp -d 100.115.92.2 "
      "--dport 8080 -j ACCEPT -w",
      "/sbin/iptables -t nat -I PREROUTING -i vmtap0 -p tcp -d 100.115.92.2 "
      "--dport 5555 -j DNAT --to-destination 127.0.0.1:5550 -w",
      "/sbin/iptables -t filter -A FORWARD -i vmtap0 -p tcp -d 127.0.0.1 "
      "--dport 5550 -j ACCEPT -w",
      "/sbin/iptables -t nat -I PREROUTING -i eth0 -p udp --dport 5353 -j DNAT "
      "--to-destination 192.168.1.1:5353 -w",
      "/sbin/iptables -t filter -A FORWARD -i eth0 -p udp -d 192.168.1.1 "
      "--dport 5353 -j ACCEPT -w",
      "/sbin/iptables -t nat -D PREROUTING -i mlan0 -p tcp --dport 5000 -j "
      "DNAT --to-destination 10.0.0.24:5001 -w",
      "/sbin/iptables -t filter -D FORWARD -i mlan0 -p tcp -d 10.0.0.24 "
      "--dport 5001 -j ACCEPT -w",
      "/sbin/iptables -t nat -D PREROUTING -i vmtap0 -p tcp -d 100.115.92.2 "
      "--dport 5555 -j DNAT --to-destination 127.0.0.1:5550 -w",
      "/sbin/iptables -t filter -D FORWARD -i vmtap0 -p tcp -d 127.0.0.1 "
      "--dport 5550 -j ACCEPT -w",
      "/sbin/iptables -t nat -D PREROUTING -i eth1 -p udp --dport 443 -j DNAT "
      "--to-destination 1.2.3.4:443 -w",
      "/sbin/iptables -t filter -D FORWARD -i eth1 -p udp -d 1.2.3.4 "
      "--dport 443 -j ACCEPT -w",
  };
  std::vector<std::string> issued_commands = mock_firewall.GetAllCommands();
  EXPECT_EQ(expected_commands.size(), issued_commands.size());
  for (int i = 0; i < expected_commands.size(); i++) {
    ASSERT_EQ(expected_commands[i], issued_commands[i]);
  }
}

TEST_F(FirewallTest, AddIpv4ForwardRules_PartialFailure) {
  MockFirewall mock_firewall;
  mock_firewall.SetRunInMinijailFailCriterion({"FORWARD"}, true, false);

  ASSERT_FALSE(mock_firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, "", 80, "wlan0", "100.115.92.2", 8080));

  // When the second issued FORWARD command fails, expect a delete command to
  // cleanup the PREROUTING command that succeeded.
  std::vector<std::string> expected_commands = {
      "/sbin/iptables -t nat -I PREROUTING -i wlan0 -p tcp --dport 80 -j DNAT "
      "--to-destination 100.115.92.2:8080 -w",
      "/sbin/iptables -t filter -A FORWARD -i wlan0 -p tcp -d 100.115.92.2 "
      "--dport 8080 -j ACCEPT -w",
      "/sbin/iptables -t nat -D PREROUTING -i wlan0 -p tcp --dport 80 -j DNAT "
      "--to-destination 100.115.92.2:8080 -w",
  };
  std::vector<std::string> issued_commands = mock_firewall.GetAllCommands();
  ASSERT_EQ(expected_commands.size(), issued_commands.size());
  for (int i = 0; i < expected_commands.size(); i++) {
    ASSERT_EQ(expected_commands[i], issued_commands[i]);
  }
}

TEST_F(FirewallTest, DeleteIpv4ForwardRules_PartialFailure) {
  MockFirewall mock_firewall1;
  MockFirewall mock_firewall2;

  mock_firewall1.SetRunInMinijailFailCriterion({"FORWARD"}, false, false);
  mock_firewall2.SetRunInMinijailFailCriterion({"PREROUTING"}, false, false);

  ASSERT_FALSE(mock_firewall1.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, "", 80, "wlan0", "100.115.92.2", 8080));
  ASSERT_FALSE(mock_firewall2.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, "", 80, "wlan0", "100.115.92.2", 8080));

  // Cleanup commands for both FORWARD and PREROUTING rules are both issued
  // regardless of any iptables failures.
  std::vector<std::string> expected_commands = {
      "/sbin/iptables -t nat -D PREROUTING -i wlan0 -p tcp --dport 80 -j DNAT "
      "--to-destination 100.115.92.2:8080 -w",
      "/sbin/iptables -t filter -D FORWARD -i wlan0 -p tcp -d 100.115.92.2 "
      "--dport 8080 -j ACCEPT -w",
  };
  std::vector<std::string> issued_commands1 = mock_firewall1.GetAllCommands();
  std::vector<std::string> issued_commands2 = mock_firewall2.GetAllCommands();
  ASSERT_EQ(expected_commands.size(), issued_commands1.size());
  ASSERT_EQ(expected_commands.size(), issued_commands2.size());
  for (int i = 0; i < expected_commands.size(); i++) {
    ASSERT_EQ(expected_commands[i], issued_commands1[i]);
    ASSERT_EQ(expected_commands[i], issued_commands2[i]);
  }
}
}  // namespace patchpanel
