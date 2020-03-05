// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/arc_service.h"

#include <linux/rtnetlink.h>
#include <net/if.h>

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
#include <shill/net/rtnl_message.h>

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

// This wrapper is required since the base class is a singleton that hides its
// constructor. It is necessary here because the message loop thread has to be
// reassociated to the container's network namespace; and since the container
// can be repeatedly created and destroyed, the handler must be as well.
class RTNetlinkHandler : public shill::RTNLHandler {
 public:
  RTNetlinkHandler() = default;
  ~RTNetlinkHandler() = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(RTNetlinkHandler);
};

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
                       DeviceManagerBase* dev_mgr,
                       Datapath* datapath)
    : shill_client_(shill_client), dev_mgr_(dev_mgr), datapath_(datapath) {
  DCHECK(shill_client_);
  DCHECK(dev_mgr_);
  DCHECK(datapath_);

  dev_mgr_->RegisterDeviceAddedHandler(
      GuestMessage::ARC,
      base::Bind(&ArcService::OnDeviceAdded, base::Unretained(this)));
  dev_mgr_->RegisterDeviceRemovedHandler(
      GuestMessage::ARC,
      base::Bind(&ArcService::OnDeviceRemoved, base::Unretained(this)));

  shill_client_->RegisterDefaultInterfaceChangedHandler(base::Bind(
      &ArcService::OnDefaultInterfaceChanged, weak_factory_.GetWeakPtr()));
}

ArcService::~ArcService() {
  if (impl_) {
    // Stop the service.
    Stop(impl_->id());
    // Delete all the bridges and veth pairs.
    dev_mgr_->ProcessDevices(
        base::Bind(&ArcService::OnDeviceRemoved, weak_factory_.GetWeakPtr()));
  }
  dev_mgr_->UnregisterAllGuestHandlers(GuestMessage::ARC);
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
    impl_ = std::make_unique<VmImpl>(shill_client_, dev_mgr_, datapath_);
  else
    impl_ = std::make_unique<ContainerImpl>(dev_mgr_, datapath_, guest);

  if (!impl_->Start(id)) {
    impl_.reset();
    return false;
  }

  // Start known host devices, any new ones will be setup in the process.
  dev_mgr_->ProcessDevices(
      base::Bind(&ArcService::StartDevice, weak_factory_.GetWeakPtr()));

  dev_mgr_->OnGuestStart(guest);
  return true;
}

void ArcService::Stop(uint32_t id) {
  if (!impl_)
    return;

  dev_mgr_->OnGuestStop(impl_->guest());

  // Stop known host devices. Note that this does not teardown any existing
  // devices.
  dev_mgr_->ProcessDevices(
      base::Bind(&ArcService::StopDevice, weak_factory_.GetWeakPtr()));

  impl_->Stop(id);
  impl_.reset();
}

void ArcService::OnDeviceAdded(Device* device) {
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
    LOG(ERROR) << "Failed to configure egress traffic rules";

  device->set_context(std::make_unique<Context>());

  StartDevice(device);
}

void ArcService::StartDevice(Device* device) {
  // This can happen if OnDeviceAdded is invoked when the container is down.
  if (!impl_ || !impl_->IsStarted())
    return;

  // For now, only start devices for ARC++.
  if (impl_->guest() != GuestMessage::ARC)
    return;

  // If there is no context, then this is a new device and it needs to run
  // through the full setup process.
  Context* ctx = dynamic_cast<Context*>(device->context());
  if (!ctx)
    return OnDeviceAdded(device);

  if (ctx->IsStarted()) {
    LOG(ERROR) << "Attempt to restart device " << device->ifname();
    return;
  }

  if (!impl_->OnStartDevice(device)) {
    LOG(ERROR) << "Failed to start device " << device->ifname();
    return;
  }

  ctx->Start();
}

void ArcService::OnDeviceRemoved(Device* device) {
  // If the container is down, this call does nothing.
  StopDevice(device);

  const auto& config = device->config();

  LOG(INFO) << "Removing device " << device->ifname()
            << " bridge: " << config.host_ifname()
            << " guest_iface: " << config.guest_ifname();

  datapath_->RemoveOutboundIPv4(config.host_ifname());
  datapath_->RemoveInboundIPv4DNAT(
      device->ifname(), IPv4AddressToString(config.guest_ipv4_addr()));

  datapath_->RemoveBridge(config.host_ifname());

  device->set_context(nullptr);
}

void ArcService::StopDevice(Device* device) {
  // This can happen if the device if OnDeviceRemoved is invoked when the
  // container is down.
  if (!impl_ || !impl_->IsStarted())
    return;

  // For now, devices are only started for ARC++.
  if (impl_->guest() != GuestMessage::ARC)
    return;

  Context* ctx = dynamic_cast<Context*>(device->context());
  if (!ctx) {
    LOG(ERROR) << "Attempt to stop removed device " << device->ifname();
    return;
  }

  if (!ctx->IsStarted()) {
    LOG(ERROR) << "Attempt to re-stop device " << device->ifname();
    return;
  }

  impl_->OnStopDevice(device);

  ctx->Stop();
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

// Context

ArcService::Context::Context() : Device::Context() {
  Stop();
}

void ArcService::Context::Start() {
  Stop();
  started_ = true;
}

void ArcService::Context::Stop() {
  started_ = false;
  link_up_ = false;
}

bool ArcService::Context::IsStarted() const {
  return started_;
}

bool ArcService::Context::IsLinkUp() const {
  return link_up_;
}

bool ArcService::Context::SetLinkUp(bool link_up) {
  if (link_up == link_up_)
    return false;

  link_up_ = link_up;
  return true;
}

const std::string& ArcService::Context::TAP() const {
  return tap_;
}

void ArcService::Context::SetTAP(const std::string& tap) {
  tap_ = tap;
}

// ARC++ specific functions.

ArcService::ContainerImpl::ContainerImpl(DeviceManagerBase* dev_mgr,
                                         Datapath* datapath,
                                         GuestMessage::GuestType guest)
    : pid_(kInvalidPID), dev_mgr_(dev_mgr), datapath_(datapath), guest_(guest) {
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

  // Start listening for RTNetlink messages in the container's net namespace
  // to be notified whenever it brings up an interface.
  if (pid_ != kTestPID) {
    ScopedNS ns(pid_);
    if (ns.IsValid()) {
      rtnl_handler_ = std::make_unique<RTNetlinkHandler>();
      rtnl_handler_->Start(RTMGRP_LINK);
      link_listener_ = std::make_unique<shill::RTNLListener>(
          shill::RTNLHandler::kRequestLink,
          Bind(&ArcService::ContainerImpl::LinkMsgHandler,
               weak_factory_.GetWeakPtr()),
          rtnl_handler_.get());
    } else {
      // This is bad - it means we won't ever be able to tell when the container
      // brings up an interface.
      LOG(ERROR)
          << "Cannot start netlink listener - invalid container namespace?";
      return false;
    }
  }

  Device::Options opts{
      .fwd_multicast = false,
      .ipv6_enabled = false,
      .use_default_interface = false,
      .is_android = true,
      .is_sticky = true,
  };
  auto config = MakeArcConfig(dev_mgr_->addr_mgr(), false /*is_arcvm*/);

  // Create the bridge.
  // Per crbug/1008686 this device cannot be deleted and then re-added.
  // So instead of removing the bridge when the service stops, bring down the
  // device instead and re-up it on restart.
  if (!datapath_->AddBridge(kArcBridge, config->host_ipv4_addr(), 30) &&
      !datapath_->MaskInterfaceFlags(kArcBridge, IFF_UP)) {
    LOG(ERROR) << "Failed to bring up arc bridge: " << kArcBridge;
    return false;
  }

  arc_device_ = std::make_unique<Device>(kArcIfname, std::move(config), opts,
                                         GuestMessage::ARC);

  OnStartDevice(arc_device_.get());

  LOG(INFO) << "ARC++ network service started {pid: " << pid_ << "}";
  return true;
}

void ArcService::ContainerImpl::Stop(uint32_t /*pid*/) {
  if (!IsStarted())
    return;

  if (pid_ != kTestPID) {
    rtnl_handler_->RemoveListener(link_listener_.get());
    link_listener_.reset();
    rtnl_handler_.reset();
  }

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

  dev_mgr_->StartForwarding(*device, device->ifname());
  return true;
}

void ArcService::ContainerImpl::OnStopDevice(Device* device) {
  const auto& config = device->config();

  LOG(INFO) << "Stopping device " << device->ifname()
            << " bridge: " << config.host_ifname()
            << " guest_iface: " << config.guest_ifname() << " pid: " << pid_;

  dev_mgr_->StopForwarding(*device, device->ifname());
  datapath_->RemoveInterface(ArcVethHostName(device->ifname()));
}

void ArcService::ContainerImpl::OnDefaultInterfaceChanged(
    const std::string& new_ifname, const std::string& prev_ifname) {}

void ArcService::ContainerImpl::LinkMsgHandler(const shill::RTNLMessage& msg) {
  if (!msg.HasAttribute(IFLA_IFNAME)) {
    LOG(ERROR) << "Link event message does not have IFLA_IFNAME";
    return;
  }
  bool link_up = msg.link_status().flags & IFF_UP;
  shill::ByteString b(msg.GetAttribute(IFLA_IFNAME));
  std::string ifname(reinterpret_cast<const char*>(
      b.GetSubstring(0, IFNAMSIZ).GetConstData()));

  auto* device = dev_mgr_->FindByGuestInterface(ifname);
  if (!device)
    return;

  Context* ctx = dynamic_cast<Context*>(device->context());
  if (!ctx) {
    LOG(DFATAL) << "Context missing";
    return;
  }

  // If the link status is unchanged, there is nothing to do.
  if (!ctx->SetLinkUp(link_up))
    return;

  if (!link_up) {
    LOG(INFO) << ifname << " is now down";
    return;
  }
  LOG(INFO) << ifname << " is now up";
}

// VM specific functions

ArcService::VmImpl::VmImpl(ShillClient* shill_client,
                           DeviceManagerBase* dev_mgr,
                           Datapath* datapath)
    : cid_(kInvalidCID),
      shill_client_(shill_client),
      dev_mgr_(dev_mgr),
      datapath_(datapath) {}

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
      .is_android = true,
      .is_sticky = true,
  };
  auto config = MakeArcConfig(dev_mgr_->addr_mgr(), true /*is_arcvm*/);

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

  arc_device_ = std::make_unique<Device>(kArcVmIfname, std::move(config), opts,
                                         GuestMessage::ARC_VM);
  arc_device_->set_context(std::make_unique<Context>());

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

  Context* ctx = dynamic_cast<Context*>(device->context());
  if (!ctx) {
    LOG(ERROR) << "Context missing";
    return false;
  }

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

  ctx->SetTAP(tap);
  ctx->Start();
  // TODO(garrick): Remove this once ARCVM supports ad hoc interface
  // configurations; but for now ARCVM needs to be treated like ARC++ N.
  OnDefaultInterfaceChanged(shill_client_->default_interface(),
                            "" /*previous*/);
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

  Context* ctx = dynamic_cast<Context*>(device->context());
  if (!ctx) {
    LOG(ERROR) << "Context missing";
    return;
  }

  // TODO(garrick): Remove this once ARCVM supports ad hoc interface
  // configurations; but for now ARCVM needs to be treated like ARC++ N.
  OnDefaultInterfaceChanged("" /*new_ifname*/,
                            shill_client_->default_interface());
  datapath_->RemoveInterface(ctx->TAP());
  ctx->Stop();
}

void ArcService::VmImpl::OnDefaultInterfaceChanged(
    const std::string& new_ifname, const std::string& prev_ifname) {
  if (!IsStarted())
    return;

  dev_mgr_->StopForwarding(*arc_device_.get(), prev_ifname);
  // TODO(garrick): Remove this once ARCVM supports ad hoc interface
  // configurations; but for now ARCVM needs to be treated like ARC++ N.
  datapath_->RemoveLegacyIPv4InboundDNAT();

  // If a new default interface was given, then re-enable with that.
  if (!new_ifname.empty()) {
    dev_mgr_->StartForwarding(*arc_device_.get(), new_ifname);
    datapath_->AddLegacyIPv4InboundDNAT(new_ifname);
  }
}

}  // namespace arc_networkd
