// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_WILCO_DTC_SUPPORTD_TELEMETRY_MOCK_SYSTEM_FILES_SERVICE_H_
#define DIAGNOSTICS_WILCO_DTC_SUPPORTD_TELEMETRY_MOCK_SYSTEM_FILES_SERVICE_H_

#include <memory>
#include <string>
#include <vector>

#include <base/optional.h>
#include <gmock/gmock.h>

#include "diagnostics/wilco_dtc_supportd/telemetry/system_files_service.h"

namespace diagnostics {

class MockSystemFilesService : public SystemFilesService {
 public:
  static FileDump CopyFileDump(const FileDump& file_dump);
  static FileDumps CopyFileDumps(const FileDumps& file_dumps);

  MockSystemFilesService();
  ~MockSystemFilesService() override;

  MockSystemFilesService(const MockSystemFilesService&) = delete;
  MockSystemFilesService& operator=(const MockSystemFilesService&) = delete;

  MOCK_METHOD(base::Optional<FileDump>, GetFileDump, (File), (override));
  MOCK_METHOD(base::Optional<FileDumps>,
              GetDirectoryDump,
              (Directory),
              (override));
  MOCK_METHOD(base::Optional<std::string>, GetVpdField, (VpdField), (override));
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_TELEMETRY_MOCK_SYSTEM_FILES_SERVICE_H_
