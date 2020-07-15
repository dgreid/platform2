// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_CPU_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_CPU_FETCHER_H_

#include <base/files/file_path.h>

#include "diagnostics/cros_healthd/system/context.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// The CpuFetcher class is responsible for gathering CPU info reported by
// cros_healthd.
class CpuFetcher final {
 public:
  explicit CpuFetcher(Context* context);
  CpuFetcher(const CpuFetcher&) = delete;
  CpuFetcher& operator=(const CpuFetcher&) = delete;
  ~CpuFetcher();

  // Returns a structure with a list of data fields for each of the device's
  // CPUs or the error that occurred fetching the information.
  chromeos::cros_healthd::mojom::CpuResultPtr FetchCpuInfo(
      const base::FilePath& root_dir);

 private:
  // Uses |context_| to obtain the CPU architecture.
  chromeos::cros_healthd::mojom::CpuArchitectureEnum GetArchitecture();

  // Unowned pointer that outlives this CachedVpdFetcher instance.
  Context* const context_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_CPU_FETCHER_H_
