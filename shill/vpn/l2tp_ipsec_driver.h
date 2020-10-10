// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_L2TP_IPSEC_DRIVER_H_
#define SHILL_VPN_L2TP_IPSEC_DRIVER_H_

#include <sys/types.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/ipconfig.h"
#include "shill/rpc_task.h"
#include "shill/service.h"
#include "shill/vpn/vpn_driver.h"

namespace shill {

class CertificateFile;
class ExternalTask;
class PPPDeviceFactory;

class L2TPIPSecDriver : public VPNDriver, public RpcTaskDelegate {
 public:
  L2TPIPSecDriver(Manager* manager, ProcessManager* process_manager);
  ~L2TPIPSecDriver() override;

  // Method to return service RPC identifier.
  const RpcIdentifier& GetServiceRpcIdentifier() const;

 protected:
  void Connect(const VPNServiceRefPtr& service, Error* error) override;
  void Disconnect() override;
  std::string GetProviderType() const override;
  IfType GetIfType() const override;
  void OnConnectTimeout() override;

 private:
  friend class L2TPIPSecDriverTest;
  FRIEND_TEST(L2TPIPSecDriverTest, AppendFlag);
  FRIEND_TEST(L2TPIPSecDriverTest, AppendValueOption);
  FRIEND_TEST(L2TPIPSecDriverTest, Cleanup);
  FRIEND_TEST(L2TPIPSecDriverTest, Connect);
  FRIEND_TEST(L2TPIPSecDriverTest, DeleteTemporaryFiles);
  FRIEND_TEST(L2TPIPSecDriverTest, Disconnect);
  FRIEND_TEST(L2TPIPSecDriverTest, GetLogin);
  FRIEND_TEST(L2TPIPSecDriverTest, InitOptions);
  FRIEND_TEST(L2TPIPSecDriverTest, InitOptionsNoHost);
  FRIEND_TEST(L2TPIPSecDriverTest, InitPEMOptions);
  FRIEND_TEST(L2TPIPSecDriverTest, InitPSKOptions);
  FRIEND_TEST(L2TPIPSecDriverTest, InitXauthOptions);
  FRIEND_TEST(L2TPIPSecDriverTest, Notify);
  FRIEND_TEST(L2TPIPSecDriverTest, NotifyWithExistingDevice);
  FRIEND_TEST(L2TPIPSecDriverTest, NotifyDisconnected);
  FRIEND_TEST(L2TPIPSecDriverTest, OnL2TPIPSecVPNDied);
  FRIEND_TEST(L2TPIPSecDriverTest, SpawnL2TPIPSecVPN);

  static const char kL2TPIPSecVPNPath[];
  static const Property kProperties[];

  bool SpawnL2TPIPSecVPN(Error* error);

  bool InitOptions(std::vector<std::string>* options, Error* error);
  bool InitPSKOptions(std::vector<std::string>* options, Error* error);
  bool InitPEMOptions(std::vector<std::string>* options);
  bool InitXauthOptions(std::vector<std::string>* options, Error* error);

  // Resets the VPN state and deallocates all resources. If there's a service
  // associated through Connect, sets its state to Service::kStateIdle and
  // disassociates from the service.
  void IdleService();

  // Resets the VPN state and deallocates all resources. If there's a service
  // associated through Connect, sets its state to Service::kStateFailure with
  // failure reason |failure| and disassociates from the service.
  void FailService(Service::ConnectFailure failure);

  // Implements the IdleService and FailService methods. Resets the VPN state
  // and deallocates all resources. If there's a service associated through
  // Connect, sets its state |state|; if |state| is Service::kStateFailure, sets
  // the failure reason to |failure|; disassociates from the service.
  void Cleanup(Service::ConnectState state, Service::ConnectFailure failure);

  void DeleteTemporaryFile(base::FilePath* temporary_file);
  void DeleteTemporaryFiles();

  // Returns true if an opton was appended.
  bool AppendValueOption(const std::string& property,
                         const std::string& option,
                         std::vector<std::string>* options);

  // Returns true if a flag was appended.
  bool AppendFlag(const std::string& property,
                  const std::string& true_option,
                  const std::string& false_option,
                  std::vector<std::string>* options);

  // Returns true if neither a PSK nor a client certificate has been provided
  // for the IPSec phase of the authentication process.
  bool IsPskRequired() const;

  // Inherit from VPNDriver to add custom properties.
  KeyValueStore GetProvider(Error* error) override;

  // Implements RpcTaskDelegate.
  void GetLogin(std::string* user, std::string* password) override;
  void Notify(const std::string& reason,
              const std::map<std::string, std::string>& dict) override;
  // Called when the l2tpipsec_vpn process exits.
  void OnL2TPIPSecVPNDied(pid_t pid, int status);

  void ReportConnectionMetrics();

  PPPDeviceFactory* ppp_device_factory_;

  std::unique_ptr<ExternalTask> external_task_;
  base::FilePath psk_file_;
  base::FilePath xauth_credentials_file_;
  std::unique_ptr<CertificateFile> certificate_file_;
  PPPDeviceRefPtr device_;
  base::WeakPtrFactory<L2TPIPSecDriver> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(L2TPIPSecDriver);
};

}  // namespace shill

#endif  // SHILL_VPN_L2TP_IPSEC_DRIVER_H_
