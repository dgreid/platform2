// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/wilco_dtc_supportd/telemetry/system_info_service_impl.h"

#include <string>

#include "base/strings/stringprintf.h"
#include <base/sys_info.h>
#include <base/time/time.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "diagnostics/wilco_dtc_supportd/telemetry/system_info_service.h"

namespace diagnostics {

class SystemInfoServiceImplTest : public testing::Test {
 public:
  SystemInfoServiceImplTest() = default;
  ~SystemInfoServiceImplTest() override = default;

  SystemInfoServiceImplTest(const SystemInfoServiceImplTest&) = delete;
  SystemInfoServiceImplTest& operator=(const SystemInfoServiceImplTest&) =
      delete;

  SystemInfoService* service() { return &service_; }

 private:
  SystemInfoServiceImpl service_;
};

TEST_F(SystemInfoServiceImplTest, GetOsVersion) {
  constexpr char kOsVersion[] = "11932.0.2019_03_20_1100";

  base::SysInfo::SetChromeOSVersionInfoForTest(
      base::StringPrintf("CHROMEOS_RELEASE_VERSION=%s", kOsVersion),
      base::Time());

  std::string version;
  EXPECT_TRUE(service()->GetOsVersion(&version));
  EXPECT_EQ(version, kOsVersion);
}

TEST_F(SystemInfoServiceImplTest, GetOsVersionNoLsbRelease) {
  base::SysInfo::SetChromeOSVersionInfoForTest("", base::Time());

  std::string version;
  EXPECT_FALSE(service()->GetOsVersion(&version));
}

TEST_F(SystemInfoServiceImplTest, GetOsMilestone) {
  constexpr int kMilestone = 75;

  base::SysInfo::SetChromeOSVersionInfoForTest(
      base::StringPrintf("CHROMEOS_RELEASE_CHROME_MILESTONE=%d", kMilestone),
      base::Time());

  int milestone = 0;
  EXPECT_TRUE(service()->GetOsMilestone(&milestone));
  EXPECT_EQ(milestone, kMilestone);
}

TEST_F(SystemInfoServiceImplTest, GetOsMilestoneNoLsbRelease) {
  base::SysInfo::SetChromeOSVersionInfoForTest("", base::Time());

  int milestone = 0;
  EXPECT_FALSE(service()->GetOsMilestone(&milestone));
}

TEST_F(SystemInfoServiceImplTest, GetOsMilestoneNotInteger) {
  base::SysInfo::SetChromeOSVersionInfoForTest(
      "CHROMEOS_RELEASE_CHROME_MILESTONE=abcdef", base::Time());

  int milestone = 0;
  EXPECT_FALSE(service()->GetOsMilestone(&milestone));
}

}  // namespace diagnostics
