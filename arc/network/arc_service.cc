// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/arc_service.h"

#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/key_value_store.h>
#include <chromeos/constants/vm_tools.h>

#include "arc/network/datapath.h"
#include "arc/network/mac_address_generator.h"
#include "arc/network/manager.h"
#include "arc/network/minijailed_process_runner.h"
#include "arc/network/net_util.h"
#include "arc/network/scoped_ns.h"

namespace arc_networkd {
namespace test {
GuestMessage::GuestType guest = GuestMessage::UNKNOWN_GUEST;
}  // namespace test

namespace {
constexpr pid_t kInvalidPID = 0;
constexpr pid_t kTestPID = -2;
constexpr uint32_t kInvalidCID = 0;
constexpr char kArcIfname[] = "arc0";
constexpr char kArcBridge[] = "arcbr0";
constexpr char kArcVmIfname[] = "arc1";
constexpr char kArcVmBridge[] = "arc_br1";
constexpr std::array<const char*, 2> kEthernetInterfacePrefixes{{"eth", "usb"}};
constexpr std::array<const char*, 2> kWifiInterfacePrefixes{{"wlan", "mlan"}};

void OneTimeSetup(const Datapath& datapath) {
  static bool done = false;
  if (done)
    return;

  auto& runner = datapath.runner();

  // Load networking modules needed by Android that are not compiled in the
  // kernel. Android does not allow auto-loading of kernel modules.
  // These must succeed.
  if (runner.modprobe_all({
          // The netfilter modules needed by netd for iptables commands.
          "ip6table_filter",
          "ip6t_ipv6header",
          "ip6t_REJECT",
          // The xfrm modules needed for Android's ipsec APIs.
          "xfrm4_mode_transport",
          "xfrm4_mode_tunnel",
          "xfrm6_mode_transport",
          "xfrm6_mode_tunnel",
          // The ipsec modules for AH and ESP encryption for ipv6.
          "ah6",
          "esp6",
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

bool IsArcVm() {
  const base::FilePath path("/run/chrome/is_arcvm");
  std::string contents;
  if (!base::ReadFileToString(path, &contents)) {
    PLOG(ERROR) << "Could not read " << path.value();
  }
  return contents == "1";
}

GuestMessage::GuestType ArcGuest() {
  if (test::guest != GuestMessage::UNKNOWN_GUEST)
    return test::guest;

  return IsArcVm() ? GuestMessage::ARC_VM : GuestMessage::ARC;
}

bool IsEthernetInterface(const std::string& ifname) {
  for (const auto& prefix : kEthernetInterfacePrefixes) {
    if (base::StartsWith(ifname, prefix,
                         base::CompareCase::INSENSITIVE_ASCII)) {
      return true;
    }
  }
  return false;
}

bool IsWifiInterface(const std::string& ifname) {
  for (const auto& prefix : kWifiInterfacePrefixes) {
    if (base::StartsWith(ifname, prefix,
                         base::CompareCase::INSENSITIVE_ASCII)) {
      return true;
    }
  }
  return false;
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
std::unique_ptr<Device::Config> MakeArcConfig(AddressManager* addr_mgr,
                                              bool is_arcvm) {
  const char* ifname = is_arcvm ? kArcVmBridge : kArcBridge;
  auto ipv4_subnet = addr_mgr->AllocateIPv4Subnet(
      is_arcvm ? AddressManager::Guest::VM_ARC : AddressManager::Guest::ARC);
  if (!ipv4_subnet) {
    LOG(ERROR) << "Subnet already in use or unavailable. Cannot make device: "
               << ifname;
    return nullptr;
  }
  auto host_ipv4_addr = ipv4_subnet->AllocateAtOffset(0);
  if (!host_ipv4_addr) {
    LOG(ERROR)
        << "Bridge address already in use or unavailable. Cannot make device: "
        << ifname;
    return nullptr;
  }
  auto guest_ipv4_addr = ipv4_subnet->AllocateAtOffset(1);
  if (!guest_ipv4_addr) {
    LOG(ERROR)
        << "ARC address already in use or unavailable. Cannot make device: "
        << ifname;
    return nullptr;
  }

  return std::make_unique<Device::Config>(
      ifname, is_arcvm ? kArcVmIfname : kArcIfname,
      addr_mgr->GenerateMacAddress(), std::move(ipv4_subnet),
      std::move(host_ipv4_addr), std::move(guest_ipv4_addr));
}

}  // namespace

ArcService::ArcService(ShillClient* shill_client,
                       Datapath* datapath,
                       AddressManager* addr_mgr,
                       TrafficForwarder* forwarder)
    : shill_client_(shill_client),
      datapath_(datapath),
      addr_mgr_(addr_mgr),
      forwarder_(forwarder) {
  shill_client_->RegisterDevicesChangedHandler(
      base::Bind(&ArcService::OnDevicesChanged, weak_factory_.GetWeakPtr()));
  shill_client_->ScanDevices(
      base::Bind(&ArcService::OnDevicesChanged, weak_factory_.GetWeakPtr()));
  shill_client_->RegisterDefaultInterfaceChangedHandler(base::Bind(
      &ArcService::OnDefaultInterfaceChanged, weak_factory_.GetWeakPtr()));
}

ArcService::~ArcService() {
  if (impl_) {
    Stop(impl_->id());
  }
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

  const auto guest = ArcGuest();
  if (guest == GuestMessage::ARC_VM)
    impl_ = std::make_unique<VmImpl>(shill_client_, datapath_, addr_mgr_,
                                     forwarder_);
  else
    impl_ = std::make_unique<ContainerImpl>(datapath_, addr_mgr_, forwarder_,
                                            guest);

  if (!impl_->Start(id)) {
    impl_.reset();
    return false;
  }

  // Start already known Shill <-> ARC mapped devices.
  for (const auto& d : devices_)
    StartDevice(d.second.get());

  return true;
}

void ArcService::Stop(uint32_t id) {
  // Stop Shill <-> ARC mapped devices.
  for (const auto& d : devices_)
    StopDevice(d.second.get());

  if (impl_) {
    impl_->Stop(id);
    impl_.reset();
  }
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

  Device::Options opts{
      .fwd_multicast = IsMulticastInterface(ifname),
      // TODO(crbug/726815) Also enable |ipv6_enabled| for cellular networks
      // once IPv6 is enabled on cellular networks in shill.
      .ipv6_enabled = IsEthernetInterface(ifname) || IsWifiInterface(ifname),
      .use_default_interface = false,
  };
  std::string host_ifname = base::StringPrintf("arc_%s", ifname.c_str());
  auto ipv4_subnet =
      addr_mgr_->AllocateIPv4Subnet(AddressManager::Guest::ARC_NET);
  if (!ipv4_subnet) {
    LOG(ERROR) << "Subnet already in use or unavailable. Cannot make device: "
               << ifname;
    return;
  }
  auto host_ipv4_addr = ipv4_subnet->AllocateAtOffset(0);
  if (!host_ipv4_addr) {
    LOG(ERROR)
        << "Bridge address already in use or unavailable. Cannot make device: "
        << ifname;
    return;
  }
  auto guest_ipv4_addr = ipv4_subnet->AllocateAtOffset(1);
  if (!guest_ipv4_addr) {
    LOG(ERROR)
        << "ARC address already in use or unavailable. Cannot make device: "
        << ifname;
    return;
  }

  auto config = std::make_unique<Device::Config>(
      host_ifname, ifname, addr_mgr_->GenerateMacAddress(),
      std::move(ipv4_subnet), std::move(host_ipv4_addr),
      std::move(guest_ipv4_addr));

  auto device = std::make_unique<Device>(ifname, std::move(config), opts);

  StartDevice(device.get());
  devices_.emplace(ifname, std::move(device));
}

void ArcService::StartDevice(Device* device) {
  if (!impl_ || !impl_->IsStarted())
    return;

  // For now, only start devices for ARC++.
  if (impl_->guest() != GuestMessage::ARC)
    return;

  const auto& config = device->config();

  LOG(INFO) << "Adding device " << device->ifname()
            << " bridge: " << config.host_ifname()
            << " guest_iface: " << config.guest_ifname();

  // Create the bridge.
  if (!datapath_->AddBridge(config.host_ifname(), config.host_ipv4_addr(),
                            30)) {
    LOG(ERROR) << "Failed to setup arc bridge: " << config.host_ifname();
    return;
  }

  // Set up iptables.
  if (!datapath_->AddInboundIPv4DNAT(
          device->ifname(), IPv4AddressToString(config.guest_ipv4_addr())))
    LOG(ERROR) << "Failed to configure ingress traffic rules for "
               << device->ifname();

  if (!datapath_->AddOutboundIPv4(config.host_ifname()))
    LOG(ERROR) << "Failed to configure egress traffic rules for "
               << device->ifname();

  if (!impl_->OnStartDevice(device)) {
    LOG(ERROR) << "Failed to start device " << device->ifname();
  }
}

void ArcService::RemoveDevice(const std::string& ifname) {
  const auto it = devices_.find(ifname);
  if (it == devices_.end()) {
    LOG(WARNING) << "Unknown device: " << ifname;
    return;
  }

  // If the container is down, this call does nothing.
  StopDevice(it->second.get());

  devices_.erase(it);
}

void ArcService::StopDevice(Device* device) {
  if (!impl_ || !impl_->IsStarted())
    return;

  // For now, devices are only started for ARC++.
  if (impl_->guest() != GuestMessage::ARC)
    return;

  impl_->OnStopDevice(device);

  const auto& config = device->config();

  LOG(INFO) << "Removing device " << device->ifname()
            << " bridge: " << config.host_ifname()
            << " guest_iface: " << config.guest_ifname();

  datapath_->RemoveOutboundIPv4(config.host_ifname());
  datapath_->RemoveInboundIPv4DNAT(
      device->ifname(), IPv4AddressToString(config.guest_ipv4_addr()));

  datapath_->RemoveBridge(config.host_ifname());
}

void ArcService::OnDefaultInterfaceChanged(const std::string& new_ifname,
                                           const std::string& prev_ifname) {
  if (impl_)
    impl_->OnDefaultInterfaceChanged(new_ifname, prev_ifname);
}

Device* ArcService::ArcDevice() const {
  if (!impl_)
    return nullptr;

  return impl_->ArcDevice();
}

// ARC++ specific functions.

ArcService::ContainerImpl::ContainerImpl(Datapath* datapath,
                                         AddressManager* addr_mgr,
                                         TrafficForwarder* forwarder,
                                         GuestMessage::GuestType guest)
    : pid_(kInvalidPID),
      datapath_(datapath),
      addr_mgr_(addr_mgr),
      forwarder_(forwarder),
      guest_(guest) {
  OneTimeSetup(*datapath_);
}

GuestMessage::GuestType ArcService::ContainerImpl::guest() const {
  return guest_;
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

  Device::Options opts{
      .fwd_multicast = false,
      .ipv6_enabled = false,
      .use_default_interface = false,
  };
  auto config = MakeArcConfig(addr_mgr_, false /*is_arcvm*/);

  // Create the bridge.
  // Per crbug/1008686 this device cannot be deleted and then re-added.
  // So instead of removing the bridge when the service stops, bring down the
  // device instead and re-up it on restart.
  if (!datapath_->AddBridge(kArcBridge, config->host_ipv4_addr(), 30) &&
      !datapath_->MaskInterfaceFlags(kArcBridge, IFF_UP)) {
    LOG(ERROR) << "Failed to bring up arc bridge: " << kArcBridge;
    return false;
  }

  arc_device_ = std::make_unique<Device>(kArcIfname, std::move(config), opts);

  OnStartDevice(arc_device_.get());

  LOG(INFO) << "ARC++ network service started {pid: " << pid_ << "}";
  return true;
}

void ArcService::ContainerImpl::Stop(uint32_t /*pid*/) {
  if (!IsStarted())
    return;

  // Per crbug/1008686 this device cannot be deleted and then re-added.
  // So instead of removing the bridge, bring it down and mark it. This will
  // allow us to detect if the device is re-added in case of a crash restart
  // and do the right thing.
  if (arc_device_) {
    OnStopDevice(arc_device_.get());
    if (!datapath_->MaskInterfaceFlags(kArcBridge, IFF_DEBUG, IFF_UP))
      LOG(ERROR) << "Failed to bring down arc bridge "
                 << "- it may not restart correctly";
  }

  LOG(INFO) << "ARC++ network service stopped {pid: " << pid_ << "}";
  pid_ = kInvalidPID;
}

bool ArcService::ContainerImpl::IsStarted(uint32_t* pid) const {
  if (pid)
    *pid = pid_;

  return pid_ != kInvalidPID;
}

bool ArcService::ContainerImpl::OnStartDevice(Device* device) {
  const auto& config = device->config();

  LOG(INFO) << "Starting device " << device->ifname()
            << " bridge: " << config.host_ifname()
            << " guest_iface: " << config.guest_ifname() << " pid: " << pid_;

  // Set up the virtual pair inside the container namespace.
  const std::string veth_ifname = ArcVethHostName(config.guest_ifname());
  {
    ScopedNS ns(pid_);
    if (!ns.IsValid() && pid_ != kTestPID) {
      LOG(ERROR)
          << "Cannot create virtual link -- invalid container namespace?";
      return false;
    }

    if (!datapath_->AddVirtualInterfacePair(veth_ifname,
                                            config.guest_ifname())) {
      LOG(ERROR) << "Failed to create virtual interface pair for "
                 << device->ifname();
      return false;
    }

    if (!datapath_->ConfigureInterface(
            config.guest_ifname(), config.guest_mac_addr(),
            config.guest_ipv4_addr(), 30, true /* link up */,
            device->options().fwd_multicast)) {
      LOG(ERROR) << "Failed to configure interface " << config.guest_ifname();
      datapath_->RemoveInterface(config.guest_ifname());
      return false;
    }
  }

  // Now pull the host end out into the root namespace and add it to the bridge.
  if (datapath_->runner().RestoreDefaultNamespace(veth_ifname, pid_) != 0) {
    LOG(ERROR) << "Failed to prepare interface " << veth_ifname;
    {
      ScopedNS ns(pid_);
      if (ns.IsValid()) {
        datapath_->RemoveInterface(config.guest_ifname());
      } else {
        LOG(ERROR) << "Failed to re-enter container namespace."
                   << " Subsequent attempts to restart " << device->ifname()
                   << " may not succeed.";
      }
    }
    return false;
  }
  if (!datapath_->ToggleInterface(veth_ifname, true /*up*/)) {
    LOG(ERROR) << "Failed to bring up interface " << veth_ifname;
    datapath_->RemoveInterface(veth_ifname);
    return false;
  }
  if (!datapath_->AddToBridge(config.host_ifname(), veth_ifname)) {
    datapath_->RemoveInterface(veth_ifname);
    LOG(ERROR) << "Failed to bridge interface " << veth_ifname;
    return false;
  }

  if (device != arc_device_.get())
    forwarder_->StartForwarding(
        device->ifname(), device->config().host_ifname(),
        device->options().ipv6_enabled, device->options().fwd_multicast);

  return true;
}

void ArcService::ContainerImpl::OnStopDevice(Device* device) {
  const auto& config = device->config();

  LOG(INFO) << "Stopping device " << device->ifname()
            << " bridge: " << config.host_ifname()
            << " guest_iface: " << config.guest_ifname() << " pid: " << pid_;

  if (device != arc_device_.get())
    forwarder_->StopForwarding(device->ifname(), device->config().host_ifname(),
                               device->options().ipv6_enabled,
                               device->options().fwd_multicast);

  datapath_->RemoveInterface(ArcVethHostName(device->ifname()));
}

void ArcService::ContainerImpl::OnDefaultInterfaceChanged(
    const std::string& new_ifname, const std::string& prev_ifname) {}

// VM specific functions

ArcService::VmImpl::VmImpl(ShillClient* shill_client,
                           Datapath* datapath,
                           AddressManager* addr_mgr,
                           TrafficForwarder* forwarder)
    : cid_(kInvalidCID),
      shill_client_(shill_client),
      datapath_(datapath),
      addr_mgr_(addr_mgr),
      forwarder_(forwarder) {}

GuestMessage::GuestType ArcService::VmImpl::guest() const {
  return GuestMessage::ARC_VM;
}

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

  Device::Options opts{
      .fwd_multicast = true,
      .ipv6_enabled = true,
      .use_default_interface = true,
  };
  auto config = MakeArcConfig(addr_mgr_, true /*is_arcvm*/);

  // Create the bridge.
  if (!datapath_->AddBridge(kArcVmBridge, config->host_ipv4_addr(), 30)) {
    LOG(ERROR) << "Failed to setup arc bridge: " << kArcVmBridge;
    return false;
  }

  // Setup the iptables.
  if (!datapath_->AddLegacyIPv4DNAT(
          IPv4AddressToString(config->guest_ipv4_addr())))
    LOG(ERROR) << "Failed to configure ARC traffic rules";

  if (!datapath_->AddOutboundIPv4(kArcVmBridge))
    LOG(ERROR) << "Failed to configure egress traffic rules";

  arc_device_ = std::make_unique<Device>(kArcVmIfname, std::move(config), opts);

  OnStartDevice(arc_device_.get());

  LOG(INFO) << "ARCVM network service started {cid: " << cid_ << "}";
  return true;
}

void ArcService::VmImpl::Stop(uint32_t cid) {
  if (cid_ != cid) {
    LOG(ERROR) << "Mismatched ARCVM CIDs " << cid_ << " != " << cid;
    return;
  }

  datapath_->RemoveOutboundIPv4(kArcVmBridge);
  datapath_->RemoveLegacyIPv4DNAT();
  OnStopDevice(arc_device_.get());
  datapath_->RemoveBridge(kArcVmBridge);
  arc_device_.reset();

  LOG(INFO) << "ARCVM network service stopped {cid: " << cid_ << "}";
  cid_ = kInvalidCID;
}

bool ArcService::VmImpl::IsStarted(uint32_t* cid) const {
  if (cid)
    *cid = cid_;

  return cid_ != kInvalidCID;
}

bool ArcService::VmImpl::OnStartDevice(Device* device) {
  // TODO(garrick): Remove this once ARCVM supports ad hoc interface
  // configurations.
  if (!device->UsesDefaultInterface())
    return false;

  const auto& config = device->config();

  LOG(INFO) << "Starting device " << device->ifname()
            << " bridge: " << config.host_ifname()
            << " guest_iface: " << config.guest_ifname() << " cid: " << cid_;

  // Since the interface will be added to the bridge, no address configuration
  // should be provided here.
  std::string tap =
      datapath_->AddTAP("" /* auto-generate name */, nullptr /* no mac addr */,
                        nullptr /* no ipv4 subnet */, vm_tools::kCrosVmUser);
  if (tap.empty()) {
    LOG(ERROR) << "Failed to create TAP device for VM";
    return false;
  }

  if (!datapath_->AddToBridge(config.host_ifname(), tap)) {
    LOG(ERROR) << "Failed to bridge TAP device " << tap;
    datapath_->RemoveInterface(tap);
    return false;
  }

  // TODO(garrick): Remove this once ARCVM supports ad hoc interface
  // configurations; but for now ARCVM needs to be treated like ARC++ N.
  OnDefaultInterfaceChanged(shill_client_->default_interface(),
                            "" /*previous*/);

  device->set_tap_ifname(tap);
  return true;
}

void ArcService::VmImpl::OnStopDevice(Device* device) {
  // TODO(garrick): Remove this once ARCVM supports ad hoc interface
  // configurations.
  if (!device->UsesDefaultInterface())
    return;

  const auto& config = device->config();

  LOG(INFO) << "Stopping " << device->ifname()
            << " bridge: " << config.host_ifname()
            << " guest_iface: " << config.guest_ifname() << " cid: " << cid_;

  // TODO(garrick): Remove this once ARCVM supports ad hoc interface
  // configurations; but for now ARCVM needs to be treated like ARC++ N.
  OnDefaultInterfaceChanged("" /*new_ifname*/,
                            shill_client_->default_interface());

  datapath_->RemoveInterface(device->tap_ifname());
  device->set_tap_ifname("");
}

void ArcService::VmImpl::OnDefaultInterfaceChanged(
    const std::string& new_ifname, const std::string& prev_ifname) {
  if (!IsStarted())
    return;

  forwarder_->StopForwarding(prev_ifname, kArcVmBridge, true /*ipv6*/,
                             true /*multicast*/);

  // TODO(garrick): Remove this once ARCVM supports ad hoc interface
  // configurations; but for now ARCVM needs to be treated like ARC++ N.
  datapath_->RemoveLegacyIPv4InboundDNAT();

  // If a new default interface was given, then re-enable with that.
  if (!new_ifname.empty()) {
    datapath_->AddLegacyIPv4InboundDNAT(new_ifname);
    forwarder_->StartForwarding(new_ifname, kArcVmBridge, true /*ipv6*/,
                                true /*multicast*/);
  }
}

}  // namespace arc_networkd
