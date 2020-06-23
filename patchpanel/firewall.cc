// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/firewall.h"

#include <arpa/inet.h>
#include <linux/capability.h>
#include <netinet/in.h>

#include <string>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/minijail/minijail.h>

namespace {

// Interface names must be shorter than 'IFNAMSIZ' chars.
// See http://man7.org/linux/man-pages/man7/netdevice.7.html
// 'IFNAMSIZ' is 16 in recent kernels.
// See https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/uapi/linux/if.h?h=v4.14#n33
constexpr size_t kInterfaceNameSize = 16;

// Interface names are passed directly to the 'iptables' command. Rather than
// auditing 'iptables' source code to see how it handles malformed names,
// do some sanitization on the names beforehand.
bool IsValidInterfaceName(const std::string& iface) {
  // |iface| should be shorter than |kInterfaceNameSize| chars and have only
  // alphanumeric characters (embedded hypens and periods are also permitted).
  if (iface.length() >= kInterfaceNameSize) {
    return false;
  }
  if (base::StartsWith(iface, "-", base::CompareCase::SENSITIVE) ||
      base::EndsWith(iface, "-", base::CompareCase::SENSITIVE) ||
      base::StartsWith(iface, ".", base::CompareCase::SENSITIVE) ||
      base::EndsWith(iface, ".", base::CompareCase::SENSITIVE)) {
    return false;
  }
  for (auto c : iface) {
    if (!std::isalnum(c) && (c != '-') && (c != '.')) {
      return false;
    }
  }
  return true;
}

}  // namespace

namespace patchpanel {

const char kIpTablesPath[] = "/sbin/iptables";
const char kIp6TablesPath[] = "/sbin/ip6tables";

const std::string ProtocolName(Protocol proto) {
  if (proto == ModifyPortRuleRequest::INVALID_PROTOCOL) {
    NOTREACHED() << "Unexpected L4 protocol value";
  }
  return base::ToLowerASCII(ModifyPortRuleRequest::Protocol_Name(proto));
}

bool Firewall::AddAcceptRules(Protocol protocol,
                              uint16_t port,
                              const std::string& interface) {
  if (port == 0U) {
    LOG(ERROR) << "Port 0 is not a valid port";
    return false;
  }

  if (!IsValidInterfaceName(interface)) {
    LOG(ERROR) << "Invalid interface name '" << interface << "'";
    return false;
  }

  if (!AddAcceptRule(kIpTablesPath, protocol, port, interface)) {
    LOG(ERROR) << "Could not add ACCEPT rule using '" << kIpTablesPath << "'";
    return false;
  }

  if (!AddAcceptRule(kIp6TablesPath, protocol, port, interface)) {
    LOG(ERROR) << "Could not add ACCEPT rule using '" << kIp6TablesPath
               << "', aborting operation";
    DeleteAcceptRule(kIpTablesPath, protocol, port, interface);
    return false;
  }

  return true;
}

bool Firewall::DeleteAcceptRules(Protocol protocol,
                                 uint16_t port,
                                 const std::string& interface) {
  if (port == 0U) {
    LOG(ERROR) << "Port 0 is not a valid port";
    return false;
  }

  if (!IsValidInterfaceName(interface)) {
    LOG(ERROR) << "Invalid interface name '" << interface << "'";
    return false;
  }

  bool ip4_success = DeleteAcceptRule(kIpTablesPath, protocol, port,
                                      interface);
  bool ip6_success = DeleteAcceptRule(kIp6TablesPath, protocol,
                                      port, interface);
  return ip4_success && ip6_success;
}

bool Firewall::AddIpv4ForwardRule(Protocol protocol,
                                  const std::string& input_ip,
                                  uint16_t port,
                                  const std::string& interface,
                                  const std::string& dst_ip,
                                  uint16_t dst_port) {
  if (!ModifyIpv4DNATRule(protocol, input_ip, port, interface, dst_ip, dst_port, "-I")) {
      return false;
  }

  if (!ModifyIpv4ForwardChain(protocol, interface, dst_ip, dst_port, "-A")) {
      ModifyIpv4DNATRule(protocol, input_ip, port, interface, dst_ip, dst_port, "-D");
      return false;
  }

  return true;
}

bool Firewall::DeleteIpv4ForwardRule(Protocol protocol,
                                     const std::string& input_ip,
                                     uint16_t port,
                                     const std::string& interface,
                                     const std::string& dst_ip,
                                     uint16_t dst_port) {
  bool success = true;
  if (!ModifyIpv4DNATRule(protocol, input_ip, port, interface, dst_ip, dst_port, "-D")) {
    success = false;
  }
  if (!ModifyIpv4ForwardChain(protocol, interface, dst_ip, dst_port, "-D")) {
    success = false;
  }
  return success;
}

bool Firewall::ModifyIpv4DNATRule(Protocol protocol,
                                  const std::string& input_ip,
                                  uint16_t port,
                                  const std::string& interface,
                                  const std::string& dst_ip,
                                  uint16_t dst_port,
                                  const std::string& operation) {
  struct in_addr addr;
  if (!input_ip.empty() && inet_pton(AF_INET, input_ip.c_str(), &addr) != 1) {
    LOG(ERROR) << "Invalid input IPv4 address '" << input_ip << "'";
    return false;
  }

  if (port == 0U) {
    LOG(ERROR) << "Port 0 is not a valid port";
    return false;
  }

  if (!IsValidInterfaceName(interface) || interface.empty()) {
    LOG(ERROR) << "Invalid interface name '" << interface << "'";
    return false;
  }

  if (inet_pton(AF_INET, dst_ip.c_str(), &addr) != 1) {
    LOG(ERROR) << "Invalid destination IPv4 address '" << dst_ip << "'";
    return false;
  }

  if (dst_port == 0U) {
    LOG(ERROR) << "Destination port 0 is not a valid port";
    return false;
  }

  // Only support deleting existing forwarding rules or inserting rules in the
  // first position: ARC++ generic inbound DNAT rule always need to go last.
  if (operation != "-I" && operation != "-D") {
    LOG(ERROR) << "Invalid chain operation '" << operation << "'";
    return false;
  }

  std::vector<std::string> argv{kIpTablesPath,
                                "-t",
                                "nat",
                                operation,
                                "PREROUTING",
                                "-i",
                                interface,
                                "-p",  // protocol
                                ProtocolName(protocol)};
  if (!input_ip.empty()) {
    argv.push_back("-d");  // input destination ip
    argv.push_back(input_ip);
  }
  argv.push_back("--dport");  // input destination port
  argv.push_back(std::to_string(port));
  argv.push_back("-j");
  argv.push_back("DNAT");
  argv.push_back("--to-destination");  // new output destination ip:port
  argv.push_back(dst_ip + ":" + std::to_string(dst_port));
  argv.push_back("-w");  // Wait for xtables lock.
  return RunInMinijail(argv) == 0;
}

bool Firewall::ModifyIpv4ForwardChain(Protocol protocol,
                                      const std::string& interface,
                                      const std::string& dst_ip,
                                      uint16_t dst_port,
                                      const std::string& operation) {
  if (!IsValidInterfaceName(interface) || interface.empty()) {
    LOG(ERROR) << "Invalid interface name '" << interface << "'";
    return false;
  }

  struct in_addr addr;
  if (inet_pton(AF_INET, dst_ip.c_str(), &addr) != 1) {
    LOG(ERROR) << "Invalid IPv4 destination address '" << dst_ip << "'";
    return false;
  }

  if (dst_port == 0U) {
    LOG(ERROR) << "Destination port 0 is not a valid port";
    return false;
  }

  // Order does not matter for the FORWARD chain: both -A or -I are possible.
  if (operation != "-A" && operation != "-I" && operation != "-D") {
    LOG(ERROR) << "Invalid chain operation '" << operation << "'";
    return false;
  }

  std::vector<std::string> argv{
      kIpTablesPath,
      "-t",
      "filter",
      operation,
      "FORWARD",
      "-i",
      interface,
      "-p",  // protocol
      ProtocolName(protocol),
      "-d",  // destination ip
      dst_ip,
      "--dport",  // destination port
      std::to_string(dst_port),
      "-j",
      "ACCEPT",
      "-w"};  // Wait for xtables lock.
  return RunInMinijail(argv) == 0;
}

bool Firewall::AddLoopbackLockdownRules(Protocol protocol, uint16_t port) {
  if (port == 0U) {
    LOG(ERROR) << "Port 0 is not a valid port";
    return false;
  }

  if (!AddLoopbackLockdownRule(kIpTablesPath, protocol, port)) {
    LOG(ERROR) << "Could not add loopback REJECT rule using '" << kIpTablesPath
               << "'";
    return false;
  }

  if (!AddLoopbackLockdownRule(kIp6TablesPath, protocol, port)) {
    LOG(ERROR) << "Could not add loopback REJECT rule using '" << kIp6TablesPath
               << "', aborting operation";
    DeleteLoopbackLockdownRule(kIpTablesPath, protocol, port);
    return false;
  }

  return true;
}

bool Firewall::DeleteLoopbackLockdownRules(Protocol protocol, uint16_t port) {
  if (port == 0U) {
    LOG(ERROR) << "Port 0 is not a valid port";
    return false;
  }

  bool ip4_success = DeleteLoopbackLockdownRule(kIpTablesPath, protocol, port);
  bool ip6_success = DeleteLoopbackLockdownRule(kIp6TablesPath, protocol, port);
  return ip4_success && ip6_success;
}

bool Firewall::AddAcceptRule(const std::string& executable_path,
                             Protocol protocol,
                             uint16_t port,
                             const std::string& interface) {
  std::vector<std::string> argv{executable_path,
                                "-I",  // insert
                                "INPUT",
                                "-p",  // protocol
                                ProtocolName(protocol),
                                "--dport",  // destination port
                                std::to_string(port)};
  if (!interface.empty()) {
    argv.push_back("-i");  // interface
    argv.push_back(interface);
  }
  argv.push_back("-j");
  argv.push_back("ACCEPT");
  argv.push_back("-w");  // Wait for xtables lock.

  return RunInMinijail(argv) == 0;
}

bool Firewall::DeleteAcceptRule(const std::string& executable_path,
                                Protocol protocol,
                                uint16_t port,
                                const std::string& interface) {
  std::vector<std::string> argv{executable_path,
                                "-D",  // delete
                                "INPUT",
                                "-p",  // protocol
                                ProtocolName(protocol),
                                "--dport",  // destination port
                                std::to_string(port)};
  if (!interface.empty()) {
    argv.push_back("-i");  // interface
    argv.push_back(interface);
  }
  argv.push_back("-j");
  argv.push_back("ACCEPT");
  argv.push_back("-w");  // Wait for xtables lock.

  return RunInMinijail(argv) == 0;
}

bool Firewall::AddLoopbackLockdownRule(const std::string& executable_path,
                                       Protocol protocol,
                                       uint16_t port) {
  std::vector<std::string> argv{
      executable_path,
      "-I",  // insert
      "OUTPUT",
      "-p",  // protocol
      ProtocolName(protocol),
      "--dport",  // destination port
      std::to_string(port),
      "-o",  // output interface
      "lo",
      "-m",  // match extension
      "owner",
      "!",
      "--uid-owner",
      "chronos",
      "-j",
      "REJECT",
      "-w",  // Wait for xtables lock.
  };

  return RunInMinijail(argv) == 0;
}

bool Firewall::DeleteLoopbackLockdownRule(const std::string& executable_path,
                                          Protocol protocol,
                                          uint16_t port) {
  std::vector<std::string> argv{
      executable_path,
      "-D",  // delete
      "OUTPUT",
      "-p",  // protocol
      ProtocolName(protocol),
      "--dport",  // destination port
      std::to_string(port),
      "-o",  // output interface
      "lo",
      "-m",  // match extension
      "owner",
      "!",
      "--uid-owner",
      "chronos",
      "-j",
      "REJECT",
      "-w",  // Wait for xtables lock.
  };

  return RunInMinijail(argv) == 0;
}

int Firewall::RunInMinijail(const std::vector<std::string>& argv) {
  brillo::Minijail* m = brillo::Minijail::GetInstance();
  minijail* jail = m->New();

  std::vector<char*> args;
  for (const auto& arg : argv) {
    args.push_back(const_cast<char*>(arg.c_str()));
  }
  args.push_back(nullptr);

  int status;
  return m->RunSyncAndDestroy(jail, args, &status) ? status : -1;
}

}  // namespace patchpanel
