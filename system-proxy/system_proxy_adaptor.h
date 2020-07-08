// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SYSTEM_PROXY_SYSTEM_PROXY_ADAPTOR_H_
#define SYSTEM_PROXY_SYSTEM_PROXY_ADAPTOR_H_

#include <memory>
#include <string>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <brillo/http/http_proxy.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "bindings/worker_common.pb.h"
#include "system_proxy/org.chromium.SystemProxy.h"

namespace brillo {
namespace dbus_utils {
class DBusObject;
}

}  // namespace brillo

namespace system_proxy {

class KerberosClient;
class SandboxedWorker;

// Implementation of the SystemProxy D-Bus interface.
class SystemProxyAdaptor : public org::chromium::SystemProxyAdaptor,
                           public org::chromium::SystemProxyInterface {
 public:
  explicit SystemProxyAdaptor(
      std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object);
  SystemProxyAdaptor(const SystemProxyAdaptor&) = delete;
  SystemProxyAdaptor& operator=(const SystemProxyAdaptor&) = delete;
  virtual ~SystemProxyAdaptor();

  // Registers the D-Bus object and interfaces.
  void RegisterAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction&
          completion_callback);

  // org::chromium::SystemProxyInterface: (see org.chromium.SystemProxy.xml).
  std::vector<uint8_t> SetAuthenticationDetails(
      const std::vector<uint8_t>& request_blob) override;
  std::vector<uint8_t> SetSystemTrafficCredentials(
      const std::vector<uint8_t>& request_blob) override;
  std::vector<uint8_t> ShutDown() override;

  void GetChromeProxyServersAsync(
      const std::string& target_url,
      const brillo::http::GetChromeProxyServersCallback& callback);

  void RequestAuthenticationCredentials(
      const worker::ProtectionSpace& protection_space);

 protected:
  virtual std::unique_ptr<SandboxedWorker> CreateWorker();
  virtual void ConnectNamespace(SandboxedWorker* worker, bool user_traffic);
  // Triggers the |WorkerActive| signal.
  void OnNamespaceConnected(SandboxedWorker* worker, bool user_traffic);

 private:
  friend class SystemProxyAdaptorTest;
  FRIEND_TEST(SystemProxyAdaptorTest, SetAuthenticationDetails);
  FRIEND_TEST(SystemProxyAdaptorTest, SetSystemTrafficCredentials);
  FRIEND_TEST(SystemProxyAdaptorTest, KerberosEnabled);
  FRIEND_TEST(SystemProxyAdaptorTest, ShutDown);
  FRIEND_TEST(SystemProxyAdaptorTest, ConnectNamespace);
  FRIEND_TEST(SystemProxyAdaptorTest, ProxyResolutionFilter);
  FRIEND_TEST(SystemProxyAdaptorTest, ProtectionSpaceAuthenticationRequired);
  FRIEND_TEST(SystemProxyAdaptorTest, ProtectionSpaceNoCredentials);

  void SetCredentialsTask(SandboxedWorker* worker,
                          const worker::Credentials& credentials);

  void SetKerberosEnabledTask(SandboxedWorker* worker,
                              bool kerberos_enabled,
                              const std::string& principal_name);

  void ShutDownTask();

  void ConnectNamespaceTask(SandboxedWorker* worker, bool user_traffic);

  bool StartWorker(SandboxedWorker* worker, bool user_traffic);

  // Checks if a worker process exists and if not creates one and sends a
  // request to patchpanel to setup the network namespace for it. Returns true
  // if the worker exists or was created successfully, false otherwise.
  bool CreateWorkerIfNeeded(bool user_traffic);

  // Called when the patchpanel D-Bus service becomes available.
  void OnPatchpanelServiceAvailable(bool is_available);

  // The callback of |GetChromeProxyServersAsync|.
  void OnGetProxyServers(bool success, const std::vector<std::string>& servers);

  // The number of tries left for setting up the network namespace of the
  // System-proxy worker for system traffic. TODO(acostinas, b/160736881) Remove
  // when patchpaneld creates the veth pair directly across the host and worker
  // network namespaces.
  int netns_reconnect_attempts_available_;

  // Worker that authenticates and forwards to a remote web proxy traffic
  // coming form Chrome OS system services.
  std::unique_ptr<SandboxedWorker> system_services_worker_;
  // Worker that authenticates and forwards to a remote web proxy traffic
  // coming form ARC++ apps.
  std::unique_ptr<SandboxedWorker> arc_worker_;
  std::unique_ptr<KerberosClient> kerberos_client_;

  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
  base::WeakPtrFactory<SystemProxyAdaptor> weak_ptr_factory_;
};

}  // namespace system_proxy
#endif  // SYSTEM_PROXY_SYSTEM_PROXY_ADAPTOR_H_
