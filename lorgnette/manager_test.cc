// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/manager.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/process.h>
#include <brillo/variant_dictionary.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <sane/sane.h>

#include "lorgnette/sane_client_fake.h"
#include "lorgnette/sane_client_impl.h"

using base::ScopedFD;
using brillo::VariantDictionary;

namespace lorgnette {

class ManagerTest : public testing::Test {
 protected:
  ManagerTest()
      : sane_client_(new SaneClientFake()),
        manager_(base::Callback<void()>(),
                 std::unique_ptr<SaneClient>(sane_client_)),
        metrics_library_(new MetricsLibraryMock) {
    manager_.metrics_library_.reset(metrics_library_);
  }

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    output_path_ = temp_dir_.GetPath().Append("scan_data.png");
  }

  void ExpectScanSuccess() {
    EXPECT_CALL(*metrics_library_, SendEnumToUMA(Manager::kMetricScanResult,
                                                 Manager::kBooleanMetricSuccess,
                                                 Manager::kBooleanMetricMax));
  }

  void ExpectScanFailure() {
    EXPECT_CALL(*metrics_library_, SendEnumToUMA(Manager::kMetricScanResult,
                                                 Manager::kBooleanMetricFailure,
                                                 Manager::kBooleanMetricMax));
  }

  void CompareImages(const std::string& path_a, const std::string& path_b) {
    brillo::ProcessImpl diff;
    diff.AddArg("/usr/bin/perceptualdiff");
    diff.AddArg("-verbose");
    diff.AddIntOption("-threshold", 1);
    diff.AddArg(path_a);
    diff.AddArg(path_b);
    EXPECT_EQ(diff.Run(), 0)
        << path_a << " and " << path_b << " are not the same image";
  }

  SaneClientFake* sane_client_;
  Manager manager_;
  MetricsLibraryMock* metrics_library_;  // Owned by manager_.
  base::ScopedTempDir temp_dir_;
  base::FilePath output_path_;
};

TEST_F(ManagerTest, ScanBlackAndWhiteSuccess) {
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(base::FilePath("./test_images/bw.pnm"),
                                     &contents));
  std::vector<uint8_t> image_data(contents.begin(), contents.end());
  std::unique_ptr<SaneDeviceFake> device = std::make_unique<SaneDeviceFake>();
  device->SetScanData(image_data);

  ScanParameters parameters;
  parameters.format = kGrayscale;
  parameters.bytes_per_line = 11;
  parameters.pixels_per_line = 85;
  parameters.lines = 29;
  parameters.depth = 1;
  device->SetScanParameters(parameters);

  sane_client_->SetDeviceForName("TestDevice", std::move(device));

  base::File scan(output_path_,
                  base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(scan.IsValid());
  base::ScopedFD scan_fd(scan.TakePlatformFile());

  ExpectScanSuccess();
  EXPECT_TRUE(manager_.ScanImage(nullptr, "TestDevice", scan_fd,
                                 brillo::VariantDictionary()));
  CompareImages("./test_images/bw.png", output_path_.value());
}

TEST_F(ManagerTest, ScanGrayscaleSuccess) {
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(base::FilePath("./test_images/gray.pnm"),
                                     &contents));
  std::vector<uint8_t> image_data(contents.begin(), contents.end());
  std::unique_ptr<SaneDeviceFake> device = std::make_unique<SaneDeviceFake>();
  device->SetScanData(image_data);

  ScanParameters parameters;
  parameters.format = kGrayscale;
  parameters.pixels_per_line = 32;
  parameters.lines = 32;
  parameters.depth = 8;
  parameters.bytes_per_line = parameters.pixels_per_line * parameters.depth / 8;
  device->SetScanParameters(parameters);

  sane_client_->SetDeviceForName("TestDevice", std::move(device));

  base::File scan(output_path_,
                  base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(scan.IsValid());
  base::ScopedFD scan_fd(scan.TakePlatformFile());

  ExpectScanSuccess();
  EXPECT_TRUE(manager_.ScanImage(nullptr, "TestDevice", scan_fd,
                                 brillo::VariantDictionary()));
  CompareImages("./test_images/gray.png", output_path_.value());
}

TEST_F(ManagerTest, ScanColorSuccess) {
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(base::FilePath("./test_images/color.pnm"),
                                     &contents));
  std::vector<uint8_t> image_data(contents.begin(), contents.end());
  std::unique_ptr<SaneDeviceFake> device = std::make_unique<SaneDeviceFake>();
  device->SetScanData(image_data);

  ScanParameters parameters;
  parameters.format = kRGB;
  parameters.bytes_per_line = 98 * 3;
  parameters.pixels_per_line = 98;
  parameters.lines = 50;
  parameters.depth = 8;
  device->SetScanParameters(parameters);

  sane_client_->SetDeviceForName("TestDevice", std::move(device));

  base::File scan(output_path_,
                  base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(scan.IsValid());
  base::ScopedFD scan_fd(scan.TakePlatformFile());

  ExpectScanSuccess();
  EXPECT_TRUE(manager_.ScanImage(nullptr, "TestDevice", scan_fd,
                                 brillo::VariantDictionary()));
  CompareImages("./test_images/color.png", output_path_.value());
}

TEST_F(ManagerTest, ScanFailNoDevice) {
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(base::FilePath("./test_images/color.pnm"),
                                     &contents));
  std::vector<uint8_t> image_data(contents.begin(), contents.end());

  base::File scan(output_path_,
                  base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(scan.IsValid());
  base::ScopedFD scan_fd(scan.TakePlatformFile());

  EXPECT_FALSE(manager_.ScanImage(nullptr, "TestDevice", scan_fd,
                                  brillo::VariantDictionary()));
}

TEST_F(ManagerTest, ScanFailToStart) {
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(base::FilePath("./test_images/color.pnm"),
                                     &contents));
  std::vector<uint8_t> image_data(contents.begin(), contents.end());
  std::unique_ptr<SaneDeviceFake> device = std::make_unique<SaneDeviceFake>();
  device->SetScanData(image_data);
  device->SetStartScanResult(false);
  sane_client_->SetDeviceForName("TestDevice", std::move(device));

  base::File scan(output_path_,
                  base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(scan.IsValid());
  base::ScopedFD scan_fd(scan.TakePlatformFile());

  ExpectScanFailure();
  EXPECT_FALSE(manager_.ScanImage(nullptr, "TestDevice", scan_fd,
                                  brillo::VariantDictionary()));
}

TEST_F(ManagerTest, ScanFailToRead) {
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(base::FilePath("./test_images/color.pnm"),
                                     &contents));
  std::vector<uint8_t> image_data(contents.begin(), contents.end());
  std::unique_ptr<SaneDeviceFake> device = std::make_unique<SaneDeviceFake>();
  device->SetScanData(image_data);
  device->SetReadScanDataResult(false);
  sane_client_->SetDeviceForName("TestDevice", std::move(device));

  base::File scan(output_path_,
                  base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(scan.IsValid());
  base::ScopedFD scan_fd(scan.TakePlatformFile());

  ExpectScanFailure();
  EXPECT_FALSE(manager_.ScanImage(nullptr, "TestDevice", scan_fd,
                                  brillo::VariantDictionary()));
}

TEST_F(ManagerTest, ScanFailBadFd) {
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(base::FilePath("./test_images/color.pnm"),
                                     &contents));
  std::vector<uint8_t> image_data(contents.begin(), contents.end());
  std::unique_ptr<SaneDeviceFake> device = std::make_unique<SaneDeviceFake>();
  device->SetScanData(image_data);
  device->SetStartScanResult(true);
  sane_client_->SetDeviceForName("TestDevice", std::move(device));

  base::ScopedFD scan_fd;
  ExpectScanFailure();
  EXPECT_FALSE(manager_.ScanImage(nullptr, "TestDevice", scan_fd,
                                  brillo::VariantDictionary()));
}

class SaneClientTest : public testing::Test {
 protected:
  void SetUp() override {
    dev_ = CreateTestDevice();
    dev_two_ = CreateTestDevice();
  }

  static SANE_Device CreateTestDevice() {
    SANE_Device dev;
    dev.name = "Test Name";
    dev.vendor = "Test Vendor";
    dev.model = "Test Model";
    dev.type = "film scanner";

    return dev;
  }

  SANE_Device dev_;
  SANE_Device dev_two_;
  const SANE_Device* empty_devices_[1] = {NULL};
  const SANE_Device* one_device_[2] = {&dev_, NULL};
  const SANE_Device* two_devices_[3] = {&dev_, &dev_two_, NULL};

  Manager::ScannerInfo info_;
};

TEST_F(SaneClientTest, ScannerInfoFromDeviceListInvalidParameters) {
  EXPECT_FALSE(SaneClientImpl::DeviceListToScannerInfo(NULL, NULL));
  EXPECT_FALSE(SaneClientImpl::DeviceListToScannerInfo(one_device_, NULL));
  EXPECT_FALSE(SaneClientImpl::DeviceListToScannerInfo(NULL, &info_));
}

TEST_F(SaneClientTest, ScannerInfoFromDeviceListNoDevices) {
  EXPECT_TRUE(SaneClientImpl::DeviceListToScannerInfo(empty_devices_, &info_));
  EXPECT_EQ(info_.size(), 0);
}

TEST_F(SaneClientTest, ScannerInfoFromDeviceListOneDevice) {
  EXPECT_TRUE(SaneClientImpl::DeviceListToScannerInfo(one_device_, &info_));
  EXPECT_EQ(info_.size(), 1);
  EXPECT_EQ(info_.count(dev_.name), 1);
  EXPECT_EQ(info_[dev_.name]["Manufacturer"], dev_.vendor);
  EXPECT_EQ(info_[dev_.name]["Model"], dev_.model);
  EXPECT_EQ(info_[dev_.name]["Type"], dev_.type);
}

TEST_F(SaneClientTest, ScannerInfoFromDeviceListNullFields) {
  dev_ = CreateTestDevice();
  dev_.name = NULL;
  EXPECT_TRUE(SaneClientImpl::DeviceListToScannerInfo(one_device_, &info_));
  EXPECT_EQ(info_.size(), 0);

  dev_ = CreateTestDevice();
  dev_.vendor = NULL;
  EXPECT_TRUE(SaneClientImpl::DeviceListToScannerInfo(one_device_, &info_));
  EXPECT_EQ(info_.size(), 1);
  EXPECT_EQ(info_.count(dev_.name), 1);
  EXPECT_EQ(info_[dev_.name]["Manufacturer"], "");
  EXPECT_EQ(info_[dev_.name]["Model"], dev_.model);
  EXPECT_EQ(info_[dev_.name]["Type"], dev_.type);

  dev_ = CreateTestDevice();
  dev_.model = NULL;
  EXPECT_TRUE(SaneClientImpl::DeviceListToScannerInfo(one_device_, &info_));
  EXPECT_EQ(info_.size(), 1);
  EXPECT_EQ(info_.count(dev_.name), 1);
  EXPECT_EQ(info_[dev_.name]["Manufacturer"], dev_.vendor);
  EXPECT_EQ(info_[dev_.name]["Model"], "");
  EXPECT_EQ(info_[dev_.name]["Type"], dev_.type);

  dev_ = CreateTestDevice();
  dev_.type = NULL;
  EXPECT_TRUE(SaneClientImpl::DeviceListToScannerInfo(one_device_, &info_));
  EXPECT_EQ(info_.size(), 1);
  EXPECT_EQ(info_.count(dev_.name), 1);
  EXPECT_EQ(info_[dev_.name]["Manufacturer"], dev_.vendor);
  EXPECT_EQ(info_[dev_.name]["Model"], dev_.model);
  EXPECT_EQ(info_[dev_.name]["Type"], "");
}

TEST_F(SaneClientTest, ScannerInfoFromDeviceListMultipleDevices) {
  EXPECT_FALSE(SaneClientImpl::DeviceListToScannerInfo(two_devices_, &info_));

  dev_two_.name = "Test Device 2";
  dev_two_.vendor = "Test Vendor 2";
  EXPECT_TRUE(SaneClientImpl::DeviceListToScannerInfo(two_devices_, &info_));
  EXPECT_EQ(info_.size(), 2);
  EXPECT_EQ(info_.count(dev_.name), 1);
  EXPECT_EQ(info_[dev_.name]["Manufacturer"], dev_.vendor);
  EXPECT_EQ(info_[dev_.name]["Model"], dev_.model);
  EXPECT_EQ(info_[dev_.name]["Type"], dev_.type);

  EXPECT_EQ(info_.count(dev_two_.name), 1);
  EXPECT_EQ(info_[dev_two_.name]["Manufacturer"], dev_two_.vendor);
  EXPECT_EQ(info_[dev_two_.name]["Model"], dev_two_.model);
  EXPECT_EQ(info_[dev_two_.name]["Type"], dev_.type);
}

}  // namespace lorgnette
