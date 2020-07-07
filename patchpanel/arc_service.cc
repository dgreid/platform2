// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/arc_service.h"

#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>

#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>
#include <brillo/key_value_store.h>
#include <chromeos/constants/vm_tools.h>

#include "patchpanel/datapath.h"
#include "patchpanel/mac_address_generator.h"
#include "patchpanel/manager.h"
#include "patchpanel/minijailed_process_runner.h"
#include "patchpanel/net_util.h"
#include "patchpanel/scoped_ns.h"

namespace patchpanel {
namespace {
constexpr pid_t kInvalidPID = 0;
constexpr uint32_t kInvalidCID = 0;
constexpr char kArcNetnsName[] = "arc_netns";
constexpr char kArcIfname[] = "arc0";
constexpr char kArcBridge[] = "arcbr0";
constexpr std::array<const char*, 2> kEthernetInterfacePrefixes{{"eth", "usb"}};
constexpr std::array<const char*, 2> kWifiInterfacePrefixes{{"wlan", "mlan"}};
constexpr std::array<const char*, 2> kCellInterfacePrefixes{{"wwan", "rmnet"}};

bool KernelVersion(int* major, int* minor) {
  struct utsname u;
  if (uname(&u) != 0) {
    PLOG(ERROR) << "uname failed";
    *major = *minor = 0;
    return false;
  }
  int unused;
  if (sscanf(u.release, "%d.%d.%d", major, minor, &unused) != 3) {
    LOG(ERROR) << "unexpected release string: " << u.release;
    *major = *minor = 0;
    return false;
  }
  return true;
}

void OneTimeSetup(const Datapath& datapath) {
  static bool done = false;
  if (done)
    return;

  auto& runner = datapath.runner();

  // Load networking modules needed by Android that are not compiled in the
  // kernel. Android does not allow auto-loading of kernel modules.
  // Expected for all kernels.
  if (runner.modprobe_all({
          // The netfilter modules needed by netd for iptables commands.
          "ip6table_filter",
          "ip6t_ipv6header",
          "ip6t_REJECT",
          // The ipsec modules for AH and ESP encryption for ipv6.
          "ah6",
          "esp6",
      }) != 0) {
    LOG(ERROR) << "One or more required kernel modules failed to load."
               << " Some Android functionality may be broken.";
  }
  // The xfrm modules needed for Android's ipsec APIs on kernels < 5.4.
  int major, minor;
  if (KernelVersion(&major, &minor) &&
      (major < 5 || (major == 5 && minor < 4)) &&
      runner.modprobe_all({
          "xfrm4_mode_transport",
          "xfrm4_mode_tunnel",
          "xfrm6_mode_transport",
          "xfrm6_mode_tunnel",
      }) != 0) {
    LOG(ERROR) << "One or more required kernel modules failed to load."
               << " Some Android functionality may be broken.";
  }

  // Optional modules.
  if (runner.modprobe_all({
          // This module is not available in kernels < 3.18
          "nf_reject_ipv6",
          // These modules are needed for supporting Chrome traffic on Android
          // VPN which uses Android's NAT feature. Android NAT sets up
          // iptables
          // rules that use these conntrack modules for FTP/TFTP.
          "nf_nat_ftp",
          "nf_nat_tftp",
          // The tun module is needed by the Android 464xlat clatd process.
          "tun",
      }) != 0) {
    LOG(WARNING) << "One or more optional kernel modules failed to load.";
  }

  // This is only needed for CTS (b/27932574).
  if (runner.chown("655360", "655360", "/sys/class/xt_idletimer") != 0) {
    LOG(ERROR) << "Failed to change ownership of xt_idletimer.";
  }

  done = true;
}

ArcService::InterfaceType InterfaceTypeFor(const std::string& ifname) {
  for (const auto& prefix : kEthernetInterfacePrefixes) {
    if (base::StartsWith(ifname, prefix,
                         base::CompareCase::INSENSITIVE_ASCII)) {
      return ArcService::InterfaceType::ETHERNET;
    }
  }
  for (const auto& prefix : kWifiInterfacePrefixes) {
    if (base::StartsWith(ifname, prefix,
                         base::CompareCase::INSENSITIVE_ASCII)) {
      return ArcService::InterfaceType::WIFI;
    }
  }
  for (const auto& prefix : kCellInterfacePrefixes) {
    if (base::StartsWith(ifname, prefix,
                         base::CompareCase::INSENSITIVE_ASCII)) {
      return ArcService::InterfaceType::CELL;
    }
  }
  return ArcService::InterfaceType::UNKNOWN;
}

bool IsMulticastInterface(const std::string& ifname) {
  if (ifname.empty()) {
    return false;
  }

  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    // If IPv4 fails, try to open a socket using IPv6.
    fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd < 0) {
      LOG(ERROR) << "Unable to create socket";
      return false;
    }
  }

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ);
  if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
    PLOG(ERROR) << "SIOCGIFFLAGS failed for " << ifname;
    close(fd);
    return false;
  }

  close(fd);
  return (ifr.ifr_flags & IFF_MULTICAST);
}

// Returns the configuration for the ARC management interface used for VPN
// forwarding, ADB-over-TCP and single-networked ARCVM.
std::unique_ptr<Device::Config> MakeArcConfig(
    AddressManager* addr_mgr,
    AddressManager::Guest guest,
    GuestMessage::GuestType guest_type) {
  auto ipv4_subnet = addr_mgr->AllocateIPv4Subnet(guest);
  if (!ipv4_subnet) {
    LOG(ERROR) << "Subnet already in use or unavailable";
    return nullptr;
  }
  auto host_ipv4_addr = ipv4_subnet->AllocateAtOffset(0);
  if (!host_ipv4_addr) {
    LOG(ERROR) << "Bridge address already in use or unavailable";
    return nullptr;
  }
  auto guest_ipv4_addr = ipv4_subnet->AllocateAtOffset(1);
  if (!guest_ipv4_addr) {
    LOG(ERROR) << "ARC address already in use or unavailable";
    return nullptr;
  }

  int subnet_index = (guest_type == GuestMessage::ARC_VM) ? 1 : kAnySubnetIndex;

  return std::make_unique<Device::Config>(
      addr_mgr->GenerateMacAddress(subnet_index), std::move(ipv4_subnet),
      std::move(host_ipv4_addr), std::move(guest_ipv4_addr));
}

}  // namespace

ArcService::ArcService(ShillClient* shill_client,
                       Datapath* datapath,
                       AddressManager* addr_mgr,
                       TrafficForwarder* forwarder,
                       GuestMessage::GuestType guest)
    : shill_client_(shill_client),
      datapath_(datapath),
      addr_mgr_(addr_mgr),
      forwarder_(forwarder),
      guest_(guest) {
  AllocateAddressConfigs();
  shill_client_->RegisterDevicesChangedHandler(
      base::Bind(&ArcService::OnDevicesChanged, weak_factory_.GetWeakPtr()));
  shill_client_->ScanDevices();
}

ArcService::~ArcService() {
  if (impl_) {
    Stop(impl_->id());
  }
}

void ArcService::AllocateAddressConfigs() {
  available_configs_.clear();
  // The first usable subnet is the "other" ARC device subnet.
  // As a temporary workaround, for ARCVM, allocate fixed MAC addresses.
  uint8_t mac_addr_index = 2;
  // Allocate 2 subnets each for Ethernet and WiFi and 1 for LTE WAN interfaces.
  for (const auto itype :
       {InterfaceType::ETHERNET, InterfaceType::ETHERNET, InterfaceType::WIFI,
        InterfaceType::WIFI, InterfaceType::CELL}) {
    auto ipv4_subnet =
        addr_mgr_->AllocateIPv4Subnet(AddressManager::Guest::ARC_NET);
    if (!ipv4_subnet) {
      LOG(ERROR) << "Subnet already in use or unavailable";
      continue;
    }
    // For here out, use the same slices.
    auto host_ipv4_addr = ipv4_subnet->AllocateAtOffset(0);
    if (!host_ipv4_addr) {
      LOG(ERROR) << "Bridge address already in use or unavailable";
      continue;
    }
    auto guest_ipv4_addr = ipv4_subnet->AllocateAtOffset(1);
    if (!guest_ipv4_addr) {
      LOG(ERROR) << "ARC address already in use or unavailable";
      continue;
    }

    MacAddress mac_addr = (guest_ == GuestMessage::ARC_VM)
                              ? addr_mgr_->GenerateMacAddress(mac_addr_index++)
                              : addr_mgr_->GenerateMacAddress();
    available_configs_[itype].emplace_back(std::make_unique<Device::Config>(
        mac_addr, std::move(ipv4_subnet), std::move(host_ipv4_addr),
        std::move(guest_ipv4_addr)));
  }
}

void ArcService::ReallocateAddressConfigs() {
  std::vector<std::string> existing_devices;
  for (const auto& d : devices_) {
    existing_devices.emplace_back(d.first);
  }
  for (const auto& d : existing_devices) {
    RemoveDevice(d);
  }
  AllocateAddressConfigs();
  all_configs_.clear();
  for (const auto& kv : available_configs_)
    for (const auto& c : kv.second)
      all_configs_.emplace_back(c.get());
  for (const auto& d : existing_devices) {
    AddDevice(d);
  }
}

std::unique_ptr<Device::Config> ArcService::AcquireConfig(
    const std::string& ifname) {
  auto itype = InterfaceTypeFor(ifname);
  if (itype == InterfaceType::UNKNOWN) {
    LOG(ERROR) << "Unsupported interface: " << ifname;
    return nullptr;
  }

  auto& configs = available_configs_[itype];
  if (configs.empty()) {
    LOG(ERROR) << "No more addresses available. Cannot make device for "
               << ifname;
    return nullptr;
  }
  std::unique_ptr<Device::Config> config;
  config = std::move(configs.front());
  configs.pop_front();
  return config;
}

void ArcService::ReleaseConfig(const std::string& ifname,
                               std::unique_ptr<Device::Config> config) {
  auto itype = InterfaceTypeFor(ifname);
  if (itype == InterfaceType::UNKNOWN) {
    LOG(ERROR) << "Unsupported interface: " << ifname;
    return;
  }

  available_configs_[itype].push_front(std::move(config));
}

bool ArcService::Start(uint32_t id) {
  if (impl_) {
    uint32_t prev_id;
    if (impl_->IsStarted(&prev_id)) {
      LOG(WARNING) << "Already running - did something crash?"
                   << " Stopping and restarting...";
      Stop(prev_id);
    }
  }

  ReallocateAddressConfigs();
  std::unique_ptr<Device::Config> arc_device_config =
      MakeArcConfig(addr_mgr_, AddressManager::Guest::ARC, guest_);
  if (guest_ == GuestMessage::ARC_VM) {
    // Append arc0 config separately from ArcService::ReallocateAddressConfigs()
    // so that VmImpl::Start() can create the necessary tap device.
    all_configs_.insert(all_configs_.begin(), arc_device_config.get());
    // Allocate TAP devices for all configs.
    for (auto* config : all_configs_) {
      auto mac = config->mac_addr();
      auto tap = datapath_->AddTAP("" /* auto-generate name */, &mac,
                                   nullptr /* no ipv4 subnet */,
                                   vm_tools::kCrosVmUser);
      if (tap.empty()) {
        LOG(ERROR) << "Failed to create TAP device";
        continue;
      }

      config->set_tap_ifname(tap);
    }
    impl_ = std::make_unique<VmImpl>(datapath_);
  } else {
    impl_ = std::make_unique<ContainerImpl>(datapath_);
    if (!datapath_->NetnsAttachName(kArcNetnsName, id)) {
      LOG(ERROR) << "Failed to attach name " << kArcNetnsName << " to pid "
                 << id;
      return false;
    }
  }
  if (!impl_->Start(id)) {
    impl_.reset();
    return false;
  }

  // Create the bridge for the management device arc0.
  // Per crbug/1008686 this device cannot be deleted and then re-added.
  // So instead of removing the bridge when the service stops, bring down the
  // device instead and re-up it on restart.
  if (!datapath_->AddBridge(kArcBridge, arc_device_config->host_ipv4_addr(),
                            30) &&
      !datapath_->MaskInterfaceFlags(kArcBridge, IFF_UP)) {
    LOG(ERROR) << "Failed to bring up arc bridge " << kArcBridge;
    return false;
  }

  Device::Options opts{
      .fwd_multicast = false,
      .ipv6_enabled = false,
  };
  arc_device_ = std::make_unique<Device>(kArcIfname, kArcBridge, kArcIfname,
                                         std::move(arc_device_config), opts);

  std::string arc_device_ifname;
  if (guest_ == GuestMessage::ARC_VM) {
    arc_device_ifname = arc_device_->config().tap_ifname();
  } else {
    arc_device_ifname = ArcVethHostName(arc_device_->guest_ifname());
    if (!datapath_->ConnectVethPair(
            impl_->id(), kArcNetnsName, arc_device_ifname,
            arc_device_->guest_ifname(), arc_device_->config().mac_addr(),
            arc_device_->config().guest_ipv4_addr(), 30,
            arc_device_->options().fwd_multicast)) {
      LOG(ERROR) << "Cannot create virtual link for device "
                 << arc_device_->phys_ifname();
      return false;
    }
  }

  if (!datapath_->AddToBridge(kArcBridge, arc_device_ifname)) {
    LOG(ERROR) << "Failed to bridge arc device " << arc_device_ifname << " to "
               << kArcBridge;
    return false;
  }
  LOG(INFO) << "Started ARC management device " << *arc_device_.get();

  // Start already known Shill <-> ARC mapped devices.
  for (const auto& d : devices_)
    StartDevice(d.second.get());

  return true;
}

void ArcService::Stop(uint32_t id) {
  // Stop Shill <-> ARC mapped devices.
  for (const auto& d : devices_)
    StopDevice(d.second.get());

  // Per crbug/1008686 this device cannot be deleted and then re-added.
  // So instead of removing the bridge, bring it down and mark it. This will
  // allow us to detect if the device is re-added in case of a crash restart
  // and do the right thing.
  if (!datapath_->MaskInterfaceFlags(kArcBridge, IFF_DEBUG, IFF_UP))
    LOG(ERROR) << "Failed to bring down arc bridge "
               << "- it may not restart correctly";

  if (guest_ == GuestMessage::ARC) {
    datapath_->RemoveInterface(ArcVethHostName(arc_device_->phys_ifname()));
    if (!datapath_->NetnsDeleteName(kArcNetnsName))
      LOG(WARNING) << "Failed to delete netns name " << kArcNetnsName;
  } else {
    // Destroy pre allocated TAP devices, including the ARC management device.
    for (const auto* config : all_configs_)
      if (!config->tap_ifname().empty())
        datapath_->RemoveInterface(config->tap_ifname());
  }

  if (impl_) {
    impl_->Stop(id);
    impl_.reset();
  }

  LOG(INFO) << "Stopped ARC management device " << *arc_device_.get();
  arc_device_.reset();
}

void ArcService::OnDevicesChanged(const std::set<std::string>& added,
                                  const std::set<std::string>& removed) {
  for (const std::string& name : removed)
    RemoveDevice(name);

  for (const std::string& name : added)
    AddDevice(name);
}

void ArcService::AddDevice(const std::string& ifname) {
  if (ifname.empty())
    return;

  if (devices_.find(ifname) != devices_.end()) {
    LOG(DFATAL) << "Attemping to add already tracked device: " << ifname;
    return;
  }

  auto itype = InterfaceTypeFor(ifname);
  Device::Options opts{
      .fwd_multicast = IsMulticastInterface(ifname),
      // TODO(crbug/726815) Also enable |ipv6_enabled| for cellular networks
      // once IPv6 is enabled on cellular networks in shill.
      .ipv6_enabled =
          (itype == InterfaceType::ETHERNET || itype == InterfaceType::WIFI),
  };

  auto config = AcquireConfig(ifname);
  if (!config) {
    LOG(ERROR) << "Cannot add device for " << ifname;
    return;
  }

  auto device = std::make_unique<Device>(ifname, ArcBridgeName(ifname), ifname,
                                         std::move(config), opts);

  StartDevice(device.get());
  devices_.emplace(ifname, std::move(device));
}

void ArcService::StartDevice(Device* device) {
  if (!impl_ || !impl_->IsStarted())
    return;

  const auto& config = device->config();

  LOG(INFO) << "Starting device " << *device;

  // Create the bridge.
  if (!datapath_->AddBridge(device->host_ifname(), config.host_ipv4_addr(),
                            30)) {
    LOG(ERROR) << "Failed to setup arc bridge: " << device->host_ifname();
    return;
  }

  // Set up iptables.
  if (!datapath_->AddInboundIPv4DNAT(
          device->phys_ifname(), IPv4AddressToString(config.guest_ipv4_addr())))
    LOG(ERROR) << "Failed to configure ingress traffic rules for "
               << device->phys_ifname();

  if (!datapath_->AddOutboundIPv4(device->host_ifname()))
    LOG(ERROR) << "Failed to configure egress traffic rules for "
               << device->phys_ifname();

  if (!impl_->OnStartDevice(device)) {
    LOG(ERROR) << "Failed to start device " << device->phys_ifname();
    return;
  }

  forwarder_->StartForwarding(device->phys_ifname(), device->host_ifname(),
                              device->options().ipv6_enabled,
                              device->options().fwd_multicast);
}

void ArcService::RemoveDevice(const std::string& ifname) {
  const auto it = devices_.find(ifname);
  if (it == devices_.end()) {
    LOG(WARNING) << "Unknown device: " << ifname;
    return;
  }

  // If the container is down, this call does nothing.
  StopDevice(it->second.get());

  ReleaseConfig(ifname, it->second->release_config());
  devices_.erase(it);
}

void ArcService::StopDevice(Device* device) {
  if (!impl_ || !impl_->IsStarted())
    return;

  LOG(INFO) << "Removing device " << *device;

  forwarder_->StopForwarding(device->phys_ifname(), device->host_ifname(),
                             device->options().ipv6_enabled,
                             device->options().fwd_multicast);
  // TAP devices are removed in VmImpl::Stop().
  if (guest_ == GuestMessage::ARC)
    datapath_->RemoveInterface(ArcVethHostName(device->phys_ifname()));

  const auto& config = device->config();
  datapath_->RemoveOutboundIPv4(device->host_ifname());
  datapath_->RemoveInboundIPv4DNAT(
      device->phys_ifname(), IPv4AddressToString(config.guest_ipv4_addr()));
  datapath_->RemoveBridge(device->host_ifname());
}

std::vector<const Device::Config*> ArcService::GetDeviceConfigs() const {
  std::vector<const Device::Config*> configs;
  for (auto* c : all_configs_)
    configs.emplace_back(c);

  return configs;
}

// ARC++ specific functions.

ArcService::ContainerImpl::ContainerImpl(Datapath* datapath)
    : pid_(kInvalidPID), datapath_(datapath) {
  OneTimeSetup(*datapath_);
}

uint32_t ArcService::ContainerImpl::id() const {
  return pid_;
}

bool ArcService::ContainerImpl::Start(uint32_t pid) {
  // This could happen if something crashes and the stop signal is not sent.
  // It can probably be addressed by stopping and restarting the service.
  if (pid_ != kInvalidPID)
    return false;

  if (pid == kInvalidPID) {
    LOG(ERROR) << "Cannot start service - invalid container PID";
    return false;
  }
  pid_ = pid;

  LOG(INFO) << "ARC++ network service started {pid: " << pid_ << "}";
  return true;
}

void ArcService::ContainerImpl::Stop(uint32_t /*pid*/) {
  if (!IsStarted())
    return;

  LOG(INFO) << "ARC++ network service stopped {pid: " << pid_ << "}";
  pid_ = kInvalidPID;
}

bool ArcService::ContainerImpl::IsStarted(uint32_t* pid) const {
  if (pid)
    *pid = pid_;

  return pid_ != kInvalidPID;
}

bool ArcService::ContainerImpl::OnStartDevice(Device* device) {
  // Set up the virtual pair inside the container namespace.
  const std::string veth_ifname = ArcVethHostName(device->guest_ifname());
  const auto& config = device->config();
  if (!datapath_->ConnectVethPair(pid_, kArcNetnsName, veth_ifname,
                                  device->guest_ifname(), config.mac_addr(),
                                  config.guest_ipv4_addr(), 30,
                                  device->options().fwd_multicast)) {
    LOG(ERROR) << "Cannot create virtual link for device "
               << device->phys_ifname();
    return false;
  }
  if (!datapath_->AddToBridge(device->host_ifname(), veth_ifname)) {
    datapath_->RemoveInterface(veth_ifname);
    LOG(ERROR) << "Failed to bridge interface " << veth_ifname;
    return false;
  }
  return true;
}

// VM specific functions

ArcService::VmImpl::VmImpl(Datapath* datapath)
    : cid_(kInvalidCID), datapath_(datapath) {}

uint32_t ArcService::VmImpl::id() const {
  return cid_;
}

bool ArcService::VmImpl::Start(uint32_t cid) {
  // This can happen if concierge crashes and doesn't send the vm down RPC.
  // It can probably be addressed by stopping and restarting the service.
  if (cid_ != kInvalidCID)
    return false;

  if (cid == kInvalidCID) {
    LOG(ERROR) << "Invalid VM cid " << cid;
    return false;
  }
  cid_ = cid;

  LOG(INFO) << "ARCVM network service started {cid: " << cid_ << "}";
  return true;
}

void ArcService::VmImpl::Stop(uint32_t cid) {
  if (cid_ != cid) {
    LOG(ERROR) << "Mismatched ARCVM CIDs " << cid_ << " != " << cid;
    return;
  }

  LOG(INFO) << "ARCVM network service stopped {cid: " << cid_ << "}";
  cid_ = kInvalidCID;
}

bool ArcService::VmImpl::IsStarted(uint32_t* cid) const {
  if (cid)
    *cid = cid_;

  return cid_ != kInvalidCID;
}

bool ArcService::VmImpl::OnStartDevice(Device* device) {
  const std::string& tap = device->config().tap_ifname();
  if (tap.empty()) {
    LOG(ERROR) << "No TAP device for: " << *device;
    return false;
  }
  if (!datapath_->AddToBridge(device->host_ifname(), tap)) {
    LOG(ERROR) << "Failed to bridge TAP device " << tap;
    return false;
  }
  return true;
}
}  // namespace patchpanel
