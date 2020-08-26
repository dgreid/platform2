// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_client_impl.h"

#include <iostream>
#include <memory>
#include <vector>

#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>

#include "lorgnette/manager.h"
#include "lorgnette/test_util.h"

using ::testing::ElementsAre;

namespace lorgnette {

class SaneDeviceImplTest : public testing::Test {
 protected:
  void SetUp() override {
    client_ = SaneClientImpl::Create();
    device_ = client_->ConnectToDevice(nullptr, "test");
    EXPECT_TRUE(device_);
  }

  void ReloadOptions() {
    dynamic_cast<SaneDeviceImpl*>(device_.get())->LoadOptions(nullptr);
  }

  std::unique_ptr<SaneClient> client_;
  std::unique_ptr<SaneDevice> device_;
};

// Check that GetValidOptionValues rejects a null input pointer.
TEST_F(SaneDeviceImplTest, GetValidOptionValuesBadPointer) {
  EXPECT_FALSE(device_->GetValidOptionValues(nullptr, nullptr));
}

// Check that GetValidOptionValues returns correct values for the test backend.
TEST_F(SaneDeviceImplTest, GetValidOptionValuesSuccess) {
  ValidOptionValues values;
  EXPECT_TRUE(device_->GetValidOptionValues(nullptr, &values));
  ASSERT_EQ(values.resolutions.size(), 1200);
  for (int i = 0; i < 1200; i++)
    EXPECT_EQ(values.resolutions[i], i + 1);

  EXPECT_THAT(values.sources,
              ElementsAre(EqualsDocumentSource(CreateDocumentSource(
                              "Flatbed", SOURCE_PLATEN, 200.0, 200.0)),
                          EqualsDocumentSource(CreateDocumentSource(
                              "Automatic Document Feeder", SOURCE_ADF_SIMPLEX,
                              200.0, 200.0))));

  EXPECT_THAT(values.color_modes,
              ElementsAre(kScanPropertyModeGray, kScanPropertyModeColor));
}

// Check that SetScanResolution works for all valid values.
TEST_F(SaneDeviceImplTest, SetResolution) {
  ValidOptionValues values;
  EXPECT_TRUE(device_->GetValidOptionValues(nullptr, &values));

  for (int resolution : values.resolutions)
    EXPECT_TRUE(device_->SetScanResolution(nullptr, resolution));
}

// Check the SetDocumentSource rejects invalid values and works properly for all
// valid values. Also check that GetDocumentSource returns that correct value
// after SetDocumentSource, even if we force-reload option values from scanner.
TEST_F(SaneDeviceImplTest, SetSource) {
  EXPECT_FALSE(device_->SetDocumentSource(nullptr, "invalid source"));

  ValidOptionValues values;
  EXPECT_TRUE(device_->GetValidOptionValues(nullptr, &values));

  // Test both with and without reloading options after setting option, since
  // it can surface different bugs.
  for (bool reload_options : {true, false}) {
    LOG(INFO) << "Testing " << (reload_options ? "with" : "without")
              << " option reloading.";
    for (const DocumentSource& source : values.sources) {
      EXPECT_TRUE(device_->SetDocumentSource(nullptr, source.name()));
      if (reload_options) {
        ReloadOptions();
      }

      std::string scanner_value;
      EXPECT_TRUE(device_->GetDocumentSource(nullptr, &scanner_value));
      EXPECT_EQ(scanner_value, source.name());
    }
  }
}

// Check that SetColorMode rejects invalid values, and accepts all valid values.
TEST_F(SaneDeviceImplTest, SetColorMode) {
  EXPECT_FALSE(device_->SetColorMode(nullptr, MODE_UNSPECIFIED));

  ValidOptionValues values;
  EXPECT_TRUE(device_->GetValidOptionValues(nullptr, &values));

  for (const std::string& mode_string : values.color_modes) {
    ColorMode mode = impl::ColorModeFromSaneString(mode_string);
    EXPECT_NE(mode, MODE_UNSPECIFIED)
        << "Unexpected ColorMode string " << mode_string;
    EXPECT_TRUE(device_->SetColorMode(nullptr, mode));
  }
}

// Check that extra calls to StartScan fail properly.
TEST_F(SaneDeviceImplTest, DuplicateStartScan) {
  EXPECT_EQ(device_->StartScan(nullptr), SANE_STATUS_GOOD);
  EXPECT_EQ(device_->StartScan(nullptr), SANE_STATUS_DEVICE_BUSY);
}

// Check the GetScanParameters correctly rejects invalid input pointers.
TEST_F(SaneDeviceImplTest, GetScanParametersFail) {
  EXPECT_FALSE(device_->GetScanParameters(nullptr, nullptr));
}

// Check that GetScanParameters returns the correct values corresponding to the
// input resolution and scan region.
TEST_F(SaneDeviceImplTest, GetScanParameters) {
  ScanParameters params;
  const int resolution = 100; /* dpi */
  EXPECT_TRUE(device_->SetScanResolution(nullptr, resolution));

  const double width = 187;  /* mm */
  const double height = 123; /* mm */

  ScanRegion region;
  region.set_top_left_x(0);
  region.set_top_left_y(0);
  region.set_bottom_right_x(width);
  region.set_bottom_right_y(height);
  EXPECT_TRUE(device_->SetScanRegion(nullptr, region));

  EXPECT_TRUE(device_->GetScanParameters(nullptr, &params));
  EXPECT_TRUE(params.format == kGrayscale);

  const double mms_per_inch = 25.4;
  EXPECT_EQ(params.bytes_per_line,
            static_cast<int>(width / mms_per_inch * resolution));
  EXPECT_EQ(params.pixels_per_line,
            static_cast<int>(width / mms_per_inch * resolution));
  EXPECT_EQ(params.lines, static_cast<int>(height / mms_per_inch * resolution));
  EXPECT_EQ(params.depth, 8);
}

// Check that ReadScanData fails when we haven't started a scan.
TEST_F(SaneDeviceImplTest, ReadScanDataWhenNotStarted) {
  std::vector<uint8_t> buf(8192);
  size_t read = 0;

  EXPECT_FALSE(device_->ReadScanData(nullptr, buf.data(), buf.size(), &read));
}

// Check that ReadScanData fails with invalid input pointers.
TEST_F(SaneDeviceImplTest, ReadScanDataBadPointers) {
  std::vector<uint8_t> buf(8192);
  size_t read = 0;

  EXPECT_EQ(device_->StartScan(nullptr), SANE_STATUS_GOOD);
  EXPECT_FALSE(device_->ReadScanData(nullptr, nullptr, buf.size(), &read));
  EXPECT_FALSE(device_->ReadScanData(nullptr, buf.data(), buf.size(), nullptr));
}

// Check that we can successfully run a scan to completion.
TEST_F(SaneDeviceImplTest, RunScan) {
  std::vector<uint8_t> buf(8192);
  size_t read = 0;

  EXPECT_EQ(device_->StartScan(nullptr), SANE_STATUS_GOOD);
  do {
    EXPECT_TRUE(device_->ReadScanData(nullptr, buf.data(), buf.size(), &read));
  } while (read != 0);
}

}  // namespace lorgnette
