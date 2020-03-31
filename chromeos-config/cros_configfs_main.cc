// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Command line utility to mount and unmount ChromeOS ConfigFS.

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/stl_util.h>
#include <base/strings/string_util.h>
#include <brillo/syslog_logging.h>

#include "chromeos-config/libcros_config/cros_config.h"

static int Mount(const std::string& progname,
                 const std::vector<std::string>& args) {
  brillo::CrosConfig cros_config;
  base::FilePath source(args[0]);
  base::FilePath target(args[1]);
  if (!cros_config.MountConfigFS(source, target)) {
    std::cerr << "Mount failed!" << std::endl;
    return 1;
  }
  return 0;
}

static int MountFallback(const std::string& progname,
                         const std::vector<std::string>& args) {
  brillo::CrosConfig cros_config;
  base::FilePath target(args[0]);
  if (!cros_config.MountFallbackConfigFS(target)) {
    std::cerr << "Fallback mount failed!" << std::endl;
    return 1;
  }
  return 0;
}

static int Unmount(const std::string& progname,
                   const std::vector<std::string>& args) {
  brillo::CrosConfig cros_config;
  base::FilePath target(args[0]);
  if (!cros_config.Unmount(target)) {
    std::cerr << "Unmount failed!" << std::endl;
    return 1;
  }
  return 0;
}

static int PrintUsage(const std::string& progname,
                      const std::vector<std::string>& args);

static struct {
  std::vector<std::string> names;
  std::vector<std::string> argnames;
  std::string description;
  int (*handler)(const std::string& progname,
                 const std::vector<std::string>& args);
} subcommands[] = {
    {
        .names = {"mount"},
        .argnames = {"source", "target"},
        .description = "Mount a ChromeOS ConfigFS image for unibuild.",
        .handler = Mount,
    },
    {
        .names = {"mount-fallback"},
        .argnames = {"target"},
        .description =
            "Mount a ChromeOS ConfigFS fallback system for non-unibuild.",
        .handler = MountFallback,
    },
    {
        .names = {"unmount"},
        .argnames = {"target"},
        .description = "Unmount a previously mounted ChromeOS ConfigFS.",
        .handler = Unmount,
    },
    {
        .names = {"help", "--help", "-h"},
        .argnames = {},
        .description = "Print usage.",
        .handler = PrintUsage,
    },
};

static int PrintUsage(const std::string& progname,
                      const std::vector<std::string>& args) {
  struct {
    std::string usage;
    std::string description;
  } usagestrings[base::size(subcommands)];

  size_t max_usage_len = 0;
  auto usagestrings_it = usagestrings;
  for (auto& subcommand : subcommands) {
    auto& usagestring = *usagestrings_it++;
    usagestring.usage = "  " + progname + " ";
    if (subcommand.names.size() == 1) {
      usagestring.usage += subcommand.names[0];
    } else {
      usagestring.usage += "{" + base::JoinString(subcommand.names, "|") + "}";
    }
    for (auto& argname : subcommand.argnames) {
      usagestring.usage += " <" + argname + ">";
    }
    max_usage_len = std::max(usagestring.usage.size(), max_usage_len);
    usagestring.description = subcommand.description;
  }

  std::cerr << "ChromeOS Master Configuration: Filesytem Manager" << std::endl
            << std::endl
            << "Usage:" << std::endl;
  for (auto& usagestring : usagestrings) {
    std::cerr << std::left << std::setw(max_usage_len) << usagestring.usage
              << "  " << usagestring.description << std::endl;
  }
  return 0;
}

int main(const int argc, const char* const argv[]) {
  std::string progname(argv[0]);
  if (argc < 2) {
    PrintUsage(progname, {});
    return 1;
  }

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  std::string subcmd_name(argv[1]);
  std::vector<std::string> args(argv + 2, argv + argc);
  for (auto& subcommand : subcommands) {
    if (std::any_of(subcommand.names.begin(), subcommand.names.end(),
                    [&subcmd_name](const std::string& name) {
                      return name == subcmd_name;
                    })) {
      if (args.size() != subcommand.argnames.size()) {
        PrintUsage(progname, {});
        std::cerr << std::endl
                  << subcmd_name << " takes " << subcommand.argnames.size()
                  << " arguments, " << args.size() << " given." << std::endl;
        return 1;
      }
      return subcommand.handler(progname, args);
    }
  }

  PrintUsage(progname, {});
  std::cerr << std::endl
            << "Unrecognized subcommand: " << subcmd_name << std::endl;
  return 1;
}
