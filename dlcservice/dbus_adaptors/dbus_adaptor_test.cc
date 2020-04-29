// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dbus/dlcservice/dbus-constants.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dlcservice/dbus_adaptors/dbus_adaptor.h"
#include "dlcservice/dlc_service.h"
#include "dlcservice/mock_dlc_service.h"
#include "dlcservice/test_utils.h"

using brillo::ErrorPtr;
using testing::_;
using testing::Return;

namespace dlcservice {

class DBusServiceTest : public BaseTest {
 public:
  DBusServiceTest() {
    dlc_service_ = std::make_unique<MockDlcService>();
    dbus_service_ = std::make_unique<DBusService>(dlc_service_.get());
  }

  void SetUp() override { BaseTest::SetUp(); }

 protected:
  std::unique_ptr<MockDlcService> dlc_service_;
  std::unique_ptr<DBusService> dbus_service_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DBusServiceTest);
};

TEST_F(DBusServiceTest, Install) {
  EXPECT_CALL(*dlc_service_, Install(DlcIdList({kFirstDlc, kSecondDlc}),
                                     kDefaultOmahaUrl, &err_))
      .WillOnce(Return(true));

  DlcModuleList dlc_list;
  dlc_list.set_omaha_url(kDefaultOmahaUrl);
  for (const auto& id : {kFirstDlc, kSecondDlc}) {
    dlc_list.add_dlc_module_infos()->set_dlc_id(id);
  }
  EXPECT_TRUE(dbus_service_->Install(&err_, dlc_list));
}

// Tries to install duplicate DLCs.
TEST_F(DBusServiceTest, InstallDuplicate) {
  EXPECT_CALL(*dlc_service_, Install(DlcIdList({kFirstDlc, kSecondDlc}),
                                     kDefaultOmahaUrl, &err_))
      .WillOnce(Return(true));

  DlcModuleList dlc_list;
  dlc_list.set_omaha_url(kDefaultOmahaUrl);
  for (const auto& id : {kFirstDlc, kSecondDlc, kSecondDlc}) {
    dlc_list.add_dlc_module_infos()->set_dlc_id(id);
  }
  EXPECT_TRUE(dbus_service_->Install(&err_, dlc_list));
}

TEST_F(DBusServiceTest, GetInstalled) {
  EXPECT_CALL(*dlc_service_, GetInstalled())
      .WillOnce(Return(DlcIdList({kFirstDlc, kSecondDlc})));

  DlcBase first_dlc(kFirstDlc);
  DlcBase second_dlc(kSecondDlc);
  first_dlc.mount_point_ = FilePath("foo-path-1");
  second_dlc.mount_point_ = FilePath("foo-path-2");
  EXPECT_CALL(*dlc_service_, GetDlc(kFirstDlc)).WillOnce(Return(&first_dlc));
  EXPECT_CALL(*dlc_service_, GetDlc(kSecondDlc)).WillOnce(Return(&second_dlc));

  DlcModuleList dlc_list;
  EXPECT_TRUE(dbus_service_->GetInstalled(&err_, &dlc_list));

  EXPECT_EQ(dlc_list.omaha_url(), "");
  EXPECT_EQ(dlc_list.dlc_module_infos_size(), 2);
  EXPECT_EQ(dlc_list.dlc_module_infos()[0].dlc_id(), kFirstDlc);
  EXPECT_EQ(dlc_list.dlc_module_infos()[0].dlc_root(), "foo-path-1/root");
  EXPECT_EQ(dlc_list.dlc_module_infos()[1].dlc_id(), kSecondDlc);
  EXPECT_EQ(dlc_list.dlc_module_infos()[1].dlc_root(), "foo-path-2/root");
}

TEST_F(DBusServiceTest, GetExistingDlcs) {
  EXPECT_CALL(*dlc_service_, GetExistingDlcs())
      .WillOnce(Return(DlcIdList({kSecondDlc})));

  DlcBase second_dlc(kSecondDlc);
  SetUpDlcWithSlots(kSecondDlc);
  second_dlc.Initialize();
  EXPECT_CALL(*dlc_service_, GetDlc(kSecondDlc)).WillOnce(Return(&second_dlc));

  DlcsWithContent dlc_list;
  EXPECT_TRUE(dbus_service_->GetExistingDlcs(&err_, &dlc_list));

  EXPECT_EQ(dlc_list.dlc_infos_size(), 1);
  auto second_dlc_info = dlc_list.dlc_infos()[0];
  EXPECT_EQ(second_dlc_info.id(), kSecondDlc);
  EXPECT_EQ(second_dlc_info.name(), "Second Dlc");
  EXPECT_EQ(second_dlc_info.description(), "unittest only description");
  EXPECT_EQ(second_dlc_info.used_bytes_on_disk(),
            second_dlc.GetUsedBytesOnDisk());
}

}  // namespace dlcservice
