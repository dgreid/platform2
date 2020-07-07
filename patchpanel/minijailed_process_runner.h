// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MINIJAILED_PROCESS_RUNNER_H_
#define PATCHPANEL_MINIJAILED_PROCESS_RUNNER_H_

#include <string>
#include <vector>

#include <brillo/minijail/minijail.h>

namespace patchpanel {

// Runs the current process with minimal privileges. This function is expected
// to be used by child processes that need only CAP_NET_RAW and to run as the
// patchpaneld user.
void EnterChildProcessJail();

// Enforces the expected processes are run with the correct privileges.
class MinijailedProcessRunner {
 public:
  // Ownership of |mj| is not assumed and must be managed by the caller.
  // If |mj| is null, the default instance will be used.
  explicit MinijailedProcessRunner(brillo::Minijail* mj = nullptr);
  virtual ~MinijailedProcessRunner() = default;

  // Moves interface |ifname| back into the default namespace
  // |pid| identifies the pid of the current namespace.
  virtual int RestoreDefaultNamespace(const std::string& ifname, pid_t pid);

  // Runs brctl.
  virtual int brctl(const std::string& cmd,
                    const std::vector<std::string>& argv,
                    bool log_failures = true);

  // Runs chown to update file ownership.
  virtual int chown(const std::string& uid,
                    const std::string& gid,
                    const std::string& file,
                    bool log_failures = true);

  // Runs ip.
  virtual int ip(const std::string& obj,
                 const std::string& cmd,
                 const std::vector<std::string>& args,
                 bool log_failures = true);
  virtual int ip6(const std::string& obj,
                  const std::string& cmd,
                  const std::vector<std::string>& args,
                  bool log_failures = true);

  // Runs iptables.
  virtual int iptables(const std::string& table,
                       const std::vector<std::string>& argv,
                       bool log_failures = true);

  virtual int ip6tables(const std::string& table,
                        const std::vector<std::string>& argv,
                        bool log_failures = true);

  // Installs all |modules| via modprobe.
  virtual int modprobe_all(const std::vector<std::string>& modules,
                           bool log_failures = true);

  // Updates kernel parameter |key| to |value| using sysctl.
  virtual int sysctl_w(const std::string& key,
                       const std::string& value,
                       bool log_failures = true);

 protected:
  // Runs a process (argv[0]) with optional arguments (argv[1]...)
  // in a minijail as an unprivileged user with CAP_NET_ADMIN and
  // CAP_NET_RAW capabilities.
  virtual int Run(const std::vector<std::string>& argv,
                  bool log_failures = true);

 private:
  brillo::Minijail* mj_;

  DISALLOW_COPY_AND_ASSIGN(MinijailedProcessRunner);
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MINIJAILED_PROCESS_RUNNER_H_
