// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/modem_helper_directory.h"

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <base/macros.h>
#include <brillo/proto_file_io.h>

#include "modemfwd/logging.h"
#include "modemfwd/modem_helper.h"
#include "modemfwd/proto_bindings/helper_manifest.pb.h"

namespace {

constexpr char kManifestName[] = "helper_manifest.prototxt";

}  // namespace

namespace modemfwd {

class ModemHelperDirectoryImpl : public ModemHelperDirectory {
 public:
  ModemHelperDirectoryImpl(const HelperManifest& manifest,
                           const base::FilePath& directory) {
    for (const HelperEntry& entry : manifest.helper()) {
      if (entry.filename().empty())
        continue;

      HelperInfo helper_info(directory.Append(entry.filename()));
      for (const std::string& extra_argument : entry.extra_argument()) {
        helper_info.extra_arguments.push_back(extra_argument);
      }

      auto helper = CreateModemHelper(helper_info);
      for (const std::string& device_id : entry.device_id()) {
        ELOG(INFO) << "Adding helper " << helper_info.executable_path.value()
                   << " for [" << device_id << "]";
        helpers_by_id_[device_id] = helper.get();
      }
      available_helpers_.push_back(std::move(helper));
    }
  }
  ModemHelperDirectoryImpl(const ModemHelperDirectoryImpl&) = delete;
  ModemHelperDirectoryImpl& operator=(const ModemHelperDirectoryImpl&) = delete;

  ~ModemHelperDirectoryImpl() override = default;

  bool FoundHelpers() const { return !helpers_by_id_.empty(); }

  ModemHelper* GetHelperForDeviceId(const std::string& id) override {
    auto it = helpers_by_id_.find(id);
    if (it == helpers_by_id_.end())
      return nullptr;

    return it->second;
  }

  void ForEachHelper(
      const base::Callback<void(const std::string&, ModemHelper*)>& callback)
      override {
    for (const auto& entry : helpers_by_id_)
      callback.Run(entry.first, entry.second);
  }

 private:
  std::vector<std::unique_ptr<ModemHelper>> available_helpers_;
  // Pointers in this map are owned by |available_helpers_|.
  std::map<std::string, ModemHelper*> helpers_by_id_;
};

std::unique_ptr<ModemHelperDirectory> CreateModemHelperDirectory(
    const base::FilePath& directory) {
  HelperManifest parsed_manifest;
  if (!brillo::ReadTextProtobuf(directory.Append(kManifestName),
                                &parsed_manifest))
    return nullptr;

  auto helper_dir =
      std::make_unique<ModemHelperDirectoryImpl>(parsed_manifest, directory);
  if (!helper_dir->FoundHelpers())
    return nullptr;

  return std::move(helper_dir);
}

}  // namespace modemfwd
