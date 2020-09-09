// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/function_templates/storage.h"

#include <utility>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/json/json_writer.h>
#include <base/strings/string_number_conversions.h>
#include <base/optional.h>
#include <brillo/strings/string_utils.h>

#include "runtime_probe/utils/file_utils.h"
#include "runtime_probe/utils/type_utils.h"

namespace runtime_probe {
namespace {
constexpr auto kStorageDirPath("/sys/class/block/");
constexpr auto kReadFileMaxSize = 1024;
constexpr auto kDefaultBytesPerSector = 512;
}  // namespace

// Get paths of all non-removeable physical storage.
std::vector<base::FilePath> StorageFunction::GetFixedDevices() const {
  std::vector<base::FilePath> res{};
  const base::FilePath storage_dir_path(kStorageDirPath);
  base::FileEnumerator storage_dir_it(storage_dir_path, true,
                                      base::FileEnumerator::SHOW_SYM_LINKS |
                                          base::FileEnumerator::FILES |
                                          base::FileEnumerator::DIRECTORIES);

  while (true) {
    const auto storage_path = storage_dir_it.Next();
    if (storage_path.empty())
      break;
    // Only return non-removable devices.
    const auto storage_removable_path = storage_path.Append("removable");
    std::string removable_res;
    if (!base::ReadFileToString(storage_removable_path, &removable_res)) {
      VLOG(2) << "Storage device " << storage_path.value()
              << " does not specify the removable property. May be a partition "
                 "of a storage device.";
      continue;
    }

    if (base::TrimWhitespaceASCII(removable_res, base::TrimPositions::TRIM_ALL)
            .as_string() != "0") {
      VLOG(2) << "Storage device " << storage_path.value() << " is removable.";
      continue;
    }

    // Skip loobpack or dm-verity device.
    if (base::StartsWith(storage_path.BaseName().value(), "loop",
                         base::CompareCase::SENSITIVE) ||
        base::StartsWith(storage_path.BaseName().value(), "dm-",
                         base::CompareCase::SENSITIVE))
      continue;

    res.push_back(storage_path);
  }

  return res;
}

// Get storage size based on |node_path|.
base::Optional<int64_t> StorageFunction::GetStorageSectorCount(
    const base::FilePath& node_path) const {
  // The sysfs entry for size info.
  const auto size_path = node_path.Append("size");
  std::string size_content;
  if (!base::ReadFileToStringWithMaxSize(size_path, &size_content,
                                         kReadFileMaxSize)) {
    LOG(WARNING) << "Storage device " << node_path.value()
                 << " does not specify size.";
    return base::nullopt;
  }

  int64_t sector_int;
  if (!StringToInt64(size_content, &sector_int)) {
    LOG(ERROR) << "Failed to parse recorded sector of" << node_path.value()
               << " to integer!";
    return base::nullopt;
  }

  return sector_int;
}

// Get the logical block size of the storage given the |node_path|.
int32_t StorageFunction::GetStorageLogicalBlockSize(
    const base::FilePath& node_path) const {
  std::string block_size_str;
  if (!base::ReadFileToString(
          node_path.Append("queue").Append("logical_block_size"),
          &block_size_str)) {
    LOG(WARNING) << "The storage driver does not specify its logical block "
                    "size in sysfs. Use default value instead.";
    return kDefaultBytesPerSector;
  }
  int32_t logical_block_size;
  if (!StringToInt(block_size_str, &logical_block_size)) {
    LOG(WARNING) << "Failed to convert retrieved block size to integer. Use "
                    "default value instead";
    return kDefaultBytesPerSector;
  }
  if (logical_block_size <= 0) {
    LOG(WARNING) << "The value of logical block size " << logical_block_size
                 << " seems errorneous. Use default value instead.";
    return kDefaultBytesPerSector;
  }
  return logical_block_size;
}

StorageFunction::DataType StorageFunction::Eval() const {
  auto json_output = InvokeHelperToJSON();
  if (!json_output) {
    LOG(ERROR)
        << "Failed to invoke helper to retrieve cached storage information.";
    return {};
  }

  DataType results(json_output->TakeList());
  for (auto& storage_res : results) {
    const auto storage_aux_res = EvalByDV(storage_res);
    if (storage_aux_res)
      storage_res.MergeDictionary(&*storage_aux_res);
  }
  return results;
}

int StorageFunction::EvalInHelper(std::string* output) const {
  const auto storage_nodes_path_list = GetFixedDevices();
  base::Value result(base::Value::Type::LIST);

  for (const auto& node_path : storage_nodes_path_list) {
    VLOG(2) << "Processnig the node " << node_path.value();

    // Get type specific fields and their values.
    auto node_res = EvalInHelperByPath(node_path);
    if (!node_res)
      continue;

    // Report the absolute path we probe the reported info from.
    node_res->SetStringKey("path", node_path.value());

    // Get size of storage.
    const auto sector_count = GetStorageSectorCount(node_path);
    const int32_t logical_block_size = GetStorageLogicalBlockSize(node_path);
    if (!sector_count) {
      node_res->SetStringKey("sectors", "-1");
      node_res->SetStringKey("size", "-1");
    } else {
      node_res->SetStringKey("sectors",
                             base::NumberToString(sector_count.value()));
      node_res->SetStringKey("size", base::NumberToString(sector_count.value() *
                                                          logical_block_size));
    }

    result.Append(std::move(*node_res));
  }
  if (!base::JSONWriter::Write(result, output)) {
    LOG(ERROR) << "Failed to serialize storage probed result to json string";
    return -1;
  }

  return 0;
}

base::Optional<base::Value> StorageFunction::EvalByDV(
    const base::Value& storage_dv) const {
  return base::nullopt;
}

}  // namespace runtime_probe
