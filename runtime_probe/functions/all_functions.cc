// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(stimim): auto generate this file.

#define _RUNTIME_PROBE_GENERATE_PROBE_FUNCTIONS

#include "runtime_probe/probe_function.h"

#include "runtime_probe/functions/ata_storage.h"
#include "runtime_probe/functions/cellular_network.h"
#include "runtime_probe/functions/ectool_i2cread.h"
#include "runtime_probe/functions/edid.h"
#include "runtime_probe/functions/ethernet_network.h"
#include "runtime_probe/functions/generic_battery.h"
#include "runtime_probe/functions/generic_network.h"
#include "runtime_probe/functions/generic_storage.h"
#include "runtime_probe/functions/input_device.h"
#include "runtime_probe/functions/memory.h"
#include "runtime_probe/functions/mmc_storage.h"
#include "runtime_probe/functions/nvme_storage.h"
#include "runtime_probe/functions/sequence.h"
#include "runtime_probe/functions/shell.h"
#include "runtime_probe/functions/sysfs.h"
#include "runtime_probe/functions/usb_camera.h"
#include "runtime_probe/functions/vpd_cached.h"
#include "runtime_probe/functions/wireless_network.h"

namespace runtime_probe {

namespace {

template <typename... ProbeFunctionType>
auto ConstructRegisteredFunctionTable() {
  return std::map<std::string_view, ProbeFunction::FactoryFunctionType>{
      {ProbeFunctionType::function_name,
       ProbeFunctionType::FromKwargsValue}...};
}

}  // namespace

auto ProbeFunction::registered_functions_ =
    ConstructRegisteredFunctionTable<AtaStorageFunction,
                                     CellularNetworkFunction,
                                     EctoolI2Cread,
                                     EdidFunction,
                                     EthernetNetworkFunction,
                                     GenericBattery,
                                     GenericNetworkFunction,
                                     GenericStorageFunction,
                                     InputDeviceFunction,
                                     MemoryFunction,
                                     MmcStorageFunction,
                                     NvmeStorageFunction,
                                     SequenceFunction,
                                     ShellFunction,
                                     SysfsFunction,
                                     UsbCameraFunction,
                                     VPDCached,
                                     WirelessNetworkFunction>();
}  // namespace runtime_probe
