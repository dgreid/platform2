/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ARC_ADBD_ADBD_H_
#define ARC_ADBD_ADBD_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>

namespace adbd {

// Represents a loadable kernel module. This is then converted to a modprobe(8)
// invocation.
struct AdbdConfigurationKernelModule {
  // Name of the kernel module.
  std::string name;

  // Optional parameters to the module.
  std::vector<std::string> parameters;
};

// Represents the configuration for the service.
struct AdbdConfiguration {
  // The USB product ID. Is SoC-specific.
  std::string usb_product_id;

  // Optional list of kernel modules that need to be loaded before setting up
  // the USB gadget.
  std::vector<AdbdConfigurationKernelModule> kernel_modules;
};

// Creates a FIFO at |path|, owned and only writable by the Android shell user.
bool CreatePipe(const base::FilePath& path);

// Returns the USB product ID for the current device, or an empty string if the
// device does not support ADB over USB.
bool GetConfiguration(AdbdConfiguration* config);

// TODO(hidehiko): Remove once libchrome rolls and provides this method.
std::string GetStrippedReleaseBoard();

// Returns the name of the UDC driver that is available in the system, or an
// empty string if none are available.
std::string GetUDCDriver();

// Sets up the ConfigFS files to be able to use the ADB gadget. The
// |serialnumber| parameter is used to setup how the device appears in "adb
// devices". The |usb_product_id| and |usb_product_name| parameters are used so
// that the USB gadget self-reports as Android running in Chrome OS.
bool SetupConfigFS(const std::string& serialnumber,
                   const std::string& usb_product_id,
                   const std::string& usb_product_name);

// Sets up FunctionFS and returns an open FD to the control endpoint of the
// fully setup ADB gadget. The gadget will be torn down if the FD is closed when
// this program exits.
base::ScopedFD SetupFunctionFS(const std::string& udc_driver_name);

// Sets up all the necessary kernel modules for the device.
bool SetupKernelModules(
    const std::vector<AdbdConfigurationKernelModule>& kernel_modules);

// Bind-mount the bulk-in/bulk-out endpoints into the shared mount for
// container.
bool BindMountUsbBulkEndpoints();
}  // namespace adbd

#endif  // ARC_ADBD_ADBD_H_
