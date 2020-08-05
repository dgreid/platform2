// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <chromeos/chromeos-config/libcros_config/fake_cros_config.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/fetchers/system_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {

namespace {

void SetUpSystemFiles(const base::FilePath& root_dir,
                      FuzzedDataProvider* provider) {
  // Populate fake cached VPD values.
  base::FilePath relative_vpd_rw_dir = root_dir.Append(kRelativeVpdRwPath);
  CHECK(WriteFileAndCreateParentDirs(
      relative_vpd_rw_dir.Append(kFirstPowerDateFileName),
      provider->ConsumeRandomLengthString()));
  base::FilePath relative_vpd_ro_dir = root_dir.Append(kRelativeVpdRoPath);
  CHECK(WriteFileAndCreateParentDirs(
      relative_vpd_ro_dir.Append(kManufactureDateFileName),
      provider->ConsumeRandomLengthString()));
  CHECK(WriteFileAndCreateParentDirs(
      relative_vpd_ro_dir.Append(kSkuNumberFileName),
      provider->ConsumeRandomLengthString()));
  // Populate fake DMI values.
  base::FilePath relative_dmi_info_path = root_dir.Append(kRelativeDmiInfoPath);
  CHECK(WriteFileAndCreateParentDirs(
      relative_dmi_info_path.Append(kBiosVersionFileName),
      provider->ConsumeRandomLengthString()));
  CHECK(WriteFileAndCreateParentDirs(
      relative_dmi_info_path.Append(kBoardNameFileName),
      provider->ConsumeRandomLengthString()));
  CHECK(WriteFileAndCreateParentDirs(
      relative_dmi_info_path.Append(kBoardVersionFileName),
      provider->ConsumeRandomLengthString()));
  CHECK(WriteFileAndCreateParentDirs(
      relative_dmi_info_path.Append(kChassisTypeFileName),
      provider->ConsumeRandomLengthString()));
  CHECK(WriteFileAndCreateParentDirs(
      relative_dmi_info_path.Append(kProductNameFileName),
      provider->ConsumeRandomLengthString()));
}

}  // namespace

class Environment {
 public:
  Environment() {
    logging::SetMinLogLevel(logging::LOG_FATAL);  // Disable logging.
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  // 100 KiB max input size. Doing multiple writes and read for significantly
  // large files can potentially cause the fuzzer to timeout.
  constexpr int kMaxInputSize = 102400;
  if (size > kMaxInputSize)
    return 0;

  FuzzedDataProvider provider(data, size);
  base::ScopedTempDir temp_dir;
  // Setup temp dir for writing out the system files the fetcher will read
  // from.
  CHECK(temp_dir.CreateUniqueTempDir());
  SetUpSystemFiles(temp_dir.GetPath(), &provider);

  MockContext mock_context;
  mock_context.Initialize();
  mock_context.fake_system_config()->SetHasSkuNumber(true);
  mock_context.fake_system_config()->SetMarketingName("fake_marketing_name");
  SystemFetcher system_fetcher{&mock_context};
  auto system_info =
      system_fetcher.FetchSystemInfo(base::FilePath(temp_dir.GetPath()));

  return 0;
}

}  // namespace diagnostics
