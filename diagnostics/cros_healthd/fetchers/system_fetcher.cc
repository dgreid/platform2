// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/system_fetcher.h"

#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/system/sys_info.h>

#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"

namespace diagnostics {

constexpr char kRelativeDmiInfoPath[] = "sys/class/dmi/id";
constexpr char kRelativeVpdRoPath[] = "sys/firmware/vpd/ro/";
constexpr char kRelativeVpdRwPath[] = "sys/firmware/vpd/rw/";

constexpr char kFirstPowerDateFileName[] = "ActivateDate";
constexpr char kManufactureDateFileName[] = "mfg_date";
constexpr char kSkuNumberFileName[] = "sku_number";
constexpr char kProductSerialNumberFileName[] = "serial_number";
constexpr char kBiosVersionFileName[] = "bios_version";
constexpr char kBoardNameFileName[] = "board_name";
constexpr char kBoardVersionFileName[] = "board_version";
constexpr char kChassisTypeFileName[] = "chassis_type";
constexpr char kProductNameFileName[] = "product_name";
constexpr char kProductModelNameFileName[] = "model_name";

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

// Fetches information from DMI. Since there are several devices that do not
// provide DMI information, these fields are optional in SystemInfo. As a
// result, a missing DMI file does not indicate a ProbeError. A ProbeError is
// reported when the "chassis_type" field cannot be successfully parsed into an
// unsigned integer.
base::Optional<mojo_ipc::ProbeErrorPtr> FetchDmiInfo(
    const base::FilePath& root_dir, mojo_ipc::SystemInfo* output_info) {
  const base::FilePath& relative_dmi_info_path =
      root_dir.Append(kRelativeDmiInfoPath);
  std::string bios_version;
  if (ReadAndTrimString(relative_dmi_info_path, kBiosVersionFileName,
                        &bios_version)) {
    output_info->bios_version = bios_version;
  }

  std::string board_name;
  if (ReadAndTrimString(relative_dmi_info_path, kBoardNameFileName,
                        &board_name)) {
    output_info->board_name = board_name;
  }

  std::string board_version;
  if (ReadAndTrimString(relative_dmi_info_path, kBoardVersionFileName,
                        &board_version)) {
    output_info->board_version = board_version;
  }

  std::string chassis_type_str;
  if (ReadAndTrimString(relative_dmi_info_path, kChassisTypeFileName,
                        &chassis_type_str)) {
    uint64_t chassis_type;
    if (base::StringToUint64(chassis_type_str, &chassis_type)) {
      output_info->chassis_type = mojo_ipc::NullableUint64::New(chassis_type);
    } else {
      return CreateAndLogProbeError(
          mojo_ipc::ErrorType::kParseError,
          base::StringPrintf("Failed to convert chassis_type: %s",
                             chassis_type_str.c_str()));
    }
  }

  std::string product_name;
  if (ReadAndTrimString(relative_dmi_info_path, kProductNameFileName,
                        &product_name)) {
    output_info->product_name = product_name;
  }

  return base::nullopt;
}

}  // namespace

SystemFetcher::SystemFetcher(Context* context) : context_(context) {
  DCHECK(context_);
}

SystemFetcher::~SystemFetcher() = default;

base::Optional<mojo_ipc::ProbeErrorPtr> SystemFetcher::FetchCachedVpdInfo(
    const base::FilePath& root_dir, mojo_ipc::SystemInfo* output_info) {
  std::string first_power_date;
  if (ReadAndTrimString(root_dir.Append(kRelativeVpdRwPath),
                        kFirstPowerDateFileName, &first_power_date)) {
    output_info->first_power_date = first_power_date;
  }

  const base::FilePath relative_vpd_ro_dir =
      root_dir.Append(kRelativeVpdRoPath);
  std::string manufacture_date;
  if (ReadAndTrimString(relative_vpd_ro_dir, kManufactureDateFileName,
                        &manufacture_date)) {
    output_info->manufacture_date = manufacture_date;
  }

  if (context_->system_config()->HasSkuNumber()) {
    std::string sku_number;
    if (!ReadAndTrimString(relative_vpd_ro_dir, kSkuNumberFileName,
                           &sku_number)) {
      return CreateAndLogProbeError(
          mojo_ipc::ErrorType::kFileReadError,
          "Unable to read VPD file " + std::string(kSkuNumberFileName) +
              " at path " + relative_vpd_ro_dir.value().c_str());
    }
    output_info->product_sku_number = sku_number;
  }

  std::string product_serial_number;
  if (ReadAndTrimString(relative_vpd_ro_dir, kProductSerialNumberFileName,
                        &product_serial_number)) {
    output_info->product_serial_number = product_serial_number;
  }

  std::string product_model_name;
  if (ReadAndTrimString(relative_vpd_ro_dir, kProductModelNameFileName,
                        &product_model_name)) {
    output_info->product_model_name = product_model_name;
  }

  return base::nullopt;
}

void SystemFetcher::FetchMasterConfigInfo(mojo_ipc::SystemInfo* output_info) {
  output_info->marketing_name = context_->system_config()->GetMarketingName();
}

base::Optional<mojo_ipc::ProbeErrorPtr> SystemFetcher::FetchOsVersion(
    mojo_ipc::OsVersion* os_version) {
  std::string milestone;
  std::string build;
  std::string patch;
  std::string release_channel;

  if (!base::SysInfo::GetLsbReleaseValue("CHROMEOS_RELEASE_CHROME_MILESTONE",
                                         &milestone)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kFileReadError,
        "Unable to read OS milestone from /etc/lsb-release");
  }

  if (!base::SysInfo::GetLsbReleaseValue("CHROMEOS_RELEASE_BUILD_NUMBER",
                                         &build)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kFileReadError,
        "Unable to read OS build number from /etc/lsb-release");
  }

  if (!base::SysInfo::GetLsbReleaseValue("CHROMEOS_RELEASE_PATCH_NUMBER",
                                         &patch)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kFileReadError,
        "Unable to read OS patch number from /etc/lsb-release");
  }

  if (!base::SysInfo::GetLsbReleaseValue("CHROMEOS_RELEASE_TRACK",
                                         &release_channel)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kFileReadError,
        "Unable to read OS release track from /etc/lsb-release");
  }

  os_version->release_milestone = milestone;
  os_version->build_number = build;
  os_version->patch_number = patch;
  os_version->release_channel = release_channel;
  return base::nullopt;
}

mojo_ipc::SystemResultPtr SystemFetcher::FetchSystemInfo(
    const base::FilePath& root_dir) {
  mojo_ipc::SystemInfo system_info;

  base::Optional<mojo_ipc::ProbeErrorPtr> error =
      FetchCachedVpdInfo(root_dir, &system_info);
  if (error.has_value())
    return mojo_ipc::SystemResult::NewError(std::move(error.value()));

  FetchMasterConfigInfo(&system_info);
  error = FetchDmiInfo(root_dir, &system_info);
  if (error.has_value())
    return mojo_ipc::SystemResult::NewError(std::move(error.value()));

  system_info.os_version = mojo_ipc::OsVersion::New();
  error = FetchOsVersion(system_info.os_version.get());
  if (error.has_value()) {
    return mojo_ipc::SystemResult::NewError(std::move(error.value()));
  }

  return mojo_ipc::SystemResult::NewSystemInfo(system_info.Clone());
}

}  // namespace diagnostics
