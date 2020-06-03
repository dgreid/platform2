// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_WILCO_DTC_SUPPORTD_TELEMETRY_FAKE_EC_SERVICE_H_
#define DIAGNOSTICS_WILCO_DTC_SUPPORTD_TELEMETRY_FAKE_EC_SERVICE_H_

#include <base/macros.h>

#include "diagnostics/wilco_dtc_supportd/telemetry/ec_service.h"

namespace diagnostics {

class FakeEcService : public EcService {
 public:
  FakeEcService();
  ~FakeEcService() override;

  void EmitEcEvent(const EcService::EcEvent& ec_event) const;

 private:
  DISALLOW_COPY_AND_ASSIGN(FakeEcService);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_TELEMETRY_FAKE_EC_SERVICE_H_
