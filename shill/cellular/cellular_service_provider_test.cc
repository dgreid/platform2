// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular_service_provider.h"

#include <set>

#include <gtest/gtest.h>

#include "shill/cellular/cellular.h"
#include "shill/cellular/mock_modem_info.h"
#include "shill/fake_store.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_profile.h"
#include "shill/test_event_dispatcher.h"

using testing::NiceMock;
using testing::Return;

namespace shill {

namespace {
const char kTestDeviceName[] = "usb0";
const char kTestDeviceAddress[] = "000102030405";
const int kTestInterfaceIndex = 1;
const char kDBusService[] = "org.freedesktop.ModemManager1";
const RpcIdentifier kDBusPath("/org/freedesktop/ModemManager1/Modem/0");
}  // namespace

class CellularServiceProviderTest : public testing::Test {
 public:
  CellularServiceProviderTest()
      : manager_(&control_, &dispatcher_, &metrics_),
        modem_info_(&control_, &manager_),
        device_info_(&manager_),
        profile_(new NiceMock<MockProfile>(&manager_)),
        provider_(&manager_) {}

  ~CellularServiceProviderTest() override = default;

  void SetUp() override {
    provider_.Start();
    provider_.set_profile_for_testing(profile_);
    EXPECT_CALL(*profile_, GetConstStorage()).WillRepeatedly(Return(&storage_));
    EXPECT_CALL(*profile_, GetStorage()).WillRepeatedly(Return(&storage_));
  }

  void TearDown() override { provider_.Stop(); }

  // TODO(b/154014577): Provide eID for identifying sim cards once supported.
  CellularRefPtr CreateDevice(const std::string& imsi,
                              const std::string& iccid) {
    CellularRefPtr cellular = new Cellular(
        &modem_info_, kTestDeviceName, kTestDeviceAddress, kTestInterfaceIndex,
        Cellular::kType3gpp, kDBusService, kDBusPath);
    cellular->CreateCapability(&modem_info_);
    cellular->set_imsi(imsi);
    cellular->set_iccid(iccid);
    return cellular;
  }

  CellularRefPtr CreateDeviceWithEid(const std::string& imsi,
                                     const std::string& iccid,
                                     const std::string& eid) {
    CellularRefPtr cellular = CreateDevice(imsi, iccid);
    cellular->set_eid_for_testing(eid);
    return cellular;
  }

  // TODO(b/154014577): Provide eID once supported.
  void SetupCellularStore(const std::string& identifier,
                          const std::string& imsi,
                          const std::string& iccid,
                          const std::string& sim_card_id) {
    storage_.SetString(identifier, kTypeProperty, kTypeCellular);
    storage_.SetString(identifier, CellularService::kStorageImsi, imsi);
    storage_.SetString(identifier, CellularService::kStorageIccid, iccid);
    storage_.SetString(identifier, CellularService::kStorageSimCardId,
                       sim_card_id);
  }

  void StoreCellularProperty(const std::string& identifier,
                             const std::string& key,
                             const std::string& value) {
    storage_.SetString(identifier, key, value);
  }

  std::set<std::string> GetStorageGroups() { return storage_.GetGroups(); }

  const std::vector<CellularServiceRefPtr>& GetProviderServices() const {
    return provider_.services_;
  }

  CellularServiceProvider* provider() { return &provider_; }

 private:
  EventDispatcherForTest dispatcher_;
  MockControl control_;
  NiceMock<MockMetrics> metrics_;
  MockManager manager_;
  MockModemInfo modem_info_;
  NiceMock<MockDeviceInfo> device_info_;
  FakeStore storage_;
  scoped_refptr<NiceMock<MockProfile>> profile_;
  CellularServiceProvider provider_;
};

TEST_F(CellularServiceProviderTest, LoadService) {
  CellularRefPtr device = CreateDevice("imsi1", "iccid1");
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  ASSERT_TRUE(service);
  EXPECT_EQ(1u, GetProviderServices().size());
  EXPECT_EQ("imsi1", service->imsi());
  EXPECT_EQ("iccid1", service->iccid());
  EXPECT_EQ("iccid1", service->sim_card_id());
  EXPECT_TRUE(service->IsVisible());

  // RemoveServicesForDevice does not destroy the services, but they should no
  // longer be marked as visible.
  provider()->RemoveServicesForDevice(device.get());
  EXPECT_EQ(1u, GetProviderServices().size());
  EXPECT_FALSE(service->IsVisible());

  // Stopping should remove all services.
  provider()->Stop();
  EXPECT_EQ(0u, GetProviderServices().size());
}

TEST_F(CellularServiceProviderTest, LoadServiceFromProfile) {
  CellularRefPtr device = CreateDevice("imsi1", "iccid1");
  std::string identifier = device->GetStorageIdentifier();

  // Add an entry in the storage with a saved property (ppp_username).
  SetupCellularStore(identifier, "imsi1", "iccid1", "iccid1");
  StoreCellularProperty(identifier, CellularService::kStoragePPPUsername,
                        "user1");

  // Ensure that the service is loaded from storage.
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  ASSERT_TRUE(service);
  EXPECT_EQ("imsi1", service->imsi());
  EXPECT_EQ("iccid1", service->iccid());
  EXPECT_EQ("user1", service->ppp_username());
}

TEST_F(CellularServiceProviderTest, LoadMultipleServicesFromProfile) {
  // Set up two cellular services with the same SIM Card Id.
  SetupCellularStore("cellular_1a", "imsi1a", "iccid1a", "eid1");
  SetupCellularStore("cellular_1b", "imsi1b", "iccid1b", "eid1");
  // Set up a third cellular service with a different SIM Card Id.
  SetupCellularStore("cellular_2", "imsi2", "iccid2", "eid2");

  CellularRefPtr device = CreateDeviceWithEid("imsi1a", "iccid1a", "eid1");

  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  ASSERT_TRUE(service);
  // Both cellular_1a and cellular_1b services should be created.
  EXPECT_EQ(2u, GetProviderServices().size());
  // cellular_1a should be returned.
  EXPECT_EQ("imsi1a", service->imsi());
  EXPECT_EQ("iccid1a", service->iccid());
}

// When a SIM or eSIM is switched the Cellular Device will be rebuilt,
// generating a new call to LoadServicesForDevice with a different ICCID. This
// should remove services with the previous ICCID.
TEST_F(CellularServiceProviderTest, SwitchDeviceIccid) {
  CellularRefPtr device = CreateDevice("imsi1", "iccid1");
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  ASSERT_TRUE(service);
  EXPECT_EQ("imsi1", service->imsi());
  EXPECT_EQ(1u, GetProviderServices().size());
  unsigned int serial_number1 = service->serial_number();

  // Removing services for the device does not destroy the services, but they
  // should no longer be marked as visible.
  provider()->RemoveServicesForDevice(device.get());
  EXPECT_EQ(1u, GetProviderServices().size());
  EXPECT_FALSE(service->IsVisible());

  // Adding a device with a new ICCID should create a new service with a
  // different serial number.
  device = CreateDevice("imsi2", "iccid2");
  service = provider()->LoadServicesForDevice(device.get());
  ASSERT_TRUE(service);
  EXPECT_EQ("imsi2", service->imsi());
  EXPECT_EQ(1u, GetProviderServices().size());
  EXPECT_NE(serial_number1, service->serial_number());

  // Stopping should remove all services.
  provider()->Stop();
  EXPECT_EQ(0u, GetProviderServices().size());
}

TEST_F(CellularServiceProviderTest, RemoveObsoleteServiceFromProfile) {
  CellularRefPtr device = CreateDevice("imsi1", "iccid1");
  std::string identifier = device->GetStorageIdentifier();

  // Add two entries in the storage with the same ICCID, one with an empty IMSI.
  // Set a property on both.
  SetupCellularStore(identifier, "", "iccid1", "iccid1");
  StoreCellularProperty(identifier, CellularService::kStoragePPPUsername,
                        "user1");
  SetupCellularStore(identifier, "imsi1", "iccid1", "iccid1");
  StoreCellularProperty(identifier, CellularService::kStoragePPPUsername,
                        "user2");

  // Ensure that the service with a non empty imsi loaded from storage.
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  ASSERT_TRUE(service);
  EXPECT_EQ("imsi1", service->imsi());
  EXPECT_EQ("iccid1", service->iccid());
  EXPECT_EQ("user2", service->ppp_username());

  // Only one provider service should exist.
  EXPECT_EQ(1u, GetProviderServices().size());
}

}  // namespace shill
