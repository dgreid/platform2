// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/manager.h"

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/time/time.h>
#include <brillo/any.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <brillo/process/process.h>
#include <brillo/variant_dictionary.h>
#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>
#include <metrics/metrics_library_mock.h>
#include <sane/sane.h>

#include "lorgnette/sane_client_fake.h"
#include "lorgnette/sane_client_impl.h"

using base::ScopedFD;
using brillo::VariantDictionary;
using brillo::dbus_utils::MockDBusMethodResponse;
using ::testing::ElementsAre;

namespace lorgnette {

#define ALIGN_UP(val, align) (((val) + (align)-1) & ~((align)-1))

class ManagerTest : public testing::Test {
 protected:
  ManagerTest()
      : sane_client_(new SaneClientFake()),
        manager_(base::Callback<void()>(),
                 std::unique_ptr<SaneClient>(sane_client_)),
        metrics_library_(new MetricsLibraryMock) {
    manager_.metrics_library_.reset(metrics_library_);
    manager_.SetProgressSignalInterval(base::TimeDelta::FromSeconds(0));
  }

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    output_path_ = temp_dir_.GetPath().Append("scan_data.png");
    base::File scan(output_path_,
                    base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    ASSERT_TRUE(scan.IsValid());
    scan_fd_ = base::ScopedFD(scan.TakePlatformFile());

    dbus_response_ =
        std::make_unique<MockDBusMethodResponse<std::vector<uint8_t>>>();
    dbus_response_->set_return_callback(base::BindRepeating(
        [](StartScanResponse* response_out,
           const std::vector<uint8_t>& serialized_response) {
          ASSERT_TRUE(response_out);
          ASSERT_TRUE(response_out->ParseFromArray(serialized_response.data(),
                                                   serialized_response.size()));
        },
        base::Unretained(&response_)));

    manager_.SetScanStatusChangedSignalSenderForTest(base::BindRepeating(
        [](std::vector<ScanStatusChangedSignal>* signals,
           const ScanStatusChangedSignal& signal) {
          signals->push_back(signal);
        },
        base::Unretained(&signals_)));
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

  void ValidateProgressSignals(const std::string& scan_uuid) {
    int progress = 0;
    for (auto it = signals_.begin();
         it != signals_.end() && next(it) != signals_.end(); it++) {
      const ScanStatusChangedSignal& signal = *it;
      EXPECT_EQ(signal.scan_uuid(), scan_uuid);
      EXPECT_EQ(signal.state(), SCAN_STATE_IN_PROGRESS);
      EXPECT_GT(signal.progress(), progress);
      progress = signal.progress();
    }
  }

  void SetUpTestDevice(const std::string& name,
                       const base::FilePath& image_path,
                       const ScanParameters& parameters) {
    std::string contents;
    ASSERT_TRUE(base::ReadFileToString(image_path, &contents));
    std::vector<uint8_t> image_data(contents.begin(), contents.end());
    std::unique_ptr<SaneDeviceFake> device = std::make_unique<SaneDeviceFake>();
    device->SetScanData(image_data);
    device->SetScanParameters(parameters);
    sane_client_->SetDeviceForName(name, std::move(device));
  }

  std::unique_ptr<MockDBusMethodResponse<std::vector<uint8_t>>> dbus_response_;
  StartScanResponse response_;
  std::vector<ScanStatusChangedSignal> signals_;

  SaneClientFake* sane_client_;
  Manager manager_;
  MetricsLibraryMock* metrics_library_;  // Owned by manager_.
  base::ScopedTempDir temp_dir_;
  base::FilePath output_path_;
  base::ScopedFD scan_fd_;
};

TEST_F(ManagerTest, GetScannerCapabilitiesSuccess) {
  std::unique_ptr<SaneDeviceFake> device = std::make_unique<SaneDeviceFake>();
  ValidOptionValues opts;
  opts.resolutions = {100, 200, 300, 600};
  opts.sources = {"FB", "Negative", "Automatic Document Feeder"};
  opts.color_modes = {kScanPropertyModeColor};
  device->SetValidOptionValues(opts);
  sane_client_->SetDeviceForName("TestDevice", std::move(device));

  std::vector<uint8_t> serialized;
  EXPECT_TRUE(
      manager_.GetScannerCapabilities(nullptr, "TestDevice", &serialized));

  ScannerCapabilities caps;
  EXPECT_TRUE(caps.ParseFromArray(serialized.data(), serialized.size()));

  EXPECT_THAT(caps.resolutions(), ElementsAre(100, 200, 300, 600));

  EXPECT_EQ(caps.sources().size(), 2);
  EXPECT_EQ(caps.sources()[0].type(), SOURCE_PLATEN);
  EXPECT_EQ(caps.sources()[0].name(), "FB");
  EXPECT_EQ(caps.sources()[1].type(), SOURCE_ADF_SIMPLEX);
  EXPECT_EQ(caps.sources()[1].name(), "Automatic Document Feeder");

  EXPECT_THAT(caps.color_modes(), ElementsAre(MODE_COLOR));
}

TEST_F(ManagerTest, ScanBlackAndWhiteSuccess) {
  ScanParameters parameters;
  parameters.format = kGrayscale;
  parameters.pixels_per_line = 85;
  parameters.lines = 29;
  parameters.depth = 1;
  parameters.bytes_per_line = ALIGN_UP(parameters.pixels_per_line, 8) / 8;
  SetUpTestDevice("TestDevice", base::FilePath("./test_images/bw.pnm"),
                  parameters);

  brillo::VariantDictionary args;
  args[kScanPropertyMode] = brillo::Any(std::string(kScanPropertyModeLineart));

  ExpectScanSuccess();
  EXPECT_TRUE(manager_.ScanImage(nullptr, "TestDevice", scan_fd_, args));
  CompareImages("./test_images/bw.png", output_path_.value());
}

TEST_F(ManagerTest, ScanGrayscaleSuccess) {
  ScanParameters parameters;
  parameters.format = kGrayscale;
  parameters.pixels_per_line = 32;
  parameters.lines = 32;
  parameters.depth = 8;
  parameters.bytes_per_line = parameters.pixels_per_line * parameters.depth / 8;
  SetUpTestDevice("TestDevice", base::FilePath("./test_images/gray.pnm"),
                  parameters);

  brillo::VariantDictionary args;
  args[kScanPropertyMode] = brillo::Any(std::string(kScanPropertyModeGray));

  ExpectScanSuccess();
  EXPECT_TRUE(manager_.ScanImage(nullptr, "TestDevice", scan_fd_, args));
  CompareImages("./test_images/gray.png", output_path_.value());
}

TEST_F(ManagerTest, ScanColorSuccess) {
  ScanParameters parameters;
  parameters.format = kRGB;
  parameters.bytes_per_line = 98 * 3;
  parameters.pixels_per_line = 98;
  parameters.lines = 50;
  parameters.depth = 8;
  SetUpTestDevice("TestDevice", base::FilePath("./test_images/color.pnm"),
                  parameters);

  brillo::VariantDictionary args;
  args[kScanPropertyMode] = brillo::Any(std::string(kScanPropertyModeColor));

  ExpectScanSuccess();
  EXPECT_TRUE(manager_.ScanImage(nullptr, "TestDevice", scan_fd_, args));
  CompareImages("./test_images/color.png", output_path_.value());
}

TEST_F(ManagerTest, Scan16BitColorSuccess) {
  ScanParameters parameters;
  parameters.format = kRGB;
  parameters.pixels_per_line = 32;
  parameters.lines = 32;
  parameters.depth = 16;
  parameters.bytes_per_line =
      parameters.pixels_per_line * parameters.depth / 8 * 3;
  // Note: technically, color16.pnm does not really contain PNM data, since
  // NetPBM assumes big endian 16-bit samples. Since SANE provides
  // endian-native samples, color16.pnm stores the samples as little-endian.
  SetUpTestDevice("TestDevice", base::FilePath("./test_images/color16.pnm"),
                  parameters);

  brillo::VariantDictionary args;
  args[kScanPropertyMode] = brillo::Any(std::string(kScanPropertyModeColor));

  ExpectScanSuccess();
  EXPECT_TRUE(manager_.ScanImage(nullptr, "TestDevice", scan_fd_, args));
  CompareImages("./test_images/color16.png", output_path_.value());
}

TEST_F(ManagerTest, ScanFailNoDevice) {
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(base::FilePath("./test_images/color.pnm"),
                                     &contents));
  std::vector<uint8_t> image_data(contents.begin(), contents.end());

  EXPECT_FALSE(manager_.ScanImage(nullptr, "TestDevice", scan_fd_,
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

  ExpectScanFailure();
  EXPECT_FALSE(manager_.ScanImage(nullptr, "TestDevice", scan_fd_,
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

  ExpectScanFailure();
  EXPECT_FALSE(manager_.ScanImage(nullptr, "TestDevice", scan_fd_,
                                  brillo::VariantDictionary()));
}

TEST_F(ManagerTest, ScanFailBadFd) {
  SetUpTestDevice("TestDevice", base::FilePath("./test_images/color.pnm"),
                  ScanParameters());
  ExpectScanFailure();
  EXPECT_FALSE(manager_.ScanImage(nullptr, "TestDevice", base::ScopedFD(),
                                  brillo::VariantDictionary()));
}

TEST_F(ManagerTest, ScanFailBadArgs) {
  SetUpTestDevice("TestDevice", base::FilePath("./test_images/color.pnm"),
                  ScanParameters());

  brillo::VariantDictionary args;
  // Const char *, not std::string.
  args[kScanPropertyMode] = brillo::Any(kScanPropertyModeColor);
  EXPECT_FALSE(manager_.ScanImage(nullptr, "TestDevice", scan_fd_, args));

  // Invalid value.
  args[kScanPropertyMode] = brillo::Any(std::string("InvalidMode"));
  EXPECT_FALSE(manager_.ScanImage(nullptr, "TestDevice", scan_fd_, args));

  args = brillo::VariantDictionary();
  // Invalid name.
  args["Invalid argument name"] = brillo::Any((uint32_t)100);
  EXPECT_FALSE(manager_.ScanImage(nullptr, "TestDevice", scan_fd_, args));

  args = brillo::VariantDictionary();
  // Invalid argument type.
  args[kScanPropertyResolution] = brillo::Any("100");
  EXPECT_FALSE(manager_.ScanImage(nullptr, "TestDevice", scan_fd_, args));
}

TEST_F(ManagerTest, StartScanBlackAndWhiteSuccess) {
  ScanParameters parameters;
  parameters.format = kGrayscale;
  parameters.bytes_per_line = 11;
  parameters.pixels_per_line = 85;
  parameters.lines = 29;
  parameters.depth = 1;
  SetUpTestDevice("TestDevice", base::FilePath("./test_images/bw.pnm"),
                  parameters);

  StartScanRequest request;
  request.set_device_name("TestDevice");
  request.mutable_settings()->set_color_mode(MODE_LINEART);

  ExpectScanSuccess();
  manager_.StartScan(std::move(dbus_response_), impl::SerializeProto(request),
                     scan_fd_);

  EXPECT_EQ(response_.state(), SCAN_STATE_IN_PROGRESS);
  EXPECT_NE(response_.scan_uuid(), "");

  EXPECT_GE(signals_.size(), 1);
  EXPECT_EQ(signals_.back().scan_uuid(), response_.scan_uuid());
  EXPECT_EQ(signals_.back().state(), SCAN_STATE_COMPLETED);
  ValidateProgressSignals(response_.scan_uuid());

  CompareImages("./test_images/bw.png", output_path_.value());
}

TEST_F(ManagerTest, StartScanGrayscaleSuccess) {
  ScanParameters parameters;
  parameters.format = kGrayscale;
  parameters.pixels_per_line = 32;
  parameters.lines = 32;
  parameters.depth = 8;
  parameters.bytes_per_line = parameters.pixels_per_line * parameters.depth / 8;
  SetUpTestDevice("TestDevice", base::FilePath("./test_images/gray.pnm"),
                  parameters);

  StartScanRequest request;
  request.set_device_name("TestDevice");
  request.mutable_settings()->set_color_mode(MODE_GRAYSCALE);

  ExpectScanSuccess();
  manager_.StartScan(std::move(dbus_response_), impl::SerializeProto(request),
                     scan_fd_);

  EXPECT_EQ(response_.state(), SCAN_STATE_IN_PROGRESS);
  EXPECT_NE(response_.scan_uuid(), "");

  EXPECT_GE(signals_.size(), 1);
  EXPECT_EQ(signals_.back().scan_uuid(), response_.scan_uuid());
  EXPECT_EQ(signals_.back().state(), SCAN_STATE_COMPLETED);
  ValidateProgressSignals(response_.scan_uuid());

  CompareImages("./test_images/gray.png", output_path_.value());
}

TEST_F(ManagerTest, StartScanColorSuccess) {
  ScanParameters parameters;
  parameters.format = kRGB;
  parameters.bytes_per_line = 98 * 3;
  parameters.pixels_per_line = 98;
  parameters.lines = 50;
  parameters.depth = 8;
  SetUpTestDevice("TestDevice", base::FilePath("./test_images/color.pnm"),
                  parameters);

  StartScanRequest request;
  request.set_device_name("TestDevice");
  request.mutable_settings()->set_color_mode(MODE_COLOR);

  ExpectScanSuccess();
  manager_.StartScan(std::move(dbus_response_), impl::SerializeProto(request),
                     scan_fd_);

  EXPECT_EQ(response_.state(), SCAN_STATE_IN_PROGRESS);
  EXPECT_NE(response_.scan_uuid(), "");

  EXPECT_GE(signals_.size(), 1);
  EXPECT_EQ(signals_.back().scan_uuid(), response_.scan_uuid());
  EXPECT_EQ(signals_.back().state(), SCAN_STATE_COMPLETED);
  ValidateProgressSignals(response_.scan_uuid());

  CompareImages("./test_images/color.png", output_path_.value());
}

TEST_F(ManagerTest, StartScan16BitColorSuccess) {
  ScanParameters parameters;
  parameters.format = kRGB;
  parameters.pixels_per_line = 32;
  parameters.lines = 32;
  parameters.depth = 16;
  parameters.bytes_per_line =
      parameters.pixels_per_line * parameters.depth / 8 * 3;
  // Note: technically, color16.pnm does not really contain PNM data, since
  // NetPBM assumes big endian 16-bit samples. Since SANE provides
  // endian-native samples, color16.pnm stores the samples as little-endian.
  SetUpTestDevice("TestDevice", base::FilePath("./test_images/color16.pnm"),
                  parameters);

  StartScanRequest request;
  request.set_device_name("TestDevice");
  request.mutable_settings()->set_color_mode(MODE_COLOR);

  ExpectScanSuccess();
  manager_.StartScan(std::move(dbus_response_), impl::SerializeProto(request),
                     scan_fd_);

  EXPECT_EQ(response_.state(), SCAN_STATE_IN_PROGRESS);
  EXPECT_NE(response_.scan_uuid(), "");

  EXPECT_GE(signals_.size(), 1);
  EXPECT_EQ(signals_.back().scan_uuid(), response_.scan_uuid());
  EXPECT_EQ(signals_.back().state(), SCAN_STATE_COMPLETED);
  ValidateProgressSignals(response_.scan_uuid());

  CompareImages("./test_images/color16.png", output_path_.value());
}

TEST_F(ManagerTest, StartScanFailNoDevice) {
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(base::FilePath("./test_images/color.pnm"),
                                     &contents));
  std::vector<uint8_t> image_data(contents.begin(), contents.end());

  StartScanRequest request;
  request.set_device_name("TestDevice");
  request.mutable_settings()->set_color_mode(MODE_COLOR);

  manager_.StartScan(std::move(dbus_response_), impl::SerializeProto(request),
                     scan_fd_);

  EXPECT_EQ(response_.state(), SCAN_STATE_FAILED);
  EXPECT_NE(response_.failure_reason(), "");
  EXPECT_EQ(signals_.size(), 0);
}

TEST_F(ManagerTest, StartScanFailToStart) {
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(base::FilePath("./test_images/color.pnm"),
                                     &contents));
  std::vector<uint8_t> image_data(contents.begin(), contents.end());
  std::unique_ptr<SaneDeviceFake> device = std::make_unique<SaneDeviceFake>();
  device->SetScanData(image_data);
  device->SetStartScanResult(false);
  sane_client_->SetDeviceForName("TestDevice", std::move(device));

  StartScanRequest request;
  request.set_device_name("TestDevice");
  request.mutable_settings()->set_color_mode(MODE_COLOR);

  ExpectScanFailure();
  manager_.StartScan(std::move(dbus_response_), impl::SerializeProto(request),
                     scan_fd_);

  EXPECT_EQ(response_.state(), SCAN_STATE_FAILED);
  EXPECT_NE(response_.failure_reason(), "");
  EXPECT_EQ(signals_.size(), 0);
}

TEST_F(ManagerTest, StartScanFailToRead) {
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(base::FilePath("./test_images/color.pnm"),
                                     &contents));
  std::vector<uint8_t> image_data(contents.begin(), contents.end());
  std::unique_ptr<SaneDeviceFake> device = std::make_unique<SaneDeviceFake>();
  device->SetScanData(image_data);
  device->SetReadScanDataResult(false);
  sane_client_->SetDeviceForName("TestDevice", std::move(device));

  StartScanRequest request;
  request.set_device_name("TestDevice");
  request.mutable_settings()->set_color_mode(MODE_COLOR);

  ExpectScanFailure();
  manager_.StartScan(std::move(dbus_response_), impl::SerializeProto(request),
                     scan_fd_);

  EXPECT_EQ(response_.state(), SCAN_STATE_IN_PROGRESS);
  EXPECT_NE(response_.scan_uuid(), "");

  EXPECT_EQ(signals_.size(), 1);
  EXPECT_EQ(signals_[0].scan_uuid(), response_.scan_uuid());
  EXPECT_EQ(signals_[0].state(), SCAN_STATE_FAILED);
  EXPECT_NE(signals_[0].failure_reason(), "");
}

TEST_F(ManagerTest, StartScanFailBadFd) {
  SetUpTestDevice("TestDevice", base::FilePath("./test_images/color.pnm"),
                  ScanParameters());

  StartScanRequest request;
  request.set_device_name("TestDevice");
  request.mutable_settings()->set_color_mode(MODE_COLOR);

  ExpectScanFailure();
  manager_.StartScan(std::move(dbus_response_), impl::SerializeProto(request),
                     base::ScopedFD());

  EXPECT_EQ(response_.state(), SCAN_STATE_IN_PROGRESS);
  EXPECT_NE(response_.scan_uuid(), "");

  EXPECT_EQ(signals_.size(), 1);
  EXPECT_EQ(signals_[0].scan_uuid(), response_.scan_uuid());
  EXPECT_EQ(signals_[0].state(), SCAN_STATE_FAILED);
  EXPECT_NE(signals_[0].failure_reason(), "");
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

  std::vector<ScannerInfo> info_;
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
  ASSERT_EQ(info_.size(), 1);
  EXPECT_EQ(info_[0].name(), dev_.name);
  EXPECT_EQ(info_[0].manufacturer(), dev_.vendor);
  EXPECT_EQ(info_[0].model(), dev_.model);
  EXPECT_EQ(info_[0].type(), dev_.type);
}

TEST_F(SaneClientTest, ScannerInfoFromDeviceListNullFields) {
  dev_ = CreateTestDevice();
  dev_.name = NULL;
  EXPECT_TRUE(SaneClientImpl::DeviceListToScannerInfo(one_device_, &info_));
  EXPECT_EQ(info_.size(), 0);

  dev_ = CreateTestDevice();
  dev_.vendor = NULL;
  EXPECT_TRUE(SaneClientImpl::DeviceListToScannerInfo(one_device_, &info_));
  ASSERT_EQ(info_.size(), 1);
  EXPECT_EQ(info_[0].name(), dev_.name);
  EXPECT_EQ(info_[0].manufacturer(), "");
  EXPECT_EQ(info_[0].model(), dev_.model);
  EXPECT_EQ(info_[0].type(), dev_.type);

  dev_ = CreateTestDevice();
  dev_.model = NULL;
  EXPECT_TRUE(SaneClientImpl::DeviceListToScannerInfo(one_device_, &info_));
  ASSERT_EQ(info_.size(), 1);
  EXPECT_EQ(info_[0].name(), dev_.name);
  EXPECT_EQ(info_[0].manufacturer(), dev_.vendor);
  EXPECT_EQ(info_[0].model(), "");
  EXPECT_EQ(info_[0].type(), dev_.type);

  dev_ = CreateTestDevice();
  dev_.type = NULL;
  EXPECT_TRUE(SaneClientImpl::DeviceListToScannerInfo(one_device_, &info_));
  ASSERT_EQ(info_.size(), 1);
  EXPECT_EQ(info_[0].name(), dev_.name);
  EXPECT_EQ(info_[0].manufacturer(), dev_.vendor);
  EXPECT_EQ(info_[0].model(), dev_.model);
  EXPECT_EQ(info_[0].type(), "");
}

TEST_F(SaneClientTest, ScannerInfoFromDeviceListMultipleDevices) {
  EXPECT_FALSE(SaneClientImpl::DeviceListToScannerInfo(two_devices_, &info_));

  dev_two_.name = "Test Device 2";
  dev_two_.vendor = "Test Vendor 2";
  EXPECT_TRUE(SaneClientImpl::DeviceListToScannerInfo(two_devices_, &info_));
  ASSERT_EQ(info_.size(), 2);
  EXPECT_EQ(info_[0].name(), dev_.name);
  EXPECT_EQ(info_[0].manufacturer(), dev_.vendor);
  EXPECT_EQ(info_[0].model(), dev_.model);
  EXPECT_EQ(info_[0].type(), dev_.type);

  EXPECT_EQ(info_[1].name(), dev_two_.name);
  EXPECT_EQ(info_[1].manufacturer(), dev_two_.vendor);
  EXPECT_EQ(info_[1].model(), dev_two_.model);
  EXPECT_EQ(info_[1].type(), dev_two_.type);
}

}  // namespace lorgnette
