// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular_capability.h"

#include <memory>

#include <chromeos/dbus/service_constants.h>

#include "shill/cellular/cellular.h"
#include "shill/cellular/cellular_capability_3gpp.h"
#include "shill/cellular/cellular_capability_cdma.h"
#include "shill/error.h"

using std::string;

namespace shill {

// All timeout values are in milliseconds
const int CellularCapability::kTimeoutActivate = 300000;
const int CellularCapability::kTimeoutConnect = 90000;
const int CellularCapability::kTimeoutDefault = 5000;
const int CellularCapability::kTimeoutDisconnect = 90000;
const int CellularCapability::kTimeoutEnable = 45000;
const int CellularCapability::kTimeoutGetLocation = 45000;
const int CellularCapability::kTimeoutRegister = 90000;
const int CellularCapability::kTimeoutReset = 90000;
const int CellularCapability::kTimeoutScan = 120000;
const int CellularCapability::kTimeoutSetInitialEpsBearer = 45000;
const int CellularCapability::kTimeoutSetupLocation = 45000;

// static
std::unique_ptr<CellularCapability> CellularCapability::Create(
    Cellular::Type type, Cellular* cellular, ModemInfo* modem_info) {
  switch (type) {
    case Cellular::kType3gpp:
      return std::make_unique<CellularCapability3gpp>(cellular, modem_info);

    case Cellular::kTypeCdma:
      return std::make_unique<CellularCapabilityCdma>(cellular, modem_info);

    default:
      NOTREACHED();
      return nullptr;
  }
}

CellularCapability::CellularCapability(Cellular* cellular,
                                       ModemInfo* modem_info)
    : cellular_(cellular), modem_info_(modem_info) {}

CellularCapability::~CellularCapability() = default;

void CellularCapability::OnUnsupportedOperation(const char* operation,
                                                Error* error) {
  string message("The ");
  message.append(operation).append(" operation is not supported.");
  Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported, message);
}

}  // namespace shill
