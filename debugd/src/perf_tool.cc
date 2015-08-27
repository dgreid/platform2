// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/perf_tool.h"

#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <sys/utsname.h>

#include <algorithm>
#include <map>

#include "debugd/src/cpu_info_parser.h"
#include "debugd/src/process_with_output.h"
#include "debugd/src/random_selector.h"

using base::StringPrintf;

namespace debugd {

namespace {

const char kUnsupportedPerfToolErrorName[] =
    "org.chromium.debugd.error.UnsupportedPerfTool";

// Location of quipper on ChromeOS.
const char kQuipperLocation[] = "/usr/bin/quipper";

// This is registered trademark symbol that appears in model name strings.
const char kRegisteredTrademarkSymbol[] = "(R)";

// Processor model name substrings for which we have perf commands.

// For 64-bit x86 processors.
const char* kx86_64CPUOddsFiles[] = {
  NULL,
};

// For 32-bit x86 processors.
const char* kx86_32CPUOddsFiles[] = {
  // 32-bit x86 doesn't have any special cases, so all processors use the
  // default commands. Add future special cases here.
  NULL,
};

// For ARMv7 processors.
const char* kARMv7CPUOddsFiles[] = {
  // ARMv7 doesn't have any special cases, so all processors use the default
  // commands. Add future special cases here.
  NULL,
};

// For miscellaneous processors models of a known architecture.
const char kMiscCPUModelOddsFile[] = "default";

// Odds file name for miscellaneous processor architectures.
const char kMiscCPUArchOddsFile[] = "unknown";

// Prefix path to attach to the CPU odds file.
const char kCPUOddsFilePrefix[] = "/etc/perf_commands/";

// Suffix to attach to the CPU odds file.
const char kCPUOddsFileSuffix[] = ".txt";

const std::map<std::string, std::string> kIntelUarchFileTable {
    // These were found on various sources on the Internet. Main ones are:
    // http://instlatx64.atw.hu/ for CPUID to model name and
    // http://www.cpu-world.com for model name to microarchitecture
    // {"06_1C", "Bonnell"},   // Atom
    // {"06_26", "Bonnell"},   // Atom
    // {"06_36", "Saltwell"},  // Atom
    // {"06_4C", "Airmont"},   // Braswell
    // {"06_4E", "Skylake"},
    // {"06_37", "Silvermont"},
    {"06_56", "Broadwell"},    // Broadwell-DE
    {"06_47", "Broadwell"},    // Broadwell-H
    {"06_3D", "Broadwell"},
    {"06_3C", "Haswell"},
    {"06_3F", "Haswell"},
    {"06_45", "Haswell"},
    {"06_46", "Haswell"},
    {"06_3A", "IvyBridge"},
    {"06_3E", "IvyBridge"},
    {"06_2A", "SandyBridge"},
    {"06_2D", "SandyBridge"},
    // {"06_0F", "Merom"},
    // {"06_16", "Merom"},
    // {"06_17", "Nehalem"},
    // {"06_1A", "Nehalem"},
    // {"06_1D", "Nehalem"},
    // {"06_1E", "Nehalem"},
    // {"06_1F", "Nehalem"},
    // {"06_2E", "Nehalem"},
    // {"06_0D", "Dothan"},
    // {"06_09", "Banias"},
    // {"0F_03", "Prescott"},
    // {"0F_04", "Prescott"},
    // {"0F_06", "Presler"},
    // {"06_25", "Westmere"},
    // {"06_2C", "Westmere"},
    // {"06_2F", "Westmere"},
};

// Struct containing the parsed CPU identity
struct CPUIdentity {
  // The system architecture from uname().
  // (Technically, not a property of the CPU.)
  std::string arch;
  // CPU model name. e.g. "Intel(R) Celeron(R) 2955U @ 1.40GHz"
  std::string model_name;
  // For Intel CPUs, the family_model numeric identifiers from CPUID, in
  // underscore-separated uppercase hex. e.g. "06_2A"
  std::string intel_family_model;
};

// Fills in |model_name| and maybe |intel_family_model| fields of |cpuid|.
void ParseCPUModel(const CPUInfoParser& cpu_info_parser, CPUIdentity* cpuid) {
  // Get CPU model name, e.g. "Intel(R) Celeron(R) 2955U @ 1.40GHz".
  cpu_info_parser.GetKey("model name", &cpuid->model_name);
  std::string vendor;
  cpu_info_parser.GetKey("vendor_id", &vendor);
  if (vendor == "GenuineIntel") {
    std::string cpu_family;
    cpu_info_parser.GetKey("cpu family", &cpu_family);
    unsigned int cpu_family_int;
    base::StringToUint(cpu_family, &cpu_family_int);

    std::string model;
    cpu_info_parser.GetKey("model", &model);
    unsigned int model_int;
    base::StringToUint(model, &model_int);

    cpuid->intel_family_model =
        StringPrintf("%02X_%02X", cpu_family_int, model_int);
  }
}

// Converts an CPU model name string into a format that can be used as a file
// name. The rules are:
// - Replace spaces with hyphens.
// - Strip all "(R)" symbols.
// - Convert to lower case.
std::string ModelNameToFileName(const std::string& model_name) {
  std::string result = model_name;
  std::replace(result.begin(), result.end(), ' ', '-');
  ReplaceSubstringsAfterOffset(&result, 0, kRegisteredTrademarkSymbol, "");
  return base::StringToLowerASCII(result);
}

// For the given |cpuid|, look for the CPU odds file that corresponds to this
// CPU. If no matches are found for |cpuid.arch|, return the odds file for
// unknown CPU types. If the arch is valid, but no matches are found for
// |cpuid.model_name|, look for an odds file for the microarchitecture (only
// Intel uarchs currently supported). Otherwise, return the odds file for
// unknown models of the CPU architecture.
std::string GetOddsFilenameForCPU(const CPUIdentity& cpuid) {
  const std::string& arch = cpuid.arch;
  const std::string& model_name = cpuid.model_name;
  const char** cpu_odds_file_list = NULL;
  if (arch == "i386" || arch == "i486" || arch == "i586" || arch == "i686") {
    cpu_odds_file_list = kx86_32CPUOddsFiles;
  } else if (arch == "amd64" || arch == "x86_64") {
    cpu_odds_file_list = kx86_64CPUOddsFiles;
  } else if (arch == "armv7l") {
    cpu_odds_file_list = kARMv7CPUOddsFiles;
  } else {
    // If the CPU arch doesn't match any of the recognized arch families, just
    // use the CPU odds file for unknown CPU types.
    return kMiscCPUArchOddsFile;
  }

  std::string adjusted_model_name = ModelNameToFileName(model_name);
  for (size_t i = 0; cpu_odds_file_list[i]; ++i) {
    if (adjusted_model_name.find(cpu_odds_file_list[i]) != std::string::npos) {
      return arch + "/" + cpu_odds_file_list[i];
    }
  }

  if (!cpuid.intel_family_model.empty()) {
    // See if we have a microarchitecture-specific file.
    const auto& it = kIntelUarchFileTable.find(cpuid.intel_family_model);
    if (it != kIntelUarchFileTable.end()) {
      const std::string& uarch = it->second;
      return arch + "/" + uarch;
    }
  }

  // If there isn't an odds file for the particular model, use the generic odds
  // for the CPU arch.
  return arch + "/" + kMiscCPUModelOddsFile;
}

}  // namespace

PerfTool::PerfTool() : PerfTool(CPUInfoParser(), new RandomSelector, uname) {}

PerfTool::PerfTool(const CPUInfoParser& cpuinfo,
                   RandomSelector* random_selector,
                   UnameFunc uname_func)
    : random_selector_(random_selector) {
  struct CPUIdentity cpuid = {};
  ParseCPUModel(cpuinfo, &cpuid);

  // Get CPU machine hardware class, e.g. "i686", "x86_64", "armv7l".
  struct utsname uname_info;
  if (!uname_func(&uname_info))
    cpuid.arch = uname_info.machine;

  std::string odds_filename = GetOddsFilenameForCPU(cpuid);
  random_selector_->SetOddsFromFile(
      kCPUOddsFilePrefix + odds_filename + kCPUOddsFileSuffix);
}

int PerfTool::GetPerfOutput(const uint32_t& duration_secs,
                            const std::vector<std::string>& perf_args,
                            std::vector<uint8_t>* perf_data,
                            std::vector<uint8_t>* perf_stat,
                            DBus::Error* error) {
  const bool is_supported_perf_subcommand =
      perf_args[0] == "perf" &&
      (perf_args[1] == "record" || perf_args[1] == "stat");
  if (!is_supported_perf_subcommand) {
    error->set(kUnsupportedPerfToolErrorName,
               "perf_args must begin with {\"perf\", \"record\"} "
               "or {\"perf\", \"stat\"}");
    return -1;
  }

  std::string output_string;
  int result =
      GetPerfOutputHelper(duration_secs, perf_args, error, &output_string);

  if (perf_args[1] == "record")
    perf_data->assign(output_string.begin(), output_string.end());
  else if (perf_args[1] == "stat")
    perf_stat->assign(output_string.begin(), output_string.end());

  return result;
}

int PerfTool::GetRandomPerfOutput(const uint32_t& duration_secs,
                                  std::vector<uint8_t>* perf_data,
                                  std::vector<uint8_t>* perf_stat,
                                  DBus::Error* error) {
  const std::vector<std::string>& perf_args = random_selector_->GetNext();
  return GetPerfOutput(
      duration_secs, perf_args, perf_data, perf_stat, error);
}

std::vector<uint8_t> PerfTool::GetRichPerfData(const uint32_t& duration_secs,
                                               DBus::Error* error) {
  const std::vector<std::string>& perf_args = random_selector_->GetNext();
  if (perf_args[1] != "record")
    return std::vector<uint8_t>();

  std::string output_string;
  int result =
      GetPerfOutputHelper(duration_secs, perf_args, error, &output_string);

  if (result > 0)
    return std::vector<uint8_t>();

  return std::vector<uint8_t>(output_string.begin(), output_string.end());
}

int PerfTool::GetPerfOutputHelper(const uint32_t& duration_secs,
                                  const std::vector<std::string>& perf_args,
                                  DBus::Error* error,
                                  std::string* data_string) {
  // This whole method is synchronous, so we create a subprocess, let it run to
  // completion, then gather up its output to return it.
  ProcessWithOutput process;
  process.SandboxAs("root", "root");
  if (!process.Init())
    *data_string = "<process init failed>";
  // If you're going to add switches to a command, have a look at the Process
  // interface; there's support for adding options specifically.
  process.AddArg(kQuipperLocation);
  process.AddArg(StringPrintf("%u", duration_secs));
  for (const auto& arg : perf_args) {
    process.AddArg(arg);
  }
  // Run the process to completion. If the process might take a while, you may
  // have to make this asynchronous using .Start().
  int status = process.Run();
  if (status != 0)
    *data_string = StringPrintf("<process exited with status: %d", status);
  process.GetOutput(data_string);

  return status;
}

}  // namespace debugd
