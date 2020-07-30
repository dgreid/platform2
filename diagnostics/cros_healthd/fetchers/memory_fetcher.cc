// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/memory_fetcher.h"

#include <string>
#include <utility>

#include <base/logging.h>
#include <base/optional.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_tokenizer.h>

#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

// Path to procfs, relative to the root directory.
constexpr char kRelativeProcPath[] = "proc";

// Sets the total_memory_kib, free_memory_kib and available_memory_kib fields of
// |info| with information read from proc/meminfo. Returns any error
// encountered probing the memory information. |info| is valid iff no error
// occurred.
base::Optional<mojo_ipc::ProbeErrorPtr> ParseProcMeminfo(
    const base::FilePath& root_dir, mojo_ipc::MemoryInfo* info) {
  std::string file_contents;
  if (!ReadAndTrimString(root_dir.Append(kRelativeProcPath), "meminfo",
                         &file_contents)) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kFileReadError,
                                  "Unable to read /proc/meminfo");
  }

  // Parse the meminfo contents for MemTotal, MemFree and MemAvailable. Note
  // that these values are actually reported in KiB from /proc/meminfo, despite
  // claiming to be in kB.
  base::StringPairs keyVals;
  if (!base::SplitStringIntoKeyValuePairs(file_contents, ':', '\n', &keyVals)) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kParseError,
                                  "Incorrectly formatted /proc/meminfo");
  }

  bool memtotal_found = false;
  bool memfree_found = false;
  bool memavailable_found = false;
  for (int i = 0; i < keyVals.size(); i++) {
    if (keyVals[i].first == "MemTotal") {
      int memtotal;
      base::StringTokenizer t(keyVals[i].second, " ");
      if (t.GetNext() && base::StringToInt(t.token(), &memtotal) &&
          t.GetNext() && t.token() == "kB") {
        info->total_memory_kib = memtotal;
        memtotal_found = true;
      } else {
        return CreateAndLogProbeError(mojo_ipc::ErrorType::kParseError,
                                      "Incorrectly formatted MemTotal");
      }
    } else if (keyVals[i].first == "MemFree") {
      int memfree;
      base::StringTokenizer t(keyVals[i].second, " ");
      if (t.GetNext() && base::StringToInt(t.token(), &memfree) &&
          t.GetNext() && t.token() == "kB") {
        info->free_memory_kib = memfree;
        memfree_found = true;
      } else {
        return CreateAndLogProbeError(mojo_ipc::ErrorType::kParseError,
                                      "Incorrectly formatted MemFree");
      }
    } else if (keyVals[i].first == "MemAvailable") {
      // Convert from kB to MB and cache the result.
      int memavailable;
      base::StringTokenizer t(keyVals[i].second, " ");
      if (t.GetNext() && base::StringToInt(t.token(), &memavailable) &&
          t.GetNext() && t.token() == "kB") {
        info->available_memory_kib = memavailable;
        memavailable_found = true;
      } else {
        return CreateAndLogProbeError(mojo_ipc::ErrorType::kParseError,
                                      "Incorrectly formatted MemAvailable");
      }
    }
  }

  if (!memtotal_found || !memfree_found || !memavailable_found) {
    std::string error_msg = !memtotal_found ? "Memtotal " : "";
    error_msg += !memfree_found ? "MemFree " : "";
    error_msg += !memavailable_found ? "MemAvailable " : "";
    error_msg += "not found in /proc/meminfo";

    return CreateAndLogProbeError(mojo_ipc::ErrorType::kParseError,
                                  std::move(error_msg));
  }

  return base::nullopt;
}

// Sets the page_faults_per_second field of |info| with information read from
// /proc/vmstat. Returns any error encountered probing the memory information.
// |info| is valid iff no error occurred.
base::Optional<mojo_ipc::ProbeErrorPtr> ParseProcVmStat(
    const base::FilePath& root_dir, mojo_ipc::MemoryInfo* info) {
  std::string file_contents;
  if (!ReadAndTrimString(root_dir.Append(kRelativeProcPath), "vmstat",
                         &file_contents)) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kFileReadError,
                                  "Unable to read /proc/vmstat");
  }

  // Parse the vmstat contents for pgfault.
  base::StringPairs keyVals;
  if (!base::SplitStringIntoKeyValuePairs(file_contents, ' ', '\n', &keyVals)) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kParseError,
                                  "Incorrectly formatted /proc/vmstat");
  }

  bool pgfault_found = false;
  for (int i = 0; i < keyVals.size(); i++) {
    if (keyVals[i].first == "pgfault") {
      int num_page_faults;
      if (base::StringToInt(keyVals[i].second, &num_page_faults)) {
        info->page_faults_since_last_boot = num_page_faults;
        pgfault_found = true;
        break;
      } else {
        return CreateAndLogProbeError(mojo_ipc::ErrorType::kParseError,
                                      "Incorrectly formatted pgfault");
      }
    }
  }

  if (!pgfault_found)
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kParseError,
                                  "pgfault not found in /proc/vmstat");

  return base::nullopt;
}

}  // namespace

mojo_ipc::MemoryResultPtr FetchMemoryInfo(const base::FilePath& root_dir) {
  mojo_ipc::MemoryInfo info;

  auto error = ParseProcMeminfo(root_dir, &info);
  if (error.has_value())
    return mojo_ipc::MemoryResult::NewError(std::move(error.value()));

  error = ParseProcVmStat(root_dir, &info);
  if (error.has_value())
    return mojo_ipc::MemoryResult::NewError(std::move(error.value()));

  return mojo_ipc::MemoryResult::NewMemoryInfo(mojo_ipc::MemoryInfo::New(info));
}

}  // namespace diagnostics
