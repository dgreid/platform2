// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_TEST_UTILS_H_
#define DLCSERVICE_TEST_UTILS_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/test/simple_test_clock.h>
#include <brillo/message_loops/fake_message_loop.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <imageloader/dbus-proxy-mocks.h>
#include <session_manager/dbus-proxy-mocks.h>
#include <update_engine/proto_bindings/update_engine.pb.h>
#include <update_engine/dbus-proxy-mocks.h>

#include "dlcservice/boot/mock_boot_device.h"
#include "dlcservice/dlc.h"
#include "dlcservice/dlc_service.h"
#include "dlcservice/mock_metrics.h"
#include "dlcservice/mock_state_change_reporter.h"
#include "dlcservice/mock_system_properties.h"

namespace dlcservice {

extern const char kFirstDlc[];
extern const char kSecondDlc[];
extern const char kThirdDlc[];
extern const char kPackage[];
extern const char kDefaultOmahaUrl[];

MATCHER_P3(CheckDlcStateProto, state, progress, root_path, "") {
  return arg.state() == state && arg.progress() == progress &&
         arg.root_path() == root_path;
};

int64_t GetFileSize(const base::FilePath& path);

class BaseTest : public testing::Test {
 public:
  BaseTest();

  void SetUp() override;

  void SetUpFilesAndDirectories();

  // Will create |path|/|id|/|package|/dlc.img file. Will return the path to the
  // generated preloaded image.
  base::FilePath SetUpDlcPreloadedImage(const DlcId& id);

  // Will create |path/|id|/|package|/dlc_[a|b]/dlc.img files.
  void SetUpDlcWithSlots(const DlcId& id);

  // Mimics an installation form update_engine on the current boot slot.
  void InstallWithUpdateEngine(const std::vector<std::string>& ids);

  void SetMountPath(const std::string& mount_path_expected);

 protected:
  brillo::ErrorPtr err_;

  base::ScopedTempDir scoped_temp_dir_;

  base::FilePath testdata_path_;
  base::FilePath manifest_path_;
  base::FilePath preloaded_content_path_;
  base::FilePath content_path_;
  base::FilePath prefs_path_;
  base::FilePath users_path_;
  base::FilePath mount_path_;

  using ImageLoaderProxyMock = org::chromium::ImageLoaderInterfaceProxyMock;
  std::unique_ptr<ImageLoaderProxyMock> mock_image_loader_proxy_;
  ImageLoaderProxyMock* mock_image_loader_proxy_ptr_;

  using UpdateEngineProxyMock = org::chromium::UpdateEngineInterfaceProxyMock;
  std::unique_ptr<UpdateEngineProxyMock> mock_update_engine_proxy_;
  UpdateEngineProxyMock* mock_update_engine_proxy_ptr_;

  using SessionManagerProxyMock =
      org::chromium::SessionManagerInterfaceProxyMock;
  std::unique_ptr<SessionManagerProxyMock> mock_session_manager_proxy_;
  SessionManagerProxyMock* mock_session_manager_proxy_ptr_;

  std::unique_ptr<MockBootDevice> mock_boot_device_;
  MockBootDevice* mock_boot_device_ptr_;

  MockMetrics* mock_metrics_;
  MockSystemProperties* mock_system_properties_;
  MockStateChangeReporter mock_state_change_reporter_;

  base::SimpleTestClock clock_;
  brillo::FakeMessageLoop loop_{&clock_};

 private:
  BaseTest(const BaseTest&) = delete;
  BaseTest& operator=(const BaseTest&) = delete;
};

}  // namespace dlcservice

#endif  // DLCSERVICE_TEST_UTILS_H_
