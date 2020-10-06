// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/port_tracker.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_util.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/time/time.h>
#include <chromeos/patchpanel/dbus/client.h>

namespace permission_broker {

namespace {
// Port forwarding is only allowed for non-reserved ports.
constexpr const uint16_t kLastSystemPort = 1023;
// Port forwarding is only allowed for some physical interfaces: Ethernet, USB
// tethering, and WiFi.
constexpr std::array<const char*, 4> kAllowedInterfacePrefixes{
    {"eth", "usb", "wlan", "mlan"}};
constexpr const char kLocalhost[] = "lo";

// Returns the network-byte order int32 representation of the IPv4 address given
// byte per byte, most significant bytes first.
constexpr uint32_t Ipv4Addr(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
  return (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
}

// TODO(hugobenichi): eventually import these values from
// platform2/arc/network/address_manager.cc
// Port forwarding can only forward to IPv4 addresses within the IPv4 prefix
// used for static IPv4 subnet assignment to guest OSs and App platforms.
constexpr const char* kGuestSubnetCidr = "100.115.92.0/23";
constexpr const struct in_addr kGuestBaseAddr = {.s_addr =
                                                     Ipv4Addr(100, 115, 92, 0)};
constexpr const struct in_addr kGuestNetmask = {.s_addr =
                                                    Ipv4Addr(255, 255, 254, 0)};

const std::string ProtocolName(Protocol proto) {
  if (proto == ModifyPortRuleRequest::INVALID_PROTOCOL) {
    NOTREACHED() << "Unexpected L4 protocol value";
  }
  return base::ToLowerASCII(ModifyPortRuleRequest::Protocol_Name(proto));
}

std::string RuleTypeName(PortTracker::PortRuleType type) {
  switch (type) {
    case PortTracker::kUnknownRule:
      return "UnknownRule";
    case PortTracker::kAccessRule:
      return "AccessRule";
    case PortTracker::kLockdownRule:
      return "LockdownRule";
    case PortTracker::kForwardingRule:
      return "ForwardingRule";
    default:
      NOTREACHED() << "Unknown rule type " << type;
      return std::to_string(type);
  }
}

std::ostream& operator<<(std::ostream& stream,
                         const PortTracker::PortRuleKey key) {
  stream << "{ " << ProtocolName(key.proto) << " :"
         << std::to_string(key.input_dst_port) << "/" << key.input_ifname
         << " }";
  return stream;
}

std::ostream& operator<<(std::ostream& stream,
                         const PortTracker::PortRule rule) {
  stream << "{ " << RuleTypeName(rule.type) << " " << ProtocolName(rule.proto)
         << " :" << std::to_string(rule.input_dst_port) << "/"
         << rule.input_ifname << " -> " << rule.dst_ip << ":" << rule.dst_port
         << " }";
  return stream;
}
}  // namespace

PortTracker::PortTracker()
    : task_runner_{base::ThreadTaskRunnerHandle::Get()} {}

// Test-only.
PortTracker::PortTracker(scoped_refptr<base::SequencedTaskRunner> task_runner)
    : task_runner_{task_runner} {}

PortTracker::~PortTracker() {
  RevokeAllPortRules();
}

bool PortTracker::ModifyPortRule(Operation op, const PortRule& rule) {
  std::unique_ptr<patchpanel::Client> patchpanel_client =
      patchpanel::Client::New();
  if (!patchpanel_client) {
    LOG(ERROR) << "Failed to open patchpanel client";
    return false;
  }

  RuleType type;
  switch (rule.type) {
    case kAccessRule:
      type = ModifyPortRuleRequest::ACCESS;
      break;
    case kLockdownRule:
      type = ModifyPortRuleRequest::LOCKDOWN;
      break;
    case kForwardingRule:
      type = ModifyPortRuleRequest::FORWARDING;
      break;
    default:
      type = ModifyPortRuleRequest::INVALID_RULE_TYPE;
      break;
  }

  return patchpanel_client->ModifyPortRule(
      op, type, rule.proto, rule.input_ifname, rule.input_dst_ip,
      rule.input_dst_port, rule.dst_ip, rule.dst_port);
}
bool PortTracker::AllowTcpPortAccess(uint16_t port,
                                     const std::string& iface,
                                     int dbus_fd) {
  PortRule rule = {
      .type = kAccessRule,
      .proto = ModifyPortRuleRequest::TCP,
      .input_dst_port = port,
      .input_ifname = iface,
  };
  return AddPortRule(rule, dbus_fd);
}

bool PortTracker::AllowUdpPortAccess(uint16_t port,
                                     const std::string& iface,
                                     int dbus_fd) {
  PortRule rule = {
      .type = kAccessRule,
      .proto = ModifyPortRuleRequest::UDP,
      .input_dst_port = port,
      .input_ifname = iface,
  };
  return AddPortRule(rule, dbus_fd);
}

bool PortTracker::RevokeTcpPortAccess(uint16_t port, const std::string& iface) {
  PortRuleKey key = {
      .proto = ModifyPortRuleRequest::TCP,
      .input_dst_port = port,
      .input_ifname = iface,
  };
  return RevokePortRule(key);
}

bool PortTracker::RevokeUdpPortAccess(uint16_t port, const std::string& iface) {
  PortRuleKey key = {
      .proto = ModifyPortRuleRequest::UDP,
      .input_dst_port = port,
      .input_ifname = iface,
  };
  return RevokePortRule(key);
}

bool PortTracker::AddPortRule(const PortRule& rule, int dbus_fd) {
  if (!ValidatePortRule(rule)) {
    return false;
  }

  PortRuleKey key = {
      .proto = rule.proto,
      .input_dst_port = rule.input_dst_port,
      .input_ifname = rule.input_ifname,
  };

  // Check if the port is not already being forwarded or allowed for access.
  if (port_rules_.find(key) != port_rules_.end()) {
    // There is a very very small chance of a race here: a process exits without
    // closing a firewall hole, and before the lifeline fd can trigger, another
    // process requests the same port. This should be allowed, but if the
    // lifeline fd hasn't triggered yet, it won't.
    // Since permission_broker is single-threaded, this race is extremely
    // unlikely to happen: the second request needs to come in at exactly the
    // right time, after the first process exits but before the lifeline fd
    // has triggered.
    return false;
  }

  // We use |lifeline_fd| to track the lifetime of the process requesting
  // port access.
  int lifeline_fd = AddLifelineFd(dbus_fd);
  if (lifeline_fd < 0) {
    LOG(ERROR) << "Tracking lifeline fd for rule " << rule << " failed";
    return false;
  }

  // Track the port rule.
  port_rules_[key] = rule;
  port_rules_[key].lifeline_fd = lifeline_fd;
  lifeline_fds_[lifeline_fd] = key;

  if (!ModifyPortRule(ModifyPortRuleRequest::CREATE, rule)) {
    // If we fail to punch the hole in the firewall, stop tracking the lifetime
    // of the process.
    LOG(ERROR) << "Failed to create rule " << rule;
    DeleteLifelineFd(lifeline_fd);
    lifeline_fds_.erase(lifeline_fd);
    port_rules_.erase(key);
    return false;
  }
  return true;
}

void PortTracker::RevokeAllPortRules() {
  VLOG(1) << "Revoking all port rules";

  // Copy the container so that we can remove elements from the original.
  std::vector<PortRuleKey> all_rules;
  all_rules.reserve(lifeline_fds_.size());
  for (const auto& kv : lifeline_fds_) {
    all_rules.push_back(kv.second);
  }
  for (const PortRuleKey& key : all_rules) {
    RevokePortRule(key);
  }

  CHECK(!HasActiveRules()) << "Failed to revoke all port rules";
}

bool PortTracker::LockDownLoopbackTcpPort(uint16_t port, int dbus_fd) {
  PortRule rule = {
      .type = kLockdownRule,
      .proto = ModifyPortRuleRequest::TCP,
      .input_dst_port = port,
      .input_ifname = kLocalhost,
  };
  return AddPortRule(rule, dbus_fd);
}

bool PortTracker::ReleaseLoopbackTcpPort(uint16_t port) {
  PortRuleKey key = {
      .proto = ModifyPortRuleRequest::TCP,
      .input_dst_port = port,
      .input_ifname = kLocalhost,
  };
  return RevokePortRule(key);
}

bool PortTracker::StartTcpPortForwarding(uint16_t input_dst_port,
                                         const std::string& input_ifname,
                                         const std::string& dst_ip,
                                         uint16_t dst_port,
                                         int dbus_fd) {
  PortRule rule = {
      .type = kForwardingRule,
      .proto = ModifyPortRuleRequest::TCP,
      .input_dst_port = input_dst_port,
      .input_ifname = input_ifname,
      .dst_ip = dst_ip,
      .dst_port = dst_port,
  };
  return AddPortRule(rule, dbus_fd);
}

bool PortTracker::StartUdpPortForwarding(uint16_t input_dst_port,
                                         const std::string& input_ifname,
                                         const std::string& dst_ip,
                                         uint16_t dst_port,
                                         int dbus_fd) {
  PortRule rule = {
      .type = kForwardingRule,
      .proto = ModifyPortRuleRequest::UDP,
      .input_dst_port = input_dst_port,
      .input_ifname = input_ifname,
      .dst_ip = dst_ip,
      .dst_port = dst_port,
  };
  return AddPortRule(rule, dbus_fd);
}

bool PortTracker::StopTcpPortForwarding(uint16_t input_dst_port,
                                        const std::string& input_ifname) {
  PortRuleKey key = {
      .proto = ModifyPortRuleRequest::TCP,
      .input_dst_port = input_dst_port,
      .input_ifname = input_ifname,
  };
  return RevokePortRule(key);
}

bool PortTracker::StopUdpPortForwarding(uint16_t input_dst_port,
                                        const std::string& input_ifname) {
  PortRuleKey key = {
      .proto = ModifyPortRuleRequest::UDP,
      .input_dst_port = input_dst_port,
      .input_ifname = input_ifname,
  };
  return RevokePortRule(key);
}

bool PortTracker::ValidatePortRule(const PortRule& rule) {
  switch (rule.type) {
    case kAccessRule:
    case kLockdownRule:
    case kForwardingRule:
      break;
    default:
      CHECK(false) << "Unknown port rule type value " << rule.type;
      return false;
  }

  switch (rule.proto) {
    case ModifyPortRuleRequest::TCP:
    case ModifyPortRuleRequest::UDP:
      break;
    default:
      CHECK(false) << "Unknown L4 protocol value " << rule.proto;
      return false;
  }

  // TODO(hugobenichi): add some validation for port access and port lockdown
  // rules as well.
  switch (rule.type) {
    case kForwardingRule: {
      // Redirecting a reserved port is not allowed.
      // Forwarding into a reserved port of the guest is allowed.
      if (rule.input_dst_port <= kLastSystemPort) {
        LOG(ERROR) << "Cannot forward system port " << rule.input_dst_port;
        return false;
      }

      struct in_addr addr;
      if (inet_pton(AF_INET, rule.dst_ip.c_str(), &addr) != 1) {
        LOG(ERROR) << "Cannot forward to invalid IPv4 address " << rule.dst_ip;
        return false;
      }

      if ((addr.s_addr & kGuestNetmask.s_addr) != kGuestBaseAddr.s_addr) {
        LOG(ERROR) << "Cannot forward to IPv4 address " << rule.dst_ip
                   << " outside of " << kGuestSubnetCidr;
        return false;
      }

      if (rule.input_ifname.empty()) {
        PLOG(ERROR) << "No interface name provided";
        return false;
      }

      bool allowedInputIface = false;
      for (const auto& prefix : kAllowedInterfacePrefixes) {
        if (base::StartsWith(rule.input_ifname, prefix,
                             base::CompareCase::SENSITIVE)) {
          allowedInputIface = true;
          break;
        }
      }
      if (!allowedInputIface) {
        PLOG(ERROR) << "Cannot forward traffic from interface "
                    << rule.input_ifname;
        return false;
      }
      break;
    }
    default:
      break;
  }

  return true;
}

void PortTracker::OnFileDescriptorReadable(int fd) {
  // The process that requested this port has died/exited, so we need to plug
  // the hole.
  if (lifeline_fds_.find(fd) == lifeline_fds_.end()) {
    LOG(ERROR) << "File descriptor " << fd << " was not being tracked";
    DeleteLifelineFd(fd);
  } else {
    if (!RevokePortRule(lifeline_fds_[fd])) {
      DeleteLifelineFd(fd);
    }
  }
}

int PortTracker::AddLifelineFd(int dbus_fd) {
  int fd = dup(dbus_fd);
  if (fd < 0) {
    PLOG(ERROR) << "dup(dbus_fd) failed";
    return -1;
  }

  lifeline_fd_controllers_[fd] = base::FileDescriptorWatcher::WatchReadable(
      fd, base::BindRepeating(&PortTracker::OnFileDescriptorReadable,
                              // The callback will not outlive the object.
                              base::Unretained(this), fd));

  return fd;
}

bool PortTracker::DeleteLifelineFd(int fd) {
  auto iter = lifeline_fd_controllers_.find(fd);
  if (iter == lifeline_fd_controllers_.end()) {
    return false;
  }

  iter->second.reset();  // Destruct the controller, which removes the callback.
  lifeline_fd_controllers_.erase(iter);

  // AddLifelineFd() calls dup(), so this function should close the fd.
  // We still return true since at this point the FileDescriptorWatcher object
  // has been destructed.
  if (IGNORE_EINTR(close(fd)) < 0) {
    PLOG(ERROR) << "close(lifeline_fd)";
  }

  return true;
}

bool PortTracker::HasActiveRules() {
  return !lifeline_fds_.empty();
}

bool PortTracker::RevokePortRule(const PortRuleKey key) {
  if (port_rules_.find(key) == port_rules_.end()) {
    LOG(ERROR) << "No port rule found for " << key;
    return false;
  }

  PortRule rule = port_rules_[key];
  bool deleted = DeleteLifelineFd(rule.lifeline_fd);
  if (!deleted) {
    LOG(ERROR) << "Failed to delete watcher for file descriptor "
               << rule.lifeline_fd << " from task runner";
  }
  port_rules_.erase(key);
  lifeline_fds_.erase(rule.lifeline_fd);
  return deleted && ModifyPortRule(ModifyPortRuleRequest::DELETE, rule);
}

}  // namespace permission_broker
