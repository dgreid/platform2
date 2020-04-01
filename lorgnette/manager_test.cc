// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/manager.h"

#include <string>
#include <vector>

#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/variant_dictionary.h>
#include <brillo/process_mock.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <sane/sane.h>

#include "lorgnette/sane_client_impl.h"

using base::ScopedFD;
using brillo::VariantDictionary;
using std::string;
using testing::_;
using testing::InSequence;
using testing::Return;

namespace lorgnette {

class ManagerTest : public testing::Test {
 public:
  ManagerTest()
     : input_scoped_fd_(kInputPipeFd),
       output_scoped_fd_(kOutputPipeFd),
       manager_(base::Callback<void()>()),
       metrics_library_(new MetricsLibraryMock) {
    manager_.metrics_library_.reset(metrics_library_);
  }

  virtual void TearDown() {
    // The fds that we have handed to these ScopedFD are not real, so we
    // must prevent our scoped fds from calling close() on them.
    int fd = input_scoped_fd_.release();
    CHECK(fd == kInvalidFd || fd == kInputPipeFd);
    fd = output_scoped_fd_.release();
    CHECK(fd == kInvalidFd || fd == kOutputPipeFd);
  }

 protected:
  static const char kDeviceName[];
  static const char kMode[];
  static const int kInvalidFd;
  static const int kInputPipeFd;
  static const int kOutputFd;
  static const int kOutputPipeFd;
  static const int kResolution;

  static void RunListScannersProcess(int fd, brillo::Process* process) {
    Manager::RunListScannersProcess(fd, process);
  }

  void RunScanImageProcess(const string& device_name,
                           int out_fd,
                           base::ScopedFD* input_scoped_fd,
                           base::ScopedFD* output_scoped_fd,
                           const VariantDictionary& scan_properties,
                           brillo::Process* scan_process,
                           brillo::Process* convert_process,
                           brillo::ErrorPtr* error) {
    manager_.RunScanImageProcess(device_name,
                                 out_fd,
                                 input_scoped_fd,
                                 output_scoped_fd,
                                 scan_properties,
                                 scan_process,
                                 convert_process,
                                 error);
  }

  static void ExpectStartScan(const char* mode,
                              int resolution,
                              brillo::ProcessMock* scan_process,
                              brillo::ProcessMock* convert_process) {
    EXPECT_CALL(*scan_process, AddArg(GetScanImagePath()));
    EXPECT_CALL(*scan_process, AddArg("-d"));
    EXPECT_CALL(*scan_process, AddArg(kDeviceName));
    if (mode) {
      EXPECT_CALL(*scan_process, AddArg("--mode"));
      EXPECT_CALL(*scan_process, AddArg(mode));
    }
    if (resolution) {
      const string kResolutionString(base::NumberToString(resolution));
      EXPECT_CALL(*scan_process, AddArg("--resolution"));
      EXPECT_CALL(*scan_process, AddArg(kResolutionString));
    }
    EXPECT_CALL(*scan_process, BindFd(kOutputPipeFd, STDOUT_FILENO));
    EXPECT_CALL(*convert_process, AddArg(GetScanConverterPath()));
    EXPECT_CALL(*convert_process, BindFd(kInputPipeFd, STDIN_FILENO));
    EXPECT_CALL(*convert_process, BindFd(kOutputFd, STDOUT_FILENO));
    EXPECT_CALL(*convert_process, Start());
    EXPECT_CALL(*scan_process, Start());
  }

  static Manager::ScannerInfo ScannerInfoFromString(
      const std::string& scanner_info_string) {
    return Manager::ScannerInfoFromString(scanner_info_string);
  }

  static std::string GetScanConverterPath() {
    return Manager::kScanConverterPath;
  }
  static std::string GetScanImagePath() { return Manager::kScanImagePath; }
  static std::string GetScanImageFromattedDeviceListCmd() {
    return Manager::kScanImageFormattedDeviceListCmd;
  }

  ScopedFD input_scoped_fd_;
  ScopedFD output_scoped_fd_;
  Manager manager_;
  MetricsLibraryMock* metrics_library_;  // Owned by manager_.
};

// kInvalidFd must equal to base::internal::ScopedFDCloseTraits::InvalidValue().
const int ManagerTest::kInvalidFd = -1;
const int ManagerTest::kInputPipeFd = 123;
const int ManagerTest::kOutputFd = 456;
const int ManagerTest::kOutputPipeFd = 789;
const char ManagerTest::kDeviceName[] = "scanner";
const int ManagerTest::kResolution = 300;
const char ManagerTest::kMode[] = "Color";

MATCHER_P(IsDbusErrorStartingWith, message, "") {
  return arg != nullptr &&
         arg->GetDomain() == brillo::errors::dbus::kDomain &&
         arg->GetCode() == kManagerServiceError &&
         base::StartsWith(arg->GetMessage(), message,
                          base::CompareCase::INSENSITIVE_ASCII);
}

TEST_F(ManagerTest, RunListScannersProcess) {
  brillo::ProcessMock process;
  const int kFd = 123;
  InSequence seq;
  EXPECT_CALL(process, AddArg(GetScanImagePath()));
  EXPECT_CALL(process, AddArg(GetScanImageFromattedDeviceListCmd()));
  EXPECT_CALL(process, BindFd(kFd, STDOUT_FILENO));
  EXPECT_CALL(process, Run());
  RunListScannersProcess(kFd, &process);
}

TEST_F(ManagerTest, RunScanImageProcessSuccess) {
  VariantDictionary props{
      {"Mode", string{kMode}},
      {"Resolution", uint32_t{kResolution}}
  };
  brillo::ProcessMock scan_process;
  brillo::ProcessMock convert_process;
  InSequence seq;
  ExpectStartScan(kMode,
                  kResolution,
                  &scan_process,
                  &convert_process);
  EXPECT_CALL(scan_process, Wait()).WillOnce(Return(0));
  EXPECT_CALL(*metrics_library_,
              SendEnumToUMA(Manager::kMetricScanResult,
                            Manager::kBooleanMetricSuccess,
                            Manager::kBooleanMetricMax));
  EXPECT_CALL(convert_process, Wait()).WillOnce(Return(0));
  EXPECT_CALL(*metrics_library_,
              SendEnumToUMA(Manager::kMetricConverterResult,
                            Manager::kBooleanMetricSuccess,
                            Manager::kBooleanMetricMax));
  brillo::ErrorPtr error;
  RunScanImageProcess(kDeviceName,
                      kOutputFd,
                      &input_scoped_fd_,
                      &output_scoped_fd_,
                      props,
                      &scan_process,
                      &convert_process,
                      &error);
  EXPECT_EQ(kInvalidFd, input_scoped_fd_.get());
  EXPECT_EQ(kInvalidFd, output_scoped_fd_.get());
  EXPECT_EQ(nullptr, error.get());
}

TEST_F(ManagerTest, RunScanImageProcessInvalidArgument) {
  const char kInvalidArgument[] = "InvalidArgument";
  VariantDictionary props{{kInvalidArgument, ""}};
  brillo::ProcessMock scan_process;
  brillo::ProcessMock convert_process;
  // For "scanimage", "-d", "<device name>".
  EXPECT_CALL(scan_process, AddArg(_)).Times(3);
  EXPECT_CALL(convert_process, AddArg(_)).Times(0);
  EXPECT_CALL(convert_process, Start()).Times(0);
  EXPECT_CALL(scan_process, Start()).Times(0);
  brillo::ErrorPtr error;
  RunScanImageProcess("", 0, nullptr, nullptr, props, &scan_process,
                      &convert_process, &error);

  // Expect that the pipe fds have not been released.
  EXPECT_EQ(kInputPipeFd, input_scoped_fd_.get());
  EXPECT_EQ(kOutputPipeFd, output_scoped_fd_.get());

  EXPECT_THAT(error, IsDbusErrorStartingWith(
      base::StringPrintf("Invalid scan parameter %s", kInvalidArgument)));
}

TEST_F(ManagerTest, RunScanImageInvalidModeArgument) {
  const char kBadMode[] = "Raytrace";
  VariantDictionary props{{"Mode", string{kBadMode}}};
  brillo::ProcessMock scan_process;
  brillo::ProcessMock convert_process;
  // For "scanimage", "-d", "<device name>".
  EXPECT_CALL(scan_process, AddArg(_)).Times(3);
  EXPECT_CALL(convert_process, AddArg(_)).Times(0);
  EXPECT_CALL(convert_process, Start()).Times(0);
  EXPECT_CALL(scan_process, Start()).Times(0);
  brillo::ErrorPtr error;
  RunScanImageProcess(kDeviceName,
                      kOutputFd,
                      &input_scoped_fd_,
                      &output_scoped_fd_,
                      props,
                      &scan_process,
                      &convert_process,
                      &error);

  // Expect that the pipe fds have not been released.
  EXPECT_EQ(kInputPipeFd, input_scoped_fd_.get());
  EXPECT_EQ(kOutputPipeFd, output_scoped_fd_.get());

  EXPECT_THAT(error, IsDbusErrorStartingWith(
      base::StringPrintf("Invalid mode parameter %s", kBadMode)));
}

TEST_F(ManagerTest, RunScanImageProcessCaptureFailure) {
  VariantDictionary props{
      {"Mode", string{kMode}},
      {"Resolution", uint32_t{kResolution}}
  };
  brillo::ProcessMock scan_process;
  brillo::ProcessMock convert_process;
  InSequence seq;
  ExpectStartScan(kMode,
                  kResolution,
                  &scan_process,
                  &convert_process);
  const int kErrorResult = 999;
  EXPECT_CALL(scan_process, Wait()).WillOnce(Return(kErrorResult));
  EXPECT_CALL(*metrics_library_,
              SendEnumToUMA(Manager::kMetricScanResult,
                            Manager::kBooleanMetricFailure,
                            Manager::kBooleanMetricMax));
  EXPECT_CALL(convert_process, Kill(SIGKILL, 1));
  EXPECT_CALL(convert_process, Wait()).Times(0);
  brillo::ErrorPtr error;
  RunScanImageProcess(kDeviceName,
                      kOutputFd,
                      &input_scoped_fd_,
                      &output_scoped_fd_,
                      props,
                      &scan_process,
                      &convert_process,
                      &error);
  EXPECT_EQ(kInvalidFd, input_scoped_fd_.get());
  EXPECT_EQ(kInvalidFd, output_scoped_fd_.get());
  EXPECT_THAT(error, IsDbusErrorStartingWith(
      base::StringPrintf("Scan process exited with result %d", kErrorResult)));
}

TEST_F(ManagerTest, RunScanImageProcessConvertFailure) {
  VariantDictionary props{
      {"Mode", string{kMode}},
      {"Resolution", uint32_t{kResolution}}
  };
  brillo::ProcessMock scan_process;
  brillo::ProcessMock convert_process;
  InSequence seq;
  ExpectStartScan(kMode,
                  kResolution,
                  &scan_process,
                  &convert_process);
  EXPECT_CALL(scan_process, Wait()).WillOnce(Return(0));
  EXPECT_CALL(*metrics_library_,
              SendEnumToUMA(Manager::kMetricScanResult,
                            Manager::kBooleanMetricSuccess,
                            Manager::kBooleanMetricMax));
  const int kErrorResult = 111;
  EXPECT_CALL(convert_process, Wait()).WillOnce(Return(kErrorResult));
  EXPECT_CALL(*metrics_library_,
              SendEnumToUMA(Manager::kMetricConverterResult,
                            Manager::kBooleanMetricFailure,
                            Manager::kBooleanMetricMax));
  brillo::ErrorPtr error;
  RunScanImageProcess(kDeviceName,
                      kOutputFd,
                      &input_scoped_fd_,
                      &output_scoped_fd_,
                      props,
                      &scan_process,
                      &convert_process,
                      &error);
  EXPECT_EQ(kInvalidFd, input_scoped_fd_.get());
  EXPECT_EQ(kInvalidFd, output_scoped_fd_.get());
  EXPECT_THAT(error, IsDbusErrorStartingWith(
      base::StringPrintf("Image converter process failed with result %d",
                         kErrorResult)));
}

TEST_F(ManagerTest, ScannerInfoFromString) {
  EXPECT_TRUE(ScannerInfoFromString("").empty());
  EXPECT_TRUE(ScannerInfoFromString("one").empty());
  EXPECT_TRUE(ScannerInfoFromString("one%two").empty());
  EXPECT_TRUE(ScannerInfoFromString("one%two%three").empty());
  const char kDevice0[] = "device0";
  const char kDevice1[] = "device1";
  const char kManufacturer0[] = "rayban";
  const char kManufacturer1[] = "oakley";
  const char kModel0[] = "model0";
  const char kModel1[] = "model1";
  const char kType0[] = "type0";
  const char kType1[] = "type1";
  const string kInputString(base::StringPrintf(
      "one\n"
      "%s%%%s%%%s%%%s\n"
      "one%%two\n"
      "%s%%%s%%%s%%%s\n"
      "one%%two%%three\n"
      "one%%two%%three%%four%%five\n",
      kDevice0, kManufacturer0, kModel0, kType0,
      kDevice1, kManufacturer1, kModel1, kType1));
  Manager::ScannerInfo scan_info = ScannerInfoFromString(kInputString);
  EXPECT_EQ(2, scan_info.size());
  EXPECT_TRUE(base::ContainsKey(scan_info, kDevice0));
  EXPECT_EQ(3, scan_info[kDevice0].size());
  EXPECT_STREQ(kManufacturer0, scan_info[kDevice0]["Manufacturer"].c_str());
  EXPECT_STREQ(kModel0, scan_info[kDevice0]["Model"].c_str());
  EXPECT_STREQ(kType0, scan_info[kDevice0]["Type"].c_str());

  EXPECT_TRUE(base::ContainsKey(scan_info, kDevice1));
  EXPECT_EQ(3, scan_info[kDevice1].size());
  EXPECT_STREQ(kManufacturer1, scan_info[kDevice1]["Manufacturer"].c_str());
  EXPECT_STREQ(kModel1, scan_info[kDevice1]["Model"].c_str());
  EXPECT_STREQ(kType1, scan_info[kDevice1]["Type"].c_str());
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
