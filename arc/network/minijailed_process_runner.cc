// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/minijailed_process_runner.h"

#include <linux/capability.h>

#include <base/logging.h>
#include <base/strings/string_util.h>
#include <brillo/process.h>

#include "arc/network/net_util.h"

namespace arc_networkd {

namespace {

constexpr char kUnprivilegedUser[] = "nobody";
constexpr char kNetworkUnprivilegedUser[] = "arc-networkd";
constexpr char kChownCapMask = CAP_TO_MASK(CAP_CHOWN);
constexpr uint64_t kModprobeCapMask = CAP_TO_MASK(CAP_SYS_MODULE);
constexpr uint64_t kNetRawCapMask = CAP_TO_MASK(CAP_NET_RAW);
constexpr uint64_t kNetRawAdminCapMask =
    CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_RAW);

// These match what is used in iptables.cc in firewalld.
constexpr char kBrctlPath[] = "/sbin/brctl";
constexpr char kChownPath[] = "/bin/chown";
constexpr char kIpPath[] = "/bin/ip";
constexpr char kIptablesPath[] = "/sbin/iptables";
constexpr char kIp6tablesPath[] = "/sbin/ip6tables";
constexpr char kModprobePath[] = "/sbin/modprobe";
constexpr char kNsEnterPath[] = "/usr/bin/nsenter";
constexpr char kTouchPath[] = "/system/bin/touch";
constexpr char kSysctlPath[] = "/usr/sbin/sysctl";
constexpr char kSentinelFile[] = "/dev/.arc_network_ready";

int RunSyncDestroy(const std::vector<std::string>& argv,
                   brillo::Minijail* mj,
                   minijail* jail,
                   bool log_failures) {
  std::vector<char*> args;
  for (const auto& arg : argv) {
    args.push_back(const_cast<char*>(arg.c_str()));
  }
  args.push_back(nullptr);

  int status;
  bool ran = mj->RunSyncAndDestroy(jail, args, &status);
  if (!ran) {
    LOG(ERROR) << "Could not execute '" << base::JoinString(argv, " ") << "'";
  } else if (log_failures && (!WIFEXITED(status) || WEXITSTATUS(status) != 0)) {
    if (WIFEXITED(status)) {
      LOG(WARNING) << "Subprocess '" << base::JoinString(argv, " ")
                   << "' exited with code " << WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      LOG(WARNING) << "Subprocess '" << base::JoinString(argv, " ")
                   << "' exited with signal " << WTERMSIG(status);
    } else {
      LOG(WARNING) << "Subprocess '" << base::JoinString(argv, " ")
                   << "' exited with unknown status " << status;
    }
  }
  return ran && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int RunSync(const std::vector<std::string>& argv,
            brillo::Minijail* mj,
            bool log_failures) {
  return RunSyncDestroy(argv, mj, mj->New(), log_failures);
}

}  // namespace

void EnterChildProcessJail() {
  brillo::Minijail* m = brillo::Minijail::GetInstance();
  struct minijail* jail = m->New();

  // Most of these return void, but DropRoot() can fail if the user/group
  // does not exist.
  CHECK(m->DropRoot(jail, kNetworkUnprivilegedUser, kNetworkUnprivilegedUser))
      << "Could not drop root privileges";
  m->UseCapabilities(jail, kNetRawCapMask);
  m->Enter(jail);
  m->Destroy(jail);
}

MinijailedProcessRunner::MinijailedProcessRunner(brillo::Minijail* mj) {
  mj_ = mj ? mj : brillo::Minijail::GetInstance();
}

int MinijailedProcessRunner::Run(const std::vector<std::string>& argv,
                                 bool log_failures) {
  minijail* jail = mj_->New();
  CHECK(mj_->DropRoot(jail, kUnprivilegedUser, kUnprivilegedUser));
  mj_->UseCapabilities(jail, kNetRawAdminCapMask);
  return RunSyncDestroy(argv, mj_, jail, log_failures);
}

int MinijailedProcessRunner::AddInterfaceToContainer(
    const std::string& host_ifname,
    const std::string& con_ifname,
    uint32_t con_ipv4_addr,
    uint32_t con_ipv4_prefix_len,
    bool enable_multicast,
    const std::string& con_pid) {
  int rc = RunSync({kNsEnterPath, "-t", con_pid, "-n", "--", kIpPath, "link",
                    "set", host_ifname, "name", con_ifname},
                   mj_, true);
  if (rc != 0)
    return rc;

  rc = RunSync(
      {kNsEnterPath, "-t", con_pid, "-n", "--", kIpPath, "addr", "add",
       IPv4AddressToCidrString(con_ipv4_addr, con_ipv4_prefix_len), "brd",
       IPv4AddressToString(
           Ipv4BroadcastAddr(con_ipv4_addr, con_ipv4_prefix_len)),
       "dev", con_ifname},
      mj_, true);

  if (rc != 0)
    return rc;

  rc = RunSync({kNsEnterPath, "-t", con_pid, "-n", "--", kIpPath, "link", "set",
                con_ifname, "up"},
               mj_, true);
  if (rc != 0)
    return rc;

  if (enable_multicast)
    rc = RunSync({kNsEnterPath, "-t", con_pid, "-n", "--", kIpPath, "link",
                  "set", "dev", con_ifname, "multicast", "on"},
                 mj_, true);

  return rc;
}

int MinijailedProcessRunner::WriteSentinelToContainer(
    const std::string& con_pid) {
  return RunSync({kNsEnterPath, "-t", con_pid, "--mount", "--pid", "--",
                  kTouchPath, kSentinelFile},
                 mj_, true);
}

int MinijailedProcessRunner::brctl(const std::string& cmd,
                                   const std::vector<std::string>& argv,
                                   bool log_failures) {
  std::vector<std::string> args = {kBrctlPath, cmd};
  args.insert(args.end(), argv.begin(), argv.end());
  return Run(args, log_failures);
}

int MinijailedProcessRunner::chown(const std::string& uid,
                                   const std::string& gid,
                                   const std::string& file,
                                   bool log_failures) {
  minijail* jail = mj_->New();
  CHECK(mj_->DropRoot(jail, kUnprivilegedUser, kUnprivilegedUser));
  mj_->UseCapabilities(jail, kChownCapMask);
  std::vector<std::string> args = {kChownPath, uid + ":" + gid, file};
  return RunSyncDestroy(args, mj_, jail, log_failures);
}

int MinijailedProcessRunner::ip(const std::string& obj,
                                const std::string& cmd,
                                const std::vector<std::string>& argv,
                                bool log_failures) {
  std::vector<std::string> args = {kIpPath, obj, cmd};
  args.insert(args.end(), argv.begin(), argv.end());
  return Run(args, log_failures);
}

int MinijailedProcessRunner::ip6(const std::string& obj,
                                 const std::string& cmd,
                                 const std::vector<std::string>& argv,
                                 bool log_failures) {
  std::vector<std::string> args = {kIpPath, "-6", obj, cmd};
  args.insert(args.end(), argv.begin(), argv.end());
  return Run(args, log_failures);
}

int MinijailedProcessRunner::iptables(const std::string& table,
                                      const std::vector<std::string>& argv,
                                      bool log_failures) {
  std::vector<std::string> args = {kIptablesPath, "-t", table};
  args.insert(args.end(), argv.begin(), argv.end());
  return RunSync(args, mj_, log_failures);
}

int MinijailedProcessRunner::ip6tables(const std::string& table,
                                       const std::vector<std::string>& argv,
                                       bool log_failures) {
  std::vector<std::string> args = {kIp6tablesPath, "-t", table};
  args.insert(args.end(), argv.begin(), argv.end());
  return RunSync(args, mj_, log_failures);
}

int MinijailedProcessRunner::modprobe_all(
    const std::vector<std::string>& modules, bool log_failures) {
  minijail* jail = mj_->New();
  CHECK(mj_->DropRoot(jail, kUnprivilegedUser, kUnprivilegedUser));
  mj_->UseCapabilities(jail, kModprobeCapMask);
  std::vector<std::string> args = {kModprobePath, "-a"};
  args.insert(args.end(), modules.begin(), modules.end());
  return RunSyncDestroy(args, mj_, jail, log_failures);
}

int MinijailedProcessRunner::sysctl_w(const std::string& key,
                                      const std::string& value,
                                      bool log_failures) {
  std::vector<std::string> args = {kSysctlPath, "-w", key + "=" + value};
  return RunSync(args, mj_, log_failures);
}

}  // namespace arc_networkd
