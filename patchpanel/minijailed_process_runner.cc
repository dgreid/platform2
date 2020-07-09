// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/minijailed_process_runner.h"

#include <linux/capability.h>
#include <sys/wait.h>
#include <utility>

#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/process/process.h>

#include "patchpanel/net_util.h"

namespace patchpanel {

namespace {

constexpr char kUnprivilegedUser[] = "nobody";
constexpr char kNetworkUnprivilegedUser[] = "patchpaneld";
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
constexpr char kSysctlPath[] = "/usr/sbin/sysctl";

// An empty string will be returned if read fails.
std::string ReadBlockingFDToString(int fd) {
  static constexpr int kBufSize = 2048;
  char buf[kBufSize] = {0};
  std::string output;
  while (true) {
    ssize_t cnt = HANDLE_EINTR(read(fd, buf, kBufSize));
    if (cnt == -1) {
      PLOG(ERROR) << __func__ << " failed";
      return {};
    }

    if (cnt == 0)
      return output;

    output.append({buf, static_cast<size_t>(cnt)});
  }
}

}  // namespace

pid_t MinijailedProcessRunner::SyscallImpl::WaitPID(pid_t pid,
                                                    int* wstatus,
                                                    int options) {
  return waitpid(pid, wstatus, options);
}

int MinijailedProcessRunner::RunSyncDestroy(
    const std::vector<std::string>& argv,
    brillo::Minijail* mj,
    minijail* jail,
    bool log_failures,
    int* fd_stdout) {
  std::vector<char*> args;
  for (const auto& arg : argv) {
    args.push_back(const_cast<char*>(arg.c_str()));
  }
  args.push_back(nullptr);

  pid_t pid;
  int status = 0;
  bool ran = mj->RunPipesAndDestroy(jail, args, &pid, nullptr /*stdin*/,
                                    fd_stdout, nullptr /*stderr*/);
  if (ran) {
    ran = syscall_->WaitPID(pid, &status) == pid;
  }

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

int MinijailedProcessRunner::RunSync(const std::vector<std::string>& argv,
                                     brillo::Minijail* mj,
                                     bool log_failures,
                                     int* fd_stdout) {
  return RunSyncDestroy(argv, mj, mj->New(), log_failures, fd_stdout);
}

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
  syscall_ = std::make_unique<SyscallImpl>();
}

MinijailedProcessRunner::MinijailedProcessRunner(
    brillo::Minijail* mj, std::unique_ptr<SyscallImpl> syscall)
    : mj_(mj), syscall_(std::move(syscall)) {}

int MinijailedProcessRunner::Run(const std::vector<std::string>& argv,
                                 bool log_failures) {
  minijail* jail = mj_->New();
  CHECK(mj_->DropRoot(jail, kUnprivilegedUser, kUnprivilegedUser));
  mj_->UseCapabilities(jail, kNetRawAdminCapMask);
  return RunSyncDestroy(argv, mj_, jail, log_failures, nullptr);
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
  return RunSyncDestroy(args, mj_, jail, log_failures, nullptr);
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
                                      bool log_failures,
                                      std::string* output) {
  std::vector<std::string> args = {kIptablesPath, "-t", table};
  args.insert(args.end(), argv.begin(), argv.end());
  if (!output) {
    return RunSync(args, mj_, log_failures, nullptr);
  }

  int fd_stdout;
  int ret = RunSync(args, mj_, log_failures, &fd_stdout);
  if (ret == 0) {
    *output = ReadBlockingFDToString(fd_stdout);
  }
  return ret;
}

int MinijailedProcessRunner::ip6tables(const std::string& table,
                                       const std::vector<std::string>& argv,
                                       bool log_failures,
                                       std::string* output) {
  std::vector<std::string> args = {kIp6tablesPath, "-t", table};
  args.insert(args.end(), argv.begin(), argv.end());
  if (!output) {
    return RunSync(args, mj_, log_failures, nullptr);
  }

  int fd_stdout;
  int ret = RunSync(args, mj_, log_failures, &fd_stdout);
  if (ret == 0) {
    *output = ReadBlockingFDToString(fd_stdout);
  }
  return ret;
}

int MinijailedProcessRunner::modprobe_all(
    const std::vector<std::string>& modules, bool log_failures) {
  minijail* jail = mj_->New();
  CHECK(mj_->DropRoot(jail, kUnprivilegedUser, kUnprivilegedUser));
  mj_->UseCapabilities(jail, kModprobeCapMask);
  std::vector<std::string> args = {kModprobePath, "-a"};
  args.insert(args.end(), modules.begin(), modules.end());
  return RunSyncDestroy(args, mj_, jail, log_failures, nullptr);
}

int MinijailedProcessRunner::sysctl_w(const std::string& key,
                                      const std::string& value,
                                      bool log_failures) {
  std::vector<std::string> args = {kSysctlPath, "-w", key + "=" + value};
  return RunSync(args, mj_, log_failures, nullptr);
}

int MinijailedProcessRunner::ip_netns_attach(const std::string& netns_name,
                                             pid_t netns_pid,
                                             bool log_failures) {
  std::vector<std::string> args = {kIpPath, "netns", "attach", netns_name,
                                   std::to_string(netns_pid)};
  return RunSync(args, mj_, log_failures, nullptr);
}

int MinijailedProcessRunner::ip_netns_delete(const std::string& netns_name,
                                             bool log_failures) {
  std::vector<std::string> args = {kIpPath, "netns", "delete", netns_name};
  return RunSync(args, mj_, log_failures, nullptr);
}

}  // namespace patchpanel
