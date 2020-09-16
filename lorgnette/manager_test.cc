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
#include <brillo/dbus/mock_dbus_method_response.h>
#include <brillo/process/process.h>
#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>
#include <metrics/metrics_library_mock.h>
#include <sane/sane.h>

#include "lorgnette/enums.h"
#include "lorgnette/sane_client_fake.h"
#include "lorgnette/sane_client_impl.h"

using brillo::dbus_utils::MockDBusMethodResponse;
using ::testing::ElementsAre;

namespace lorgnette {

namespace {

void ValidateSignals(const std::vector<ScanStatusChangedSignal>& signals,
                     const std::string& scan_uuid) {
  EXPECT_GE(signals.size(), 1);
  EXPECT_EQ(signals.back().scan_uuid(), scan_uuid);
  EXPECT_EQ(signals.back().state(), SCAN_STATE_COMPLETED);

  int progress = 0;
  int page = 0;
  for (int i = 0; i < signals.size() - 1; i++) {
    const ScanStatusChangedSignal& signal = signals[i];
    EXPECT_EQ(signal.scan_uuid(), scan_uuid);
    EXPECT_EQ(signal.page(), page);

    if (signal.state() == SCAN_STATE_IN_PROGRESS) {
      EXPECT_GT(signal.progress(), progress);
      progress = signal.progress();
    } else if (signal.state() == SCAN_STATE_PAGE_COMPLETED) {
      page++;
      progress = 0;
    }
  }
}

template <typename T>
std::unique_ptr<MockDBusMethodResponse<std::vector<uint8_t>>>
BuildMockDBusResponse(T* response) {
  auto dbus_response =
      std::make_unique<MockDBusMethodResponse<std::vector<uint8_t>>>();
  dbus_response->set_return_callback(base::BindRepeating(
      [](T* response_out, const std::vector<uint8_t>& serialized_response) {
        ASSERT_TRUE(response_out);
        ASSERT_TRUE(response_out->ParseFromArray(serialized_response.data(),
                                                 serialized_response.size()));
      },
      base::Unretained(response)));
  return dbus_response;
}

DocumentSource CreateDocumentSource(const std::string& name,
                                    SourceType type,
                                    const ScannableArea& area) {
  DocumentSource source;
  source.set_name(name);
  source.set_type(type);
  *source.mutable_area() = area;
  return source;
}

ScannableArea CreateScannableArea(double width, double height) {
  ScannableArea area;
  area.set_width(width);
  area.set_height(height);
  return area;
}

}  // namespace

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

    manager_.SetScanStatusChangedSignalSenderForTest(base::BindRepeating(
        [](std::vector<ScanStatusChangedSignal>* signals,
           const ScanStatusChangedSignal& signal) {
          signals->push_back(signal);
        },
        base::Unretained(&signals_)));
  }

  void ExpectScanRequest(DocumentScanSaneBackend backend) {
    EXPECT_CALL(*metrics_library_,
                SendEnumToUMA(Manager::kMetricScanRequested, backend,
                              DocumentScanSaneBackend::kMaxValue));
  }

  void ExpectScanSuccess(DocumentScanSaneBackend backend) {
    EXPECT_CALL(*metrics_library_,
                SendEnumToUMA(Manager::kMetricScanSucceeded, backend,
                              DocumentScanSaneBackend::kMaxValue));
  }

  void ExpectScanFailure(DocumentScanSaneBackend backend) {
    EXPECT_CALL(*metrics_library_,
                SendEnumToUMA(Manager::kMetricScanFailed, backend,
                              DocumentScanSaneBackend::kMaxValue));
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

  StartScanResponse StartScan(const std::string& device_name,
                              ColorMode color_mode) {
    StartScanRequest request;
    request.set_device_name(device_name);
    request.mutable_settings()->set_color_mode(color_mode);

    std::vector<uint8_t> serialized_response =
        manager_.StartScan(impl::SerializeProto(request));

    StartScanResponse response;
    EXPECT_TRUE(response.ParseFromArray(serialized_response.data(),
                                        serialized_response.size()));
    return response;
  }

  GetNextImageResponse GetNextImage(const std::string& scan_uuid,
                                    const base::ScopedFD& output_fd) {
    GetNextImageRequest request;
    request.set_scan_uuid(scan_uuid);

    GetNextImageResponse response;
    manager_.GetNextImage(BuildMockDBusResponse(&response),
                          impl::SerializeProto(request), output_fd);
    return response;
  }

  // Run a one-page scan to completion, and verify that it was successful.
  void RunScanSuccess(const std::string& device_name, ColorMode color_mode) {
    StartScanResponse response = StartScan(device_name, color_mode);
    EXPECT_EQ(response.state(), SCAN_STATE_IN_PROGRESS);
    EXPECT_NE(response.scan_uuid(), "");

    GetNextImageResponse get_next_image_response =
        GetNextImage(response.scan_uuid(), scan_fd_);
    EXPECT_TRUE(get_next_image_response.success());

    ValidateSignals(signals_, response.scan_uuid());
  }

  std::vector<ScanStatusChangedSignal> signals_;

  SaneClientFake* sane_client_;
  Manager manager_;
  MetricsLibraryMock* metrics_library_;  // Owned by manager_.
  base::ScopedTempDir temp_dir_;
  base::FilePath output_path_;
  base::ScopedFD scan_fd_;
};

TEST_F(ManagerTest, GetScannerCapabilitiesInvalidIppUsbFailure) {
  std::vector<uint8_t> serialized;
  brillo::ErrorPtr error;
  EXPECT_FALSE(
      manager_.GetScannerCapabilities(&error, "ippusb:invalid", &serialized));
  EXPECT_NE(error, nullptr);
  EXPECT_NE(error->GetMessage().find("ippusb"), std::string::npos);
}

TEST_F(ManagerTest, GetScannerCapabilitiesSuccess) {
  std::unique_ptr<SaneDeviceFake> device = std::make_unique<SaneDeviceFake>();
  ValidOptionValues opts;
  opts.resolutions = {100, 200, 300, 600};
  opts.sources = {
      CreateDocumentSource("FB", SOURCE_PLATEN,
                           CreateScannableArea(355.2, 417.9)),
      CreateDocumentSource("Negative", SOURCE_UNSPECIFIED,
                           CreateScannableArea(355.2, 204.0)),
      CreateDocumentSource("Automatic Document Feeder", SOURCE_ADF_SIMPLEX,
                           CreateScannableArea(212.9, 212.2))};
  opts.color_modes = {kScanPropertyModeColor};
  device->SetValidOptionValues(opts);
  sane_client_->SetDeviceForName("TestDevice", std::move(device));

  std::vector<uint8_t> serialized;
  EXPECT_TRUE(
      manager_.GetScannerCapabilities(nullptr, "TestDevice", &serialized));

  ScannerCapabilities caps;
  EXPECT_TRUE(caps.ParseFromArray(serialized.data(), serialized.size()));

  EXPECT_THAT(caps.resolutions(), ElementsAre(100, 200, 300, 600));

  ASSERT_EQ(caps.sources().size(), 2);
  EXPECT_EQ(caps.sources()[0].type(), SOURCE_PLATEN);
  EXPECT_EQ(caps.sources()[0].name(), "FB");
  EXPECT_EQ(caps.sources()[0].area().width(), 355.2);
  EXPECT_EQ(caps.sources()[0].area().height(), 417.9);

  EXPECT_EQ(caps.sources()[1].type(), SOURCE_ADF_SIMPLEX);
  EXPECT_EQ(caps.sources()[1].name(), "Automatic Document Feeder");
  EXPECT_EQ(caps.sources()[1].area().width(), 212.9);
  EXPECT_EQ(caps.sources()[1].area().height(), 212.2);

  EXPECT_THAT(caps.color_modes(), ElementsAre(MODE_COLOR));
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

  ExpectScanRequest(kOtherBackend);
  ExpectScanSuccess(kOtherBackend);
  RunScanSuccess("TestDevice", MODE_LINEART);
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

  ExpectScanRequest(kOtherBackend);
  ExpectScanSuccess(kOtherBackend);
  RunScanSuccess("TestDevice", MODE_GRAYSCALE);
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

  ExpectScanRequest(kOtherBackend);
  ExpectScanSuccess(kOtherBackend);
  RunScanSuccess("TestDevice", MODE_COLOR);
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

  ExpectScanRequest(kOtherBackend);
  ExpectScanSuccess(kOtherBackend);
  RunScanSuccess("TestDevice", MODE_COLOR);
  CompareImages("./test_images/color16.png", output_path_.value());
}

TEST_F(ManagerTest, StartScanFailNoDevice) {
  StartScanResponse response = StartScan("TestDevice", MODE_COLOR);

  EXPECT_EQ(response.state(), SCAN_STATE_FAILED);
  EXPECT_NE(response.failure_reason(), "");
  EXPECT_EQ(signals_.size(), 0);
}

TEST_F(ManagerTest, StartScanFailToStart) {
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(base::FilePath("./test_images/color.pnm"),
                                     &contents));
  std::vector<uint8_t> image_data(contents.begin(), contents.end());
  std::unique_ptr<SaneDeviceFake> device = std::make_unique<SaneDeviceFake>();
  device->SetScanData(image_data);
  device->SetStartScanResult(SANE_STATUS_IO_ERROR);
  sane_client_->SetDeviceForName("TestDevice", std::move(device));

  ExpectScanRequest(kOtherBackend);
  ExpectScanFailure(kOtherBackend);
  StartScanResponse response = StartScan("TestDevice", MODE_COLOR);

  EXPECT_EQ(response.state(), SCAN_STATE_FAILED);
  EXPECT_NE(response.failure_reason(), "");
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

  ExpectScanRequest(kOtherBackend);
  ExpectScanFailure(kOtherBackend);
  StartScanResponse response = StartScan("TestDevice", MODE_COLOR);

  EXPECT_EQ(response.state(), SCAN_STATE_IN_PROGRESS);
  EXPECT_NE(response.scan_uuid(), "");

  GetNextImageResponse get_next_image_response =
      GetNextImage(response.scan_uuid(), scan_fd_);
  EXPECT_TRUE(get_next_image_response.success());

  EXPECT_EQ(signals_.size(), 1);
  EXPECT_EQ(signals_[0].scan_uuid(), response.scan_uuid());
  EXPECT_EQ(signals_[0].state(), SCAN_STATE_FAILED);
  EXPECT_NE(signals_[0].failure_reason(), "");
}

TEST_F(ManagerTest, GetNextImageBadFd) {
  SetUpTestDevice("TestDevice", base::FilePath("./test_images/color.pnm"),
                  ScanParameters());

  ExpectScanRequest(kOtherBackend);
  StartScanResponse response = StartScan("TestDevice", MODE_COLOR);

  EXPECT_EQ(response.state(), SCAN_STATE_IN_PROGRESS);
  EXPECT_NE(response.scan_uuid(), "");

  GetNextImageResponse get_next_image_response =
      GetNextImage(response.scan_uuid(), base::ScopedFD());
  EXPECT_FALSE(get_next_image_response.success());
  EXPECT_NE(get_next_image_response.failure_reason(), "");

  // Scan should not have failed.
  EXPECT_EQ(signals_.size(), 0);
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

namespace {

SANE_Option_Descriptor CreateDescriptor(const char* name,
                                        SANE_Value_Type type,
                                        int size) {
  SANE_Option_Descriptor desc;
  desc.name = name;
  desc.type = type;
  desc.constraint_type = SANE_CONSTRAINT_NONE;
  desc.size = size;
  return desc;
}

}  // namespace

TEST(SaneOptionIntTest, SetIntSucceeds) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word)), 7);
  EXPECT_TRUE(option.SetInt(54));
  EXPECT_EQ(*static_cast<SANE_Int*>(option.GetPointer()), 54);
}

TEST(SaneOptionIntTest, SetDoubleSucceeds) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word)), 7);
  // Should round towards 0.
  EXPECT_TRUE(option.SetDouble(295.7));
  EXPECT_EQ(*static_cast<SANE_Int*>(option.GetPointer()), 295);
}

TEST(SaneOptionIntTest, SetStringFails) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word)), 7);
  EXPECT_TRUE(option.SetInt(17));
  EXPECT_FALSE(option.SetString("test"));
  EXPECT_EQ(*static_cast<SANE_Int*>(option.GetPointer()), 17);
}

TEST(SaneOptionIntTest, GetIndex) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word)), 7);
  EXPECT_EQ(option.GetIndex(), 7);
}

TEST(SaneOptionIntTest, GetName) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word)), 7);
  EXPECT_EQ(option.GetName(), "Test Name");
}

TEST(SaneOptionIntTest, DisplayValue) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word)), 2);
  EXPECT_TRUE(option.SetInt(247));
  EXPECT_EQ(option.DisplayValue(), "247");
}

TEST(SaneOptionIntTest, CopiesDoNotAlias) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word)), 2);
  EXPECT_TRUE(option.SetInt(88));
  EXPECT_EQ(option.DisplayValue(), "88");

  SaneOption option_two = option;
  EXPECT_TRUE(option_two.SetInt(9));
  EXPECT_EQ(option_two.DisplayValue(), "9");
  EXPECT_EQ(option.DisplayValue(), "88");
}

TEST(SaneOptionFixedTest, SetIntSucceeds) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 7);
  EXPECT_TRUE(option.SetInt(54));
  SANE_Fixed f = *static_cast<SANE_Fixed*>(option.GetPointer());
  EXPECT_EQ(static_cast<int>(SANE_UNFIX(f)), 54);
}

TEST(SaneOptionFixedTest, SetDoubleSucceeds) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 7);
  EXPECT_TRUE(option.SetDouble(436.2));
  SANE_Fixed f = *static_cast<SANE_Fixed*>(option.GetPointer());
  EXPECT_FLOAT_EQ(SANE_UNFIX(f), 436.2);
}

TEST(SaneOptionFixedTest, SetStringFails) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 7);
  EXPECT_TRUE(option.SetInt(17));
  EXPECT_FALSE(option.SetString("test"));
  SANE_Fixed f = *static_cast<SANE_Fixed*>(option.GetPointer());
  EXPECT_EQ(static_cast<int>(SANE_UNFIX(f)), 17);
}

TEST(SaneOptionFixedTest, GetIndex) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 7);
  EXPECT_EQ(option.GetIndex(), 7);
}

TEST(SaneOptionFixedTest, GetName) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 7);
  EXPECT_EQ(option.GetName(), "Test Name");
}

TEST(SaneOptionFixedTest, DisplayValue) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 2);
  EXPECT_TRUE(option.SetInt(247));
  EXPECT_EQ(option.DisplayValue(), "247");
}

TEST(SaneOptionFixedTest, CopiesDoNotAlias) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 2);
  EXPECT_TRUE(option.SetInt(88));
  EXPECT_EQ(option.DisplayValue(), "88");

  SaneOption option_two = option;
  EXPECT_TRUE(option_two.SetInt(9));
  EXPECT_EQ(option_two.DisplayValue(), "9");
  EXPECT_EQ(option.DisplayValue(), "88");
}

TEST(SaneOptionStringTest, SetStringSucceeds) {
  SaneOption option(CreateDescriptor("Test Name", SANE_TYPE_STRING, 8), 7);
  EXPECT_TRUE(option.SetString("test"));
  EXPECT_STREQ(static_cast<char*>(option.GetPointer()), "test");

  // Longest string that fits (with null terminator).
  EXPECT_TRUE(option.SetString("1234567"));
  EXPECT_STREQ(static_cast<char*>(option.GetPointer()), "1234567");
}

TEST(SaneOptionStringTest, SetStringTooLongFails) {
  SaneOption option(CreateDescriptor("Test Name", SANE_TYPE_STRING, 8), 7);
  EXPECT_TRUE(option.SetString("test"));

  // String that is exactly one character too long.
  EXPECT_FALSE(option.SetString("12345678"));

  // String that is many characters too long.
  EXPECT_FALSE(option.SetString("This is a much longer string than can fit."));
  EXPECT_STREQ(static_cast<char*>(option.GetPointer()), "test");
}

TEST(SaneOptionStringTest, SetIntFails) {
  SaneOption option(CreateDescriptor("Test Name", SANE_TYPE_STRING, 32), 7);
  EXPECT_TRUE(option.SetString("test"));
  EXPECT_FALSE(option.SetInt(54));
  EXPECT_STREQ(static_cast<char*>(option.GetPointer()), "test");
}

TEST(SaneOptionStringTest, GetIndex) {
  SaneOption option(CreateDescriptor("Test Name", SANE_TYPE_STRING, 32), 7);
  EXPECT_EQ(option.GetIndex(), 7);
}

TEST(SaneOptionStringTest, GetName) {
  SaneOption option(CreateDescriptor("Test Name", SANE_TYPE_STRING, 32), 7);
  EXPECT_EQ(option.GetName(), "Test Name");
}

TEST(SaneOptionStringTest, DisplayValue) {
  SaneOption option(CreateDescriptor("Test Name", SANE_TYPE_STRING, 32), 2);
  EXPECT_TRUE(option.SetString("test string"));
  EXPECT_EQ(option.DisplayValue(), "test string");
}

TEST(SaneOptionStringTest, CopiesDoNotAlias) {
  SaneOption option(CreateDescriptor("Test Name", SANE_TYPE_STRING, 32), 2);
  EXPECT_TRUE(option.SetString("test string"));
  EXPECT_EQ(option.DisplayValue(), "test string");

  SaneOption option_two = option;
  EXPECT_TRUE(option_two.SetString("other value"));
  EXPECT_EQ(option.DisplayValue(), "test string");
  EXPECT_EQ(option_two.DisplayValue(), "other value");
}

TEST(ValidOptionValues, InvalidDescriptorWordList) {
  SANE_Option_Descriptor desc;
  desc.constraint_type = SANE_CONSTRAINT_STRING_LIST;
  std::vector<SANE_String_Const> valid_values = {nullptr};
  desc.constraint.string_list = valid_values.data();

  base::Optional<std::vector<uint32_t>> values =
      SaneDeviceImpl::GetValidIntOptionValues(nullptr, desc);
  EXPECT_FALSE(values.has_value());
}

TEST(ValidOptionValues, EmptyWordList) {
  SANE_Option_Descriptor desc;
  desc.constraint_type = SANE_CONSTRAINT_WORD_LIST;
  std::vector<SANE_Word> valid_values = {0};
  desc.constraint.word_list = valid_values.data();

  base::Optional<std::vector<uint32_t>> values =
      SaneDeviceImpl::GetValidIntOptionValues(nullptr, desc);
  EXPECT_TRUE(values.has_value());
  EXPECT_EQ(values.value().size(), 0);
}

TEST(ValidOptionValues, NonEmptyWordList) {
  SANE_Option_Descriptor desc;
  desc.constraint_type = SANE_CONSTRAINT_WORD_LIST;
  std::vector<SANE_Word> valid_values = {4, 0, 729, 368234, 15};
  desc.constraint.word_list = valid_values.data();

  base::Optional<std::vector<uint32_t>> values =
      SaneDeviceImpl::GetValidIntOptionValues(nullptr, desc);
  EXPECT_TRUE(values.has_value());
  EXPECT_EQ(values.value().size(), 4);
  EXPECT_EQ(values.value(), std::vector<uint32_t>({0, 729, 368234, 15}));
}

TEST(ValidOptionValues, InvalidDescriptorRangeList) {
  SANE_Option_Descriptor desc;
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  SANE_Range range;
  desc.constraint.range = &range;

  base::Optional<std::vector<std::string>> values =
      SaneDeviceImpl::GetValidStringOptionValues(nullptr, desc);
  EXPECT_FALSE(values.has_value());
}

TEST(ValidOptionValues, EmptyRangeList) {
  SANE_Option_Descriptor desc;
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  SANE_Range range;
  range.min = 5;
  range.max = 4;
  range.quant = 1;
  desc.constraint.range = &range;

  base::Optional<std::vector<uint32_t>> values =
      SaneDeviceImpl::GetValidIntOptionValues(nullptr, desc);
  EXPECT_TRUE(values.has_value());
  EXPECT_EQ(values.value().size(), 0);
}

TEST(ValidOptionValues, SingleStepRangeList) {
  SANE_Option_Descriptor desc;
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  SANE_Range range;
  range.min = 5;
  range.max = 11;
  range.quant = 1;
  desc.constraint.range = &range;

  base::Optional<std::vector<uint32_t>> values =
      SaneDeviceImpl::GetValidIntOptionValues(nullptr, desc);
  EXPECT_TRUE(values.has_value());
  EXPECT_EQ(values.value(), std::vector<uint32_t>({5, 6, 7, 8, 9, 10, 11}));
}

TEST(ValidOptionValues, FourStepRangeList) {
  SANE_Option_Descriptor desc;
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  SANE_Range range;
  range.min = 13;
  range.max = 28;
  range.quant = 4;
  desc.constraint.range = &range;

  base::Optional<std::vector<uint32_t>> values =
      SaneDeviceImpl::GetValidIntOptionValues(nullptr, desc);
  EXPECT_TRUE(values.has_value());
  EXPECT_EQ(values.value(), std::vector<uint32_t>({13, 17, 21, 25}));
}

TEST(ValidOptionValues, InvalidDescriptorStringList) {
  SANE_Option_Descriptor desc;
  desc.constraint_type = SANE_CONSTRAINT_WORD_LIST;
  std::vector<SANE_Word> valid_values = {4, 0, 729, 368234, 15};
  desc.constraint.word_list = valid_values.data();

  base::Optional<std::vector<std::string>> values =
      SaneDeviceImpl::GetValidStringOptionValues(nullptr, desc);
  EXPECT_FALSE(values.has_value());
}

TEST(ValidOptionValues, EmptyStringList) {
  SANE_Option_Descriptor desc;
  desc.constraint_type = SANE_CONSTRAINT_STRING_LIST;
  std::vector<SANE_String_Const> valid_values = {nullptr};
  desc.constraint.string_list = valid_values.data();

  base::Optional<std::vector<std::string>> values =
      SaneDeviceImpl::GetValidStringOptionValues(nullptr, desc);
  EXPECT_TRUE(values.has_value());
  EXPECT_EQ(values.value().size(), 0);
}

TEST(ValidOptionValues, NonEmptyStringList) {
  SANE_Option_Descriptor desc;
  desc.constraint_type = SANE_CONSTRAINT_STRING_LIST;
  std::vector<SANE_String_Const> valid_values = {"Color", "Gray", "Lineart",
                                                 nullptr};
  desc.constraint.string_list = valid_values.data();

  base::Optional<std::vector<std::string>> values =
      SaneDeviceImpl::GetValidStringOptionValues(nullptr, desc);
  desc.constraint.string_list = valid_values.data();
  values = SaneDeviceImpl::GetValidStringOptionValues(nullptr, desc);
  EXPECT_TRUE(values.has_value());
  EXPECT_EQ(values.value().size(), 3);
  EXPECT_EQ(values.value(),
            std::vector<std::string>({"Color", "Gray", "Lineart"}));
}

TEST(GetOptionRange, InvalidConstraint) {
  SANE_Option_Descriptor desc;
  desc.name = "Test";
  desc.constraint_type = SANE_CONSTRAINT_WORD_LIST;

  EXPECT_FALSE(SaneDeviceImpl::GetOptionRange(nullptr, desc).has_value());

  desc.constraint_type = SANE_CONSTRAINT_NONE;
  EXPECT_FALSE(SaneDeviceImpl::GetOptionRange(nullptr, desc).has_value());
}

TEST(GetOptionRange, InvalidType) {
  SANE_Option_Descriptor desc;
  desc.name = "Test";
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  SANE_Range range;
  range.min = 13;
  range.max = 28;
  range.quant = 4;
  desc.constraint.range = &range;

  desc.type = SANE_TYPE_STRING;
  EXPECT_FALSE(SaneDeviceImpl::GetOptionRange(nullptr, desc).has_value());

  desc.type = SANE_TYPE_BOOL;
  EXPECT_FALSE(SaneDeviceImpl::GetOptionRange(nullptr, desc).has_value());
}

TEST(GetOptionRange, ValidFixedValue) {
  SANE_Option_Descriptor desc;
  desc.name = "Test";
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  SANE_Range range;
  range.min = SANE_FIX(2.3);
  range.max = SANE_FIX(4.9);
  range.quant = SANE_FIX(0.1);
  desc.constraint.range = &range;
  desc.type = SANE_TYPE_FIXED;

  base::Optional<OptionRange> range_result =
      SaneDeviceImpl::GetOptionRange(nullptr, desc);
  EXPECT_TRUE(range_result.has_value());
  EXPECT_NEAR(range_result.value().start, 2.3, 1e-4);
  EXPECT_NEAR(range_result.value().size, 2.6, 1e-4);
}

TEST(GetOptionRange, ValidIntValue) {
  SANE_Option_Descriptor desc;
  desc.name = "Test";
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  SANE_Range range;
  range.min = 3;
  range.max = 27;
  range.quant = 1;
  desc.constraint.range = &range;
  desc.type = SANE_TYPE_INT;

  base::Optional<OptionRange> range_result =
      SaneDeviceImpl::GetOptionRange(nullptr, desc);
  EXPECT_TRUE(range_result.has_value());
  EXPECT_NEAR(range_result.value().start, 3, 1e-4);
  EXPECT_NEAR(range_result.value().size, 24, 1e-4);
}

}  // namespace lorgnette
