// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <sys/wait.h>

#include <string>

#include <base/command_line.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/process.h>
#include <brillo/syslog_logging.h>

#include "vpn-manager/daemon.h"
#include "vpn-manager/ipsec_manager.h"
#include "vpn-manager/l2tp_manager.h"

using vpn_manager::IpsecManager;
using vpn_manager::L2tpManager;
using vpn_manager::ServiceManager;

// True if a signal has requested termination.
static bool s_terminate_request = false;

void HandleSignal(int sig_num) {
  LOG(INFO) << "Caught signal " << sig_num;
  switch (sig_num) {
    case SIGTERM:
    case SIGINT:
      s_terminate_request = true;
      break;
    case SIGALRM:
      break;
  }
}

static void InstallSignalHandlers() {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = HandleSignal;
  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGALRM, &sa, nullptr);
}

static void LockDownUmask() {
  // Only user and group may access configuration files we create.
  umask(S_IWGRP | S_IROTH | S_IWOTH);
}

// Run the main event loop.  The events to handle are:
// 1) timeout from poll
// 2) caught signal
// 3) stdout/err of child process ready
// 4) child process dies
static void RunEventLoop(IpsecManager* ipsec, L2tpManager* l2tp) {
  do {
    int status;
    int ipsec_poll_timeout = ipsec->Poll();
    int l2tp_poll_timeout = l2tp->Poll();
    int poll_timeout = (ipsec_poll_timeout > l2tp_poll_timeout)
                           ? ipsec_poll_timeout
                           : l2tp_poll_timeout;

    const int poll_input_count = 3;
    struct pollfd poll_inputs[poll_input_count] = {
        {ipsec->output_fd(), POLLIN},    // ipsec output
        {l2tp->output_fd(), POLLIN},     // l2tp output
        {l2tp->ppp_output_fd(), POLLIN}  // ppp output
    };
    int poll_result = poll(poll_inputs, poll_input_count, poll_timeout);
    if (poll_result < 0 && errno != EINTR) {
      int saved_errno = errno;
      LOG(ERROR) << "Unexpected poll error: " << saved_errno;
      return;
    }

    // Check if there are any child processes to be reaped without
    // blocking.
    pid_t pid = waitpid(-1, &status, WNOHANG);
    if (pid > 0 && (ipsec->IsChild(pid) || l2tp->IsChild(pid))) {
      LOG(WARNING) << "Child process " << pid << " stopped early";
      s_terminate_request = true;
    }

    if (poll_inputs[0].revents & POLLIN)
      ipsec->ProcessOutput();
    if (poll_inputs[1].revents & POLLIN)
      l2tp->ProcessOutput();
    if (poll_inputs[2].revents & POLLIN)
      l2tp->ProcessPppOutput();
  } while (!ipsec->was_stopped() && !s_terminate_request);
}

int main(int argc, char* argv[]) {
  DEFINE_string(client_cert_id, "", "PKCS#11 key ID with client certificate");
  DEFINE_string(client_cert_slot, "", "PKCS#11 slot for client certificate");
  DEFINE_int32(log_level, 0, "Log verbosity; negative values enable debugging");
  DEFINE_string(psk_file, "", "File with IPsec pre-shared key");
  DEFINE_string(remote_host, "", "VPN server hostname");
  DEFINE_string(server_ca_file, "", "File with IPsec server CA in DER format");
  DEFINE_string(server_id, "", "ID expected from server");
  DEFINE_string(user_pin, "", "PKCS#11 User PIN");
  DEFINE_string(xauth_credentials_file, "", "File with Xauth user credentials");
  DEFINE_string(shill_task_service, "", "RPC Connection Identifier");
  DEFINE_string(shill_task_path, "", "RPC Object Identifier");

  // IpsecManager related flags.

  // Phase 1 ciphersuites:
  // aes128-sha256-modp3072: new strongSwan default
  // aes128-sha1-modp2048: old strongSwan default
  // 3des-sha1-modp1536: strongSwan fallback
  // 3des-sha1-modp1024: for compatibility with Windows RRAS, which requires
  //                     using the modp1024 dh-group
  DEFINE_string(ike,
                "aes128-sha256-modp3072,"
                "aes128-sha1-modp2048,"
                "3des-sha1-modp1536,"
                "3des-sha1-modp1024",
                "ike proposals");

  // Phase 2 ciphersuites:
  // Cisco ASA L2TP/IPsec setup instructions indicate using md5 for
  // authentication for the IPsec SA.  Default StrongS/WAN setup is
  // to only propose SHA1.
  DEFINE_string(esp,
                "aes128gcm16,"
                "aes128-sha256,"
                "aes128-sha1,"
                "3des-sha1,"
                "aes128-md5,"
                "3des-md5",
                "esp proposals");

  DEFINE_int32(ipsec_timeout, 30, "timeout for ipsec to be established");
  DEFINE_string(leftprotoport, "17/1701", "client protocol/port");
  DEFINE_bool(nat_traversal, true, "Enable NAT-T nat traversal");
  DEFINE_bool(pfs, false, "pfs");
  DEFINE_bool(rekey, true, "rekey");
  DEFINE_string(rightprotoport, "17/1701", "server protocol/port");
  DEFINE_string(tunnel_group, "", "Cisco Tunnel Group Name");
  DEFINE_string(type, "transport", "IPsec type (transport or tunnel)");

  // L2tpManager related flags.
  DEFINE_bool(defaultroute, true, "defaultroute");
  DEFINE_bool(length_bit, true, "length bit");
  DEFINE_bool(require_chap, true, "require chap");
  DEFINE_bool(refuse_pap, false, "refuse chap");
  DEFINE_bool(require_authentication, true, "require authentication");
  DEFINE_string(password, "", "password (insecure - use pppd plugin instead)");
  DEFINE_bool(ppp_lcp_echo, true, "ppp lcp echo connection monitoring");
  DEFINE_int32(ppp_setup_timeout, 60, "timeout to setup ppp (seconds)");
  DEFINE_string(pppd_plugin, "", "pppd plugin");
  DEFINE_bool(usepeerdns, true, "usepeerdns - ask peer for DNS");
  DEFINE_string(user, "", "user name");
  DEFINE_bool(systemconfig, true, "enable ppp to configure IPs/routes/DNS");

  base::FilePath run_path("/run/l2tpipsec_vpn");
  base::FilePath persistent_path = run_path.Append("current");

  // Make sure the run path exists. Since shill no longer runs as root user, we
  // can't create the run path dir here. It should have been created in shill
  // init scripts.
  if (!base::DirectoryExists(run_path)) {
    PLOG(ERROR) << "Directory does not exist " << run_path.value();
    return vpn_manager::kServiceErrorInternal;
  }

  // Make sure the persistent path exists and is accessible for read by
  // non-root users. This will allow items like CA certificates to be
  // visible by the l2tpipsec process even after it has dropped privileges.
  if (!base::DirectoryExists(persistent_path) &&
      !base::CreateDirectory(persistent_path)) {
    PLOG(ERROR) << "Unable to create directory " << persistent_path.value();
    return vpn_manager::kServiceErrorInternal;
  }
  if (chmod(persistent_path.value().c_str(),
            S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
    PLOG(ERROR) << "Unable to change permissions of directory "
                << persistent_path.value();
    return vpn_manager::kServiceErrorInternal;
  }

  // Create temp_path directory under run_path, to be deleted at exit.
  base::ScopedTempDir scoped_temp_dir;
  if (!scoped_temp_dir.CreateUniqueTempDirUnderPath(run_path)) {
    PLOG(ERROR) << "Unable create temp directory under " << run_path.value();
    return vpn_manager::kServiceErrorInternal;
  }
  const base::FilePath& temp_path = scoped_temp_dir.GetPath();

  brillo::FlagHelper::Init(argc, argv, "Chromium OS l2tpipsec VPN");
  int log_flags = brillo::kLogToSyslog;
  if (isatty(STDOUT_FILENO))
    log_flags |= brillo::kLogToStderr;
  brillo::InitLog(log_flags);
  logging::SetMinLogLevel(FLAGS_log_level);
  brillo::OpenLog("l2tpipsec_vpn", true);

  // TODO(mortonm): When shill sandboxing lands and sticks, simplify this code
  // by removing this if statement, since we will always be spawned in a
  // minijail after that.
  if (getenv("SHILL_TASK_SERVICE") == nullptr ||
      getenv("SHILL_TASK_SERVICE") == nullptr) {
    // The necessary env vars were not passed in, implying we were spawned in a
    // minijail. Clear the env vars left over from the parent and then set
    // RPC-related vars to reflect the values that were passed in on the command
    // line.
    if (clearenv() != 0) {
      PLOG(ERROR) << "Unable to clear environment vars";
      return vpn_manager::kServiceErrorInternal;
    }
    if (setenv("SHILL_TASK_SERVICE", FLAGS_shill_task_service.c_str(), 0) !=
        0) {
      PLOG(ERROR) << "Unable to set SHILL_TASK_SERVICE to: "
                  << FLAGS_shill_task_service.c_str();
      return vpn_manager::kServiceErrorInternal;
    }
    if (setenv("SHILL_TASK_PATH", FLAGS_shill_task_path.c_str(), 0) != 0) {
      PLOG(ERROR) << "Unable to set SHILL_TASK_PATH to: "
                  << FLAGS_shill_task_path.c_str();
      return vpn_manager::kServiceErrorInternal;
    }
  }

  IpsecManager ipsec(FLAGS_esp, FLAGS_ike, FLAGS_ipsec_timeout,
                     FLAGS_leftprotoport, FLAGS_rekey, FLAGS_rightprotoport,
                     FLAGS_tunnel_group, FLAGS_type, temp_path,
                     persistent_path);
  L2tpManager l2tp(FLAGS_defaultroute, FLAGS_length_bit, FLAGS_require_chap,
                   FLAGS_refuse_pap, FLAGS_require_authentication,
                   FLAGS_password, FLAGS_ppp_lcp_echo, FLAGS_ppp_setup_timeout,
                   FLAGS_pppd_plugin, FLAGS_usepeerdns, FLAGS_user,
                   FLAGS_systemconfig, temp_path);

  LockDownUmask();

  sockaddr_storage remote_address;
  if (!ServiceManager::ResolveNameToSockAddr(FLAGS_remote_host,
                                             &remote_address)) {
    LOG(ERROR) << "Unable to resolve hostname " << FLAGS_remote_host;
    return vpn_manager::kServiceErrorResolveHostnameFailed;
  }

  if (FLAGS_psk_file.empty() && !FLAGS_xauth_credentials_file.empty()) {
    LOG(ERROR) << "Providing XAUTH credentials without a PSK is invalid";
    return vpn_manager::kServiceErrorInvalidArgument;
  }

  if (!ipsec.Initialize(1, remote_address, FLAGS_psk_file,
                        FLAGS_xauth_credentials_file, FLAGS_server_ca_file,
                        FLAGS_server_id, FLAGS_client_cert_slot,
                        FLAGS_client_cert_id, FLAGS_user_pin)) {
    return ipsec.GetError();
  }
  if (!l2tp.Initialize(remote_address)) {
    return l2tp.GetError();
  }
  ServiceManager::SetLayerOrder(&ipsec, &l2tp);

  InstallSignalHandlers();
  if (!ipsec.Start()) {
    LOG(ERROR) << "Unable to start IPsec layer";
    return ipsec.GetError();
  }

  RunEventLoop(&ipsec, &l2tp);

  LOG(INFO) << "Shutting down...";
  l2tp.Stop();
  return ipsec.GetError();
}
