// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/optional.h>
#include <base/run_loop.h>
#include <brillo/message_loops/base_message_loop.h>
#include <brillo/message_loops/message_loop_utils.h>
#include <dbus/dlcservice/dbus-constants.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <update_engine/proto_bindings/update_engine.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <imageloader/dbus-proxy-mocks.h>
#include <update_engine/dbus-constants.h>
#include <update_engine/dbus-proxy-mocks.h>

#include "dlcservice/boot/boot_slot.h"
#include "dlcservice/boot/mock_boot_device.h"
#include "dlcservice/dlc_service.h"
#include "dlcservice/utils.h"

using brillo::ErrorPtr;
using std::move;
using std::string;
using std::vector;
using testing::_;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;
using update_engine::Operation;
using update_engine::StatusResult;

namespace dlcservice {

namespace {

constexpr char kFirstDlc[] = "First-Dlc";
constexpr char kSecondDlc[] = "Second-Dlc";
constexpr char kThirdDlc[] = "Third-Dlc";
constexpr char kPackage[] = "Package";

constexpr char kManifestWithPreloadAllowedName[] =
    "imageloader-preload-allowed.json";

MATCHER_P(ProtoHasUrl,
          url,
          string("The protobuf provided does not have url: ") + url) {
  return url == arg.omaha_url();
}

DlcModuleList CreateDlcModuleList(const vector<DlcId>& ids,
                                  const string& omaha_url = "") {
  DlcModuleList dlc_module_list;
  dlc_module_list.set_omaha_url(omaha_url);
  for (const string& id : ids) {
    DlcModuleInfo* dlc_info = dlc_module_list.add_dlc_module_infos();
    dlc_info->set_dlc_id(id);
  }
  return dlc_module_list;
}

class DlcServiceTestObserver : public DlcService::Observer {
 public:
  void SendInstallStatus(const InstallStatus& install_status) override {
    install_status_.emplace(install_status);
  }

  bool IsSendInstallStatusCalled() { return install_status_.has_value(); }

  InstallStatus GetInstallStatus() {
    EXPECT_TRUE(install_status_.has_value())
        << "SendInstallStatus() was not called.";
    base::Optional<InstallStatus> tmp;
    tmp.swap(install_status_);
    return *tmp;
  }

 private:
  base::Optional<InstallStatus> install_status_;
};

}  // namespace

class DlcServiceTest : public testing::Test {
 public:
  DlcServiceTest() {
    loop_.SetAsCurrent();

    // Initialize DLC path.
    CHECK(scoped_temp_dir_.CreateUniqueTempDir());
    manifest_path_ = JoinPaths(scoped_temp_dir_.GetPath(), "rootfs");
    preloaded_content_path_ =
        JoinPaths(scoped_temp_dir_.GetPath(), "preloaded_stateful");
    content_path_ = JoinPaths(scoped_temp_dir_.GetPath(), "stateful");
    mount_path_ = JoinPaths(scoped_temp_dir_.GetPath(), "mount");
    base::FilePath mount_root_path = JoinPaths(mount_path_, "root");
    base::CreateDirectory(manifest_path_);
    base::CreateDirectory(preloaded_content_path_);
    base::CreateDirectory(content_path_);
    base::CreateDirectory(mount_root_path);
    testdata_path_ = JoinPaths(getenv("SRC"), "testdata");

    // Create DLC manifest sub-directories.
    for (auto&& id : {kFirstDlc, kSecondDlc, kThirdDlc}) {
      base::CreateDirectory(JoinPaths(manifest_path_, id, kPackage));
      base::CopyFile(JoinPaths(testdata_path_, id, kPackage, kManifestName),
                     JoinPaths(manifest_path_, id, kPackage, kManifestName));
    }

    // Create mocks with default behaviors.
    mock_boot_device_ = std::make_unique<MockBootDevice>();
    EXPECT_CALL(*(mock_boot_device_.get()), GetBootDevice())
        .WillOnce(Return("/dev/sdb5"));
    EXPECT_CALL(*(mock_boot_device_.get()), IsRemovableDevice(_))
        .WillOnce(Return(false));
    current_slot_ = dlcservice::BootSlot::Slot::B;

    mock_image_loader_proxy_ =
        std::make_unique<StrictMock<ImageLoaderProxyMock>>();
    mock_image_loader_proxy_ptr_ = mock_image_loader_proxy_.get();

    mock_update_engine_proxy_ =
        std::make_unique<StrictMock<UpdateEngineProxyMock>>();
    mock_update_engine_proxy_ptr_ = mock_update_engine_proxy_.get();
    EXPECT_CALL(*mock_update_engine_proxy_ptr_,
                DoRegisterStatusUpdateAdvancedSignalHandler(_, _))
        .Times(1);
  }

  int64_t GetFileSize(const base::FilePath& path) {
    int64_t file_size;
    EXPECT_TRUE(base::GetFileSize(path, &file_size));
    return file_size;
  }

  void ResizeImageFile(const base::FilePath& image_path, int64_t image_size) {
    constexpr uint32_t file_flags =
        base::File::FLAG_WRITE | base::File::FLAG_OPEN;
    base::File file(image_path, file_flags);
    EXPECT_TRUE(file.SetLength(image_size));
  }

  void CreateImageFileWithRightSize(const base::FilePath& image_path,
                                    const base::FilePath& manifest_path,
                                    const string& id,
                                    const string& package) {
    imageloader::Manifest manifest;
    dlcservice::GetDlcManifest(manifest_path, id, package, &manifest);
    int64_t image_size = manifest.preallocated_size();

    constexpr uint32_t file_flags = base::File::FLAG_WRITE |
                                    base::File::FLAG_READ |
                                    base::File::FLAG_CREATE;
    base::File file(image_path, file_flags);
    EXPECT_TRUE(file.SetLength(image_size));
  }

  // Will modify DLC with |id| and |package| manifest file to allow preloading.
  void SetUpDlcPreloadAllowed(const string& id, const string& package) {
    auto from = JoinPaths(testdata_path_, id, kPackage,
                          kManifestWithPreloadAllowedName);
    auto to = JoinPaths(manifest_path_, id, kPackage, kManifestName);
    EXPECT_TRUE(base::PathExists(from));
    EXPECT_TRUE(base::PathExists(to));
    EXPECT_TRUE(base::CopyFile(from, to));
  }

  // Will create |path|/|id|/|package|/dlc.img file.
  void SetUpDlcWithoutSlots(const base::FilePath& path,
                            const string& id,
                            const string& package) {
    base::FilePath image_path = JoinPaths(path, id, package, kDlcImageFileName);
    base::CreateDirectory(image_path.DirName());
    CreateImageFileWithRightSize(image_path, manifest_path_, id, package);
  }
  // Will create |path/|id|/|package|/dlc_[a|b]/dlc.img files.
  void SetUpDlcWithSlots(const base::FilePath& path,
                         const string& id,
                         const string& package) {
    // Create DLC content sub-directories and empty images.
    for (const auto& slot : {BootSlot::Slot::A, BootSlot::Slot::B}) {
      base::FilePath image_path = GetDlcImagePath(path, id, package, slot);
      base::CreateDirectory(image_path.DirName());
      CreateImageFileWithRightSize(image_path, manifest_path_, id, package);
    }
  }

  void ConstructDlcService() {
    SystemState::Initialize(
        move(mock_image_loader_proxy_), move(mock_update_engine_proxy_),
        std::make_unique<BootSlot>(move(mock_boot_device_)), manifest_path_,
        preloaded_content_path_, content_path_, /*for_test=*/true);
    dlc_service_ = std::make_unique<DlcService>();

    dlc_service_test_observer_ = std::make_unique<DlcServiceTestObserver>();
    dlc_service_->AddObserver(dlc_service_test_observer_.get());
  }

  void SetUp() override {
    SetUpDlcWithSlots(content_path_, kFirstDlc, kPackage);
    ConstructDlcService();
    EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
        .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
    dlc_service_->LoadDlcModuleImages();
  }

  void SetMountPath(const string& mount_path_expected) {
    ON_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
        .WillByDefault(
            DoAll(SetArgPointee<3>(mount_path_expected), Return(true)));
  }

  inline void CheckDlcState(const DlcId& id_in,
                            const DlcState::State& state_in,
                            bool fail = false) {
    DlcState state;
    if (fail) {
      EXPECT_FALSE(dlc_service_->GetState(id_in, &state, err_ptr_));
      return;
    }
    EXPECT_TRUE(dlc_service_->GetState(id_in, &state, err_ptr_));
    EXPECT_EQ(state_in, state.state());
  }

 protected:
  ErrorPtr err_;
  ErrorPtr* err_ptr_{&err_};

  base::MessageLoopForIO base_loop_;
  brillo::BaseMessageLoop loop_{&base_loop_};

  base::ScopedTempDir scoped_temp_dir_;

  base::FilePath testdata_path_;
  base::FilePath manifest_path_;
  base::FilePath preloaded_content_path_;
  base::FilePath content_path_;
  base::FilePath mount_path_;

  std::unique_ptr<MockBootDevice> mock_boot_device_;
  dlcservice::BootSlot::Slot current_slot_;

  using ImageLoaderProxyMock = org::chromium::ImageLoaderInterfaceProxyMock;
  std::unique_ptr<ImageLoaderProxyMock> mock_image_loader_proxy_;
  ImageLoaderProxyMock* mock_image_loader_proxy_ptr_;

  using UpdateEngineProxyMock = org::chromium::UpdateEngineInterfaceProxyMock;
  std::unique_ptr<UpdateEngineProxyMock> mock_update_engine_proxy_;
  UpdateEngineProxyMock* mock_update_engine_proxy_ptr_;

  std::unique_ptr<DlcService> dlc_service_;
  std::unique_ptr<DlcServiceTestObserver> dlc_service_test_observer_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DlcServiceTest);
};

class DlcServiceSkipLoadTest : public DlcServiceTest {
 public:
  // Need this to skip calling |LoadDlcModuleImages()|.
  void SetUp() override {
    SetUpDlcWithSlots(content_path_, kFirstDlc, kPackage);
    ConstructDlcService();
  }
};

class DlcServiceSkipConstructionTest : public DlcServiceTest {
 public:
  // Need this to skip construction of |DlcService|.
  void SetUp() override {}
};

TEST_F(DlcServiceSkipLoadTest, StartUpMountSuccessTest) {
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));

  dlc_service_->LoadDlcModuleImages();

  // Startup successfully to mount.
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceSkipLoadTest, StartUpMountFailureTest) {
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(""), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kFirstDlc, _, _))
      .WillOnce(Return(true));

  dlc_service_->LoadDlcModuleImages();

  // Startup with failure to mount.
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceSkipLoadTest, StartUpImageLoaderFailureTest) {
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kFirstDlc, _, _))
      .WillOnce(Return(true));

  dlc_service_->LoadDlcModuleImages();

  // Startup with image_loader failure.
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceSkipLoadTest, StartUpInactiveImageDoesntExistTest) {
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));

  base::FilePath inactive_image_path =
      GetDlcImagePath(content_path_, kFirstDlc, kPackage,
                      current_slot_ == BootSlot::Slot::A ? BootSlot::Slot::B
                                                         : BootSlot::Slot::A);
  base::DeleteFile(inactive_image_path, false);
  dlc_service_->LoadDlcModuleImages();

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceSkipLoadTest, PreloadAllowedDlcTest) {
  SetUpDlcPreloadAllowed(kFirstDlc, kPackage);
  SetUpDlcWithoutSlots(preloaded_content_path_, kFirstDlc, kPackage);
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kFirstDlc, _, _))
      .WillOnce(Return(true));

  dlc_service_->LoadDlcModuleImages();

  DlcModuleList dlc_module_list;
  EXPECT_TRUE(dlc_service_->GetInstalled(&dlc_module_list, err_ptr_));
  EXPECT_EQ(dlc_module_list.dlc_module_infos_size(), 1);

  DlcModuleInfo dlc_module = dlc_module_list.dlc_module_infos(0);
  EXPECT_EQ(dlc_module.dlc_id(), kFirstDlc);
  EXPECT_FALSE(dlc_module.dlc_root().empty());
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceSkipLoadTest, PreloadNotAllowedDlcTest) {
  SetUpDlcWithoutSlots(preloaded_content_path_, kFirstDlc, kPackage);
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kFirstDlc, _, _))
      .WillOnce(Return(true));

  dlc_service_->LoadDlcModuleImages();

  DlcModuleList dlc_module_list;
  EXPECT_TRUE(dlc_service_->GetInstalled(&dlc_module_list, err_ptr_));
  EXPECT_EQ(dlc_module_list.dlc_module_infos_size(), 0);
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, GetInstalledTest) {
  DlcModuleList dlc_module_list;
  EXPECT_TRUE(dlc_service_->GetInstalled(&dlc_module_list, err_ptr_));
  EXPECT_EQ(dlc_module_list.dlc_module_infos_size(), 1);

  DlcModuleInfo dlc_module = dlc_module_list.dlc_module_infos(0);
  EXPECT_EQ(dlc_module.dlc_id(), kFirstDlc);
  EXPECT_FALSE(dlc_module.dlc_root().empty());
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, UninstallTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kFirstDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));

  EXPECT_TRUE(dlc_service_->Uninstall(kFirstDlc, err_ptr_));
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, UninstallNotInstalledIsValidTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_TRUE(dlc_service_->Uninstall(kSecondDlc, err_ptr_));
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, UninstallFailToSetDlcActiveValueFalse) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kFirstDlc, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));

  EXPECT_TRUE(dlc_service_->Uninstall(kFirstDlc, err_ptr_));
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, UninstallInvalidDlcTest) {
  const auto& id = "invalid-dlc-id";
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_FALSE(dlc_service_->Uninstall(id, err_ptr_));
  CheckDlcState(id, DlcState::NOT_INSTALLED, /*fail=*/true);
}

TEST_F(DlcServiceTest, UninstallUnmountFailureTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(false), Return(true)));

  EXPECT_FALSE(dlc_service_->Uninstall(kFirstDlc, err_ptr_));
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, UninstallImageLoaderFailureTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(Return(false));

  // |ImageLoader| not available.
  EXPECT_FALSE(dlc_service_->Uninstall(kFirstDlc, err_ptr_));
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, UninstallUpdateEngineBusyFailureTest) {
  StatusResult status_result;
  status_result.set_current_operation(Operation::CHECKING_FOR_UPDATE);
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(status_result), Return(true)));

  EXPECT_FALSE(dlc_service_->Uninstall(kFirstDlc, err_ptr_));
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, UninstallUpdatedNeedRebootSuccessTest) {
  StatusResult status_result;
  status_result.set_current_operation(Operation::UPDATED_NEED_REBOOT);
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(status_result), Return(true)));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kFirstDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Uninstall(kFirstDlc, err_ptr_));
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, UninstallInstallingFails) {
  const string omaha_url_default = "";
  DlcModuleList dlc_module_list =
      CreateDlcModuleList({kSecondDlc}, omaha_url_default);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              AttemptInstall(ProtoHasUrl(omaha_url_default), _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  EXPECT_FALSE(dlc_service_->Uninstall(kSecondDlc, err_ptr_));
  EXPECT_EQ(err_->GetCode(), kErrorBusy);
}

TEST_F(DlcServiceTest, UninstallInstallingButInstalledFails) {
  const string omaha_url_default = "";
  DlcModuleList dlc_module_list =
      CreateDlcModuleList({kFirstDlc, kSecondDlc}, omaha_url_default);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              AttemptInstall(ProtoHasUrl(omaha_url_default), _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kFirstDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  EXPECT_FALSE(dlc_service_->Uninstall(kFirstDlc, err_ptr_));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  EXPECT_EQ(err_->GetCode(), kErrorBusy);
}

TEST_F(DlcServiceTest, InstallEmptyDlcModuleListTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_FALSE(dlc_service_->Install({}, err_ptr_));
}

TEST_F(DlcServiceTest, InstallInvalidDlcTest) {
  const auto& id = "bad-dlc-id";
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, id, _, _))
      .WillOnce(Return(true));

  EXPECT_FALSE(dlc_service_->Install(CreateDlcModuleList({id}), err_ptr_));
  CheckDlcState(id, DlcState::NOT_INSTALLED, /*fail=*/true);
}

TEST_F(DlcServiceTest, InstallTest) {
  const string omaha_url_default = "";
  DlcModuleList dlc_module_list =
      CreateDlcModuleList({kSecondDlc}, omaha_url_default);

  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              AttemptInstall(ProtoHasUrl(omaha_url_default), _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  constexpr int expected_permissions = 0755;
  int permissions;
  base::FilePath module_path = JoinPaths(content_path_, kSecondDlc, kPackage);
  base::GetPosixFilePermissions(module_path, &permissions);
  EXPECT_EQ(permissions, expected_permissions);
  base::FilePath image_a_path =
      GetDlcImagePath(content_path_, kSecondDlc, kPackage, BootSlot::Slot::A);
  base::GetPosixFilePermissions(image_a_path.DirName(), &permissions);
  EXPECT_EQ(permissions, expected_permissions);
  base::FilePath image_b_path =
      GetDlcImagePath(content_path_, kSecondDlc, kPackage, BootSlot::Slot::B);
  base::GetPosixFilePermissions(image_b_path.DirName(), &permissions);
  EXPECT_EQ(permissions, expected_permissions);
}

TEST_F(DlcServiceTest, InstallAlreadyInstalledValid) {
  const string omaha_url_default = "";
  DlcModuleList dlc_module_list =
      CreateDlcModuleList({kFirstDlc}, omaha_url_default);

  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kFirstDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, InstallDuplicatesSucceeds) {
  const string omaha_url_default = "";
  DlcModuleList dlc_module_list =
      CreateDlcModuleList({kSecondDlc, kSecondDlc}, omaha_url_default);

  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              AttemptInstall(ProtoHasUrl(omaha_url_default), _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));

  for (const auto& id : {kFirstDlc, kSecondDlc})
    for (const auto& path : {JoinPaths(content_path_, id)})
      EXPECT_TRUE(base::PathExists(path));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
}

TEST_F(DlcServiceTest, InstallAlreadyInstalledAndDuplicatesSucceeds) {
  const string omaha_url_default = "";
  DlcModuleList dlc_module_list = CreateDlcModuleList(
      {kFirstDlc, kSecondDlc, kSecondDlc}, omaha_url_default);

  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              AttemptInstall(ProtoHasUrl(omaha_url_default), _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kFirstDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));

  for (const auto& id : {kFirstDlc, kSecondDlc})
    for (const auto& path : {JoinPaths(content_path_, id)})
      EXPECT_TRUE(base::PathExists(path));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
}

TEST_F(DlcServiceTest, InstallCannotSetDlcActiveValue) {
  const string omaha_url_default = "";
  DlcModuleList dlc_module_list =
      CreateDlcModuleList({kSecondDlc}, omaha_url_default);

  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              AttemptInstall(ProtoHasUrl(omaha_url_default), _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(false));

  EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
}

TEST_F(DlcServiceTest, InstallUpdateEngineDownThenBackUpTest) {
  const string omaha_url_default = "";
  DlcModuleList dlc_module_list =
      CreateDlcModuleList({kSecondDlc}, omaha_url_default);

  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              AttemptInstall(ProtoHasUrl(omaha_url_default), _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_FALSE(dlc_service_->Install(dlc_module_list, err_ptr_));
  EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
}

TEST_F(DlcServiceTest, InstallUpdateEngineBusyThenFreeTest) {
  const string omaha_url_default = "";
  DlcModuleList dlc_module_list =
      CreateDlcModuleList({kSecondDlc}, omaha_url_default);

  SetMountPath(mount_path_.value());
  StatusResult status_result;
  status_result.set_current_operation(Operation::UPDATED_NEED_REBOOT);
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(status_result), Return(true)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              AttemptInstall(ProtoHasUrl(omaha_url_default), _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_FALSE(dlc_service_->Install(dlc_module_list, err_ptr_));
  EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
}

TEST_F(DlcServiceTest, InstallFailureCleansUp) {
  const string omaha_url_default = "";
  DlcModuleList dlc_module_list =
      CreateDlcModuleList({kSecondDlc, kThirdDlc}, omaha_url_default);

  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              AttemptInstall(ProtoHasUrl(omaha_url_default), _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kThirdDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_FALSE(dlc_service_->Install(dlc_module_list, err_ptr_));

  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kThirdDlc)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
  CheckDlcState(kThirdDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, InstallUrlTest) {
  const string omaha_url_override = "http://random.url";
  DlcModuleList dlc_module_list =
      CreateDlcModuleList({kSecondDlc}, omaha_url_override);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));

  dlc_service_->Install(dlc_module_list, err_ptr_);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
}

TEST_F(DlcServiceTest, OnStatusUpdateAdvancedSignalDlcRootTest) {
  const vector<DlcId> ids = {kSecondDlc, kThirdDlc};
  DlcModuleList dlc_module_list = CreateDlcModuleList(ids);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));

  for (const string& id : ids) {
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::INSTALLING);
  }

  StatusResult status_result;
  status_result.set_current_operation(Operation::IDLE);
  status_result.set_is_install(true);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  for (const string& id : ids) {
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::INSTALLED);
  }

  DlcModuleList dlc_module_list_after;
  EXPECT_TRUE(dlc_service_->GetInstalled(&dlc_module_list_after, err_ptr_));
  EXPECT_EQ(dlc_module_list_after.dlc_module_infos_size(), 3);

  for (const DlcModuleInfo& dlc_module :
       dlc_module_list_after.dlc_module_infos())
    EXPECT_FALSE(dlc_module.dlc_root().empty());
}

TEST_F(DlcServiceTest, OnStatusUpdateAdvancedSignalNoRemountTest) {
  const vector<DlcId> ids = {kFirstDlc, kSecondDlc};
  DlcModuleList dlc_module_list = CreateDlcModuleList(ids);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kFirstDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));

  for (const string& id : ids)
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  StatusResult status_result;
  status_result.set_current_operation(Operation::IDLE);
  status_result.set_is_install(true);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  for (const string& id : ids)
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
}

TEST_F(DlcServiceTest, OnStatusUpdateAdvancedSignalTest) {
  const vector<DlcId> ids = {kSecondDlc, kThirdDlc};
  DlcModuleList dlc_module_list = CreateDlcModuleList(ids);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kThirdDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));

  for (const string& id : ids) {
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::INSTALLING);
  }

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)))
      .WillOnce(DoAll(SetArgPointee<3>(""), Return(true)));

  StatusResult status_result;
  status_result.set_current_operation(Operation::IDLE);
  status_result.set_is_install(true);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLED);
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kThirdDlc)));
  CheckDlcState(kThirdDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, ReportingFailureCleanupTest) {
  const vector<DlcId> ids = {kSecondDlc, kThirdDlc};
  DlcModuleList dlc_module_list = CreateDlcModuleList(ids);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kThirdDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));

  for (const string& id : ids) {
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::INSTALLING);
  }

  {
    StatusResult status_result;
    status_result.set_current_operation(Operation::REPORTING_ERROR_EVENT);
    status_result.set_is_install(true);
    dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
  }
  {
    StatusResult status_result;
    status_result.set_current_operation(Operation::IDLE);
    status_result.set_is_install(false);
    dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
  }

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  for (const string& id : ids) {
    EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::NOT_INSTALLED);
  }
}

TEST_F(DlcServiceTest, ReportingFailureSignalTest) {
  const vector<DlcId> ids = {kSecondDlc, kThirdDlc};
  DlcModuleList dlc_module_list = CreateDlcModuleList(ids);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kThirdDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));

  for (const auto& id : ids) {
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::INSTALLING);
  }

  {
    StatusResult status_result;
    status_result.set_current_operation(Operation::REPORTING_ERROR_EVENT);
    status_result.set_is_install(true);
    dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
  }
  {
    StatusResult status_result;
    status_result.set_current_operation(Operation::IDLE);
    status_result.set_is_install(false);
    dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
  }

  EXPECT_EQ(dlc_service_test_observer_->GetInstallStatus().status(),
            Status::FAILED);

  for (const auto& id : ids)
    CheckDlcState(id, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, ProbableUpdateEngineRestartCleanupTest) {
  const vector<DlcId> ids = {kSecondDlc, kThirdDlc};
  DlcModuleList dlc_module_list = CreateDlcModuleList(ids);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kThirdDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));

  for (const string& id : ids) {
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::INSTALLING);
  }

  StatusResult status_result;
  status_result.set_current_operation(Operation::IDLE);
  status_result.set_is_install(false);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  for (const string& id : ids) {
    EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::NOT_INSTALLED);
  }
}

TEST_F(DlcServiceTest, UpdateEngineFailSafeTest) {
  const vector<DlcId> ids = {kSecondDlc};
  DlcModuleList dlc_module_list = CreateDlcModuleList(ids);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));

  for (const string& id : ids) {
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::INSTALLING);
  }

  MessageLoopRunUntil(
      &loop_, base::TimeDelta::FromSeconds(DlcService::kUECheckTimeout * 2),
      base::Bind([]() { return false; }));

  for (const string& id : ids) {
    EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::NOT_INSTALLED);
  }
}

TEST_F(DlcServiceTest, UpdateEngineFailAfterSignalsSafeTest) {
  const vector<DlcId> ids = {kSecondDlc};
  DlcModuleList dlc_module_list = CreateDlcModuleList(ids);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));

  for (const string& id : ids) {
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::INSTALLING);
  }

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));

  StatusResult status_result;
  status_result.set_current_operation(Operation::DOWNLOADING);
  status_result.set_is_install(true);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  MessageLoopRunUntil(
      &loop_, base::TimeDelta::FromSeconds(DlcService::kUECheckTimeout * 2),
      base::Bind([]() { return false; }));

  for (const string& id : ids) {
    EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::NOT_INSTALLED);
  }
}

TEST_F(DlcServiceTest, OnStatusUpdateAdvancedSignalDownloadProgressTest) {
  const vector<DlcId> ids = {kSecondDlc, kThirdDlc};
  DlcModuleList dlc_module_list = CreateDlcModuleList(ids);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));

  for (const auto& id : ids)
    CheckDlcState(id, DlcState::INSTALLING);

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));

  StatusResult status_result;
  status_result.set_is_install(true);

  const vector<Operation> install_operation_sequence = {
      Operation::CHECKING_FOR_UPDATE, Operation::UPDATE_AVAILABLE,
      Operation::FINALIZING};

  for (const auto& op : install_operation_sequence) {
    status_result.set_current_operation(op);
    dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
    EXPECT_FALSE(dlc_service_test_observer_->IsSendInstallStatusCalled());
  }

  status_result.set_current_operation(Operation::DOWNLOADING);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
  EXPECT_EQ(dlc_service_test_observer_->GetInstallStatus().status(),
            Status::RUNNING);

  status_result.set_current_operation(Operation::IDLE);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
  EXPECT_EQ(dlc_service_test_observer_->GetInstallStatus().status(),
            Status::COMPLETED);

  for (const auto& id : ids)
    CheckDlcState(id, DlcState::INSTALLED);
}

TEST_F(
    DlcServiceTest,
    OnStatusUpdateAdvancedSignalSubsequentialBadOrNonInstalledDlcsNonBlocking) {
  const vector<DlcId> ids = {kSecondDlc};
  DlcModuleList dlc_module_list = CreateDlcModuleList(ids);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));

  for (int i = 0; i < 5; i++) {
    EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_update_engine_proxy_ptr_,
                SetDlcActiveValue(true, kSecondDlc, _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_update_engine_proxy_ptr_,
                SetDlcActiveValue(false, kSecondDlc, _, _))
        .WillOnce(Return(true));
    EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));
    for (const auto& id : ids)
      CheckDlcState(id, DlcState::INSTALLING);

    EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
        .WillOnce(Return(false));
    StatusResult status_result;
    status_result.set_is_install(true);
    status_result.set_current_operation(Operation::IDLE);
    dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
    for (const auto& id : ids) {
      EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, id)));
      CheckDlcState(id, DlcState::NOT_INSTALLED);
    }
  }
}

TEST_F(DlcServiceTest, PeriodCheckUpdateEngineInstallSignalRaceChecker) {
  const vector<DlcId> ids = {kSecondDlc, kThirdDlc};
  DlcModuleList dlc_module_list = CreateDlcModuleList(ids);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kThirdDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install(dlc_module_list, err_ptr_));

  MessageLoopRunUntil(
      &loop_, base::TimeDelta::FromSeconds(DlcService::kUECheckTimeout * 5),
      base::Bind([]() { return false; }));

  for (const string& id : ids) {
    EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::NOT_INSTALLED);
  }
}

TEST_F(DlcServiceTest, StrongerInstalledDlcRefresh) {
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));

  base::FilePath root_path;
  {
    DlcModuleList dlc_module_list;
    EXPECT_TRUE(dlc_service_->GetInstalled(&dlc_module_list, err_ptr_));
    EXPECT_EQ(dlc_module_list.dlc_module_infos_size(), 1);
    const auto& dlc_info = dlc_module_list.dlc_module_infos(0);
    EXPECT_EQ(dlc_info.dlc_id(), kFirstDlc);
    root_path = base::FilePath(dlc_info.dlc_root());
    EXPECT_TRUE(base::PathExists(root_path));
  }

  // Mimic a force deletion of DLC.
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kFirstDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_TRUE(base::DeleteFile(root_path, true));
  {
    DlcModuleList dlc_module_list;
    EXPECT_TRUE(dlc_service_->GetInstalled(&dlc_module_list, err_ptr_));
    EXPECT_EQ(dlc_module_list.dlc_module_infos_size(), 0);
    EXPECT_FALSE(base::PathExists(root_path));
  }

  // Mimic a force creation of DLC.
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(root_path.value()), Return(true)));
  SetUpDlcWithSlots(content_path_, kFirstDlc, kPackage);
  EXPECT_TRUE(base::CreateDirectory(root_path));
  {
    DlcModuleList dlc_module_list;
    EXPECT_TRUE(dlc_service_->GetInstalled(&dlc_module_list, err_ptr_));
    EXPECT_EQ(dlc_module_list.dlc_module_infos_size(), 1);
    const auto& dlc_info = dlc_module_list.dlc_module_infos(0);
    EXPECT_EQ(dlc_info.dlc_id(), kFirstDlc);
    EXPECT_TRUE(base::PathExists(root_path));
  }
}

TEST_F(DlcServiceTest, MimicUpdateRebootWherePreallocatedSizeIncreasedTest) {
  // Check A and B images.
  for (const auto& slot : {kDlcDirAName, kDlcDirBName})
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc, kPackage,
                                           slot, kDlcImageFileName)));

  base::FilePath inactive_img_path =
      GetDlcImagePath(content_path_, kFirstDlc, kPackage,
                      current_slot_ == BootSlot::Slot::A ? BootSlot::Slot::B
                                                         : BootSlot::Slot::A);

  imageloader::Manifest manifest;
  dlcservice::GetDlcManifest(manifest_path_, kFirstDlc, kPackage, &manifest);
  int64_t inactive_img_size = manifest.preallocated_size();
  int64_t new_inactive_img_size = inactive_img_size / 2;
  EXPECT_TRUE(new_inactive_img_size < inactive_img_size);

  ResizeImageFile(inactive_img_path, new_inactive_img_size);
  EXPECT_EQ(new_inactive_img_size, GetFileSize(inactive_img_path));

  DlcModuleList dlc_module_list;
  EXPECT_TRUE(dlc_service_->GetInstalled(&dlc_module_list, err_ptr_));

  EXPECT_EQ(inactive_img_size, GetFileSize(inactive_img_path));
}

}  // namespace dlcservice
