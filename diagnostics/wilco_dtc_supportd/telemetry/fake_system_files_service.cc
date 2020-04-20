// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/wilco_dtc_supportd/telemetry/fake_system_files_service.h"

#include <utility>

#include <base/logging.h>

namespace diagnostics {

FakeSystemFilesService::FakeSystemFilesService() = default;
FakeSystemFilesService::~FakeSystemFilesService() = default;

bool FakeSystemFilesService::GetFileDump(File location, FileDump* dump) {
  DCHECK(dump);

  dumped_files_.push_back(location);

  auto it = file_dump_.find(location);

  if (it == file_dump_.end())
    return false;

  dump->contents = it->second.contents;
  dump->path = it->second.path;
  dump->canonical_path = it->second.canonical_path;

  return true;
}

bool FakeSystemFilesService::GetDirectoryDump(
    Directory location, std::vector<std::unique_ptr<FileDump>>* dumps) {
  DCHECK(dumps);

  dumped_directories_.push_back(location);

  auto it = directory_dump_.find(location);

  if (it == directory_dump_.end())
    return false;

  for (const auto& file_dump : it->second) {
    auto dump = std::make_unique<SystemFilesService::FileDump>();

    dump->contents = file_dump->contents;
    dump->path = file_dump->path;
    dump->canonical_path = file_dump->canonical_path;

    dumps->push_back(std::move(dump));
  }

  return true;
}

base::Optional<std::string> FakeSystemFilesService::GetVpdField(
    VpdField vpd_field) {
  auto it = vpd_field_dump_.find(vpd_field);

  if (it == vpd_field_dump_.end())
    return base::nullopt;

  return it->second.contents;
}

void FakeSystemFilesService::set_file_dump(
    File location, const SystemFilesService::FileDump& file_dump) {
  auto& ref = file_dump_[location];

  ref.contents = file_dump.contents;
  ref.path = file_dump.path;
  ref.canonical_path = file_dump.canonical_path;
}

void FakeSystemFilesService::set_directory_dump(
    Directory location,
    const std::vector<std::unique_ptr<SystemFilesService::FileDump>>&
        directory_dump) {
  auto& ref = directory_dump_[location];
  ref.clear();

  for (const auto& file_dump : directory_dump) {
    auto dump = std::make_unique<SystemFilesService::FileDump>();

    dump->contents = file_dump->contents;
    dump->path = file_dump->path;
    dump->canonical_path = file_dump->canonical_path;

    ref.push_back(std::move(dump));
  }
}

void FakeSystemFilesService::set_vpd_field(
    VpdField vpd_field, const SystemFilesService::FileDump& file_dump) {
  auto& ref = vpd_field_dump_[vpd_field];

  ref.contents = file_dump.contents;
  ref.path = file_dump.path;
  ref.canonical_path = file_dump.canonical_path;
}

const std::deque<SystemFilesService::File>&
FakeSystemFilesService::get_dumped_files() const {
  return dumped_files_;
}

const std::deque<SystemFilesService::Directory>&
FakeSystemFilesService::get_dumped_directories() const {
  return dumped_directories_;
}

}  // namespace diagnostics
