// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_TEST_UTILS_H_
#define DLCSERVICE_TEST_UTILS_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <update_engine/proto_bindings/update_engine.pb.h>
#include <imageloader/dbus-proxy-mocks.h>
#include <update_engine/dbus-proxy-mocks.h>

#include "dlcservice/dlc.h"
#include "dlcservice/dlc_service.h"

namespace dlcservice {

extern const char kFirstDlc[];
extern const char kSecondDlc[];
extern const char kThirdDlc[];
extern const char kPackage[];
extern const char kDefaultOmahaUrl[];

class BaseTest : public testing::Test {
 public:
  BaseTest();

  void SetUp() override;

  void SetUpFilesAndDirectories();

  int64_t GetFileSize(const base::FilePath& path);

  void ResizeImageFile(const base::FilePath& image_path, int64_t image_size);

  void CreateImageFileWithRightSize(const base::FilePath& image_path,
                                    const base::FilePath& manifest_path,
                                    const std::string& id,
                                    const std::string& package);

  // Will create |path|/|id|/|package|/dlc.img file.
  void SetUpDlcWithoutSlots(const std::string& id);

  // Will create |path/|id|/|package|/dlc_[a|b]/dlc.img files.
  void SetUpDlcWithSlots(const std::string& id);

  void SetMountPath(const std::string& mount_path_expected);

 protected:
  brillo::ErrorPtr err_;

  base::ScopedTempDir scoped_temp_dir_;

  base::FilePath testdata_path_;
  base::FilePath manifest_path_;
  base::FilePath preloaded_content_path_;
  base::FilePath content_path_;
  base::FilePath prefs_path_;
  base::FilePath mount_path_;

  using ImageLoaderProxyMock = org::chromium::ImageLoaderInterfaceProxyMock;
  std::unique_ptr<ImageLoaderProxyMock> mock_image_loader_proxy_;
  ImageLoaderProxyMock* mock_image_loader_proxy_ptr_;

  using UpdateEngineProxyMock = org::chromium::UpdateEngineInterfaceProxyMock;
  std::unique_ptr<UpdateEngineProxyMock> mock_update_engine_proxy_;
  UpdateEngineProxyMock* mock_update_engine_proxy_ptr_;

 private:
  DISALLOW_COPY_AND_ASSIGN(BaseTest);
};

}  // namespace dlcservice

#endif  // DLCSERVICE_TEST_UTILS_H_
