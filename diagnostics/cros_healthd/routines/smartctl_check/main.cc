// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <iostream>
#include <string>

#include <base/command_line.h>
#include <base/logging.h>
#include <brillo/dbus/dbus_connection.h>
#include <brillo/errors/error.h>
#include <brillo/flag_helper.h>
#include <dbus/dbus.h>

#include "debugd/dbus-proxies.h"
#include "diagnostics/cros_healthd/routines/smartctl_check/smartctl_check_utils.h"

// 'smartctl-check' command-line tool:
//
// Invokes smartctl -A via debugd over DBus.
//
int main(int argc, char** argv) {
  brillo::FlagHelper::Init(argc, argv, "smartctl-check - diagnostic routine.");
  logging::InitLogging(logging::LoggingSettings());

  brillo::DBusConnection connection;
  scoped_refptr<dbus::Bus> bus = connection.Connect();
  if (!bus) {
    LOG(ERROR) << "smartctl-check: Could not connect to system DBus bus";
    return EXIT_FAILURE;
  }
  org::chromium::debugdProxy proxy(bus);
  std::string output;
  brillo::ErrorPtr error;

  if (proxy.Smartctl("attributes", &output, &error)) {
    VLOG(1) << "Smartctl succeeded.";

    int available_spare_pct, available_spare_threshold_pct;
    if (diagnostics::ScrapeAvailableSparePercents(
            output, &available_spare_pct, &available_spare_threshold_pct)) {
      if (available_spare_pct > available_spare_threshold_pct) {
        VLOG(1) << "smartctl-check: PASSED: available_spare ("
                << available_spare_pct
                << "%) is greater than available_spare_threshold ("
                << available_spare_threshold_pct << "%)";
        return EXIT_SUCCESS;
      } else {
        LOG(ERROR) << "smartctl-check: FAILURE: available spare is less than "
                      "available spare threshold";
        return EXIT_FAILURE;
      }
    } else {
      LOG(ERROR) << "smartctl-check: FAILURE: unable to parse smartctl output";
      return EXIT_FAILURE;
    }
  } else {
    LOG(ERROR) << "smartctl-check: FAILURE: unable to connect to debugd "
               << error->GetDomain() << " code=" << error->GetCode()
               << " message=\"" << error->GetMessage() << "\"";
    return EXIT_FAILURE;
  }
}
