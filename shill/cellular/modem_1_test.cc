// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/modem.h"

#include <memory>

#include <base/files/scoped_temp_dir.h>
#include <ModemManager/ModemManager.h>

#include "shill/cellular/cellular_capability.h"
#include "shill/cellular/mock_cellular.h"
#include "shill/cellular/mock_modem_info.h"
#include "shill/manager.h"
#include "shill/mock_control.h"
#include "shill/mock_dbus_properties_proxy.h"
#include "shill/mock_device_info.h"
#include "shill/net/mock_rtnl_handler.h"
#include "shill/net/rtnl_handler.h"
#include "shill/test_event_dispatcher.h"
#include "shill/testing.h"

using testing::_;
using testing::ByMove;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using testing::Test;

namespace shill {

namespace {

const int kTestInterfaceIndex = 5;
const char kLinkName[] = "usb0";
const char kService[] = "org.freedesktop.ModemManager1";
const RpcIdentifier kPath("/org/freedesktop/ModemManager1/Modem/0");
const unsigned char kAddress[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};

}  // namespace

class Modem1Test : public Test {
 public:
  Modem1Test()
      : modem_info_(nullptr, &dispatcher_, nullptr, nullptr),
        device_info_(modem_info_.manager()),
        modem_(new Modem1(kService, kPath, &modem_info_)) {}

  void SetUp() override;
  void TearDown() override;

  void ReplaceSingletons() { modem_->rtnl_handler_ = &rtnl_handler_; }

 protected:
  EventDispatcherForTest dispatcher_;
  MockModemInfo modem_info_;
  MockDeviceInfo device_info_;
  std::unique_ptr<Modem1> modem_;
  MockRTNLHandler rtnl_handler_;
  ByteString expected_address_;
};

void Modem1Test::SetUp() {
  EXPECT_EQ(kService, modem_->service_);
  EXPECT_EQ(kPath, modem_->path_);
  ReplaceSingletons();
  expected_address_ = ByteString(kAddress, arraysize(kAddress));

  EXPECT_CALL(rtnl_handler_, GetInterfaceIndex(kLinkName))
      .WillRepeatedly(Return(kTestInterfaceIndex));

  EXPECT_CALL(*modem_info_.mock_manager(), device_info())
      .WillRepeatedly(Return(&device_info_));
  EXPECT_CALL(device_info_, GetMacAddress(kTestInterfaceIndex, _))
      .WillOnce(DoAll(SetArgPointee<1>(expected_address_), Return(true)));
}

void Modem1Test::TearDown() {
  modem_.reset();
}

TEST_F(Modem1Test, CreateDeviceMM1) {
  InterfaceToProperties properties;

  KeyValueStore modem_properties;
  modem_properties.Set<uint32_t>(MM_MODEM_PROPERTY_UNLOCKREQUIRED,
                                 MM_MODEM_LOCK_NONE);
  std::vector<std::tuple<std::string, uint32_t>> ports = {
      std::make_tuple(kLinkName, MM_MODEM_PORT_TYPE_NET)};
  modem_properties.SetVariant(MM_MODEM_PROPERTY_PORTS, brillo::Any(ports));
  properties[MM_DBUS_INTERFACE_MODEM] = modem_properties;

  KeyValueStore modem3gpp_properties;
  modem3gpp_properties.Set<uint32_t>(
      MM_MODEM_MODEM3GPP_PROPERTY_REGISTRATIONSTATE,
      MM_MODEM_3GPP_REGISTRATION_STATE_HOME);
  properties[MM_DBUS_INTERFACE_MODEM_MODEM3GPP] = modem3gpp_properties;

  EXPECT_CALL(*(modem_info_.mock_control_interface()),
              CreateDBusPropertiesProxy(kPath, kService))
      .WillOnce(Return(ByMove(std::make_unique<MockDBusPropertiesProxy>())));
  modem_->CreateDeviceMM1(properties);
  EXPECT_NE(nullptr, modem_->device());
  EXPECT_TRUE(modem_->device()->capability_->IsRegistered());
}

}  // namespace shill
