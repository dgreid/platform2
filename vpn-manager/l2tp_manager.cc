// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vpn-manager/l2tp_manager.h"

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>  // for setenv()
#include <sys/stat.h>
#include <sys/types.h>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/pattern.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/process/process.h>

using ::base::FilePath;
using ::base::StringPrintf;
using ::brillo::ProcessImpl;

namespace vpn_manager {

namespace {

const char kL2tpConnectionName[] = "managed";
// Environment variable available to ppp plugin to know the resolved address
// of the L2TP server.
const char kLnsAddress[] = "LNS_ADDRESS";
const char kPppInterfacePath[] = "/sys/class/net/ppp0";
const char kPppLogPrefix[] = "pppd: ";
const char kPppAuthenticationFailurePattern[] = "*authentication failed*";
const char kBpsParameter[] = "1000000";
const char kRedialParameter[] = "yes";
const char kRedialTimeoutParameter[] = "2";
const char kMaxRedialsParameter[] = "30";
// Path to pid file that contains pid for xl2tpd process.
const char kXl2tpdPidFilePath[] = "/run/l2tpipsec_vpn/xl2tpd.pid";

// xl2tpd (1.3.12 at the time of writing) uses fgets with a size 1024 buffer to
// get configuration lines. If a configuration line was longer than that and
// didn't contain the comment delimiter ';', it could be used to populate
// multiple configuration options.
constexpr size_t kXl2tpdMaxConfigurationLength = 1023;

bool AddString(std::string* config, const char* key, const std::string& value) {
  if (value.find('\n') != value.npos) {
    std::string escaped_value = value;
    // Escape newlines prior to logging.
    size_t pos;
    while ((pos = escaped_value.find('\n')) != escaped_value.npos) {
      escaped_value.replace(pos, 1, "\\n");
    }
    LOG(ERROR) << key << " value may not contain a newline: '" << escaped_value
               << "'";
    return false;
  }

  auto line = StringPrintf("%s = %s\n", key, value.c_str());
  if (line.size() > kXl2tpdMaxConfigurationLength) {
    LOG(ERROR) << "Line may not exceed " << kXl2tpdMaxConfigurationLength
               << " characters: '" << line << "'";
    return false;
  }

  config->append(line);
  return true;
}

void AddBool(std::string* config, const char* key, bool value) {
  config->append(StringPrintf("%s = %s\n", key, value ? "yes" : "no"));
}

}  // namespace

L2tpManager::L2tpManager(bool default_route,
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
                         const base::FilePath& temp_path)
    : ServiceManager("l2tp", temp_path),
      default_route_(default_route),
      length_bit_(length_bit),
      require_chap_(require_chap),
      refuse_pap_(refuse_pap),
      require_authentication_(require_authentication),
      password_(password),
      ppp_lcp_echo_(ppp_lcp_echo),
      ppp_setup_timeout_(ppp_setup_timeout),
      pppd_plugin_(pppd_plugin),
      use_peer_dns_(use_peer_dns),
      user_(user),
      system_config_(system_config),
      was_initiated_(false),
      output_fd_(-1),
      ppp_output_fd_(-1),
      ppp_interface_path_(kPppInterfacePath),
      l2tpd_(new ProcessImpl) {}

int L2tpManager::GetPppSetupTimeoutForTesting() {
  return ppp_setup_timeout_;
}

void L2tpManager::SetDefaultRouteForTesting(bool default_route) {
  default_route_ = default_route;
}

void L2tpManager::SetPasswordForTesting(const std::string& password) {
  password_ = password;
}

void L2tpManager::SetPppdPluginForTesting(const std::string& pppd_plugin) {
  pppd_plugin_ = pppd_plugin;
}

void L2tpManager::SetPppLcpEchoForTesting(bool ppp_lcp_echo) {
  ppp_lcp_echo_ = ppp_lcp_echo;
}

void L2tpManager::SetUsePeerDnsForTesting(bool use_peer_dns) {
  use_peer_dns_ = use_peer_dns;
}

void L2tpManager::SetUserForTesting(const std::string& user) {
  user_ = user;
}

void L2tpManager::SetSystemConfigForTesting(bool system_config) {
  system_config_ = system_config;
}

bool L2tpManager::Initialize(const sockaddr_storage& remote_address) {
  if (!ConvertSockAddrToIPString(remote_address, &remote_address_text_)) {
    LOG(ERROR) << "Unable to convert sockaddr to name for remote host";
    RegisterError(kServiceErrorInternal);
    return false;
  }
  remote_address_ = remote_address;

  if (user_.empty()) {
    LOG(ERROR) << "l2tp layer requires user name";
    RegisterError(kServiceErrorInvalidArgument);
    return false;
  }
  if (!pppd_plugin_.empty() && !base::PathExists(FilePath(pppd_plugin_))) {
    LOG(WARNING) << "pppd_plugin (" << pppd_plugin_ << ") does not exist";
  }
  if (!password_.empty()) {
    LOG(WARNING) << "Passing a password on the command-line is insecure";
  }
  return true;
}

bool L2tpManager::CreatePppLogFifo() {
  ppp_output_path_ = temp_path().Append("pppd.log");
  const char* fifo_path = ppp_output_path_.value().c_str();
  if (HANDLE_EINTR(mkfifo(fifo_path, S_IRUSR | S_IWUSR)) == 0) {
    ppp_output_fd_ = HANDLE_EINTR(open(fifo_path, O_RDONLY | O_NONBLOCK));
    if (ppp_output_fd_ != -1)
      return true;
  }
  return false;
}

base::Optional<std::string> L2tpManager::FormatL2tpdConfiguration(
    const std::string& ppp_config_path) {
  std::string l2tpd_config;
  bool success = true;
  l2tpd_config.append(StringPrintf("[lac %s]\n", kL2tpConnectionName));
  success &= AddString(&l2tpd_config, "lns", remote_address_text_);
  AddBool(&l2tpd_config, "require chap", require_chap_);
  AddBool(&l2tpd_config, "refuse pap", refuse_pap_);
  AddBool(&l2tpd_config, "require authentication", require_authentication_);
  success &= AddString(&l2tpd_config, "name", user_);
  if (VLOG_IS_ON(4)) {
    AddBool(&l2tpd_config, "ppp debug", true);
  }
  success &= AddString(&l2tpd_config, "pppoptfile", ppp_config_path);
  AddBool(&l2tpd_config, "length bit", length_bit_);
  success &= AddString(&l2tpd_config, "bps", kBpsParameter);
  success &= AddString(&l2tpd_config, "redial", kRedialParameter);
  success &=
      AddString(&l2tpd_config, "redial timeout", kRedialTimeoutParameter);
  success &= AddString(&l2tpd_config, "max redials", kMaxRedialsParameter);

  if (!success) {
    return base::nullopt;
  }
  return l2tpd_config;
}

std::string L2tpManager::FormatPppdConfiguration() {
  std::string pppd_config(
      "ipcp-accept-local\n"
      "ipcp-accept-remote\n"
      "refuse-eap\n"
      "noccp\n"
      "noauth\n"
      "crtscts\n"
      "mtu 1410\n"
      "mru 1410\n"
      "lock\n"
      "connect-delay 5000\n");
  pppd_config.append(
      StringPrintf("%sdefaultroute\n", default_route_ ? "" : "no"));
  if (ppp_lcp_echo_) {
    pppd_config.append(
        "lcp-echo-failure 4\n"
        "lcp-echo-interval 30\n");
  }
  if (ppp_output_fd_ != -1) {
    pppd_config.append(
        StringPrintf("logfile %s\n", ppp_output_path_.value().c_str()));
  }
  if (use_peer_dns_) {
    pppd_config.append("usepeerdns\n");
  }
  if (!system_config_) {
    // nosystemconfig is only supported by the chromiumos patched
    // version of pppd.
    pppd_config.append("nosystemconfig\n");
  }
  if (!pppd_plugin_.empty()) {
    DLOG(INFO) << "Using pppd plugin " << pppd_plugin_;
    pppd_config.append(StringPrintf("plugin %s\n", pppd_plugin_.c_str()));
  }
  if (VLOG_IS_ON(2)) {
    pppd_config.append("debug\n");
  }
  return pppd_config;
}

bool L2tpManager::Initiate() {
  std::string control_string;
  control_string = StringPrintf("c %s", kL2tpConnectionName);
  if (pppd_plugin_.empty()) {
    control_string.append(
        StringPrintf(" %s %s\n", user_.c_str(), password_.c_str()));
  } else {
    // otherwise the plugin must specify username and password.
    control_string.append("\n");
  }
  if (!base::WriteFile(l2tpd_control_path_, control_string.c_str(),
                       control_string.size())) {
    return false;
  }
  was_initiated_ = true;
  return true;
}

bool L2tpManager::Terminate() {
  std::string control_string = StringPrintf("d %s\n", kL2tpConnectionName);
  if (!base::WriteFile(l2tpd_control_path_, control_string.c_str(),
                       control_string.size())) {
    return false;
  }
  return true;
}

bool L2tpManager::Start() {
  FilePath pppd_config_path = temp_path().Append("pppd.conf");
  auto l2tpd_config = FormatL2tpdConfiguration(pppd_config_path.value());
  if (!l2tpd_config) {
    LOG(ERROR) << "Failed to write xl2tpd configuration";
    RegisterError(kServiceErrorInvalidArgument);
    return false;
  }
  FilePath l2tpd_config_path = temp_path().Append("l2tpd.conf");
  if (!base::WriteFile(l2tpd_config_path, l2tpd_config.value().c_str(),
                       l2tpd_config.value().size())) {
    LOG(ERROR) << "Unable to write l2tpd config to "
               << l2tpd_config_path.value();
    RegisterError(kServiceErrorInternal);
    return false;
  }

  if (!CreatePppLogFifo()) {
    PLOG(ERROR) << "Unable to create ppp log fifo";
    RegisterError(kServiceErrorInternal);
    return false;
  }
  std::string pppd_config = FormatPppdConfiguration();
  if (!base::WriteFile(pppd_config_path, pppd_config.c_str(),
                       pppd_config.size())) {
    LOG(ERROR) << "Unable to write pppd config to " << pppd_config_path.value();
    RegisterError(kServiceErrorInternal);
    return false;
  }
  l2tpd_control_path_ = temp_path().Append("l2tpd.control");
  base::DeleteFile(l2tpd_control_path_);

  if (!pppd_plugin_.empty()) {
    // Pass the resolved LNS address to the plugin.
    setenv(kLnsAddress, remote_address_text_.c_str(), 1);
  }

  // crbug/1046396 - Kill existing xl2tpd process first.
  base::FilePath pidFilePath(kXl2tpdPidFilePath);
  if (base::PathExists(pidFilePath)) {
    std::string pid_str;
    int pid = 0;
    base::ReadFileToString(pidFilePath, &pid_str);
    base::StringToInt(pid_str, &pid);
    if (pid != 0) {
      LOG(INFO) << "Killing existing xl2tpd process " << pid;
      l2tpd_->Reset(pid);
      l2tpd_->Kill(SIGKILL, 0);
    } else {
      LOG(ERROR) << "Unable to parse pid file " << kXl2tpdPidFilePath;
    }
  }

  l2tpd_->Reset(0);
  l2tpd_->AddArg(L2TPD);
  l2tpd_->AddStringOption("-c", l2tpd_config_path.value());
  l2tpd_->AddStringOption("-C", l2tpd_control_path_.value());
  l2tpd_->AddArg("-D");
  l2tpd_->AddStringOption("-p", kXl2tpdPidFilePath);
  l2tpd_->RedirectUsingPipe(STDERR_FILENO, false);
  l2tpd_->Start();
  output_fd_ = l2tpd_->GetPipe(STDERR_FILENO);
  start_ticks_ = base::TimeTicks::Now();
  return true;
}

int L2tpManager::Poll() {
  if (is_running())
    return -1;
  if (start_ticks_.is_null())
    return -1;
  if (!was_initiated_ && base::PathExists(l2tpd_control_path_)) {
    if (!Initiate()) {
      LOG(ERROR) << "Unable to initiate connection";
      RegisterError(kServiceErrorL2tpConnectionFailed);
      Terminate();
      OnStopped(false);
      return -1;
    }
    // With the connection initated, check if it's up in 1s.
    return 1000;
  }
  if (was_initiated_ && base::PathExists(FilePath(ppp_interface_path_))) {
    LOG(INFO) << "L2TP connection now up";
    OnStarted();
    return -1;
  }
  // Check for the ppp setup timeout.  This includes the time
  // to start pppd, it to set up its control file, l2tp connection
  // setup, ppp connection setup.  Authentication happens after
  // the ppp device is created.
  if (base::TimeTicks::Now() - start_ticks_ >
      base::TimeDelta::FromSeconds(ppp_setup_timeout_)) {
    RegisterError(kServiceErrorPppConnectionFailed);
    LOG(ERROR) << "PPP setup timed out";
    // Cleanly terminate if the control file exists.
    if (was_initiated_)
      Terminate();
    OnStopped(false);
    // Poll in 1 second in order to check if clean shutdown worked.
  }
  return 1000;
}

void L2tpManager::ProcessOutput() {
  WriteFdToSyslog(output_fd_, "", &partial_output_line_);
}

void L2tpManager::ProcessPppOutput() {
  WriteFdToSyslog(ppp_output_fd_, kPppLogPrefix, &partial_ppp_output_line_);
}

bool L2tpManager::IsChild(pid_t pid) {
  return pid == l2tpd_->pid();
}

void L2tpManager::Stop() {
  if (l2tpd_->pid()) {
    LOG(INFO) << "Shutting down L2TP";
    Terminate();
  }
  OnStopped(false);
}

void L2tpManager::OnSyslogOutput(const std::string& prefix,
                                 const std::string& line) {
  if (prefix == kPppLogPrefix &&
      base::MatchPattern(line, kPppAuthenticationFailurePattern)) {
    LOG(ERROR) << "PPP authentication failed";
    RegisterError(kServiceErrorPppAuthenticationFailed);
    Stop();
  }
}

}  // namespace vpn_manager
