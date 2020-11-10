// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_PPPOE_PPPOE_SERVICE_H_
#define SHILL_PPPOE_PPPOE_SERVICE_H_

#include <map>
#include <memory>
#include <string>

#include <gtest/gtest_prod.h>

#include "shill/ethernet/ethernet.h"
#include "shill/ethernet/ethernet_service.h"
#include "shill/refptr_types.h"
#include "shill/rpc_task.h"

namespace shill {

class Error;
class ExternalTask;
class Manager;
class PPPDeviceFactory;
class ProcessManager;
class StoreInterface;

// PPPoEService is an EthernetService that manages PPPoE connectivity on a
// single Ethernet device.  To do this it spawns and manages pppd instances.
// When pppX interfaces are created in the course of a connection they are
// wrapped with a PPPDevice, and are made to SelectService the PPPoEService that
// created them.
class PPPoEService : public EthernetService, public RpcTaskDelegate {
 public:
  PPPoEService(Manager* manager, base::WeakPtr<Ethernet> ethernet);
  PPPoEService(const PPPoEService&) = delete;
  PPPoEService& operator=(const PPPoEService&) = delete;

  ~PPPoEService() override;

  bool Load(const StoreInterface* storage) override;
  bool Save(StoreInterface* storage) override;
  bool Unload() override;

  // Inherited from Service.
  const RpcIdentifier& GetInnerDeviceRpcIdentifier() const override;

  // Inherited from RpcTaskDelegate.
  void GetLogin(std::string* user, std::string* password) override;
  void Notify(const std::string& reason,
              const std::map<std::string, std::string>& dict) override;

 protected:
  // Inherited from EthernetService.
  void OnConnect(Error* error) override;
  void OnDisconnect(Error* error, const char* reason) override;

 private:
  friend class PPPoEServiceTest;
  FRIEND_TEST(PPPoEServiceTest, Disconnect);
  FRIEND_TEST(PPPoEServiceTest, OnPPPConnected);

  void OnPPPAuthenticating();
  void OnPPPAuthenticated();
  void OnPPPConnected(const std::map<std::string, std::string>& params);
  void OnPPPDied(pid_t pid, int exit);

  PPPDeviceFactory* ppp_device_factory_;
  ProcessManager* process_manager_;

  std::string username_;
  std::string password_;

  // Properties for PPPDaemon::Options.
  int lcp_echo_interval_;
  int lcp_echo_failure_;
  int max_failure_;

  bool authenticating_;
  std::unique_ptr<ExternalTask> pppd_;
  PPPDeviceRefPtr ppp_device_;

  base::WeakPtrFactory<PPPoEService> weak_ptr_factory_;
};

}  // namespace shill

#endif  // SHILL_PPPOE_PPPOE_SERVICE_H_
