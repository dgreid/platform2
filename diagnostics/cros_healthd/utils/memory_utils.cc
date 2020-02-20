// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/memory_utils.h"

#include <string>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_tokenizer.h>

#include "diagnostics/cros_healthd/utils/file_utils.h"

namespace diagnostics {

namespace {

using ::chromeos::cros_healthd::mojom::MemoryInfo;
using ::chromeos::cros_healthd::mojom::MemoryInfoPtr;

// Path to procfs, relative to the root directory.
constexpr char kRelativeProcPath[] = "proc";

// Sets the total_memory_kib, free_memory_kib and available_memory_kib fields of
// |info| with information read from proc/meminfo. Returns true iff all three
// fields were found and valid within proc/meminfo.
bool ParseProcMeminfo(const base::FilePath& root_dir, MemoryInfo* info) {
  std::string file_contents;
  if (!ReadAndTrimString(root_dir.Append(kRelativeProcPath), "meminfo",
                         &file_contents)) {
    LOG(ERROR) << "Unable to read /proc/meminfo.";
    return false;
  }

  // Parse the meminfo contents for MemTotal, MemFree and MemAvailable. Note
  // that these values are actually reported in KiB from /proc/meminfo, despite
  // claiming to be in kB.
  base::StringPairs keyVals;
  if (!base::SplitStringIntoKeyValuePairs(file_contents, ':', '\n', &keyVals)) {
    LOG(ERROR) << "Incorrectly formatted /proc/meminfo.";
    return false;
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
        LOG(ERROR) << "Incorrectly formatted MemTotal.";
        return false;
      }
    } else if (keyVals[i].first == "MemFree") {
      int memfree;
      base::StringTokenizer t(keyVals[i].second, " ");
      if (t.GetNext() && base::StringToInt(t.token(), &memfree) &&
          t.GetNext() && t.token() == "kB") {
        info->free_memory_kib = memfree;
        memfree_found = true;
      } else {
        LOG(ERROR) << "Incorrectly formatted MemFree.";
        return false;
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
        LOG(ERROR) << "Incorrectly formatted MemAvailable.";
        return false;
      }
    }
  }

  return memtotal_found && memfree_found && memavailable_found;
}

// Sets the page_faults_per_second field of |info| with information read from
// /proc/vmstat. Returns true iff the field was found and valid within
// /proc/vmstat.
bool ParseProcVmStat(const base::FilePath& root_dir, MemoryInfo* info) {
  std::string file_contents;
  if (!ReadAndTrimString(root_dir.Append(kRelativeProcPath), "vmstat",
                         &file_contents)) {
    LOG(ERROR) << "Unable to read /proc/vmstat.";
    return false;
  }

  // Parse the vmstat contents for pgfault.
  base::StringPairs keyVals;
  if (!base::SplitStringIntoKeyValuePairs(file_contents, ' ', '\n', &keyVals)) {
    LOG(ERROR) << "Incorrectly formatted /proc/vmstat.";
    return false;
  }

  for (int i = 0; i < keyVals.size(); i++) {
    if (keyVals[i].first == "pgfault") {
      int num_page_faults;
      if (base::StringToInt(keyVals[i].second, &num_page_faults)) {
        info->page_faults_since_last_boot = num_page_faults;
        return true;
      } else {
        LOG(ERROR) << "Incorrectly formatted pgfault.";
        return false;
      }
    }
  }

  // At this point, pgfault must not have been found.
  return false;
}

}  // namespace

MemoryInfoPtr FetchMemoryInfo(const base::FilePath& root_dir) {
  MemoryInfo info;

  if (!ParseProcMeminfo(root_dir, &info) || !ParseProcVmStat(root_dir, &info))
    return MemoryInfoPtr();

  return info.Clone();
}

}  // namespace diagnostics
