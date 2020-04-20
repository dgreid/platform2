// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_WILCO_DTC_SUPPORTD_TELEMETRY_FAKE_SYSTEM_FILES_SERVICE_H_
#define DIAGNOSTICS_WILCO_DTC_SUPPORTD_TELEMETRY_FAKE_SYSTEM_FILES_SERVICE_H_

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <base/macros.h>
#include <base/optional.h>

#include "diagnostics/wilco_dtc_supportd/telemetry/system_files_service.h"

namespace diagnostics {

class FakeSystemFilesService : public SystemFilesService {
 public:
  FakeSystemFilesService();
  ~FakeSystemFilesService() override;

  // SystemFilesService overrides:
  base::Optional<FileDump> GetFileDump(File location) override;
  bool GetDirectoryDump(Directory location,
                        std::vector<std::unique_ptr<FileDump>>* dumps) override;
  base::Optional<std::string> GetVpdField(VpdField vpd_field) override;

  // Set file dump for GetFileDump. If not set false will be returned.
  void set_file_dump(File location,
                     const SystemFilesService::FileDump& file_dump);

  // Set directory dump for GetDirectoryDump. If not set false will be returned.
  void set_directory_dump(
      Directory location,
      const std::vector<std::unique_ptr<SystemFilesService::FileDump>>&
          directory_dump);

  // Set file dump for GetVpdField. If not set false will be returned.
  void set_vpd_field(VpdField vpd_field,
                     const SystemFilesService::FileDump& file_dump);

  // Used to check the location parameter passed to GetFileDump
  const std::deque<File>& get_dumped_files() const;

  // Used to check the location parameter passed to GetDirectoryDump
  const std::deque<Directory>& get_dumped_directories() const;

 private:
  std::unordered_map<File, SystemFilesService::FileDump> file_dump_;
  std::unordered_map<Directory,
                     std::vector<std::unique_ptr<SystemFilesService::FileDump>>>
      directory_dump_;
  std::unordered_map<VpdField, SystemFilesService::FileDump> vpd_field_dump_;

  std::deque<File> dumped_files_;
  std::deque<Directory> dumped_directories_;

  DISALLOW_COPY_AND_ASSIGN(FakeSystemFilesService);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_TELEMETRY_FAKE_SYSTEM_FILES_SERVICE_H_
