// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/cpu_utils.h"

#include <sys/utsname.h>

#include <set>
#include <sstream>
#include <string>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include "diagnostics/common/file_utils.h"

namespace diagnostics {

namespace {

using ::chromeos::cros_healthd::mojom::CpuArchitectureEnum;
using ::chromeos::cros_healthd::mojom::CpuInfo;
using ::chromeos::cros_healthd::mojom::CpuInfoPtr;

constexpr char kCpuinfoMaxFreqFile[] = "cpuinfo_max_freq";
constexpr char kRelativeCpufreqPolicyPath[] =
    "sys/devices/system/cpu/cpufreq/policy";
constexpr char kRelativeCpuinfoPath[] = "proc/cpuinfo";

constexpr char kModelNameKey[] = "model name";
constexpr char kPhysicalIdKey[] = "physical id";
constexpr char kProcessorIdKey[] = "processor";

// Uses uname to obtain the CPU architecture.
CpuArchitectureEnum GetArchitecture() {
  struct utsname buf;
  if (uname(&buf))
    return CpuArchitectureEnum::kUnknown;

  std::stringstream ss;
  ss << buf.machine;
  std::string machine = ss.str();
  if (machine == "x86_64")
    return CpuArchitectureEnum::kX86_64;

  return CpuArchitectureEnum::kUnknown;
}

// Parses |processor| to obtain |processor_id|, |physical_id|, and |model_name|.
// Returns true on success.
bool ParseProcessor(const std::string& processor,
                    std::string* processor_id,
                    std::string* physical_id,
                    std::string* model_name) {
  base::StringPairs pairs;
  base::SplitStringIntoKeyValuePairs(processor, ':', '\n', &pairs);
  for (const auto& key_value : pairs) {
    if (key_value.first.find(kProcessorIdKey) != std::string::npos)
      base::TrimWhitespaceASCII(key_value.second, base::TRIM_ALL, processor_id);
    else if (key_value.first.find(kPhysicalIdKey) != std::string::npos)
      base::TrimWhitespaceASCII(key_value.second, base::TRIM_ALL, physical_id);
    else if (key_value.first.find(kModelNameKey) != std::string::npos)
      base::TrimWhitespaceASCII(key_value.second, base::TRIM_ALL, model_name);

    if (!processor_id->empty() && !physical_id->empty() &&
        !model_name->empty()) {
      return true;
    }
  }

  return false;
}

// Uses |processor_info| to get information for a device's CPU(s). A single
// CpuInfoPtr is created for each physical CPU, and it is assumed that each CPU
// has the same |architecture|.
std::vector<CpuInfoPtr> GetCpuInfoFromProcessorInfo(
    const std::vector<std::string>& processor_info,
    const base::FilePath& root_dir,
    CpuArchitectureEnum architecture) {
  std::vector<CpuInfoPtr> cpu_info;
  std::set<std::string> physical_ids;
  for (const auto& processor : processor_info) {
    std::string processor_id;
    std::string physical_id;
    std::string model_name;
    bool success =
        ParseProcessor(processor, &processor_id, &physical_id, &model_name);
    if (!success || physical_ids.find(physical_id) != physical_ids.end())
      continue;

    physical_ids.insert(physical_id);
    uint32_t max_clock_speed_khz = 0;
    success = ReadInteger(
        root_dir.Append(kRelativeCpufreqPolicyPath + processor_id),
        kCpuinfoMaxFreqFile, &base::StringToUint, &max_clock_speed_khz);
    if (!success)
      continue;

    cpu_info.push_back(
        CpuInfo::New(model_name, architecture, max_clock_speed_khz));
  }

  return cpu_info;
}

}  // namespace

std::vector<CpuInfoPtr> FetchCpuInfo(const base::FilePath& root_dir) {
  std::string contents;
  if (!ReadFileToString(root_dir.Append(kRelativeCpuinfoPath), &contents))
    return std::vector<CpuInfoPtr>();

  std::vector<std::string> processor_info = base::SplitStringUsingSubstr(
      contents, "\n\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  return GetCpuInfoFromProcessorInfo(processor_info, root_dir,
                                     GetArchitecture());
}

}  // namespace diagnostics
