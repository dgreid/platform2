// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/manager.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <utility>

#include "base/files/scoped_file.h"
#include <base/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/key_value_store.h>
#include <brillo/minijail/minijail.h>

#include "patchpanel/ipc.pb.h"
#include "patchpanel/mac_address_generator.h"
#include "patchpanel/net_util.h"
#include "patchpanel/routing_service.h"
#include "patchpanel/scoped_ns.h"

namespace patchpanel {
namespace {
constexpr int kSubprocessRestartDelayMs = 900;

constexpr char kNDProxyFeatureName[] = "ARC NDProxy";
constexpr int kNDProxyMinAndroidSdkVersion = 28;  // P
constexpr int kNDProxyMinChromeMilestone = 80;

// Time interval between epoll checks on file descriptors committed by callers
// of ConnectNamespace DBus API.
constexpr const base::TimeDelta kConnectNamespaceCheckInterval =
    base::TimeDelta::FromSeconds(5);

// Passes |method_call| to |handler| and passes the response to
// |response_sender|. If |handler| returns nullptr, an empty response is
// created and sent.
void HandleSynchronousDBusMethodCall(
    base::Callback<std::unique_ptr<dbus::Response>(dbus::MethodCall*)> handler,
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  std::unique_ptr<dbus::Response> response = handler.Run(method_call);
  if (!response)
    response = dbus::Response::FromMethodCall(method_call);
  response_sender.Run(std::move(response));
}

}  // namespace

Manager::Manager(std::unique_ptr<HelperProcess> adb_proxy,
                 std::unique_ptr<HelperProcess> mcast_proxy,
                 std::unique_ptr<HelperProcess> nd_proxy)
    : adb_proxy_(std::move(adb_proxy)),
      mcast_proxy_(std::move(mcast_proxy)),
      nd_proxy_(std::move(nd_proxy)) {
  runner_ = std::make_unique<MinijailedProcessRunner>();
  datapath_ = std::make_unique<Datapath>(runner_.get());
  connected_namespaces_epollfd_ = epoll_create(1 /* size */);
}

Manager::~Manager() {
  OnShutdown(nullptr);
}

std::map<const std::string, bool> Manager::cached_feature_enabled_ = {};

bool Manager::ShouldEnableFeature(
    int min_android_sdk_version,
    int min_chrome_milestone,
    const std::vector<std::string>& supported_boards,
    const std::string& feature_name) {
  static const char kLsbReleasePath[] = "/etc/lsb-release";

  const auto& cached_result = cached_feature_enabled_.find(feature_name);
  if (cached_result != cached_feature_enabled_.end())
    return cached_result->second;

  auto check = [min_android_sdk_version, min_chrome_milestone,
                &supported_boards, &feature_name]() {
    brillo::KeyValueStore store;
    if (!store.Load(base::FilePath(kLsbReleasePath))) {
      LOG(ERROR) << "Could not read lsb-release";
      return false;
    }

    std::string value;
    if (!store.GetString("CHROMEOS_ARC_ANDROID_SDK_VERSION", &value)) {
      LOG(ERROR) << feature_name
                 << " disabled - cannot determine Android SDK version";
      return false;
    }
    int ver = 0;
    if (!base::StringToInt(value.c_str(), &ver)) {
      LOG(ERROR) << feature_name << " disabled - invalid Android SDK version";
      return false;
    }
    if (ver < min_android_sdk_version) {
      LOG(INFO) << feature_name << " disabled for Android SDK " << value;
      return false;
    }

    if (!store.GetString("CHROMEOS_RELEASE_CHROME_MILESTONE", &value)) {
      LOG(ERROR) << feature_name
                 << " disabled - cannot determine ChromeOS milestone";
      return false;
    }
    if (!base::StringToInt(value.c_str(), &ver)) {
      LOG(ERROR) << feature_name << " disabled - invalid ChromeOS milestone";
      return false;
    }
    if (ver < min_chrome_milestone) {
      LOG(INFO) << feature_name << " disabled for ChromeOS milestone " << value;
      return false;
    }

    if (!store.GetString("CHROMEOS_RELEASE_BOARD", &value)) {
      LOG(ERROR) << feature_name << " disabled - cannot determine board";
      return false;
    }
    if (!supported_boards.empty() &&
        std::find(supported_boards.begin(), supported_boards.end(), value) ==
            supported_boards.end()) {
      LOG(INFO) << feature_name << " disabled for board " << value;
      return false;
    }
    return true;
  };

  bool result = check();
  cached_feature_enabled_.emplace(feature_name, result);
  return result;
}

int Manager::OnInit() {
  prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);

  // Handle subprocess lifecycle.
  process_reaper_.Register(this);

  CHECK(process_reaper_.WatchForChild(
      FROM_HERE, adb_proxy_->pid(),
      base::Bind(&Manager::OnSubprocessExited, weak_factory_.GetWeakPtr(),
                 adb_proxy_->pid())))
      << "Failed to watch adb-proxy child process";
  CHECK(process_reaper_.WatchForChild(
      FROM_HERE, mcast_proxy_->pid(),
      base::Bind(&Manager::OnSubprocessExited, weak_factory_.GetWeakPtr(),
                 nd_proxy_->pid())))
      << "Failed to watch multicast-proxy child process";
  CHECK(process_reaper_.WatchForChild(
      FROM_HERE, nd_proxy_->pid(),
      base::Bind(&Manager::OnSubprocessExited, weak_factory_.GetWeakPtr(),
                 nd_proxy_->pid())))
      << "Failed to watch nd-proxy child process";

  // Run after Daemon::OnInit().
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&Manager::InitialSetup, weak_factory_.GetWeakPtr()));

  return DBusDaemon::OnInit();
}

void Manager::InitialSetup() {
  LOG(INFO) << "Setting up DBus service interface";
  dbus_svc_path_ = bus_->GetExportedObject(
      dbus::ObjectPath(patchpanel::kPatchPanelServicePath));
  if (!dbus_svc_path_) {
    LOG(FATAL) << "Failed to export " << patchpanel::kPatchPanelServicePath
               << " object";
  }

  using ServiceMethod =
      std::unique_ptr<dbus::Response> (Manager::*)(dbus::MethodCall*);
  const std::map<const char*, ServiceMethod> kServiceMethods = {
      {patchpanel::kArcStartupMethod, &Manager::OnArcStartup},
      {patchpanel::kArcShutdownMethod, &Manager::OnArcShutdown},
      {patchpanel::kArcVmStartupMethod, &Manager::OnArcVmStartup},
      {patchpanel::kArcVmShutdownMethod, &Manager::OnArcVmShutdown},
      {patchpanel::kTerminaVmStartupMethod, &Manager::OnTerminaVmStartup},
      {patchpanel::kTerminaVmShutdownMethod, &Manager::OnTerminaVmShutdown},
      {patchpanel::kPluginVmStartupMethod, &Manager::OnPluginVmStartup},
      {patchpanel::kPluginVmShutdownMethod, &Manager::OnPluginVmShutdown},
      {patchpanel::kSetVpnIntentMethod, &Manager::OnSetVpnIntent},
      {patchpanel::kConnectNamespaceMethod, &Manager::OnConnectNamespace},
      {patchpanel::kGetTrafficCountersMethod, &Manager::OnGetTrafficCounters},
      {patchpanel::kModifyPortRuleMethod, &Manager::OnModifyPortRule},
  };

  for (const auto& kv : kServiceMethods) {
    if (!dbus_svc_path_->ExportMethodAndBlock(
            patchpanel::kPatchPanelInterface, kv.first,
            base::Bind(&HandleSynchronousDBusMethodCall,
                       base::Bind(kv.second, base::Unretained(this))))) {
      LOG(FATAL) << "Failed to export method " << kv.first;
    }
  }

  if (!bus_->RequestOwnershipAndBlock(patchpanel::kPatchPanelServiceName,
                                      dbus::Bus::REQUIRE_PRIMARY)) {
    LOG(FATAL) << "Failed to take ownership of "
               << patchpanel::kPatchPanelServiceName;
  }
  LOG(INFO) << "DBus service interface ready";

  auto& runner = datapath_->runner();
  // Enable IPv4 packet forwarding
  if (runner.sysctl_w("net.ipv4.ip_forward", "1") != 0) {
    LOG(ERROR) << "Failed to update net.ipv4.ip_forward."
               << " Guest connectivity will not work correctly.";
  }
  // Limit local port range: Android owns 47104-61000.
  // TODO(garrick): The original history behind this tweak is gone. Some
  // investigation is needed to see if it is still applicable.
  if (runner.sysctl_w("net.ipv4.ip_local_port_range", "32768 47103") != 0) {
    LOG(ERROR) << "Failed to limit local port range. Some Android features or"
               << " apps may not work correctly.";
  }
  // Enable IPv6 packet forarding
  if (runner.sysctl_w("net.ipv6.conf.all.forwarding", "1") != 0) {
    LOG(ERROR) << "Failed to update net.ipv6.conf.all.forwarding."
               << " IPv6 functionality may be broken.";
  }
  // Kernel proxy_ndp is only needed for legacy IPv6 configuration
  if (!ShouldEnableFeature(kNDProxyMinAndroidSdkVersion,
                           kNDProxyMinChromeMilestone, {},
                           kNDProxyFeatureName) &&
      runner.sysctl_w("net.ipv6.conf.all.proxy_ndp", "1") != 0) {
    LOG(ERROR) << "Failed to update net.ipv6.conf.all.proxy_ndp."
               << " IPv6 functionality may be broken.";
  }

  if (!datapath_->AddSNATMarkRules()) {
    LOG(ERROR) << "Failed to install SNAT mark rules."
               << " Guest connectivity may be broken.";
  }
  if (!datapath_->AddForwardEstablishedRule()) {
    LOG(ERROR) << "Failed to install forwarding rule for established"
               << " connections.";
  }

  // TODO(chromium:898210): Move interface-specific masquerading setup to shill;
  // such that we can better set up the masquerade rules based on connection
  // type rather than interface names.
  if (!datapath_->AddInterfaceSNAT("wwan+")) {
    LOG(ERROR) << "Failed to set up wifi masquerade";
  }

  if (!datapath_->AddOutboundIPv4SNATMark("vmtap+")) {
    LOG(ERROR) << "Failed to set up NAT for TAP devices."
               << " Guest connectivity may be broken.";
  }

  routing_svc_ = std::make_unique<RoutingService>();

  nd_proxy_->RegisterDeviceMessageHandler(base::Bind(
      &Manager::OnDeviceMessageFromNDProxy, weak_factory_.GetWeakPtr()));

  shill_client_ = std::make_unique<ShillClient>(bus_);
  auto* const forwarder = static_cast<TrafficForwarder*>(this);

  GuestMessage::GuestType arc_guest =
      USE_ARCVM ? GuestMessage::ARC_VM : GuestMessage::ARC;
  arc_svc_ = std::make_unique<ArcService>(shill_client_.get(), datapath_.get(),
                                          &addr_mgr_, forwarder, arc_guest);
  cros_svc_ = std::make_unique<CrostiniService>(shill_client_.get(), &addr_mgr_,
                                                datapath_.get(), forwarder);
  network_monitor_svc_ =
      std::make_unique<NetworkMonitorService>(shill_client_.get());
  network_monitor_svc_->Start();

  counters_svc_ =
      std::make_unique<CountersService>(shill_client_.get(), runner_.get());

  nd_proxy_->Listen();
}

void Manager::OnShutdown(int* exit_code) {
  LOG(INFO) << "Shutting down and cleaning up";
  cros_svc_.reset();
  arc_svc_.reset();
  close(connected_namespaces_epollfd_);
  // Tear down any remaining connected namespace.
  std::vector<int> connected_namespaces_fdkeys;
  for (const auto& kv : connected_namespaces_)
    connected_namespaces_fdkeys.push_back(kv.first);
  for (const int fdkey : connected_namespaces_fdkeys)
    DisconnectNamespace(fdkey);

  datapath_->RemoveOutboundIPv4SNATMark("vmtap+");
  datapath_->RemoveInterfaceSNAT("wwan+");
  datapath_->RemoveForwardEstablishedRule();
  datapath_->RemoveSNATMarkRules();

  auto& runner = datapath_->runner();
  // Restore original local port range.
  // TODO(garrick): The original history behind this tweak is gone. Some
  // investigation is needed to see if it is still applicable.
  if (runner.sysctl_w("net.ipv4.ip_local_port_range", "32768 61000") != 0) {
    LOG(ERROR) << "Failed to restore local port range";
  }
  // Disable packet forwarding
  if (runner.sysctl_w("net.ipv6.conf.all.forwarding", "0") != 0) {
    LOG(ERROR) << "Failed to restore net.ipv6.conf.all.forwarding.";
  }
  if (runner.sysctl_w("net.ipv4.ip_forward", "0") != 0) {
    LOG(ERROR) << "Failed to restore net.ipv4.ip_forward.";
  }
}

void Manager::OnSubprocessExited(pid_t pid, const siginfo_t&) {
  LOG(ERROR) << "Subprocess " << pid << " exited unexpectedly -"
             << " attempting to restart";

  HelperProcess* proc;
  if (pid == adb_proxy_->pid()) {
    proc = adb_proxy_.get();
  } else if (pid == mcast_proxy_->pid()) {
    proc = mcast_proxy_.get();
  } else if (pid == nd_proxy_->pid()) {
    proc = nd_proxy_.get();
  } else {
    LOG(DFATAL) << "Unknown child process";
    return;
  }

  process_reaper_.ForgetChild(pid);

  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&Manager::RestartSubprocess, weak_factory_.GetWeakPtr(), proc),
      base::TimeDelta::FromMilliseconds((2 << proc->restarts()) *
                                        kSubprocessRestartDelayMs));
}

void Manager::RestartSubprocess(HelperProcess* subproc) {
  if (subproc->Restart()) {
    DCHECK(process_reaper_.WatchForChild(
        FROM_HERE, subproc->pid(),
        base::Bind(&Manager::OnSubprocessExited, weak_factory_.GetWeakPtr(),
                   subproc->pid())))
        << "Failed to watch child process " << subproc->pid();
  }
}

bool Manager::StartArc(pid_t pid) {
  if (!arc_svc_->Start(pid))
    return false;

  GuestMessage msg;
  msg.set_event(GuestMessage::START);
  msg.set_type(GuestMessage::ARC);
  msg.set_arc_pid(pid);
  SendGuestMessage(msg);

  return true;
}

void Manager::StopArc() {
  GuestMessage msg;
  msg.set_event(GuestMessage::STOP);
  msg.set_type(GuestMessage::ARC);
  SendGuestMessage(msg);

  // After the ARC container has stopped, the pid is not known anymore.
  // The pid argument is ignored by ArcService.
  arc_svc_->Stop(0);
}

bool Manager::StartArcVm(uint32_t cid) {
  if (!arc_svc_->Start(cid))
    return false;

  GuestMessage msg;
  msg.set_event(GuestMessage::START);
  msg.set_type(GuestMessage::ARC_VM);
  msg.set_arcvm_vsock_cid(cid);
  SendGuestMessage(msg);

  return true;
}

void Manager::StopArcVm(uint32_t cid) {
  GuestMessage msg;
  msg.set_event(GuestMessage::STOP);
  msg.set_type(GuestMessage::ARC_VM);
  SendGuestMessage(msg);

  arc_svc_->Stop(cid);
}

bool Manager::StartCrosVm(uint64_t vm_id,
                          GuestMessage::GuestType vm_type,
                          uint32_t subnet_index) {
  DCHECK(vm_type == GuestMessage::TERMINA_VM ||
         vm_type == GuestMessage::PLUGIN_VM);

  if (!cros_svc_->Start(vm_id, vm_type == GuestMessage::TERMINA_VM,
                        subnet_index))
    return false;

  GuestMessage msg;
  msg.set_event(GuestMessage::START);
  msg.set_type(vm_type);
  SendGuestMessage(msg);

  return true;
}

void Manager::StopCrosVm(uint64_t vm_id, GuestMessage::GuestType vm_type) {
  GuestMessage msg;
  msg.set_event(GuestMessage::STOP);
  msg.set_type(vm_type);
  SendGuestMessage(msg);

  cros_svc_->Stop(vm_id, vm_type == GuestMessage::TERMINA_VM);
}

std::unique_ptr<dbus::Response> Manager::OnArcStartup(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "ARC++ starting up";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::ArcStartupRequest request;
  patchpanel::ArcStartupResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse request";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (!StartArc(request.pid()))
    LOG(ERROR) << "Failed to start ARC++ network service";

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Manager::OnArcShutdown(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "ARC++ shutting down";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::ArcShutdownRequest request;
  patchpanel::ArcShutdownResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse request";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  StopArc();

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Manager::OnArcVmStartup(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "ARCVM starting up";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::ArcVmStartupRequest request;
  patchpanel::ArcVmStartupResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse request";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (!StartArcVm(request.cid())) {
    LOG(ERROR) << "Failed to start ARCVM network service";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Populate the response with the known devices.
  for (const auto* config : arc_svc_->GetDeviceConfigs()) {
    if (config->tap_ifname().empty())
      continue;

    auto* dev = response.add_devices();
    dev->set_ifname(config->tap_ifname());
    dev->set_ipv4_addr(config->guest_ipv4_addr());
  }

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Manager::OnArcVmShutdown(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "ARCVM shutting down";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::ArcVmShutdownRequest request;
  patchpanel::ArcVmShutdownResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse request";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  StopArcVm(request.cid());

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Manager::OnTerminaVmStartup(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "Termina VM starting up";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::TerminaVmStartupRequest request;
  patchpanel::TerminaVmStartupResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse request";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  const int32_t cid = request.cid();
  if (!StartCrosVm(cid, GuestMessage::TERMINA_VM)) {
    LOG(ERROR) << "Failed to start Termina VM network service";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  const auto* const tap = cros_svc_->TAP(cid, true /*is_termina*/);
  if (!tap) {
    LOG(DFATAL) << "TAP device missing";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto* dev = response.mutable_device();
  dev->set_ifname(tap->host_ifname());
  const auto* subnet = tap->config().ipv4_subnet();
  if (!subnet) {
    LOG(DFATAL) << "Missing required subnet for {cid: " << cid << "}";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  auto* resp_subnet = dev->mutable_ipv4_subnet();
  resp_subnet->set_base_addr(subnet->BaseAddress());
  resp_subnet->set_prefix_len(subnet->PrefixLength());
  subnet = tap->config().lxd_ipv4_subnet();
  if (!subnet) {
    LOG(DFATAL) << "Missing required lxd subnet for {cid: " << cid << "}";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  resp_subnet = response.mutable_container_subnet();
  resp_subnet->set_base_addr(subnet->BaseAddress());
  resp_subnet->set_prefix_len(subnet->PrefixLength());

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Manager::OnTerminaVmShutdown(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "Termina VM shutting down";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::TerminaVmShutdownRequest request;
  patchpanel::TerminaVmShutdownResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse request";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  StopCrosVm(request.cid(), GuestMessage::TERMINA_VM);

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Manager::OnPluginVmStartup(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "Plugin VM starting up";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::PluginVmStartupRequest request;
  patchpanel::PluginVmStartupResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse request";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  const uint64_t vm_id = request.id();
  if (!StartCrosVm(vm_id, GuestMessage::PLUGIN_VM, request.subnet_index())) {
    LOG(ERROR) << "Failed to start Plugin VM network service";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  const auto* const tap = cros_svc_->TAP(vm_id, false /*is_termina*/);
  if (!tap) {
    LOG(DFATAL) << "TAP device missing";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto* dev = response.mutable_device();
  dev->set_ifname(tap->host_ifname());
  const auto* subnet = tap->config().ipv4_subnet();
  if (!subnet) {
    LOG(DFATAL) << "Missing required subnet for {cid: " << vm_id << "}";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  auto* resp_subnet = dev->mutable_ipv4_subnet();
  resp_subnet->set_base_addr(subnet->BaseAddress());
  resp_subnet->set_prefix_len(subnet->PrefixLength());

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Manager::OnPluginVmShutdown(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "Plugin VM shutting down";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::PluginVmShutdownRequest request;
  patchpanel::PluginVmShutdownResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse request";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  StopCrosVm(request.id(), GuestMessage::PLUGIN_VM);

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Manager::OnSetVpnIntent(
    dbus::MethodCall* method_call) {
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::SetVpnIntentRequest request;
  patchpanel::SetVpnIntentResponse response;

  bool success = reader.PopArrayOfBytesAsProto(&request);
  if (!success) {
    LOG(ERROR) << "Unable to parse SetVpnIntentRequest";
    // Do not return yet to make sure we close the received fd.
  }

  base::ScopedFD client_socket;
  reader.PopFileDescriptor(&client_socket);

  if (success)
    success = routing_svc_->SetVpnFwmark(client_socket.get(), request.policy());

  response.set_success(success);

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Manager::OnConnectNamespace(
    dbus::MethodCall* method_call) {
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::ConnectNamespaceRequest request;
  patchpanel::ConnectNamespaceResponse response;

  bool success = true;
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse ConnectNamespaceRequest";
    // Do not return yet to make sure we close the received fd and
    // validate other arguments.
    success = false;
  }

  base::ScopedFD client_fd;
  reader.PopFileDescriptor(&client_fd);
  if (!client_fd.is_valid()) {
    LOG(ERROR) << "ConnectNamespaceRequest: invalid file descriptor";
    success = false;
  }

  pid_t pid = request.pid();
  {
    ScopedNS ns(pid);
    if (!ns.IsValid()) {
      LOG(ERROR) << "ConnectNamespaceRequest: invalid namespace pid " << pid;
      success = false;
    }
  }

  const std::string& outbound_ifname = request.outbound_physical_device();
  if (!outbound_ifname.empty() && !shill_client_->has_device(outbound_ifname)) {
    LOG(ERROR) << "ConnectNamespaceRequest: invalid outbound ifname "
               << outbound_ifname;
    success = false;
  }

  if (success)
    ConnectNamespace(std::move(client_fd), request, response);

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Manager::OnGetTrafficCounters(
    dbus::MethodCall* method_call) {
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::TrafficCountersRequest request;
  patchpanel::TrafficCountersResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse TrafficCountersRequest";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  const std::set<std::string> devices{request.devices().begin(),
                                      request.devices().end()};
  const auto counters = counters_svc_->GetCounters(devices);
  for (const auto& kv : counters) {
    auto* traffic_counter = response.add_counters();
    const auto& source_and_device = kv.first;
    const auto& counter = kv.second;
    traffic_counter->set_source(source_and_device.first);
    traffic_counter->set_device(source_and_device.second);
    traffic_counter->set_rx_bytes(counter.rx_bytes);
    traffic_counter->set_rx_packets(counter.rx_packets);
    traffic_counter->set_tx_bytes(counter.tx_bytes);
    traffic_counter->set_tx_packets(counter.tx_packets);
  }

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Manager::OnModifyPortRule(
    dbus::MethodCall* method_call) {
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  patchpanel::ModifyPortRuleRequest request;
  patchpanel::ModifyPortRuleResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse ModifyPortRequest";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  response.set_success(ModifyPortRule(request));
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

void Manager::ConnectNamespace(
    base::ScopedFD client_fd,
    const patchpanel::ConnectNamespaceRequest& request,
    patchpanel::ConnectNamespaceResponse& response) {
  std::unique_ptr<Subnet> subnet =
      addr_mgr_.AllocateIPv4Subnet(AddressManager::Guest::MINIJAIL_NETNS);
  if (!subnet) {
    LOG(ERROR) << "ConnectNamespaceRequest: exhausted IPv4 subnet space";
    return;
  }

  const std::string ifname_id = std::to_string(connected_namespaces_next_id_);
  const std::string netns_name = "connected_netns_" + ifname_id;
  const std::string host_ifname = "arc_ns" + ifname_id;
  const std::string client_ifname = "veth" + ifname_id;
  const uint32_t host_ipv4_addr = subnet->AddressAtOffset(0);
  const uint32_t client_ipv4_addr = subnet->AddressAtOffset(1);

  // Veth interface configuration and client routing configuration:
  //  - attach a name to the client namespace.
  //  - create veth pair across the current namespace and the client namespace.
  //  - configure IPv4 address on remote veth inside client namespace.
  //  - configure IPv4 address on local veth inside host namespace.
  //  - add a default IPv4 /0 route sending traffic to that remote veth.
  pid_t pid = request.pid();
  if (!datapath_->NetnsAttachName(netns_name, pid)) {
    LOG(ERROR) << "ConnectNamespaceRequest: failed to attach name "
               << netns_name << " to namespace pid " << pid;
    return;
  }
  if (!datapath_->ConnectVethPair(pid, netns_name, host_ifname, client_ifname,
                                  addr_mgr_.GenerateMacAddress(),
                                  client_ipv4_addr, subnet->PrefixLength(),
                                  false /* enable_multicast */)) {
    LOG(ERROR) << "ConnectNamespaceRequest: failed to create veth pair for "
                  "namespace pid "
               << pid;
    datapath_->NetnsDeleteName(netns_name);
    return;
  }
  if (!datapath_->ConfigureInterface(
          host_ifname, addr_mgr_.GenerateMacAddress(), host_ipv4_addr,
          subnet->PrefixLength(), true /* link up */,
          false /* enable_multicast */)) {
    LOG(ERROR) << "ConnectNamespaceRequest: cannot configure host interface "
               << host_ifname;
    datapath_->RemoveInterface(host_ifname);
    datapath_->NetnsDeleteName(netns_name);
    return;
  }
  bool peer_route_setup_success;
  {
    ScopedNS ns(pid);
    peer_route_setup_success =
        ns.IsValid() &&
        datapath_->AddIPv4Route(host_ipv4_addr, INADDR_ANY, INADDR_ANY);
  }
  if (!peer_route_setup_success) {
    LOG(ERROR) << "ConnectNamespaceRequest: failed to add default /0 route to "
               << host_ifname << " inside namespace pid " << pid;
    datapath_->RemoveInterface(host_ifname);
    datapath_->NetnsDeleteName(netns_name);
    return;
  }

  // Host namespace routing configuration
  //  - ingress: add route to client subnet via |host_ifname|.
  //  - egress: - allow forwarding for traffic outgoing |host_ifname|.
  //            - add SNAT mark 0x1/0x1 for traffic outgoing |host_ifname|.
  //  Note that by default unsolicited ingress traffic is not forwarded to the
  //  client namespace unless the client specifically set port forwarding
  //  through permission_broker DBus APIs.
  // TODO(hugobenichi) If allow_user_traffic is false, then prevent forwarding
  // both ways between client namespace and other guest containers and VMs.
  // TODO(hugobenichi) If outbound_physical_device is defined, then set strong
  // routing to that interface routing table.
  if (!datapath_->AddIPv4Route(host_ipv4_addr, subnet->BaseAddress(),
                               subnet->Netmask())) {
    LOG(ERROR)
        << "ConnectNamespaceRequest: failed to set route to client namespace";
    datapath_->RemoveInterface(host_ifname);
    datapath_->NetnsDeleteName(netns_name);
    return;
  }
  if (!datapath_->AddOutboundIPv4(host_ifname)) {
    LOG(ERROR) << "ConnectNamespaceRequest: failed to allow FORWARD for "
                  "traffic outgoing from "
               << host_ifname;
    datapath_->RemoveInterface(host_ifname);
    datapath_->DeleteIPv4Route(host_ipv4_addr, subnet->BaseAddress(),
                               subnet->Netmask());
    datapath_->NetnsDeleteName(netns_name);
    return;
  }
  if (!datapath_->AddOutboundIPv4SNATMark(host_ifname)) {
    LOG(ERROR) << "ConnectNamespaceRequest: failed to set SNAT for traffic "
                  "outgoing from "
               << host_ifname;
    datapath_->RemoveInterface(host_ifname);
    datapath_->DeleteIPv4Route(host_ipv4_addr, subnet->BaseAddress(),
                               subnet->Netmask());
    datapath_->RemoveOutboundIPv4(host_ifname);
    datapath_->NetnsDeleteName(netns_name);
    return;
  }

  // Dup the client fd into our own: this guarantees that the fd number will
  // be stable and tied to the actual kernel resources used by the client.
  base::ScopedFD local_client_fd(dup(client_fd.get()));
  if (!local_client_fd.is_valid()) {
    PLOG(ERROR) << "ConnectNamespaceRequest: failed to dup() client fd";
    datapath_->RemoveInterface(host_ifname);
    datapath_->DeleteIPv4Route(host_ipv4_addr, subnet->BaseAddress(),
                               subnet->Netmask());
    datapath_->RemoveOutboundIPv4(host_ifname);
    datapath_->RemoveOutboundIPv4SNATMark(host_ifname);
    datapath_->NetnsDeleteName(netns_name);
    return;
  }

  // Add the dupe fd to the epoll watcher.
  // TODO(hugobenichi) Find a way to reuse base::FileDescriptorWatcher for
  // listening to EPOLLHUP.
  struct epoll_event epevent;
  epevent.events = EPOLLIN;  // EPOLLERR | EPOLLHUP are always waited for.
  epevent.data.fd = local_client_fd.get();
  if (epoll_ctl(connected_namespaces_epollfd_, EPOLL_CTL_ADD,
                local_client_fd.get(), &epevent) != 0) {
    PLOG(ERROR) << "ConnectNamespaceResponse: epoll_ctl(EPOLL_CTL_ADD) failed";
    datapath_->RemoveInterface(host_ifname);
    datapath_->DeleteIPv4Route(host_ipv4_addr, subnet->BaseAddress(),
                               subnet->Netmask());
    datapath_->RemoveOutboundIPv4(host_ifname);
    datapath_->RemoveOutboundIPv4SNATMark(host_ifname);
    datapath_->NetnsDeleteName(netns_name);
    return;
  }

  // Prepare the response before storing ConnectNamespaceInfo.
  response.set_peer_ifname(client_ifname);
  response.set_peer_ipv4_address(host_ipv4_addr);
  response.set_host_ifname(host_ifname);
  response.set_host_ipv4_address(client_ipv4_addr);
  auto* response_subnet = response.mutable_ipv4_subnet();
  response_subnet->set_base_addr(subnet->BaseAddress());
  response_subnet->set_prefix_len(subnet->PrefixLength());

  // Store ConnectNamespaceInfo
  connected_namespaces_next_id_++;
  int fdkey = local_client_fd.release();
  connected_namespaces_[fdkey] = {};
  ConnectNamespaceInfo& ns_info = connected_namespaces_[fdkey];
  ns_info.pid = request.pid();
  ns_info.netns_name = std::move(netns_name);
  ns_info.outbound_ifname = request.outbound_physical_device();
  ns_info.host_ifname = std::move(host_ifname);
  ns_info.client_ifname = std::move(client_ifname);
  ns_info.client_subnet = std::move(subnet);

  LOG(INFO) << "Connected network namespace " << ns_info;

  if (connected_namespaces_.size() == 1) {
    LOG(INFO) << "Starting ConnectNamespace client fds monitoring";
    CheckConnectedNamespaces();
  }
}

void Manager::DisconnectNamespace(int client_fd) {
  auto it = connected_namespaces_.find(client_fd);
  if (it == connected_namespaces_.end()) {
    LOG(ERROR) << "No ConnectNamespaceInfo found for client_fd " << client_fd;
    return;
  }

  // Remove the client fd dupe from the epoll watcher and close it.
  if (epoll_ctl(connected_namespaces_epollfd_, EPOLL_CTL_DEL, client_fd,
                nullptr) != 0)
    PLOG(ERROR) << "DisconnectNamespace: epoll_ctl(EPOLL_CTL_DEL) failed";
  if (close(client_fd) < 0)
    PLOG(ERROR) << "DisconnectNamespace: close(client_fd) failed";

  // Destroy the interface configuration and routing configuration:
  //  - destroy veth pair.
  //  - remove forwarding rules on host namespace.
  //  - remove SNAT marking rule on host namespace.
  //  Delete the network namespace attached to the client namespace.
  //  Note that the default route set inside the client namespace by patchpanel
  //  is not destroyed: it is assumed the client will also teardown its
  //  namespace if it triggered DisconnectNamespace.
  datapath_->RemoveInterface(it->second.host_ifname);
  datapath_->RemoveOutboundIPv4(it->second.host_ifname);
  datapath_->RemoveOutboundIPv4SNATMark(it->second.host_ifname);
  datapath_->DeleteIPv4Route(it->second.client_subnet->AddressAtOffset(0),
                             it->second.client_subnet->BaseAddress(),
                             it->second.client_subnet->Netmask());
  datapath_->NetnsDeleteName(it->second.netns_name);

  LOG(INFO) << "Disconnected network namespace " << it->second;

  // This release the allocated IPv4 subnet.
  connected_namespaces_.erase(it);
}

// TODO(hugobenichi) Generalize this check to all resources created by
// patchpanel on behalf of a remote client.
void Manager::CheckConnectedNamespaces() {
  int max_event = 10;
  struct epoll_event epevents[max_event];
  int nready = epoll_wait(connected_namespaces_epollfd_, epevents, max_event,
                          0 /* do not block */);
  if (nready < 0)
    PLOG(ERROR) << "CheckConnectedNamespaces: epoll_wait(0) failed";

  for (int i = 0; i < nready; i++)
    if (epevents[i].events & (EPOLLHUP | EPOLLERR))
      DisconnectNamespace(epevents[i].data.fd);

  if (connected_namespaces_.empty()) {
    LOG(INFO) << "Stopping ConnectNamespace client fds monitoring";
    return;
  }

  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&Manager::CheckConnectedNamespaces,
                 weak_factory_.GetWeakPtr()),
      kConnectNamespaceCheckInterval);
}

bool Manager::ModifyPortRule(const patchpanel::ModifyPortRuleRequest& request) {
  switch (request.proto()) {
    case patchpanel::ModifyPortRuleRequest::TCP:
    case patchpanel::ModifyPortRuleRequest::UDP:
      break;
    default:
      LOG(ERROR) << "Unknown protocol " << request.proto();
      return false;
  }

  switch (request.op()) {
    case patchpanel::ModifyPortRuleRequest::CREATE:
      switch (request.type()) {
        case patchpanel::ModifyPortRuleRequest::ACCESS:
          return firewall_.AddAcceptRules(request.proto(),
                                          request.input_dst_port(),
                                          request.input_ifname());
        case patchpanel::ModifyPortRuleRequest::LOCKDOWN:
          return firewall_.AddLoopbackLockdownRules(request.proto(),
                                                    request.input_dst_port());
        case patchpanel::ModifyPortRuleRequest::FORWARDING:
          return firewall_.AddIpv4ForwardRule(
              request.proto(), request.input_dst_ip(), request.input_dst_port(),
              request.input_ifname(), request.dst_ip(), request.dst_port());
        default:
          LOG(ERROR) << "Unknown port rule type " << request.type();
          return false;
      }
    case patchpanel::ModifyPortRuleRequest::DELETE:
      switch (request.type()) {
        case patchpanel::ModifyPortRuleRequest::ACCESS:
          return firewall_.DeleteAcceptRules(request.proto(),
                                             request.input_dst_port(),
                                             request.input_ifname());
        case patchpanel::ModifyPortRuleRequest::LOCKDOWN:
          return firewall_.DeleteLoopbackLockdownRules(
              request.proto(), request.input_dst_port());
        case patchpanel::ModifyPortRuleRequest::FORWARDING:
          return firewall_.DeleteIpv4ForwardRule(
              request.proto(), request.input_dst_ip(), request.input_dst_port(),
              request.input_ifname(), request.dst_ip(), request.dst_port());
        default:
          LOG(ERROR) << "Unknown port rule type " << request.type();
          return false;
      }
    default:
      LOG(ERROR) << "Unknown operation " << request.op();
      return false;
  }
}

void Manager::SendGuestMessage(const GuestMessage& msg) {
  IpHelperMessage ipm;
  *ipm.mutable_guest_message() = msg;
  adb_proxy_->SendMessage(ipm);
  mcast_proxy_->SendMessage(ipm);
  nd_proxy_->SendMessage(ipm);
}

void Manager::StartForwarding(const std::string& ifname_physical,
                              const std::string& ifname_virtual,
                              bool ipv6,
                              bool multicast) {
  if (ifname_physical.empty() || ifname_virtual.empty())
    return;

  IpHelperMessage ipm;
  DeviceMessage* msg = ipm.mutable_device_message();
  msg->set_dev_ifname(ifname_physical);
  msg->set_br_ifname(ifname_virtual);

  if (ipv6) {
    LOG(INFO) << "Starting IPv6 forwarding from " << ifname_physical << " to "
              << ifname_virtual;

    if (!datapath_->AddIPv6Forwarding(ifname_physical, ifname_virtual)) {
      LOG(ERROR) << "Failed to setup iptables forwarding rule for IPv6 from "
                 << ifname_physical << " to " << ifname_virtual;
    }
    if (!datapath_->MaskInterfaceFlags(ifname_physical, IFF_ALLMULTI)) {
      LOG(WARNING) << "Failed to setup all multicast mode for interface "
                   << ifname_physical;
    }
    if (!datapath_->MaskInterfaceFlags(ifname_virtual, IFF_ALLMULTI)) {
      LOG(WARNING) << "Failed to setup all multicast mode for interface "
                   << ifname_virtual;
    }
    nd_proxy_->SendMessage(ipm);
  }

  if (multicast) {
    LOG(INFO) << "Starting multicast forwarding from " << ifname_physical
              << " to " << ifname_virtual;
    mcast_proxy_->SendMessage(ipm);
  }
}

void Manager::StopForwarding(const std::string& ifname_physical,
                             const std::string& ifname_virtual,
                             bool ipv6,
                             bool multicast) {
  if (ifname_physical.empty())
    return;

  IpHelperMessage ipm;
  DeviceMessage* msg = ipm.mutable_device_message();
  msg->set_dev_ifname(ifname_physical);
  msg->set_teardown(true);
  if (!ifname_virtual.empty()) {
    msg->set_br_ifname(ifname_virtual);
  }

  if (ipv6) {
    if (ifname_virtual.empty()) {
      LOG(INFO) << "Stopping IPv6 forwarding on " << ifname_physical;
    } else {
      LOG(INFO) << "Stopping IPv6 forwarding from " << ifname_physical << " to "
                << ifname_virtual;
      datapath_->RemoveIPv6Forwarding(ifname_physical, ifname_virtual);
    }
    nd_proxy_->SendMessage(ipm);
  }

  if (multicast) {
    if (ifname_virtual.empty()) {
      LOG(INFO) << "Stopping multicast forwarding on " << ifname_physical;
    } else {
      LOG(INFO) << "Stopping multicast forwarding from " << ifname_physical
                << " to " << ifname_virtual;
    }
    mcast_proxy_->SendMessage(ipm);
  }
}

void Manager::OnDeviceMessageFromNDProxy(const DeviceMessage& msg) {
  LOG_IF(DFATAL, msg.dev_ifname().empty())
      << "Received DeviceMessage w/ empty dev_ifname";

  if (!datapath_->AddIPv6HostRoute(msg.dev_ifname(), msg.guest_ip6addr(),
                                   128)) {
    LOG(WARNING) << "Failed to setup the IPv6 route for interface "
                 << msg.dev_ifname();
  }
}

std::ostream& operator<<(std::ostream& stream,
                         const Manager::ConnectNamespaceInfo& ns_info) {
  stream << "{ pid: " << ns_info.pid;
  if (!ns_info.outbound_ifname.empty()) {
    stream << ", outbound_ifname: " << ns_info.outbound_ifname;
  }
  stream << ", host_ifname: " << ns_info.host_ifname
         << ", client_ifname: " << ns_info.client_ifname
         << ", subnet: " << ns_info.client_subnet->ToCidrString() << '}';
  return stream;
}

}  // namespace patchpanel
