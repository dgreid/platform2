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
#include "lorgnette/test_util.h"

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
      CreateDocumentSource("FB", SOURCE_PLATEN, 355.2, 417.9),
      CreateDocumentSource("Negative", SOURCE_UNSPECIFIED, 355.2, 204.0),
      CreateDocumentSource("Automatic Document Feeder", SOURCE_ADF_SIMPLEX,
                           212.9, 212.2)};
  opts.color_modes = {kScanPropertyModeColor};
  device->SetValidOptionValues(opts);
  sane_client_->SetDeviceForName("TestDevice", std::move(device));

  std::vector<uint8_t> serialized;
  EXPECT_TRUE(
      manager_.GetScannerCapabilities(nullptr, "TestDevice", &serialized));

  ScannerCapabilities caps;
  EXPECT_TRUE(caps.ParseFromArray(serialized.data(), serialized.size()));

  EXPECT_THAT(caps.resolutions(), ElementsAre(100, 200, 300, 600));

  EXPECT_THAT(caps.sources(),
              ElementsAre(EqualsDocumentSource(CreateDocumentSource(
                              "FB", SOURCE_PLATEN, 355.2, 417.9)),
                          EqualsDocumentSource(CreateDocumentSource(
                              "Automatic Document Feeder", SOURCE_ADF_SIMPLEX,
                              212.9, 212.2))));

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

}  // namespace lorgnette
