// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/openvpn_driver.h"

#include <arpa/inet.h>

#include <limits>

#include <base/files/file_util.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/certificate_file.h"
#include "shill/connection.h"
#include "shill/device_info.h"
#include "shill/error.h"
#include "shill/ipconfig.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/net/sockets.h"
#include "shill/process_manager.h"
#include "shill/rpc_task.h"
#include "shill/virtual_device.h"
#include "shill/vpn/openvpn_management_server.h"
#include "shill/vpn/vpn_provider.h"
#include "shill/vpn/vpn_service.h"

using base::FilePath;
using base::SplitString;
using base::Unretained;
using std::map;
using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kVPN;
static string ObjectID(const OpenVPNDriver* o) {
  return o->GetServiceRpcIdentifier().value();
}
}  // namespace Logging

namespace {

const char kChromeOSReleaseName[] = "CHROMEOS_RELEASE_NAME";
const char kChromeOSReleaseVersion[] = "CHROMEOS_RELEASE_VERSION";
const char kOpenVPNForeignOptionPrefix[] = "foreign_option_";
const char kOpenVPNIfconfigBroadcast[] = "ifconfig_broadcast";
const char kOpenVPNIfconfigLocal[] = "ifconfig_local";
const char kOpenVPNIfconfigNetmask[] = "ifconfig_netmask";
const char kOpenVPNIfconfigRemote[] = "ifconfig_remote";
const char kOpenVPNRedirectGateway[] = "redirect_gateway";
const char kOpenVPNRouteOptionPrefix[] = "route_";
const char kOpenVPNRouteNetGateway[] = "route_net_gateway";
const char kOpenVPNRouteVPNGateway[] = "route_vpn_gateway";
const char kOpenVPNTrustedIP[] = "trusted_ip";
const char kOpenVPNTunMTU[] = "tun_mtu";

// Typically OpenVPN will set environment variables like:
//   route_net_gateway=<existing default LAN gateway>
//   route_vpn_gateway=10.8.0.1
//   route_gateway_1=10.8.0.1
//   route_netmask_1=255.255.255.0
//   route_network_1=192.168.10.0
// This example shows a split include route of 192.168.10.0/24, and
// 10.8.0.1 is the ifconfig_remote (remote peer) address.
const char kOpenVPNRouteNetworkPrefix[] = "network_";
const char kOpenVPNRouteNetmaskPrefix[] = "netmask_";
const char kOpenVPNRouteGatewayPrefix[] = "gateway_";

const char kDefaultPKCS11Provider[] = "libchaps.so";

// Some configurations pass the netmask in the ifconfig_remote property.
// This is due to some servers not explicitly indicating that they are using
// a "broadcast mode" network instead of peer-to-peer.  See
// http://crbug.com/241264 for an example of this issue.
const char kSuspectedNetmaskPrefix[] = "255.";

void DoNothingWithExitStatus(int exit_status) {}

}  // namespace

// static
const char OpenVPNDriver::kDefaultCACertificates[] =
    "/etc/ssl/certs/ca-certificates.crt";
// static
const char OpenVPNDriver::kOpenVPNPath[] = "/usr/sbin/openvpn";
// static
const char OpenVPNDriver::kOpenVPNScript[] = SHIMDIR "/openvpn-script";
// static
const VPNDriver::Property OpenVPNDriver::kProperties[] = {
    {kOpenVPNAuthNoCacheProperty, 0},
    {kOpenVPNAuthProperty, 0},
    {kOpenVPNAuthRetryProperty, 0},
    {kOpenVPNAuthUserPassProperty, 0},
    {kOpenVPNCipherProperty, 0},
    {kOpenVPNClientCertIdProperty, Property::kCredential},
    {kOpenVPNCompLZOProperty, 0},
    {kOpenVPNCompNoAdaptProperty, 0},
    {kOpenVPNCompressProperty, 0},
    {kOpenVPNExtraHostsProperty, Property::kArray},
    {kOpenVPNIgnoreDefaultRouteProperty, 0},
    {kOpenVPNKeyDirectionProperty, 0},
    {kOpenVPNNsCertTypeProperty, 0},
    {kOpenVPNOTPProperty,
     Property::kEphemeral | Property::kCredential | Property::kWriteOnly},
    {kOpenVPNPasswordProperty, Property::kCredential | Property::kWriteOnly},
    {kOpenVPNPinProperty, Property::kCredential},
    {kOpenVPNPortProperty, 0},
    {kOpenVPNProtoProperty, 0},
    {kOpenVPNPushPeerInfoProperty, 0},
    {kOpenVPNRemoteCertEKUProperty, 0},
    {kOpenVPNRemoteCertKUProperty, 0},
    {kOpenVPNRemoteCertTLSProperty, 0},
    {kOpenVPNRenegSecProperty, 0},
    {kOpenVPNServerPollTimeoutProperty, 0},
    {kOpenVPNShaperProperty, 0},
    {kOpenVPNStaticChallengeProperty, 0},
    {kOpenVPNTLSAuthContentsProperty, 0},
    {kOpenVPNTLSRemoteProperty, 0},
    {kOpenVPNTLSVersionMinProperty, 0},
    {kOpenVPNTokenProperty,
     Property::kEphemeral | Property::kCredential | Property::kWriteOnly},
    {kOpenVPNUserProperty, 0},
    {kProviderHostProperty, 0},
    {kProviderTypeProperty, 0},
    {kOpenVPNCaCertPemProperty, Property::kArray},
    {kOpenVPNExtraCertPemProperty, Property::kArray},
    {kOpenVPNPingExitProperty, 0},
    {kOpenVPNPingProperty, 0},
    {kOpenVPNPingRestartProperty, 0},
    {kOpenVPNTLSAuthProperty, 0},
    {kOpenVPNVerbProperty, 0},
    {kOpenVPNVerifyHashProperty, 0},
    {kOpenVPNVerifyX509NameProperty, 0},
    {kOpenVPNVerifyX509TypeProperty, 0},
    {kVPNMTUProperty, 0},
};

const char OpenVPNDriver::kLSBReleaseFile[] = "/etc/lsb-release";

// Directory where OpenVPN configuration files are exported while the
// process is running.
const char OpenVPNDriver::kDefaultOpenVPNConfigurationDirectory[] =
    RUNDIR "/openvpn_config";

const int OpenVPNDriver::kConnectTimeoutSeconds = 2 * 60;
const int OpenVPNDriver::kReconnectOfflineTimeoutSeconds = 2 * 60;
const int OpenVPNDriver::kReconnectTLSErrorTimeoutSeconds = 20;

OpenVPNDriver::OpenVPNDriver(Manager* manager, ProcessManager* process_manager)
    : VPNDriver(manager, process_manager, kProperties, base::size(kProperties)),
      management_server_(new OpenVPNManagementServer(this)),
      certificate_file_(new CertificateFile()),
      extra_certificates_file_(new CertificateFile()),
      lsb_release_file_(kLSBReleaseFile),
      openvpn_config_directory_(kDefaultOpenVPNConfigurationDirectory),
      pid_(0),
      link_down_(false) {}

OpenVPNDriver::~OpenVPNDriver() {
  IdleService();
}

void OpenVPNDriver::IdleService() {
  Cleanup(Service::kStateIdle, Service::kFailureNone,
          Service::kErrorDetailsNone);
}

void OpenVPNDriver::FailService(Service::ConnectFailure failure,
                                const string& error_details) {
  Cleanup(Service::kStateFailure, failure, error_details);
}

void OpenVPNDriver::Cleanup(Service::ConnectState state,
                            Service::ConnectFailure failure,
                            const string& error_details) {
  SLOG(this, 2) << __func__ << "(" << Service::ConnectStateToString(state)
                << ", " << error_details << ")";
  StopConnectTimeout();
  // Disconnecting the management interface will terminate the openvpn
  // process. Ensure this is handled robustly by first unregistering
  // the callback for OnOpenVPNDied, and then terminating and reaping
  // the process with StopProcess().
  if (pid_) {
    process_manager()->UpdateExitCallback(pid_,
                                          base::Bind(DoNothingWithExitStatus));
  }
  management_server_->Stop();
  if (!tls_auth_file_.empty()) {
    base::DeleteFile(tls_auth_file_, false);
    tls_auth_file_.clear();
  }
  if (!openvpn_config_file_.empty()) {
    base::DeleteFile(openvpn_config_file_, false);
    openvpn_config_file_.clear();
  }
  manager()->RemoveDefaultServiceObserver(this);
  rpc_task_.reset();
  int interface_index = -1;
  if (device_) {
    interface_index = device_->interface_index();
    device_->DropConnection();
    device_->SetEnabled(false);
    device_ = nullptr;
  }
  if (pid_) {
    process_manager()->StopProcessAndBlock(pid_);
    pid_ = 0;
  }
  if (interface_index >= 0) {
    manager()->device_info()->DeleteInterface(interface_index);
  }
  tunnel_interface_.clear();
  if (service()) {
    if (state == Service::kStateFailure) {
      service()->SetErrorDetails(error_details);
      service()->SetFailure(failure);
    } else {
      service()->SetState(state);
    }
    set_service(nullptr);
  }
  ip_properties_ = IPConfig::Properties();
}

// static
string OpenVPNDriver::JoinOptions(const vector<vector<string>>& options,
                                  char separator) {
  vector<string> option_strings;
  for (const auto& option : options) {
    vector<string> quoted_option;
    for (const auto& argument : option) {
      if (argument.find(' ') != string::npos ||
          argument.find('\t') != string::npos ||
          argument.find('"') != string::npos ||
          argument.find(separator) != string::npos) {
        string quoted_argument(argument);
        const char separator_chars[] = {separator, '\0'};
        base::ReplaceChars(argument, separator_chars, " ", &quoted_argument);
        base::ReplaceChars(quoted_argument, "\\", "\\\\", &quoted_argument);
        base::ReplaceChars(quoted_argument, "\"", "\\\"", &quoted_argument);
        quoted_option.push_back("\"" + quoted_argument + "\"");
      } else {
        quoted_option.push_back(argument);
      }
    }
    option_strings.push_back(base::JoinString(quoted_option, " "));
  }
  return base::JoinString(option_strings, string{separator});
}

bool OpenVPNDriver::WriteConfigFile(const vector<vector<string>>& options,
                                    FilePath* config_file) {
  if (!base::DirectoryExists(openvpn_config_directory_)) {
    if (!base::CreateDirectory(openvpn_config_directory_)) {
      LOG(ERROR) << "Unable to create configuration directory  "
                 << openvpn_config_directory_.value();
      return false;
    }
    // OpenVPN running as user 'openvpn' needs access to the config directory,
    // and openvpn user is not member of shill group so make the dir
    // world-readable. We'd rather not have openvpn belong to shill group since
    // shill is more privileged than openvpn, hence the idea of 'dropping'
    // UID/GID from shill to openvpn. Moreover since shill no longer runs with
    // CAP_CHOWN, we can't chown the dir to shill:openvpn.
    if (chmod(openvpn_config_directory_.value().c_str(),
              S_IRWXU | S_IRWXG | S_IROTH)) {
      LOG(ERROR) << "Failed to set permissions on "
                 << openvpn_config_directory_.value();
      base::DeleteFile(openvpn_config_directory_, true);
      return false;
    }
  }

  string contents = JoinOptions(options, '\n');
  contents.push_back('\n');
  if (!base::CreateTemporaryFileInDir(openvpn_config_directory_, config_file) ||
      base::WriteFile(*config_file, contents.data(), contents.size()) !=
          static_cast<int>(contents.size()) ||
      chmod(config_file->value().c_str(), S_IRWXU | S_IRWXG | S_IROTH)) {
    // Make the config file world-readable. Same rationale as listed above for
    // the config directory.
    LOG(ERROR) << "Unable to setup OpenVPN config file.";
    return false;
  }
  return true;
}

bool OpenVPNDriver::SpawnOpenVPN() {
  SLOG(this, 2) << __func__ << "(" << tunnel_interface_ << ")";

  vector<vector<string>> options;
  Error error;
  pid_t openvpn_pid;
  InitOptions(&options, &error);
  if (error.IsFailure()) {
    return false;
  }
  LOG(INFO) << "OpenVPN process options: " << JoinOptions(options, ',');
  if (!WriteConfigFile(options, &openvpn_config_file_)) {
    return false;
  }

  // TODO(quiche): This should be migrated to use ExternalTask.
  // (crbug.com/246263).
  CHECK(!pid_);

  vector<string> args = GetCommandLineArgs();
  LOG(INFO) << "OpenVPN command line args: " << base::JoinString(args, " ");

  // OpenSSL compatibility settings.
  // TODO(crbug.com/1047146): Drop these stop-gaps after addressing the
  // underlying problems described in the bug.
  const map<string, string> kEnv = {
      {"OPENSSL_CONF", "/etc/ssl/openssl.cnf.compat"},
      {"OPENSSL_CHROMIUM_SKIP_TRUSTED_PURPOSE_CHECK", "1"},
      {"OPENSSL_CHROMIUM_GENERATE_METRICS", "1"},
  };

  uint64_t capmask = CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_RAW) |
                     CAP_TO_MASK(CAP_SETUID) | CAP_TO_MASK(CAP_SETGID);
  openvpn_pid = process_manager()->StartProcessInMinijail(
      FROM_HERE, base::FilePath(kOpenVPNPath), args, kEnv, "shill", "shill",
      capmask, true, true,
      base::Bind(&OpenVPNDriver::OnOpenVPNDied, base::Unretained(this)));
  if (openvpn_pid == -1) {
    LOG(ERROR) << "Minijail couldn't run our child process";
    return false;
  }

  pid_ = openvpn_pid;
  return true;
}

void OpenVPNDriver::OnOpenVPNDied(int exit_status) {
  SLOG(nullptr, 2) << __func__ << "(" << pid_ << ", " << exit_status << ")";
  pid_ = 0;
  FailService(Service::kFailureInternal, Service::kErrorDetailsNone);
  // TODO(petkov): Figure if we need to restart the connection.
}

void OpenVPNDriver::ClaimInterface(const string& link_name,
                                   int interface_index) {
  SLOG(this, 2) << "Claiming " << link_name << " for OpenVPN tunnel";
  tunnel_interface_ = link_name;
  CHECK(!device_);
  device_ = new VirtualDevice(manager(), link_name, interface_index,
                              Technology::kVPN);
  device_->SetEnabled(true);

  rpc_task_.reset(new RpcTask(control_interface(), this));
  if (SpawnOpenVPN()) {
    manager()->AddDefaultServiceObserver(this);
  } else {
    FailService(Service::kFailureInternal, Service::kErrorDetailsNone);
  }
}

void OpenVPNDriver::GetLogin(string* /*user*/, string* /*password*/) {
  NOTREACHED();
}

void OpenVPNDriver::Notify(const string& reason,
                           const map<string, string>& dict) {
  LOG(INFO) << "IP configuration received: " << reason;
  if (reason != "up") {
    device_->DropConnection();
    return;
  }
  // On restart/reconnect, update the existing IP configuration.
  ParseIPConfiguration(dict, &ip_properties_);
  device_->SelectService(service());
  device_->UpdateIPConfig(ip_properties_);
  ReportConnectionMetrics();
  StopConnectTimeout();
}

void OpenVPNDriver::ParseIPConfiguration(
    const map<string, string>& configuration,
    IPConfig::Properties* properties) const {
  ForeignOptions foreign_options;
  RouteOptions routes;
  bool redirect_gateway = false;

  properties->address_family = IPAddress::kFamilyIPv4;
  if (!properties->subnet_prefix) {
    properties->subnet_prefix =
        IPAddress::GetMaxPrefixLength(properties->address_family);
  }
  for (const auto& configuration_map : configuration) {
    const string& key = configuration_map.first;
    const string& value = configuration_map.second;
    SLOG(this, 2) << "Processing: " << key << " -> " << value;
    if (base::LowerCaseEqualsASCII(key, kOpenVPNIfconfigLocal)) {
      properties->address = value;
    } else if (base::LowerCaseEqualsASCII(key, kOpenVPNIfconfigBroadcast)) {
      properties->broadcast_address = value;
    } else if (base::LowerCaseEqualsASCII(key, kOpenVPNIfconfigNetmask)) {
      properties->subnet_prefix =
          IPAddress::GetPrefixLengthFromMask(properties->address_family, value);
    } else if (base::LowerCaseEqualsASCII(key, kOpenVPNIfconfigRemote)) {
      if (base::StartsWith(value, kSuspectedNetmaskPrefix,
                           base::CompareCase::INSENSITIVE_ASCII)) {
        LOG(WARNING) << "Option " << key << " value " << value
                     << " looks more like a netmask than a peer address; "
                     << "assuming it is the former.";
        // In this situation, the "peer_address" value will be left
        // unset and Connection::UpdateFromIPConfig() will treat the
        // interface as if it were a broadcast-style network.  The
        // kernel will, automatically set the peer address equal to
        // the local address.
        properties->subnet_prefix = IPAddress::GetPrefixLengthFromMask(
            properties->address_family, value);
      } else {
        // This creates an explicit route to the peer address in SetRoutes().
        properties->peer_address = value;
      }
    } else if (base::LowerCaseEqualsASCII(key, kOpenVPNRedirectGateway)) {
      redirect_gateway = true;
    } else if (base::LowerCaseEqualsASCII(key, kOpenVPNTrustedIP)) {
      size_t prefix = IPAddress::GetMaxPrefixLength(properties->address_family);
      properties->exclusion_list.push_back(value + "/" +
                                           base::NumberToString(prefix));
    } else if (base::LowerCaseEqualsASCII(key, kOpenVPNTunMTU)) {
      int mtu = 0;
      if (base::StringToInt(value, &mtu) && mtu >= IPConfig::kMinIPv4MTU) {
        properties->mtu = mtu;
      } else {
        LOG(ERROR) << "MTU " << value << " ignored.";
      }
    } else if (base::StartsWith(key, kOpenVPNForeignOptionPrefix,
                                base::CompareCase::INSENSITIVE_ASCII)) {
      const string suffix = key.substr(strlen(kOpenVPNForeignOptionPrefix));
      int order = 0;
      if (base::StringToInt(suffix, &order)) {
        foreign_options[order] = value;
      } else {
        LOG(ERROR) << "Ignored unexpected foreign option suffix: " << suffix;
      }
    } else if (base::LowerCaseEqualsASCII(key, kOpenVPNRouteNetGateway) ||
               base::LowerCaseEqualsASCII(key, kOpenVPNRouteVPNGateway)) {
      // These options are unused.  Catch them here so that they don't
      // get passed to ParseRouteOption().
    } else if (base::StartsWith(key, kOpenVPNRouteOptionPrefix,
                                base::CompareCase::INSENSITIVE_ASCII)) {
      ParseRouteOption(key.substr(strlen(kOpenVPNRouteOptionPrefix)), value,
                       &routes);
    } else {
      SLOG(this, 2) << "Key ignored.";
    }
  }
  ParseForeignOptions(foreign_options, properties);

  manager()->vpn_provider()->SetDefaultRoutingPolicy(properties);

  // Since we use persist-tun, we expect that a reconnection will use the same
  // routes *and* that OpenVPN will not re-provide us with all the needed
  // routing information. Simply re-use the routing information we attained from
  // the initial connection.
  if (!properties->routes.empty())
    return;

  // Ignore the route_vpn_gateway parameter as VPNs don't need gateway IPs.
  // This guarantees that we will pass the various sanity checks in
  // connection.cc.
  properties->gateway = properties->address;

  if (redirect_gateway &&
      const_args()->Contains<string>(kOpenVPNIgnoreDefaultRouteProperty)) {
    LOG(INFO) << "Ignoring default route parameter as requested by "
              << "configuration.";
    redirect_gateway = false;
  }
  properties->default_route = properties->blackhole_ipv6 = redirect_gateway;
  SetRoutes(routes, properties);
}

// static
void OpenVPNDriver::ParseForeignOptions(const ForeignOptions& options,
                                        IPConfig::Properties* properties) {
  vector<string> domain_search;
  vector<string> dns_servers;
  for (const auto& option_map : options) {
    ParseForeignOption(option_map.second, &domain_search, &dns_servers);
  }
  if (!domain_search.empty()) {
    properties->domain_search.swap(domain_search);
  }
  LOG_IF(INFO, properties->domain_search.empty())
      << "No search domains provided.";
  if (!dns_servers.empty()) {
    properties->dns_servers.swap(dns_servers);
  }
  LOG_IF(WARNING, properties->dns_servers.empty())
      << "No DNS servers provided.";
}

// static
void OpenVPNDriver::ParseForeignOption(const string& option,
                                       vector<string>* domain_search,
                                       vector<string>* dns_servers) {
  SLOG(nullptr, 2) << __func__ << "(" << option << ")";
  vector<string> tokens =
      SplitString(option, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (tokens.size() != 3 ||
      !base::LowerCaseEqualsASCII(tokens[0], "dhcp-option")) {
    return;
  }
  if (base::LowerCaseEqualsASCII(tokens[1], "domain")) {
    domain_search->push_back(tokens[2]);
  } else if (base::LowerCaseEqualsASCII(tokens[1], "dns")) {
    dns_servers->push_back(tokens[2]);
  }
}

// static
IPConfig::Route* OpenVPNDriver::GetRouteOptionEntry(const string& prefix,
                                                    const string& key,
                                                    RouteOptions* routes) {
  int order = 0;
  if (!base::StartsWith(key, prefix, base::CompareCase::INSENSITIVE_ASCII) ||
      !base::StringToInt(key.substr(prefix.size()), &order)) {
    return nullptr;
  }
  return &(*routes)[order];
}

// static
void OpenVPNDriver::ParseRouteOption(const string& key,
                                     const string& value,
                                     RouteOptions* routes) {
  // IPv4 uses route_{network,netmask,gateway}_<index>
  // IPv6 uses route_ipv6_{network,gateway}_<index>
  IPConfig::Route* route =
      GetRouteOptionEntry(kOpenVPNRouteNetworkPrefix, key, routes);
  if (route) {
    route->host = value;
    return;
  }
  route = GetRouteOptionEntry(kOpenVPNRouteNetmaskPrefix, key, routes);
  if (route) {
    route->prefix =
        IPAddress::GetPrefixLengthFromMask(IPAddress::kFamilyIPv4, value);
    return;
  }
  route = GetRouteOptionEntry(kOpenVPNRouteGatewayPrefix, key, routes);
  if (route) {
    route->gateway = value;
    return;
  }
  LOG(WARNING) << "Unknown route option ignored: " << key;
}

// static
void OpenVPNDriver::SetRoutes(const RouteOptions& routes,
                              IPConfig::Properties* properties) {
  vector<IPConfig::Route> new_routes;
  int32_t max_prefix =
      IPAddress::GetMaxPrefixLength(properties->address_family);
  if (!properties->peer_address.empty()) {
    // --topology net30 or p2p will set ifconfig_remote

    // Setting a point-to-point address in the kernel will create a route
    // in RT_TABLE_MAIN instead of our per-device table.  To avoid this,
    // create an explicit host route here, and clear
    // |properties->peer_address.|
    IPConfig::Route route(properties->peer_address, max_prefix,
                          properties->address);
    new_routes.push_back(route);
    properties->peer_address.clear();
  } else if (properties->subnet_prefix != max_prefix) {
    // --topology subnet will set ifconfig_netmask instead
    IPAddress network_addr(properties->address);
    if (network_addr.family() != properties->address_family) {
      LOG(WARNING) << "Error obtaining network address for "
                   << properties->address;
    } else {
      network_addr.set_prefix(properties->subnet_prefix);

      IPConfig::Route route(network_addr.GetNetworkPart().ToString(),
                            properties->subnet_prefix, properties->address);
      new_routes.push_back(route);
    }
  }

  // Ignore |route.gateway|.  If it's wrong, it can cause the kernel to
  // refuse to add the route.  If it's correct, it has no effect anyway.
  for (const auto& route_map : routes) {
    const IPConfig::Route& route = route_map.second;
    if (route.host.empty() || route.gateway.empty()) {
      LOG(WARNING) << "Ignoring incomplete route: " << route_map.first;
      continue;
    }
    IPConfig::Route new_route(route.host, route.prefix, properties->address);
    new_routes.push_back(new_route);
  }

  if (!new_routes.empty()) {
    properties->routes.swap(new_routes);
  } else if (!properties->default_route) {
    LOG(WARNING) << "No routes provided.";
  }
}

// static
bool OpenVPNDriver::SplitPortFromHost(const string& host,
                                      string* name,
                                      string* port) {
  vector<string> tokens =
      SplitString(host, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  int port_number = 0;
  if (tokens.size() != 2 || tokens[0].empty() || tokens[1].empty() ||
      !base::IsAsciiDigit(tokens[1][0]) ||
      !base::StringToInt(tokens[1], &port_number) ||
      port_number > std::numeric_limits<uint16_t>::max()) {
    return false;
  }
  *name = tokens[0];
  *port = tokens[1];
  return true;
}

void OpenVPNDriver::Connect(const VPNServiceRefPtr& service, Error* error) {
  StartConnectTimeout(kConnectTimeoutSeconds);
  set_service(service);
  service->SetState(Service::kStateConfiguring);
  if (!manager()->device_info()->CreateTunnelInterface(base::BindOnce(
          &OpenVPNDriver::ClaimInterface, weak_factory_.GetWeakPtr()))) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInternalError,
                          "Could not create tunnel interface.");
    FailService(Service::kFailureInternal, Service::kErrorDetailsNone);
  }
  // Wait for the ClaimInterface callback to continue the connection process.
}

void OpenVPNDriver::InitOptions(vector<vector<string>>* options, Error* error) {
  string vpnhost = args()->Lookup<string>(kProviderHostProperty, "");
  if (vpnhost.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "VPN host not specified.");
    return;
  }
  AppendOption("client", options);
  AppendOption("tls-client", options);

  AppendRemoteOption(vpnhost, options);
  if (args()->Contains<vector<string>>(kOpenVPNExtraHostsProperty)) {
    for (const auto& host :
         args()->Get<vector<string>>(kOpenVPNExtraHostsProperty)) {
      AppendRemoteOption(host, options);
    }
  }
  AppendOption("mark", "1280", options);  // 0x500: source type = 5 (Native VPN)
  AppendOption("nobind", options);
  AppendOption("persist-key", options);
  AppendOption("persist-tun", options);

  CHECK(!tunnel_interface_.empty());
  AppendOption("dev", tunnel_interface_, options);
  AppendOption("dev-type", "tun", options);

  InitLoggingOptions(options);

  AppendValueOption(kVPNMTUProperty, "mtu", options);
  AppendValueOption(kOpenVPNProtoProperty, "proto", options);
  AppendValueOption(kOpenVPNPortProperty, "port", options);
  AppendValueOption(kOpenVPNTLSAuthProperty, "tls-auth", options);
  {
    string contents =
        args()->Lookup<string>(kOpenVPNTLSAuthContentsProperty, "");
    if (!contents.empty()) {
      if (!base::CreateTemporaryFile(&tls_auth_file_) ||
          base::WriteFile(tls_auth_file_, contents.data(), contents.size()) !=
              static_cast<int>(contents.size())) {
        Error::PopulateAndLog(FROM_HERE, error, Error::kInternalError,
                              "Unable to setup tls-auth file.");
        return;
      }
      AppendOption("tls-auth", tls_auth_file_.value(), options);
    }
  }

  if (args()->Contains<string>(kOpenVPNTLSVersionMinProperty)) {
    AppendOption("tls-version-min",
                 args()->Get<string>(kOpenVPNTLSVersionMinProperty), options);
  }

  string tls_remote = args()->Lookup<string>(kOpenVPNTLSRemoteProperty, "");
  if (!tls_remote.empty()) {
    AppendOption("verify-x509-name", tls_remote, "name-prefix", options);
  }

  AppendValueOption(kOpenVPNCipherProperty, "cipher", options);
  AppendValueOption(kOpenVPNAuthProperty, "auth", options);
  AppendFlag(kOpenVPNAuthNoCacheProperty, "auth-nocache", options);
  AppendValueOption(kOpenVPNAuthRetryProperty, "auth-retry", options);
  AppendFlag(kOpenVPNCompLZOProperty, "comp-lzo", options);
  AppendFlag(kOpenVPNCompNoAdaptProperty, "comp-noadapt", options);
  AppendValueOption(kOpenVPNCompressProperty, "compress", options);
  AppendFlag(kOpenVPNPushPeerInfoProperty, "push-peer-info", options);
  AppendValueOption(kOpenVPNRenegSecProperty, "reneg-sec", options);
  AppendValueOption(kOpenVPNShaperProperty, "shaper", options);
  AppendValueOption(kOpenVPNServerPollTimeoutProperty, "server-poll-timeout",
                    options);

  if (!InitCAOptions(options, error)) {
    return;
  }

  // Additional remote certificate verification options.
  InitCertificateVerifyOptions(options);
  if (!InitExtraCertOptions(options, error)) {
    return;
  }

  // Client-side ping support.
  AppendValueOption(kOpenVPNPingProperty, "ping", options);
  AppendValueOption(kOpenVPNPingExitProperty, "ping-exit", options);
  AppendValueOption(kOpenVPNPingRestartProperty, "ping-restart", options);

  AppendValueOption(kOpenVPNNsCertTypeProperty, "ns-cert-type", options);

  InitClientAuthOptions(options);
  InitPKCS11Options(options);

  // TLS support.
  string remote_cert_tls =
      args()->Lookup<string>(kOpenVPNRemoteCertTLSProperty, "");
  if (remote_cert_tls.empty()) {
    remote_cert_tls = "server";
  }
  if (remote_cert_tls != "none") {
    AppendOption("remote-cert-tls", remote_cert_tls, options);
  }

  AppendValueOption(kOpenVPNKeyDirectionProperty, "key-direction", options);
  AppendValueOption(kOpenVPNRemoteCertEKUProperty, "remote-cert-eku", options);
  AppendDelimitedValueOption(kOpenVPNRemoteCertKUProperty, "remote-cert-ku",
                             ' ', options);

  if (!InitManagementChannelOptions(options, error)) {
    return;
  }

  // Setup openvpn-script options and RPC information required to send back
  // Layer 3 configuration.
  AppendOption("setenv", kRpcTaskServiceVariable,
               rpc_task_->GetRpcConnectionIdentifier().value(), options);
  AppendOption("setenv", kRpcTaskPathVariable,
               rpc_task_->GetRpcIdentifier().value(), options);
  AppendOption("script-security", "2", options);
  AppendOption("up", kOpenVPNScript, options);
  AppendOption("up-restart", options);

  // Disable openvpn handling since we do route+ifconfig work.
  AppendOption("route-noexec", options);
  AppendOption("ifconfig-noexec", options);

  // Drop root privileges on connection and enable callback scripts to send
  // notify messages.
  AppendOption("user", "openvpn", options);
  AppendOption("group", "openvpn", options);
}

bool OpenVPNDriver::InitCAOptions(vector<vector<string>>* options,
                                  Error* error) {
  vector<string> ca_cert_pem;
  if (args()->Contains<vector<string>>(kOpenVPNCaCertPemProperty)) {
    ca_cert_pem = args()->Get<vector<string>>(kOpenVPNCaCertPemProperty);
  }
  if (ca_cert_pem.empty()) {
    // Use default CAs if no CA certificate is provided.
    AppendOption("ca", kDefaultCACertificates, options);
    return true;
  }

  FilePath certfile = certificate_file_->CreatePEMFromStrings(ca_cert_pem);
  if (certfile.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Unable to extract PEM CA certificates.");
    return false;
  }
  AppendOption("ca", certfile.value(), options);
  return true;
}

void OpenVPNDriver::InitCertificateVerifyOptions(
    std::vector<std::vector<std::string>>* options) {
  AppendValueOption(kOpenVPNVerifyHashProperty, "verify-hash", options);
  string x509_name = args()->Lookup<string>(kOpenVPNVerifyX509NameProperty, "");
  if (!x509_name.empty()) {
    string x509_type =
        args()->Lookup<string>(kOpenVPNVerifyX509TypeProperty, "");
    if (x509_type.empty()) {
      AppendOption("verify-x509-name", x509_name, options);
    } else {
      AppendOption("verify-x509-name", x509_name, x509_type, options);
    }
  }
}

bool OpenVPNDriver::InitExtraCertOptions(vector<vector<string>>* options,
                                         Error* error) {
  if (!args()->Contains<vector<string>>(kOpenVPNExtraCertPemProperty)) {
    // It's okay for this parameter to be unspecified.
    return true;
  }

  vector<string> extra_certs =
      args()->Get<vector<string>>(kOpenVPNExtraCertPemProperty);
  if (extra_certs.empty()) {
    // It's okay for this parameter to be empty.
    return true;
  }

  FilePath certfile =
      extra_certificates_file_->CreatePEMFromStrings(extra_certs);
  if (certfile.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Unable to extract extra PEM CA certificates.");
    return false;
  }

  AppendOption("extra-certs", certfile.value(), options);
  return true;
}

void OpenVPNDriver::InitPKCS11Options(vector<vector<string>>* options) {
  string id = args()->Lookup<string>(kOpenVPNClientCertIdProperty, "");
  if (!id.empty()) {
    AppendOption("pkcs11-providers", kDefaultPKCS11Provider, options);
    AppendOption("pkcs11-id", id, options);
  }
}

void OpenVPNDriver::InitClientAuthOptions(vector<vector<string>>* options) {
  // If the AuthUserPass property is set, or the User property is non-empty, or
  // a client cert was not provided, specify user-password client
  // authentication.
  if (args()->Contains<string>(kOpenVPNAuthUserPassProperty) ||
      !args()->Lookup<string>(kOpenVPNUserProperty, "").empty() ||
      args()->Lookup<string>(kOpenVPNClientCertIdProperty, "").empty()) {
    AppendOption("auth-user-pass", options);
  }
}

bool OpenVPNDriver::InitManagementChannelOptions(
    vector<vector<string>>* options, Error* error) {
  if (!management_server_->Start(&sockets_, options)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInternalError,
                          "Unable to setup management channel.");
    return false;
  }
  // If there's a connected default service already, allow the openvpn client to
  // establish connection as soon as it's started. Otherwise, hold the client
  // until an underlying service connects and OnDefaultServiceChanged is
  // invoked.
  if (manager()->IsConnected()) {
    management_server_->ReleaseHold();
  }
  return true;
}

void OpenVPNDriver::InitLoggingOptions(vector<vector<string>>* options) {
  AppendOption("syslog", options);

  string verb = args()->Lookup<string>(kOpenVPNVerbProperty, "");
  if (!verb.empty()) {
    AppendOption("verb", verb, options);
    return;
  }

  if (SLOG_IS_ON(VPN, 6)) {
    // Maximum output:
    // --verb 9 enables PKCS11 debug, TCP stream, link read/write
    // --verb 8 enables event waits, scheduler, tls_session
    AppendOption("verb", "9", options);
  } else if (SLOG_IS_ON(VPN, 5)) {
    // --verb 7 enables data channel encryption keys, routing,
    // pkcs11 actions, pings, push/pull debug
    AppendOption("verb", "7", options);
  } else if (SLOG_IS_ON(VPN, 4)) {
    // --verb 6 enables tcp/udp reads/writes (short), tun/tap reads/writes
    // --verb 5 enables printing 'R' or 'W' per packet to stdout
    AppendOption("verb", "6", options);
  } else if (SLOG_IS_ON(VPN, 3)) {
    // --verb 4 enables logging packet drops, options
    AppendOption("verb", "4", options);
  } else if (SLOG_IS_ON(VPN, 0)) {
    // --verb 3 is the old default for `ff_debug +vpn`
    AppendOption("verb", "3", options);
  }
}

void OpenVPNDriver::AppendOption(const string& option,
                                 vector<vector<string>>* options) {
  options->push_back(vector<string>{option});
}

void OpenVPNDriver::AppendOption(const string& option,
                                 const string& value,
                                 vector<vector<string>>* options) {
  options->push_back(vector<string>{option, value});
}

void OpenVPNDriver::AppendOption(const string& option,
                                 const string& value0,
                                 const string& value1,
                                 vector<vector<string>>* options) {
  options->push_back(vector<string>{option, value0, value1});
}

void OpenVPNDriver::AppendRemoteOption(const string& host,
                                       vector<vector<string>>* options) {
  string host_name, host_port;
  if (SplitPortFromHost(host, &host_name, &host_port)) {
    DCHECK(!host_name.empty());
    DCHECK(!host_port.empty());
    AppendOption("remote", host_name, host_port, options);
  } else {
    AppendOption("remote", host, options);
  }
}

bool OpenVPNDriver::AppendValueOption(const string& property,
                                      const string& option,
                                      vector<vector<string>>* options) {
  string value = args()->Lookup<string>(property, "");
  if (!value.empty()) {
    AppendOption(option, value, options);
    return true;
  }
  return false;
}

bool OpenVPNDriver::AppendDelimitedValueOption(
    const string& property,
    const string& option,
    char delimiter,
    vector<vector<string>>* options) {
  string value = args()->Lookup<string>(property, "");
  if (!value.empty()) {
    vector<string> parts =
        SplitString(value, std::string{delimiter}, base::TRIM_WHITESPACE,
                    base::SPLIT_WANT_ALL);
    parts.insert(parts.begin(), option);
    options->push_back(parts);
    return true;
  }
  return false;
}

bool OpenVPNDriver::AppendFlag(const string& property,
                               const string& option,
                               vector<vector<string>>* options) {
  if (args()->Contains<string>(property)) {
    AppendOption(option, options);
    return true;
  }
  return false;
}

const RpcIdentifier& OpenVPNDriver::GetServiceRpcIdentifier() const {
  static RpcIdentifier null_identifier("(openvpn_driver)");
  if (service() == nullptr)
    return null_identifier;
  return service()->GetRpcIdentifier();
}

void OpenVPNDriver::Disconnect() {
  SLOG(this, 2) << __func__;
  IdleService();
}

void OpenVPNDriver::OnConnectTimeout() {
  VPNDriver::OnConnectTimeout();
  Service::ConnectFailure failure =
      management_server_->state() == OpenVPNManagementServer::kStateResolve
          ? Service::kFailureDNSLookup
          : Service::kFailureConnect;
  FailService(failure, Service::kErrorDetailsNone);
}

void OpenVPNDriver::OnReconnecting(ReconnectReason reason) {
  LOG(INFO) << __func__ << "(" << reason << ")";
  int timeout_seconds = GetReconnectTimeoutSeconds(reason);
  if (reason == kReconnectReasonTLSError &&
      timeout_seconds < connect_timeout_seconds()) {
    // Reconnect due to TLS error happens during connect so we need to cancel
    // the original connect timeout first and then reduce the time limit.
    StopConnectTimeout();
  }
  StartConnectTimeout(timeout_seconds);
  // On restart/reconnect, drop the VPN connection, if any. The openvpn client
  // might be in hold state if the VPN connection was previously established
  // successfully. The hold will be released by OnDefaultServiceChanged when a
  // new default service connects. This ensures that the client will use a fully
  // functional underlying connection to reconnect.
  if (device_) {
    device_->SetServiceState(Service::kStateConfiguring);
    device_->ResetConnection();
  }
}

// static
int OpenVPNDriver::GetReconnectTimeoutSeconds(ReconnectReason reason) {
  switch (reason) {
    case kReconnectReasonOffline:
      return kReconnectOfflineTimeoutSeconds;
    case kReconnectReasonTLSError:
      return kReconnectTLSErrorTimeoutSeconds;
    default:
      break;
  }
  return kConnectTimeoutSeconds;
}

string OpenVPNDriver::GetProviderType() const {
  return kProviderOpenVpn;
}

VPNDriver::IfType OpenVPNDriver::GetIfType() const {
  return kDriverManaged;
}

KeyValueStore OpenVPNDriver::GetProvider(Error* error) {
  SLOG(this, 2) << __func__;
  KeyValueStore props = VPNDriver::GetProvider(error);
  props.Set<bool>(
      kPassphraseRequiredProperty,
      args()->Lookup<string>(kOpenVPNPasswordProperty, "").empty() &&
          args()->Lookup<string>(kOpenVPNTokenProperty, "").empty());
  return props;
}

vector<string> OpenVPNDriver::GetCommandLineArgs() {
  SLOG(this, 2) << __func__ << "(" << lsb_release_file_.value() << ")";
  vector<string> args =
      vector<string>{"--config", openvpn_config_file_.value()};
  string contents;
  if (!base::ReadFileToString(lsb_release_file_, &contents)) {
    LOG(ERROR) << "Unable to read the lsb-release file: "
               << lsb_release_file_.value();
    return args;
  }
  vector<string> lines =
      SplitString(contents, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  for (const auto& line : lines) {
    const size_t assign = line.find('=');
    if (assign == string::npos) {
      continue;
    }
    const string key = line.substr(0, assign);
    const string value = line.substr(assign + 1);
    if (key == kChromeOSReleaseName) {
      args.push_back("--setenv");
      args.push_back("UV_PLAT");
      args.push_back(value);
    } else if (key == kChromeOSReleaseVersion) {
      args.push_back("--setenv");
      args.push_back("UV_PLAT_REL");
      args.push_back(value);
    }
    // Other LSB release values are irrelevant.
  }
  return args;
}

void OpenVPNDriver::OnDefaultServiceChanged(
    const ServiceRefPtr& /*logical_service*/,
    bool /*logical_service_changed*/,
    const ServiceRefPtr& physical_service,
    bool physical_service_changed) {
  if (!physical_service_changed)
    return;

  SLOG(this, 2) << __func__ << "("
                << (physical_service ? physical_service->log_name() : "-")
                << ")";

  if (!device_)
    return;

  // Inform the user that the VPN is reconnecting.
  device_->SetServiceState(Service::kStateConfiguring);
  device_->ResetConnection();
  StopConnectTimeout();

  if (physical_service && physical_service->state() == Service::kStateOnline) {
    // The original service is no longer the default, but manager was able
    // to find another physical service that is already Online.
    // Ask the management server to reconnect immediately.
    management_server_->ReleaseHold();
    management_server_->Restart();
    StartConnectTimeout(GetReconnectTimeoutSeconds(kReconnectReasonOffline));
  } else {
    // The default physical service went away, and nothing else is available
    // right now.  All we can do is wait.
    if (link_down_)
      return;
    SLOG(this, 2) << __func__ << " - physical connection lost";
    link_down_ = true;

    management_server_->Hold();
    management_server_->Restart();
  }
}

void OpenVPNDriver::OnDefaultServiceStateChanged(const ServiceRefPtr& service) {
  if (link_down_ && service->state() == Service::kStateOnline) {
    link_down_ = false;
    management_server_->ReleaseHold();
    StartConnectTimeout(GetReconnectTimeoutSeconds(kReconnectReasonOffline));
  }
}

void OpenVPNDriver::ReportConnectionMetrics() {
  metrics()->SendEnumToUMA(Metrics::kMetricVpnDriver,
                           Metrics::kVpnDriverOpenVpn,
                           Metrics::kMetricVpnDriverMax);

  if (args()->Contains<vector<string>>(kOpenVPNCaCertPemProperty) &&
      !args()->Get<vector<string>>(kOpenVPNCaCertPemProperty).empty()) {
    metrics()->SendEnumToUMA(
        Metrics::kMetricVpnRemoteAuthenticationType,
        Metrics::kVpnRemoteAuthenticationTypeOpenVpnCertificate,
        Metrics::kMetricVpnRemoteAuthenticationTypeMax);
  } else {
    metrics()->SendEnumToUMA(
        Metrics::kMetricVpnRemoteAuthenticationType,
        Metrics::kVpnRemoteAuthenticationTypeOpenVpnDefault,
        Metrics::kMetricVpnRemoteAuthenticationTypeMax);
  }

  bool has_user_authentication = false;
  if (args()->Lookup<string>(kOpenVPNTokenProperty, "") != "") {
    metrics()->SendEnumToUMA(
        Metrics::kMetricVpnUserAuthenticationType,
        Metrics::kVpnUserAuthenticationTypeOpenVpnUsernameToken,
        Metrics::kMetricVpnUserAuthenticationTypeMax);
    has_user_authentication = true;
  }
  if (args()->Lookup<string>(kOpenVPNOTPProperty, "") != "") {
    metrics()->SendEnumToUMA(
        Metrics::kMetricVpnUserAuthenticationType,
        Metrics::kVpnUserAuthenticationTypeOpenVpnUsernamePasswordOtp,
        Metrics::kMetricVpnUserAuthenticationTypeMax);
    has_user_authentication = true;
  }
  if (args()->Lookup<string>(kOpenVPNAuthUserPassProperty, "") != "" ||
      args()->Lookup<string>(kOpenVPNUserProperty, "") != "") {
    metrics()->SendEnumToUMA(
        Metrics::kMetricVpnUserAuthenticationType,
        Metrics::kVpnUserAuthenticationTypeOpenVpnUsernamePassword,
        Metrics::kMetricVpnUserAuthenticationTypeMax);
    has_user_authentication = true;
  }
  if (args()->Lookup<string>(kOpenVPNClientCertIdProperty, "") != "") {
    metrics()->SendEnumToUMA(
        Metrics::kMetricVpnUserAuthenticationType,
        Metrics::kVpnUserAuthenticationTypeOpenVpnCertificate,
        Metrics::kMetricVpnUserAuthenticationTypeMax);
    has_user_authentication = true;
  }
  if (!has_user_authentication) {
    metrics()->SendEnumToUMA(Metrics::kMetricVpnUserAuthenticationType,
                             Metrics::kVpnUserAuthenticationTypeOpenVpnNone,
                             Metrics::kMetricVpnUserAuthenticationTypeMax);
  }
}

}  // namespace shill
