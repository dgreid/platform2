// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <iostream>
#include <string>

#include "diagnostics/cros_health_tool/diag/diag.h"
#include "diagnostics/cros_health_tool/event/event.h"
#include "diagnostics/cros_health_tool/telem/telem.h"

namespace {

void PrintHelp() {
  std::cout << "cros-health-tool" << std::endl;
  std::cout << "    subtools: diag, telem, event" << std::endl;
  std::cout << "    Usage: cros-health-tool {subtool} $@" << std::endl;
  std::cout << "    Help: cros-health-tool {subtool} --help" << std::endl;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintHelp();
    return EXIT_FAILURE;
  }

  // Shift input parameters so they can be forwarded directly to the subtool.
  int subtool_argc = argc - 1;
  char** subtool_argv = &argv[1];

  std::string subtool = subtool_argv[0];
  if (subtool == "diag") {
    return diagnostics::diag_main(subtool_argc, subtool_argv);
  } else if (subtool == "event") {
    return diagnostics::event_main(subtool_argc, subtool_argv);
  } else if (subtool == "telem") {
    return diagnostics::telem_main(subtool_argc, subtool_argv);
  } else if (subtool == "help" || subtool == "--help" || subtool == "-h") {
    PrintHelp();
    return EXIT_SUCCESS;
  } else {
    PrintHelp();
  }

  return EXIT_FAILURE;
}
