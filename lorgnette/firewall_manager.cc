// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/firewall_manager.h"

#include <algorithm>
#include <vector>

#include <base/bind.h>
#include <brillo/errors/error.h>

using std::string;

namespace lorgnette {

namespace {

const uint16_t kCanonBjnpPort = 8612;
const int kInvalidFd = -1;

}  // namespace

FirewallManager::FirewallManager(const std::string& interface)
    : lifeline_read_fd_(kInvalidFd),
      lifeline_write_fd_(kInvalidFd),
      interface_(interface) {}

FirewallManager::~FirewallManager() {
  if (lifeline_read_fd_ != kInvalidFd) {
    close(lifeline_read_fd_);
    close(lifeline_write_fd_);
  }
}

void FirewallManager::Init(const scoped_refptr<dbus::Bus>& bus) {
  CHECK(!permission_broker_proxy_) << "Already started";

  if (!SetupLifelinePipe()) {
    return;
  }

  if (!bus) {
    LOG(ERROR) << "Bus is null; assuming we are in tests.";
    return;
  }

  permission_broker_proxy_.reset(new org::chromium::PermissionBrokerProxy(bus));

  // This will connect the name owner changed signal in DBus object proxy,
  // The callback will be invoked as soon as service is avalilable and will
  // be cleared after it is invoked. So this will be an one time callback.
  permission_broker_proxy_->GetObjectProxy()->WaitForServiceToBeAvailable(
      base::Bind(&FirewallManager::OnServiceAvailable,
                 weak_factory_.GetWeakPtr()));

  // This will continuously monitor the name owner of the service. However,
  // it does not connect the name owner changed signal in DBus object proxy
  // for some reason. In order to connect the name owner changed signal,
  // either WaitForServiceToBeAvaiable or ConnectToSignal need to be invoked.
  // Since we're not interested in any signals from the proxy,
  // WaitForServiceToBeAvailable is used.
  permission_broker_proxy_->GetObjectProxy()->SetNameOwnerChangedCallback(
      base::Bind(&FirewallManager::OnServiceNameChanged,
                 weak_factory_.GetWeakPtr()));
}

void FirewallManager::RequestScannerPortAccess() {
  // Request access for all well-known ports that the scanimage process will
  // listen to.
  RequestUdpPortAccess(kCanonBjnpPort);
}

bool FirewallManager::SetupLifelinePipe() {
  if (lifeline_read_fd_ != kInvalidFd) {
    LOG(ERROR) << "Lifeline pipe already created";
    return false;
  }

  // Setup lifeline pipe.
  int fds[2];
  if (pipe(fds) != 0) {
    PLOG(ERROR) << "Failed to create lifeline pipe";
    return false;
  }
  lifeline_read_fd_ = fds[0];
  lifeline_write_fd_ = fds[1];

  return true;
}

void FirewallManager::OnServiceAvailable(bool service_available) {
  LOG(INFO) << "FirewallManager::OnServiceAvailabe " << service_available;
  // Nothing to be done if proxy service is not available.
  if (!service_available) {
    return;
  }
  RequestAllPortsAccess();
}

void FirewallManager::OnServiceNameChanged(const string& old_owner,
                                           const string& new_owner) {
  LOG(INFO) << "FirewallManager::OnServiceNameChanged old " << old_owner
            << " new " << new_owner;
  // Nothing to be done if no owner is attached to the proxy service.
  if (new_owner.empty()) {
    return;
  }
  RequestAllPortsAccess();
}

void FirewallManager::RequestAllPortsAccess() {
  std::set<uint16_t> attempted_ports;
  attempted_ports.swap(requested_ports_);
  for (const auto& port : attempted_ports) {
    RequestUdpPortAccess(port);
  }
}

void FirewallManager::ReleaseAllPortsAccess() {
  auto release_ports = std::vector<decltype(requested_ports_)::value_type>(
      requested_ports_.size());
  std::copy(requested_ports_.begin(), requested_ports_.end(),
            release_ports.begin());

  for (const auto& port : release_ports) {
    ReleaseUdpPortAccess(port);
  }
}

void FirewallManager::RequestUdpPortAccess(uint16_t port) {
  if (!permission_broker_proxy_) {
    LOG(INFO) << "Permission broker does not exist (yet); adding request for "
              << "port " << port << " to queue.";
    requested_ports_.insert(port);
    return;
  }

  bool allowed = false;
  // Pass the read end of the pipe to permission_broker, for it to monitor this
  // process.
  brillo::ErrorPtr error;
  if (!permission_broker_proxy_->RequestUdpPortAccess(
          port, interface_, lifeline_read_fd_, &allowed, &error)) {
    LOG(ERROR) << "Failed to request UDP port access: " << error->GetCode()
               << " " << error->GetMessage();
    return;
  }
  if (!allowed) {
    LOG(ERROR) << "Access request for UDP port " << port << " on interface "
               << interface_ << " is denied";
    return;
  }
  LOG(INFO) << "Access granted for UDP port " << port << " on interface "
            << interface_;
  requested_ports_.insert(port);
}

void FirewallManager::ReleaseUdpPortAccess(uint16_t port) {
  brillo::ErrorPtr error;
  bool success;
  if (requested_ports_.find(port) == requested_ports_.end()) {
    LOG(ERROR) << "UDP access has not been requested for port: " << port;
    return;
  }
  if (!permission_broker_proxy_) {
    requested_ports_.erase(port);
    return;
  }

  if (!permission_broker_proxy_->ReleaseUdpPort(port, interface_, &success,
                                                &error)) {
    LOG(ERROR) << "Failed to release UDP port access: " << error->GetCode()
               << " " << error->GetMessage();
    return;
  }
  if (!success) {
    LOG(ERROR) << "Release request for UDP port " << port << " on interface "
               << interface_ << " is denied";
    return;
  }
  LOG(INFO) << "Access released for UDP port " << port << " on interface "
            << interface_;
  requested_ports_.erase(port);
}

}  // namespace lorgnette
