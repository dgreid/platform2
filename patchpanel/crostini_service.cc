// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/crostini_service.h"

#include <memory>
#include <utility>

#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include "base/threading/thread_task_runner_handle.h"
#include <chromeos/constants/vm_tools.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/object_path.h>

#include "patchpanel/adb_proxy.h"
#include "patchpanel/device.h"

namespace patchpanel {
namespace {
constexpr int32_t kInvalidID = 0;
constexpr int kDbusTimeoutMs = 200;
// The maximum number of ADB sideloading query failures before stopping.
constexpr int kAdbSideloadMaxTry = 5;
constexpr base::TimeDelta kAdbSideloadUpdateDelay =
    base::TimeDelta::FromMilliseconds(5000);

std::string MakeKey(uint64_t vm_id, bool is_termina) {
  return base::StringPrintf("%s:%s", is_termina ? "t" : "p",
                            base::NumberToString(vm_id).c_str());
}

}  // namespace

CrostiniService::CrostiniService(ShillClient* shill_client,
                                 AddressManager* addr_mgr,
                                 Datapath* datapath,
                                 TrafficForwarder* forwarder)
    : shill_client_(shill_client),
      addr_mgr_(addr_mgr),
      datapath_(datapath),
      forwarder_(forwarder),
      adb_sideloading_enabled_(false) {
  DCHECK(shill_client_);
  DCHECK(addr_mgr_);
  DCHECK(datapath_);
  DCHECK(forwarder_);

  // Setup for ADB sideloading.
  if (!SetupFirewallClient()) {
    LOG(ERROR) << "Failed to setup firewall client for ADB sideloading";
  } else {
    CheckAdbSideloadingStatus();
  }

  shill_client_->RegisterDefaultInterfaceChangedHandler(base::Bind(
      &CrostiniService::OnDefaultInterfaceChanged, weak_factory_.GetWeakPtr()));
}

CrostiniService::~CrostiniService() {
  if (bus_)
    bus_->ShutdownAndBlock();
}

bool CrostiniService::Start(uint64_t vm_id, bool is_termina, int subnet_index) {
  if (vm_id == kInvalidID) {
    LOG(ERROR) << "Invalid VM id";
    return false;
  }

  const auto key = MakeKey(vm_id, is_termina);
  if (taps_.find(key) != taps_.end()) {
    LOG(WARNING) << "Already started for {id: " << vm_id << "}";
    return false;
  }

  auto tap = AddTAP(is_termina, subnet_index);
  if (!tap) {
    LOG(ERROR) << "Cannot start for {id: " << vm_id << "}";
    return false;
  }

  LOG(INFO) << "Crostini network service started for {id: " << vm_id << "}";
  StartForwarding(shill_client_->default_interface(), tap->host_ifname());

  if (adb_sideloading_enabled_)
    StartAdbPortForwarding(tap->phys_ifname());

  taps_.emplace(key, std::move(tap));
  return true;
}

void CrostiniService::Stop(uint64_t vm_id, bool is_termina) {
  const auto key = MakeKey(vm_id, is_termina);
  const auto it = taps_.find(key);
  if (it == taps_.end()) {
    LOG(WARNING) << "Unknown {id: " << vm_id << "}";
    return;
  }

  const auto& ifname = it->second->host_ifname();
  StopForwarding(shill_client_->default_interface(), ifname);
  if (adb_sideloading_enabled_)
    StopAdbPortForwarding(ifname);
  datapath_->RemoveInterface(ifname);
  taps_.erase(key);

  LOG(INFO) << "Crostini network service stopped for {id: " << vm_id << "}";
}

const Device* const CrostiniService::TAP(uint64_t vm_id,
                                         bool is_termina) const {
  const auto it = taps_.find(MakeKey(vm_id, is_termina));
  if (it == taps_.end()) {
    return nullptr;
  }
  return it->second.get();
}

std::unique_ptr<Device> CrostiniService::AddTAP(bool is_termina,
                                                int subnet_index) {
  auto ipv4_subnet = addr_mgr_->AllocateIPv4Subnet(
      is_termina ? AddressManager::Guest::VM_TERMINA
                 : AddressManager::Guest::VM_PLUGIN,
      subnet_index);
  if (!ipv4_subnet) {
    LOG(ERROR) << "Subnet already in use or unavailable.";
    return nullptr;
  }
  auto host_ipv4_addr = ipv4_subnet->AllocateAtOffset(0);
  if (!host_ipv4_addr) {
    LOG(ERROR) << "Host address already in use or unavailable.";
    return nullptr;
  }
  auto guest_ipv4_addr = ipv4_subnet->AllocateAtOffset(1);
  if (!guest_ipv4_addr) {
    LOG(ERROR) << "VM address already in use or unavailable.";
    return nullptr;
  }
  std::unique_ptr<Subnet> lxd_subnet;
  if (is_termina) {
    lxd_subnet =
        addr_mgr_->AllocateIPv4Subnet(AddressManager::Guest::CONTAINER);
    if (!lxd_subnet) {
      LOG(ERROR) << "lxd subnet already in use or unavailable.";
      return nullptr;
    }
  }

  const auto mac_addr = addr_mgr_->GenerateMacAddress(subnet_index);
  const std::string tap =
      datapath_->AddTAP("" /* auto-generate name */, &mac_addr,
                        host_ipv4_addr.get(), vm_tools::kCrosVmUser);
  if (tap.empty()) {
    LOG(ERROR) << "Failed to create TAP device.";
    return nullptr;
  }

  if (lxd_subnet) {
    // Setup lxd route for the container using the VM as a gateway.
    if (!datapath_->AddIPv4Route(ipv4_subnet->AddressAtOffset(1),
                                 lxd_subnet->AddressAtOffset(0),
                                 lxd_subnet->Netmask())) {
      LOG(ERROR) << "Failed to setup lxd route";
      return nullptr;
    }
  }

  auto config = std::make_unique<Device::Config>(
      mac_addr, std::move(ipv4_subnet), std::move(host_ipv4_addr),
      std::move(guest_ipv4_addr), std::move(lxd_subnet));

  Device::Options opts{
      .fwd_multicast = true,
      .ipv6_enabled = true,
  };

  return std::make_unique<Device>(tap, tap, "", std::move(config), opts);
}

void CrostiniService::OnDefaultInterfaceChanged(
    const std::string& new_ifname, const std::string& prev_ifname) {
  for (const auto& t : taps_) {
    StopForwarding(prev_ifname, t.second->host_ifname());
    StartForwarding(new_ifname, t.second->host_ifname());
  }
}

void CrostiniService::StartForwarding(const std::string& phys_ifname,
                                      const std::string& virt_ifname) {
  if (!phys_ifname.empty())
    forwarder_->StartForwarding(phys_ifname, virt_ifname, true /*ipv6*/,
                                true /*multicast*/);
}

void CrostiniService::StopForwarding(const std::string& phys_ifname,
                                     const std::string& virt_ifname) {
  if (!phys_ifname.empty())
    forwarder_->StopForwarding(phys_ifname, virt_ifname, true /*ipv6*/,
                               true /*multicast*/);
}

bool CrostiniService::SetupFirewallClient() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;

  bus_ = new dbus::Bus(options);
  if (!bus_->Connect()) {
    LOG(ERROR) << "Failed to connect to system bus";
    return false;
  }

  permission_broker_proxy_.reset(
      new org::chromium::PermissionBrokerProxy(bus_));

  return true;
}

void CrostiniService::StartAdbPortForwarding(const std::string& ifname) {
  if (!permission_broker_proxy_)
    return;

  DCHECK(lifeline_fds_.find(ifname) == lifeline_fds_.end());
  // Setup lifeline pipe.
  int lifeline_fds[2];
  if (pipe(lifeline_fds) != 0) {
    PLOG(ERROR) << "Failed to create lifeline pipe";
    return;
  }
  base::ScopedFD lifeline_read_fd(lifeline_fds[0]);
  base::ScopedFD lifeline_write_fd(lifeline_fds[1]);

  bool allowed = false;
  brillo::ErrorPtr error;
  permission_broker_proxy_->RequestAdbPortForward(ifname, lifeline_fds[0],
                                                  &allowed, &error);
  if (error) {
    LOG(ERROR) << "Error calling D-Bus proxy call to interface "
               << "'" << permission_broker_proxy_->GetObjectPath().value()
               << "': " << error->GetMessage();
    return;
  }
  if (!allowed) {
    LOG(ERROR) << "ADB port forwarding on " << ifname << " not allowed";
    return;
  }

  permission_broker_proxy_->RequestTcpPortAccess(
      kAdbProxyTcpListenPort, ifname, lifeline_fds[0], &allowed, &error);
  if (error) {
    LOG(ERROR) << "Error calling D-Bus proxy call to interface "
               << "'" << permission_broker_proxy_->GetObjectPath().value()
               << "': " << error->GetMessage();
    return;
  }
  if (!allowed) {
    LOG(ERROR) << "ADB port access on " << ifname << " not allowed";
    return;
  }

  if (datapath_->runner().sysctl_w(
          "net.ipv4.conf." + ifname + ".route_localnet", "1") != 0) {
    LOG(ERROR) << "Failed to set up route localnet for " << ifname;
    return;
  }

  lifeline_fds_.emplace(ifname, std::move(lifeline_write_fd));
}

void CrostiniService::StopAdbPortForwarding(const std::string& ifname) {
  lifeline_fds_.erase(ifname);
}

void CrostiniService::CheckAdbSideloadingStatus() {
  static int num_try = 0;
  if (num_try >= kAdbSideloadMaxTry) {
    LOG(WARNING) << "Failed to get ADB sideloading status after " << num_try
                 << " tries. ADB sideloading will not work";
    return;
  }

  dbus::ObjectProxy* proxy = bus_->GetObjectProxy(
      login_manager::kSessionManagerServiceName,
      dbus::ObjectPath(login_manager::kSessionManagerServicePath));
  dbus::MethodCall method_call(login_manager::kSessionManagerInterface,
                               login_manager::kSessionManagerQueryAdbSideload);
  std::unique_ptr<dbus::Response> dbus_response =
      proxy->CallMethodAndBlock(&method_call, kDbusTimeoutMs);

  if (!dbus_response) {
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&CrostiniService::CheckAdbSideloadingStatus,
                       weak_factory_.GetWeakPtr()),
        kAdbSideloadUpdateDelay);
    num_try++;
    return;
  }

  dbus::MessageReader reader(dbus_response.get());
  reader.PopBool(&adb_sideloading_enabled_);
  if (!adb_sideloading_enabled_)
    return;

  // If ADB sideloading is enabled, start ADB forwarding on all configured
  // Crostini's TAP interfaces.
  for (const auto& tap : taps_) {
    StartAdbPortForwarding(tap.second->phys_ifname());
  }
}

}  // namespace patchpanel
