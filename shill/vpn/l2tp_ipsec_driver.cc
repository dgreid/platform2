// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The term "L2TP / IPSec" refers to a pair of layered protocols used
// together to establish a tunneled VPN connection.  First, an "IPSec"
// link is created, which secures a single IP traffic pair between the
// client and server.  For this link to complete, one or two levels of
// authentication are performed.  The first, inner mandatory authentication
// ensures the two parties establishing the IPSec link are correct.  This
// can use a certificate exchange or a less secure "shared group key"
// (PSK) authentication.  An optional outer IPSec authentication can also be
// performed, which is not fully supported by shill's implementation.
// In order to support "tunnel groups" from some vendor VPNs shill supports
// supplying the authentication realm portion during the outer authentication.
//
// When IPSec authentication completes, traffic is tunneled through a
// layer 2 tunnel, called "L2TP".  Using the secured link, we tunnel a
// PPP link, through which a second layer of authentication is performed,
// using the provided "user" and "password" properties.

#include "shill/vpn/l2tp_ipsec_driver.h"

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/stl_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <vpn-manager/service_error.h>

#include "shill/certificate_file.h"
#include "shill/device_info.h"
#include "shill/error.h"
#include "shill/external_task.h"
#include "shill/ipconfig.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/ppp_daemon.h"
#include "shill/ppp_device.h"
#include "shill/ppp_device_factory.h"
#include "shill/process_manager.h"
#include "shill/scope_logger.h"
#include "shill/vpn/vpn_provider.h"
#include "shill/vpn/vpn_service.h"

using base::Bind;
using base::FilePath;
using std::map;
using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kVPN;
static string ObjectID(const L2TPIPSecDriver* l) {
  return l->GetServiceRpcIdentifier().value();
}
}  // namespace Logging

namespace {

const char kL2TPIPSecIPSecTimeoutProperty[] = "L2TPIPsec.IPsecTimeout";
const char kL2TPIPSecLeftProtoPortProperty[] = "L2TPIPsec.LeftProtoPort";
const char kL2TPIPSecLengthBitProperty[] = "L2TPIPsec.LengthBit";
const char kL2TPIPSecPFSProperty[] = "L2TPIPsec.PFS";
const char kL2TPIPSecRefusePapProperty[] = "L2TPIPsec.RefusePap";
const char kL2TPIPSecRekeyProperty[] = "L2TPIPsec.Rekey";
const char kL2TPIPSecRequireAuthProperty[] = "L2TPIPsec.RequireAuth";
const char kL2TPIPSecRequireChapProperty[] = "L2TPIPsec.RequireChap";
const char kL2TPIPSecRightProtoPortProperty[] = "L2TPIPsec.RightProtoPort";

constexpr int kConnectTimeoutSeconds = 60;

Service::ConnectFailure ExitStatusToFailure(int status) {
  switch (status) {
    case vpn_manager::kServiceErrorNoError:
      return Service::kFailureNone;
    case vpn_manager::kServiceErrorInternal:
    case vpn_manager::kServiceErrorInvalidArgument:
      return Service::kFailureInternal;
    case vpn_manager::kServiceErrorResolveHostnameFailed:
      return Service::kFailureDNSLookup;
    case vpn_manager::kServiceErrorIpsecConnectionFailed:
    case vpn_manager::kServiceErrorL2tpConnectionFailed:
    case vpn_manager::kServiceErrorPppConnectionFailed:
      return Service::kFailureConnect;
    case vpn_manager::kServiceErrorIpsecPresharedKeyAuthenticationFailed:
      return Service::kFailureIPSecPSKAuth;
    case vpn_manager::kServiceErrorIpsecCertificateAuthenticationFailed:
      return Service::kFailureIPSecCertAuth;
    case vpn_manager::kServiceErrorPppAuthenticationFailed:
      return Service::kFailurePPPAuth;
    default:
      return Service::kFailureUnknown;
  }
}

}  // namespace

// static
const char L2TPIPSecDriver::kL2TPIPSecVPNPath[] = "/usr/sbin/l2tpipsec_vpn";
// static
const VPNDriver::Property L2TPIPSecDriver::kProperties[] = {
    {kL2tpIpsecClientCertIdProperty, 0},
    {kL2tpIpsecClientCertSlotProperty, 0},
    {kL2tpIpsecPasswordProperty, Property::kCredential | Property::kWriteOnly},
    {kL2tpIpsecPinProperty, Property::kCredential},
    {kL2tpIpsecPskProperty, Property::kCredential | Property::kWriteOnly},
    {kL2tpIpsecUserProperty, 0},
    {kProviderHostProperty, 0},
    {kProviderTypeProperty, 0},
    {kL2tpIpsecCaCertPemProperty, Property::kArray},
    {kL2tpIpsecTunnelGroupProperty, 0},
    {kL2TPIPSecIPSecTimeoutProperty, 0},
    {kL2TPIPSecLeftProtoPortProperty, 0},
    {kL2TPIPSecLengthBitProperty, 0},
    {kL2TPIPSecPFSProperty, 0},
    {kL2TPIPSecRefusePapProperty, 0},
    {kL2TPIPSecRekeyProperty, 0},
    {kL2TPIPSecRequireAuthProperty, 0},
    {kL2TPIPSecRequireChapProperty, 0},
    {kL2TPIPSecRightProtoPortProperty, 0},
    {kL2tpIpsecXauthUserProperty, Property::kCredential | Property::kWriteOnly},
    {kL2tpIpsecXauthPasswordProperty,
     Property::kCredential | Property::kWriteOnly},
    {kL2tpIpsecLcpEchoDisabledProperty, 0},
};

L2TPIPSecDriver::L2TPIPSecDriver(Manager* manager,
                                 ProcessManager* process_manager)
    : VPNDriver(manager, process_manager, kProperties, base::size(kProperties)),
      ppp_device_factory_(PPPDeviceFactory::GetInstance()),
      certificate_file_(new CertificateFile()),
      weak_ptr_factory_(this) {}

L2TPIPSecDriver::~L2TPIPSecDriver() {
  IdleService();
}

const RpcIdentifier& L2TPIPSecDriver::GetServiceRpcIdentifier() const {
  static RpcIdentifier null_identifier("(l2tp_ipsec_driver)");
  if (service() == nullptr)
    return null_identifier;
  return service()->GetRpcIdentifier();
}

void L2TPIPSecDriver::Connect(const VPNServiceRefPtr& service, Error* error) {
  StartConnectTimeout(kConnectTimeoutSeconds);
  set_service(service);
  service->SetState(Service::kStateConfiguring);
  if (!SpawnL2TPIPSecVPN(error)) {
    FailService(Service::kFailureInternal);
  }
}

void L2TPIPSecDriver::Disconnect() {
  SLOG(this, 2) << __func__;
  IdleService();
}

void L2TPIPSecDriver::OnConnectTimeout() {
  VPNDriver::OnConnectTimeout();
  FailService(Service::kFailureConnect);
}

string L2TPIPSecDriver::GetProviderType() const {
  return kProviderL2tpIpsec;
}

VPNDriver::IfType L2TPIPSecDriver::GetIfType() const {
  return kDriverManaged;
}

void L2TPIPSecDriver::IdleService() {
  Cleanup(Service::kStateIdle, Service::kFailureNone);
}

void L2TPIPSecDriver::FailService(Service::ConnectFailure failure) {
  Cleanup(Service::kStateFailure, failure);
}

void L2TPIPSecDriver::Cleanup(Service::ConnectState state,
                              Service::ConnectFailure failure) {
  SLOG(this, 2) << __func__ << "(" << Service::ConnectStateToString(state)
                << ", " << Service::ConnectFailureToString(failure) << ")";
  StopConnectTimeout();
  DeleteTemporaryFiles();
  external_task_.reset();
  if (device_) {
    device_->DropConnection();
    device_->SetEnabled(false);
    device_ = nullptr;
  }
  if (service()) {
    if (state == Service::kStateFailure) {
      service()->SetFailure(failure);
    } else {
      service()->SetState(state);
    }
    set_service(nullptr);
  }
}

void L2TPIPSecDriver::DeleteTemporaryFile(base::FilePath* temporary_file) {
  if (!temporary_file->empty()) {
    base::DeleteFile(*temporary_file, false);
    temporary_file->clear();
  }
}

void L2TPIPSecDriver::DeleteTemporaryFiles() {
  DeleteTemporaryFile(&psk_file_);
  DeleteTemporaryFile(&xauth_credentials_file_);
}

bool L2TPIPSecDriver::SpawnL2TPIPSecVPN(Error* error) {
  SLOG(this, 2) << __func__;
  auto external_task_local = std::make_unique<ExternalTask>(
      control_interface(), process_manager(), weak_ptr_factory_.GetWeakPtr(),
      Bind(&L2TPIPSecDriver::OnL2TPIPSecVPNDied,
           weak_ptr_factory_.GetWeakPtr()));

  vector<string> options;
  map<string, string> environment;  // No env vars passed.
  if (!InitOptions(&options, error)) {
    return false;
  }
  LOG(INFO) << "L2TP/IPSec VPN process options: "
            << base::JoinString(options, " ");

  uint64_t capmask = CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_RAW) |
                     CAP_TO_MASK(CAP_NET_BIND_SERVICE) |
                     CAP_TO_MASK(CAP_SETUID) | CAP_TO_MASK(CAP_SETGID) |
                     CAP_TO_MASK(CAP_KILL);
  if (!external_task_local->StartInMinijail(FilePath(kL2TPIPSecVPNPath),
                                            &options, "shill", "shill", capmask,
                                            true, true, error)) {
    return false;
  }
  external_task_ = std::move(external_task_local);
  return true;
}

bool L2TPIPSecDriver::InitOptions(vector<string>* options, Error* error) {
  string vpnhost = args()->Lookup<string>(kProviderHostProperty, "");
  if (vpnhost.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "VPN host not specified.");
    return false;
  }

  if (!InitPSKOptions(options, error)) {
    return false;
  }

  if (!InitXauthOptions(options, error)) {
    return false;
  }

  options->push_back(base::StringPrintf("--remote_host=%s", vpnhost.c_str()));
  options->push_back(
      base::StringPrintf("--pppd_plugin=%s", PPPDaemon::kShimPluginPath));
  // Disable pppd from configuring IP addresses, routes, DNS.
  options->push_back("--nosystemconfig");

  // Accept a PEM CA certificate.
  InitPEMOptions(options);

  AppendValueOption(kL2tpIpsecClientCertIdProperty, "--client_cert_id",
                    options);
  AppendValueOption(kL2tpIpsecClientCertSlotProperty, "--client_cert_slot",
                    options);
  AppendValueOption(kL2tpIpsecPinProperty, "--user_pin", options);
  AppendValueOption(kL2tpIpsecUserProperty, "--user", options);
  AppendValueOption(kL2TPIPSecIPSecTimeoutProperty, "--ipsec_timeout", options);
  AppendValueOption(kL2TPIPSecLeftProtoPortProperty, "--leftprotoport",
                    options);
  AppendFlag(kL2TPIPSecPFSProperty, "--pfs", "--nopfs", options);
  AppendFlag(kL2TPIPSecRekeyProperty, "--rekey", "--norekey", options);
  AppendValueOption(kL2TPIPSecRightProtoPortProperty, "--rightprotoport",
                    options);
  AppendFlag(kL2TPIPSecRequireChapProperty, "--require_chap",
             "--norequire_chap", options);
  AppendFlag(kL2TPIPSecRefusePapProperty, "--refuse_pap", "--norefuse_pap",
             options);
  AppendFlag(kL2TPIPSecRequireAuthProperty, "--require_authentication",
             "--norequire_authentication", options);
  AppendFlag(kL2TPIPSecLengthBitProperty, "--length_bit", "--nolength_bit",
             options);
  AppendFlag(kL2tpIpsecLcpEchoDisabledProperty, "--noppp_lcp_echo",
             "--ppp_lcp_echo", options);
  AppendValueOption(kL2tpIpsecTunnelGroupProperty, "--tunnel_group", options);
  if (SLOG_IS_ON(VPN, 0)) {
    options->push_back(base::StringPrintf(
        "--log_level=%d", -ScopeLogger::GetInstance()->verbose_level()));
  }
  return true;
}

bool L2TPIPSecDriver::InitPSKOptions(vector<string>* options, Error* error) {
  string psk = args()->Lookup<string>(kL2tpIpsecPskProperty, "");
  if (!psk.empty()) {
    if (!base::CreateTemporaryFileInDir(manager()->run_path(), &psk_file_) ||
        chmod(psk_file_.value().c_str(), S_IRUSR | S_IWUSR) ||
        base::WriteFile(psk_file_, psk.data(), psk.size()) !=
            static_cast<int>(psk.size())) {
      Error::PopulateAndLog(FROM_HERE, error, Error::kInternalError,
                            "Unable to setup psk file.");
      return false;
    }
    options->push_back(
        base::StringPrintf("--psk_file=%s", psk_file_.value().c_str()));
  }
  return true;
}

bool L2TPIPSecDriver::InitPEMOptions(vector<string>* options) {
  vector<string> ca_certs;
  if (args()->Contains<Strings>(kL2tpIpsecCaCertPemProperty)) {
    ca_certs = args()->Get<Strings>(kL2tpIpsecCaCertPemProperty);
  }
  if (ca_certs.empty()) {
    return false;
  }
  FilePath certfile = certificate_file_->CreatePEMFromStrings(ca_certs);
  if (certfile.empty()) {
    LOG(ERROR) << "Unable to extract certificates from PEM string.";
    return false;
  }
  options->push_back(
      base::StringPrintf("--server_ca_file=%s", certfile.value().c_str()));
  return true;
}

bool L2TPIPSecDriver::InitXauthOptions(vector<string>* options, Error* error) {
  string user = args()->Lookup<string>(kL2tpIpsecXauthUserProperty, "");
  string password = args()->Lookup<string>(kL2tpIpsecXauthPasswordProperty, "");
  if (user.empty() && password.empty()) {
    // Xauth credentials not configured.
    return true;
  }
  if (user.empty() || password.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "XAUTH credentials are partially configured.");
    return false;
  }
  string xauth_credentials = user + "\n" + password + "\n";
  if (!base::CreateTemporaryFileInDir(manager()->run_path(),
                                      &xauth_credentials_file_) ||
      chmod(xauth_credentials_file_.value().c_str(), S_IRUSR | S_IWUSR) ||
      base::WriteFile(xauth_credentials_file_, xauth_credentials.data(),
                      xauth_credentials.size()) !=
          static_cast<int>(xauth_credentials.size())) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInternalError,
                          "Unable to setup XAUTH credentials file.");
    return false;
  }
  options->push_back(base::StringPrintf(
      "--xauth_credentials_file=%s", xauth_credentials_file_.value().c_str()));
  return true;
}

bool L2TPIPSecDriver::AppendValueOption(const string& property,
                                        const string& option,
                                        vector<string>* options) {
  string value = args()->Lookup<string>(property, "");
  if (!value.empty()) {
    options->push_back(
        base::StringPrintf("%s=%s", option.c_str(), value.c_str()));
    return true;
  }
  return false;
}

bool L2TPIPSecDriver::AppendFlag(const string& property,
                                 const string& true_option,
                                 const string& false_option,
                                 vector<string>* options) {
  string value = args()->Lookup<string>(property, "");
  if (!value.empty()) {
    options->push_back(value == "true" ? true_option : false_option);
    return true;
  }
  return false;
}

void L2TPIPSecDriver::OnL2TPIPSecVPNDied(pid_t /*pid*/, int status) {
  FailService(ExitStatusToFailure(status));
  // TODO(petkov): Figure if we need to restart the connection.
}

void L2TPIPSecDriver::GetLogin(string* user, string* password) {
  LOG(INFO) << "Login requested.";
  string user_property = args()->Lookup<string>(kL2tpIpsecUserProperty, "");
  if (user_property.empty()) {
    LOG(ERROR) << "User not set.";
    return;
  }
  string password_property =
      args()->Lookup<string>(kL2tpIpsecPasswordProperty, "");
  if (password_property.empty()) {
    LOG(ERROR) << "Password not set.";
    return;
  }
  *user = user_property;
  *password = password_property;
}

void L2TPIPSecDriver::Notify(const string& reason,
                             const map<string, string>& dict) {
  LOG(INFO) << "IP configuration received: " << reason;

  if (reason == kPPPReasonAuthenticating || reason == kPPPReasonAuthenticated) {
    // These are uninteresting intermediate states that do not indicate failure.
    return;
  }

  if (reason != kPPPReasonConnect) {
    DCHECK_EQ(kPPPReasonDisconnect, reason);
    // TODO(crbug.com/989361) We should move into a disconnecting state, stop
    // this task if it exists, and wait for the task to fully shut down before
    // completing the disconnection. This should wait for the VPNDriver code to
    // be refactored, as the disconnect flow is a mess as it stands.
    external_task_.reset();
    FailService(Service::kFailureUnknown);
    return;
  }

  DeleteTemporaryFiles();

  string interface_name = PPPDevice::GetInterfaceName(dict);
  int interface_index = manager()->device_info()->GetIndex(interface_name);
  if (interface_index < 0) {
    // TODO(petkov): Consider handling the race when the RTNL notification about
    // the new PPP device has not been received yet. We can keep the IP
    // configuration and apply it when ClaimInterface is
    // invoked. crbug.com/212446.
    NOTIMPLEMENTED() << ": No device info for " << interface_name << ".";
    return;
  }

  if (!device_) {
    device_ = ppp_device_factory_->CreatePPPDevice(manager(), interface_name,
                                                   interface_index);
  }
  device_->SetEnabled(true);
  device_->SelectService(service());

  IPConfig::Properties properties = device_->ParseIPConfiguration(dict);

  // There is no IPv6 support for L2TP/IPsec VPN at this moment, so create a
  // blackhole route for IPv6 traffic after establishing a IPv4 VPN.
  // TODO(benchan): Generalize this when IPv6 support is added.
  properties.blackhole_ipv6 = true;

  // Reduce MTU to the minimum viable for IPv6, since the IPSec layer consumes
  // some variable portion of the payload.  Although this system does not yet
  // support IPv6, it is a reasonable value to start with, since the minimum
  // IPv6 packet size will plausibly be a size any gateway would support, and
  // is also larger than the IPv4 minimum size.
  properties.mtu = IPConfig::kMinIPv6MTU;

  manager()->vpn_provider()->SetDefaultRoutingPolicy(&properties);
  device_->UpdateIPConfig(properties);

  ReportConnectionMetrics();
  StopConnectTimeout();
}

bool L2TPIPSecDriver::IsPskRequired() const {
  return const_args()->Lookup<string>(kL2tpIpsecPskProperty, "").empty() &&
         const_args()
             ->Lookup<string>(kL2tpIpsecClientCertIdProperty, "")
             .empty();
}

KeyValueStore L2TPIPSecDriver::GetProvider(Error* error) {
  SLOG(this, 2) << __func__;
  KeyValueStore props = VPNDriver::GetProvider(error);
  props.Set<bool>(
      kPassphraseRequiredProperty,
      args()->Lookup<string>(kL2tpIpsecPasswordProperty, "").empty());
  props.Set<bool>(kL2tpIpsecPskRequiredProperty, IsPskRequired());
  return props;
}

void L2TPIPSecDriver::ReportConnectionMetrics() {
  metrics()->SendEnumToUMA(Metrics::kMetricVpnDriver,
                           Metrics::kVpnDriverL2tpIpsec,
                           Metrics::kMetricVpnDriverMax);

  // We output an enum for each of the authentication types specified,
  // even if more than one is set at the same time.
  bool has_remote_authentication = false;
  if (args()->Contains<Strings>(kL2tpIpsecCaCertPemProperty) &&
      !args()->Get<Strings>(kL2tpIpsecCaCertPemProperty).empty()) {
    metrics()->SendEnumToUMA(
        Metrics::kMetricVpnRemoteAuthenticationType,
        Metrics::kVpnRemoteAuthenticationTypeL2tpIpsecCertificate,
        Metrics::kMetricVpnRemoteAuthenticationTypeMax);
    has_remote_authentication = true;
  }
  if (args()->Lookup<string>(kL2tpIpsecPskProperty, "") != "") {
    metrics()->SendEnumToUMA(Metrics::kMetricVpnRemoteAuthenticationType,
                             Metrics::kVpnRemoteAuthenticationTypeL2tpIpsecPsk,
                             Metrics::kMetricVpnRemoteAuthenticationTypeMax);
    has_remote_authentication = true;
  }
  if (!has_remote_authentication) {
    metrics()->SendEnumToUMA(
        Metrics::kMetricVpnRemoteAuthenticationType,
        Metrics::kVpnRemoteAuthenticationTypeL2tpIpsecDefault,
        Metrics::kMetricVpnRemoteAuthenticationTypeMax);
  }

  bool has_user_authentication = false;
  if (args()->Lookup<string>(kL2tpIpsecClientCertIdProperty, "") != "") {
    metrics()->SendEnumToUMA(
        Metrics::kMetricVpnUserAuthenticationType,
        Metrics::kVpnUserAuthenticationTypeL2tpIpsecCertificate,
        Metrics::kMetricVpnUserAuthenticationTypeMax);
    has_user_authentication = true;
  }
  if (args()->Lookup<string>(kL2tpIpsecPasswordProperty, "") != "") {
    metrics()->SendEnumToUMA(
        Metrics::kMetricVpnUserAuthenticationType,
        Metrics::kVpnUserAuthenticationTypeL2tpIpsecUsernamePassword,
        Metrics::kMetricVpnUserAuthenticationTypeMax);
    has_user_authentication = true;
  }
  if (!has_user_authentication) {
    metrics()->SendEnumToUMA(Metrics::kMetricVpnUserAuthenticationType,
                             Metrics::kVpnUserAuthenticationTypeL2tpIpsecNone,
                             Metrics::kMetricVpnUserAuthenticationTypeMax);
  }
}

}  // namespace shill
