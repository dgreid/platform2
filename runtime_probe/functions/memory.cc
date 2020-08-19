// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/memory.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/values.h>

namespace runtime_probe {

namespace {

constexpr char kSysfsDmiPath[] = "/sys/firmware/dmi/entries";
constexpr auto kMemoryType = 17;

uint16_t MemorySize(uint16_t size) {
  // bit 15: 0=MB, 1=KB
  if (size & (1UL << 15)) {
    size = (size ^ (1UL << 15)) >> 10;
  }
  return size;
}

// SmbiosString gets the string associated with the given SMBIOS raw data.
// If the arguments are valid, |id|-th string in the SMBIOS string table is
// returned; otherwise, nullptr is returned.
std::unique_ptr<std::string> SmbiosString(const std::vector<uint8_t>& blob,
                                          uint8_t skip_bytes,
                                          uint8_t id) {
  auto output = std::make_unique<std::string>();
  if (id == 0)
    return output;
  uint8_t count = 0;
  auto data = reinterpret_cast<const char*>(blob.data());
  for (size_t i = skip_bytes, start_i = i; i < blob.size(); ++i) {
    if (data[i] == '\0') {
      ++count;
      if (count == id) {
        output->assign(data + start_i, i - start_i);
        return output;
      }
      start_i = i + 1;
    }
  }
  return nullptr;
}

base::Value GetMemoryInfo() {
  base::Value results(base::Value::Type::LIST);

  const base::FilePath dmi_dirname(kSysfsDmiPath);
  for (int entry = 0;; ++entry) {
    const base::FilePath dmi_basename(
        base::StringPrintf("%d-%d", kMemoryType, entry));
    auto dmi_path = dmi_dirname.Append(dmi_basename);
    if (!base::DirectoryExists(dmi_path))
      break;
    base::Value info(base::Value::Type::DICTIONARY);
    std::string raw_bytes;
    if (!base::ReadFileToString(dmi_path.Append("raw"), &raw_bytes)) {
      LOG(ERROR) << "Failed to read file in sysfs: " << dmi_path.value();
      continue;
    }

    auto dmi_memory = DmiMemory::From(
        std::vector<uint8_t>(raw_bytes.begin(), raw_bytes.end()));
    if (!dmi_memory) {
      LOG(ERROR) << "Failed to parse DMI raw data: " << dmi_path.value();
      continue;
    }

    // The field "slot" denotes to the entry number instead of the physical slot
    // number, which refers to mosys' output. To be compatible with current
    // HWID, we still preserve this field.
    info.SetIntKey("slot", entry);
    info.SetStringKey("path", dmi_path.value());
    info.SetIntKey("size", dmi_memory->size);
    info.SetIntKey("speed", dmi_memory->speed);
    info.SetStringKey("locator", dmi_memory->locator);
    info.SetStringKey("part", dmi_memory->part_number);
    results.GetList().push_back(std::move(info));
  }

  return results;
}

}  // namespace

std::unique_ptr<DmiMemory> DmiMemory::From(const std::vector<uint8_t>& blob) {
  if (blob.size() < sizeof(DmiMemoryRaw))
    return nullptr;

  DmiMemoryRaw dmi_memory_raw;
  std::copy(blob.begin(), blob.begin() + sizeof(DmiMemoryRaw),
            reinterpret_cast<uint8_t*>(&dmi_memory_raw));

  if (dmi_memory_raw.length < sizeof(DmiMemoryRaw))
    return nullptr;

  auto dmi_memory = std::make_unique<DmiMemory>();
  dmi_memory->size = MemorySize(dmi_memory_raw.size);
  dmi_memory->speed = dmi_memory_raw.speed;

  auto ret = SmbiosString(blob, dmi_memory_raw.length, dmi_memory_raw.locator);
  if (!ret)
    return nullptr;
  dmi_memory->locator = std::move(*ret);

  ret = SmbiosString(blob, dmi_memory_raw.length, dmi_memory_raw.part_number);
  if (!ret)
    return nullptr;
  dmi_memory->part_number = std::move(*ret);
  return dmi_memory;
}

MemoryFunction::DataType MemoryFunction::Eval() const {
  auto json_output = InvokeHelperToJSON();
  if (!json_output) {
    LOG(ERROR) << "Failed to invoke helper to retrieve memory results.";
    return {};
  }
  if (!json_output->is_list()) {
    LOG(ERROR) << "Failed to parse json output as list.";
    return {};
  }

  // TODO(b/161770131): replace with TakeList() after libchrome uprev.
  return DataType(std::move(json_output->GetList()));
}

int MemoryFunction::EvalInHelper(std::string* output) const {
  auto results = GetMemoryInfo();
  if (!base::JSONWriter::Write(results, output)) {
    LOG(ERROR) << "Failed to serialize memory probed result to json string.";
    return -1;
  }
  return 0;
}

}  // namespace runtime_probe
