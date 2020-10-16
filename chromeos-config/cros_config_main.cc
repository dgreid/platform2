// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Command-line utility to access to the Chrome OS master configuration.

#include <iostream>
#include <string>

#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>

#include "chromeos-config/libcros_config/cros_config.h"
#include "chromeos-config/libcros_config/identity.h"

int main(int argc, char* argv[]) {
  DEFINE_string(test_file, "",
                "Override path to system config database for testing.");
  DEFINE_string(test_name, "", "Override platform name for testing.");
  DEFINE_string(test_arch, "x86_64",
                "Override the machine architecture for testing.");
  DEFINE_int32(test_sku_id, brillo::kDefaultSkuId,
               "Override SKU ID for testing.");
  DEFINE_string(whitelabel_tag, "", "Override whitelabel tag for testing.");

  std::string usage = "Chrome OS Model Configuration\n\nUsage:\n  " +
                      std::string(argv[0]) + " [flags] <path> <key>\n\n" +
                      "Set CROS_CONFIG_DEBUG=1 in your environment to emit " +
                      "debug logging messages.\n";
  brillo::FlagHelper::Init(argc, argv, usage);

  if (FLAGS_test_sku_id != brillo::kDefaultSkuId && FLAGS_test_file.empty()) {
    std::cerr << "Passing --test_sku_id requires you pass --test_file.\n";
    return 1;
  }

  CHECK_EQ(FLAGS_test_file.empty(), FLAGS_test_name.empty())
      << "You must pass both --test_file and --test_name or neither.";

  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_FILE;
  settings.log_file_path = "/var/log/cros_config.log";
  settings.lock_log = logging::DONT_LOCK_LOG_FILE;
  settings.delete_old = logging::APPEND_TO_OLD_LOG_FILE;
  logging::InitLogging(settings);
  logging::SetMinLogLevel(-3);

  brillo::CrosConfig cros_config;
  if (FLAGS_test_file.empty()) {
    if (!cros_config.Init()) {
      return 1;
    }
  } else {
    if (!cros_config.InitForTest(
            FLAGS_test_sku_id, base::FilePath(FLAGS_test_file),
            brillo::CrosConfigIdentity::CurrentSystemArchitecture(
                FLAGS_test_arch),
            FLAGS_test_name, FLAGS_whitelabel_tag)) {
      return 1;
    }
  }

  base::CommandLine::StringVector args =
      base::CommandLine::ForCurrentProcess()->GetArgs();

  if (args.size() != 2) {
    std::cerr << usage << "\nPass --help for more information." << std::endl;
    return 1;
  }

  std::string path = args[0];
  std::string property = args[1];

  std::string value;
  bool result = cros_config.GetString(path, property, &value);
  if (!result) {
    return 1;
  }

  std::cout << value;
  return 0;
}
