// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_DELEGATE_H_
#define DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_DELEGATE_H_

#include "mojo/cros_healthd.mojom.h"

namespace diagnostics {

// Responsible for bootstrapping a mojo connection to cros_healthd.
class CrosHealthdMojoAdapterDelegate {
 public:
  virtual ~CrosHealthdMojoAdapterDelegate() = default;

  // Bootstraps a mojo connection to cros_healthd, then returns one end of the
  // bound pipe.
  virtual chromeos::cros_healthd::mojom::CrosHealthdServiceFactoryPtr
  GetCrosHealthdServiceFactory() = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_DELEGATE_H_
