// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/dbus/client.h"

#include <fcntl.h>

#include <base/bind.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/object_path.h>

#include "patchpanel/net_util.h"

namespace patchpanel {

namespace {

std::ostream& operator<<(std::ostream& stream,
                         const ModifyPortRuleRequest& request) {
  stream << "{ operation: "
         << ModifyPortRuleRequest::Operation_Name(request.op())
         << ", rule type: "
         << ModifyPortRuleRequest::RuleType_Name(request.type())
         << ", protocol: "
         << ModifyPortRuleRequest::Protocol_Name(request.proto());
  if (!request.input_ifname().empty()) {
    stream << ", input interface name: " << request.input_ifname();
  }
  if (!request.input_dst_ip().empty()) {
    stream << ", input destination IP: " << request.input_dst_ip();
  }
  stream << ", input destination port: " << request.input_dst_port();
  if (!request.dst_ip().empty()) {
    stream << ", destination IP: " << request.dst_ip();
  }
  if (request.dst_port() != 0) {
    stream << ", destination port: " << request.dst_port();
  }
  stream << " }";
  return stream;
}

void OnGetTrafficCountersDBusResponse(
    Client::GetTrafficCountersCallback callback,
    dbus::Response* dbus_response) {
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send TrafficCountersRequest message to patchpanel "
                  "service";
    std::move(callback).Run({});
    return;
  }

  TrafficCountersResponse response;
  dbus::MessageReader reader(dbus_response);
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse TrafficCountersResponse proto";
    std::move(callback).Run({});
    return;
  }

  std::move(callback).Run(
      {response.counters().begin(), response.counters().end()});
}

void OnNeighborConnectedStateChangedSignal(
    const Client::NeighborConnectedStateChangedHandler& handler,
    dbus::Signal* signal) {
  dbus::MessageReader reader(signal);
  NeighborConnectedStateChangedSignal proto;
  if (!reader.PopArrayOfBytesAsProto(&proto)) {
    LOG(ERROR) << "Failed to parse NeighborConnectedStateChangedSignal proto";
    return;
  }

  handler.Run(proto);
}

void OnSignalConnectedCallback(const std::string& interface_name,
                               const std::string& signal_name,
                               bool success) {
  if (!success)
    LOG(ERROR) << "Failed to connect to " << signal_name;
}

class ClientImpl : public Client {
 public:
  ClientImpl(const scoped_refptr<dbus::Bus>& bus, dbus::ObjectProxy* proxy)
      : bus_(std::move(bus)), proxy_(proxy) {}
  ~ClientImpl();

  bool NotifyArcStartup(pid_t pid) override;
  bool NotifyArcShutdown() override;

  std::vector<NetworkDevice> NotifyArcVmStartup(uint32_t cid) override;
  bool NotifyArcVmShutdown(uint32_t cid) override;

  bool NotifyTerminaVmStartup(uint32_t cid,
                              NetworkDevice* device,
                              IPv4Subnet* container_subnet) override;
  bool NotifyTerminaVmShutdown(uint32_t cid) override;

  bool NotifyPluginVmStartup(uint64_t vm_id,
                             int subnet_index,
                             NetworkDevice* device) override;
  bool NotifyPluginVmShutdown(uint64_t vm_id) override;

  bool DefaultVpnRouting(int socket) override;

  bool RouteOnVpn(int socket) override;

  bool BypassVpn(int socket) override;

  std::pair<base::ScopedFD, patchpanel::ConnectNamespaceResponse>
  ConnectNamespace(pid_t pid,
                   const std::string& outbound_ifname,
                   bool forward_user_traffic) override;

  void GetTrafficCounters(const std::set<std::string>& devices,
                          GetTrafficCountersCallback callback) override;

  bool ModifyPortRule(patchpanel::ModifyPortRuleRequest::Operation op,
                      patchpanel::ModifyPortRuleRequest::RuleType type,
                      patchpanel::ModifyPortRuleRequest::Protocol proto,
                      const std::string& input_ifname,
                      const std::string& input_dst_ip,
                      uint32_t input_dst_port,
                      const std::string& dst_ip,
                      uint32_t dst_port) override;

  void RegisterNeighborConnectedStateChangedHandler(
      NeighborConnectedStateChangedHandler handler) override;

 private:
  scoped_refptr<dbus::Bus> bus_;
  dbus::ObjectProxy* proxy_ = nullptr;  // owned by bus_

  bool SendSetVpnIntentRequest(int socket,
                               SetVpnIntentRequest::VpnRoutingPolicy policy);

  DISALLOW_COPY_AND_ASSIGN(ClientImpl);
};

ClientImpl::~ClientImpl() {
  if (bus_)
    bus_->ShutdownAndBlock();
}

bool ClientImpl::NotifyArcStartup(pid_t pid) {
  dbus::MethodCall method_call(kPatchPanelInterface, kArcStartupMethod);
  dbus::MessageWriter writer(&method_call);

  ArcStartupRequest request;
  request.set_pid(static_cast<uint32_t>(pid));

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode ArcStartupRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  ArcStartupResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  return true;
}

bool ClientImpl::NotifyArcShutdown() {
  dbus::MethodCall method_call(kPatchPanelInterface, kArcShutdownMethod);
  dbus::MessageWriter writer(&method_call);

  ArcShutdownRequest request;
  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode ArcShutdownRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  ArcShutdownResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  return true;
}

std::vector<NetworkDevice> ClientImpl::NotifyArcVmStartup(uint32_t cid) {
  dbus::MethodCall method_call(kPatchPanelInterface, kArcVmStartupMethod);
  dbus::MessageWriter writer(&method_call);

  ArcVmStartupRequest request;
  request.set_cid(cid);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode ArcVmStartupRequest proto";
    return {};
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return {};
  }

  dbus::MessageReader reader(dbus_response.get());
  ArcVmStartupResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return {};
  }

  std::vector<NetworkDevice> devices;
  for (const auto& d : response.devices()) {
    devices.emplace_back(d);
  }
  return devices;
}

bool ClientImpl::NotifyArcVmShutdown(uint32_t cid) {
  dbus::MethodCall method_call(kPatchPanelInterface, kArcVmShutdownMethod);
  dbus::MessageWriter writer(&method_call);

  ArcVmShutdownRequest request;
  request.set_cid(cid);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode ArcVmShutdownRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  ArcVmShutdownResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  return true;
}

bool ClientImpl::NotifyTerminaVmStartup(uint32_t cid,
                                        NetworkDevice* device,
                                        IPv4Subnet* container_subnet) {
  dbus::MethodCall method_call(kPatchPanelInterface, kTerminaVmStartupMethod);
  dbus::MessageWriter writer(&method_call);

  TerminaVmStartupRequest request;
  request.set_cid(cid);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode TerminaVmStartupRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  TerminaVmStartupResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  if (!response.has_device()) {
    LOG(ERROR) << "No device found";
    return false;
  }
  *device = response.device();

  if (response.has_container_subnet()) {
    *container_subnet = response.container_subnet();
  } else {
    LOG(WARNING) << "No container subnet found";
  }

  return true;
}

bool ClientImpl::NotifyTerminaVmShutdown(uint32_t cid) {
  dbus::MethodCall method_call(kPatchPanelInterface, kTerminaVmShutdownMethod);
  dbus::MessageWriter writer(&method_call);

  TerminaVmShutdownRequest request;
  request.set_cid(cid);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode TerminaVmShutdownRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  TerminaVmShutdownResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  return true;
}

bool ClientImpl::NotifyPluginVmStartup(uint64_t vm_id,
                                       int subnet_index,
                                       NetworkDevice* device) {
  dbus::MethodCall method_call(kPatchPanelInterface, kPluginVmStartupMethod);
  dbus::MessageWriter writer(&method_call);

  PluginVmStartupRequest request;
  request.set_id(vm_id);
  request.set_subnet_index(subnet_index);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode PluginVmStartupRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  PluginVmStartupResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  if (!response.has_device()) {
    LOG(ERROR) << "No device found";
    return false;
  }
  *device = response.device();

  return true;
}

bool ClientImpl::NotifyPluginVmShutdown(uint64_t vm_id) {
  dbus::MethodCall method_call(kPatchPanelInterface, kPluginVmShutdownMethod);
  dbus::MessageWriter writer(&method_call);

  PluginVmShutdownRequest request;
  request.set_id(vm_id);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode PluginVmShutdownRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  PluginVmShutdownResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  return true;
}

bool ClientImpl::DefaultVpnRouting(int socket) {
  return SendSetVpnIntentRequest(socket, SetVpnIntentRequest::DEFAULT_ROUTING);
}

bool ClientImpl::RouteOnVpn(int socket) {
  return SendSetVpnIntentRequest(socket, SetVpnIntentRequest::ROUTE_ON_VPN);
}

bool ClientImpl::BypassVpn(int socket) {
  return SendSetVpnIntentRequest(socket, SetVpnIntentRequest::BYPASS_VPN);
}

bool ClientImpl::SendSetVpnIntentRequest(
    int socket, SetVpnIntentRequest::VpnRoutingPolicy policy) {
  dbus::MethodCall method_call(kPatchPanelInterface, kSetVpnIntentMethod);
  dbus::MessageWriter writer(&method_call);

  SetVpnIntentRequest request;
  SetVpnIntentResponse response;
  request.set_policy(policy);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode SetVpnIntentRequest proto";
    return false;
  }
  writer.AppendFileDescriptor(socket);

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR)
        << "Failed to send SetVpnIntentRequest message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse SetVpnIntentResponse proto";
    return false;
  }

  if (!response.success()) {
    LOG(ERROR) << "SetVpnIntentRequest failed";
    return false;
  }
  return true;
}

std::pair<base::ScopedFD, patchpanel::ConnectNamespaceResponse>
ClientImpl::ConnectNamespace(pid_t pid,
                             const std::string& outbound_ifname,
                             bool forward_user_traffic) {
  // Prepare and serialize the request proto.
  ConnectNamespaceRequest request;
  request.set_pid(static_cast<int32_t>(pid));
  request.set_outbound_physical_device(outbound_ifname);
  request.set_allow_user_traffic(forward_user_traffic);

  dbus::MethodCall method_call(kPatchPanelInterface, kConnectNamespaceMethod);
  dbus::MessageWriter writer(&method_call);
  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode ConnectNamespaceRequest proto";
    return {};
  }

  // Prepare an fd pair and append one fd directly after the serialized request.
  int pipe_fds[2] = {-1, -1};
  if (pipe2(pipe_fds, O_CLOEXEC) < 0) {
    PLOG(ERROR) << "Failed to create a pair of fds with pipe2()";
    return {};
  }
  base::ScopedFD fd_local(pipe_fds[0]);
  // MessageWriter::AppendFileDescriptor duplicates the fd, so use ScopeFD to
  // make sure the original fd is closed eventually.
  base::ScopedFD fd_remote(pipe_fds[1]);
  writer.AppendFileDescriptor(pipe_fds[1]);

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send ConnectNamespace message to patchpanel";
    return {};
  }

  dbus::MessageReader reader(dbus_response.get());
  ConnectNamespaceResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse ConnectNamespaceResponse proto";
    return {};
  }

  if (response.peer_ifname().empty() || response.host_ifname().empty()) {
    LOG(ERROR) << "ConnectNamespace for netns pid " << pid << " failed";
    return {};
  }

  std::string subnet_info = IPv4AddressToCidrString(
      response.ipv4_subnet().base_addr(), response.ipv4_subnet().prefix_len());
  LOG(INFO) << "ConnectNamespace for netns pid " << pid
            << " succeeded: peer_ifname=" << response.peer_ifname()
            << " peer_ipv4_address="
            << IPv4AddressToString(response.peer_ipv4_address())
            << " host_ifname=" << response.host_ifname()
            << " host_ipv4_address="
            << IPv4AddressToString(response.host_ipv4_address())
            << " subnet=" << subnet_info;

  return std::make_pair(std::move(fd_local), std::move(response));
}

void ClientImpl::GetTrafficCounters(const std::set<std::string>& devices,
                                    GetTrafficCountersCallback callback) {
  dbus::MethodCall method_call(kPatchPanelInterface, kGetTrafficCountersMethod);
  dbus::MessageWriter writer(&method_call);

  TrafficCountersRequest request;
  for (const auto& device : devices) {
    request.add_devices(device);
  }

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode TrafficCountersRequest proto";
    std::move(callback).Run({});
    return;
  }

  proxy_->CallMethod(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
      base::BindOnce(&OnGetTrafficCountersDBusResponse, std::move(callback)));
}

bool ClientImpl::ModifyPortRule(ModifyPortRuleRequest::Operation op,
                                ModifyPortRuleRequest::RuleType type,
                                ModifyPortRuleRequest::Protocol proto,
                                const std::string& input_ifname,
                                const std::string& input_dst_ip,
                                uint32_t input_dst_port,
                                const std::string& dst_ip,
                                uint32_t dst_port) {
  dbus::MethodCall method_call(kPatchPanelInterface, kModifyPortRuleMethod);
  dbus::MessageWriter writer(&method_call);

  ModifyPortRuleRequest request;
  ModifyPortRuleResponse response;

  request.set_op(op);
  request.set_type(type);
  request.set_proto(proto);
  request.set_input_ifname(input_ifname);
  request.set_input_dst_ip(input_dst_ip);
  request.set_input_dst_port(input_dst_port);
  request.set_dst_ip(dst_ip);
  request.set_dst_port(dst_port);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode ModifyPortRuleRequest proto " << request;
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR)
        << "Failed to send ModifyPortRuleRequest message to patchpanel service "
        << request;
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse ModifyPortRuleResponse proto " << request;
    return false;
  }

  if (!response.success()) {
    LOG(ERROR) << "ModifyPortRuleRequest failed " << request;
    return false;
  }
  return true;
}

void ClientImpl::RegisterNeighborConnectedStateChangedHandler(
    NeighborConnectedStateChangedHandler handler) {
  proxy_->ConnectToSignal(
      kPatchPanelInterface, kNeighborConnectedStateChangedSignal,
      base::BindRepeating(OnNeighborConnectedStateChangedSignal, handler),
      base::BindOnce(OnSignalConnectedCallback));
}

}  // namespace

// static
std::unique_ptr<Client> Client::New() {
  dbus::Bus::Options opts;
  opts.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(std::move(opts)));

  if (!bus->Connect()) {
    LOG(ERROR) << "Failed to connect to system bus";
    return nullptr;
  }

  dbus::ObjectProxy* proxy = bus->GetObjectProxy(
      kPatchPanelServiceName, dbus::ObjectPath(kPatchPanelServicePath));
  if (!proxy) {
    LOG(ERROR) << "Unable to get dbus proxy for " << kPatchPanelServiceName;
    return nullptr;
  }

  return std::make_unique<ClientImpl>(std::move(bus), proxy);
}

std::unique_ptr<Client> Client::New(const scoped_refptr<dbus::Bus>& bus,
                                    dbus::ObjectProxy* proxy) {
  return std::make_unique<ClientImpl>(std::move(bus), proxy);
}

}  // namespace patchpanel
