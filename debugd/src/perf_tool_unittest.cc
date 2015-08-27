// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/perf_tool.h"

#include <string.h>
#include <sys/utsname.h>

#include <sstream>

#include <base/memory/scoped_ptr.h>
#include <base/strings/string_util.h>
#include <gtest/gtest.h>

#include "debugd/src/random_selector.h"

namespace debugd {

namespace {

// Mock version of Random selector that just stores the odds file path, without
// reading from it. This allows us to test the file path strings without needing
// them to exist.
class MockRandomSelector : public RandomSelector {
 public:
  // Override the SetOddsFromFile() function to simply store the filename.
  void SetOddsFromFile(const std::string& filename) override {
    filename_ = filename;
  }

  const std::string& filename() const {
    return filename_;
  }

 private:
  std::string filename_;
};

// Construct mock /proc/cpuinfo contents.
CPUInfoParser MakeMockCPUInfo(const char* model_name, int intel_model) {
  const bool is_intel = intel_model != 0;
  std::stringstream contents;
  if (is_intel) {
    contents
        << "processor\t: 0\n"
        << "vendor_id\t: GenuineIntel\n"
        << "cpu family\t: 6\n"
        << "model\t\t: " << intel_model << "\n"
        << "model name\t: " << model_name << "\n"
        << "\n";
  } else {
    contents
        << "processor\t: 0\n"
        << "vendor_id\t: OtherVendor\n"
        << "model name\t: " << model_name << "\n"
        << "\n";
  }
  return CPUInfoParser(CPUInfoParser::SET_CONTENTS, contents.str());
}

PerfTool::UnameFunc MakeMockUnameFunc(const char* arch) {
  return [arch](struct utsname* buf) -> int {
    strncpy(buf->machine, arch, sizeof(buf->machine));
    return 0;
  };
}

}  // namespace

// Tests the CPU odds file lookup against various CPU architecture and CPU model
// name strings.
TEST(PerfToolTest, TestCPUOddsFileLookup) {
  // Use a struct to define the expected outputs for each set of inputs.
  const struct {
    // Inputs.
    const char* arch;
    const char* model_name;
    uint8_t intel_model;
    // Expected output.
    const char* filename;
  } kOddsFileTestCases[] = {
    // 64-bit x86.
    { .arch = "x86_64",
      .model_name = "Intel(R) Celeron(R) 2955U @ 1.40GHz",
      .intel_model = 0x45,  // Haswell
      .filename = "/etc/perf_commands/x86_64/Haswell.txt" },
    { .arch = "x86_64",
      .model_name = "Intel(R) Core(TM) i5-2467M CPU @ 1.60GHz",
      .intel_model = 0x2A,  // SandyBridge
      .filename = "/etc/perf_commands/x86_64/SandyBridge.txt" },
    { .arch = "x86_64",
      .model_name = "Intel(R) Core(TM) i5-3427U CPU @ 1.80GHz",
      .intel_model = 0x3A,  // IvyBridge
      .filename = "/etc/perf_commands/x86_64/IvyBridge.txt" },
    { .arch = "x86_64",
      .model_name = "Intel(R) Celeron(R) CPU 867 @ 1.30GHz",
      .intel_model = 0x2A,  // SandyBridge
      .filename = "/etc/perf_commands/x86_64/SandyBridge.txt" },
    { .arch = "x86_64",
      .model_name = "Intel(R) Celeron(R) CPU 847 @ 1.10GHz",
      .intel_model = 0x2A,  // SandyBridge
      .filename = "/etc/perf_commands/x86_64/SandyBridge.txt" },
    { .arch = "x86_64",
      .model_name = "Intel(R) Celeron(R) CPU N2830 @ 2.13GHz",
      .intel_model = 0x37,  // Silvermont
      .filename = "/etc/perf_commands/x86_64/default.txt" },

    // 32-bit x86.
    { .arch = "i686",
      .model_name = "Intel(R) Atom(TM) CPU N455 @ 1.66GHz",
      .intel_model = 0x1C,  // Bonnell
      .filename = "/etc/perf_commands/i686/default.txt" },
    { .arch = "i686",
      .model_name = "Intel(R) Atom(TM) CPU N570 @ 1.66GHz",
      .intel_model = 0x1C,  // Bonnell
      .filename = "/etc/perf_commands/i686/default.txt" },
    { .arch = "i686",
      .model_name = "Intel(R) Pentium(TM) M 1.3 @ 1.3GHz",
      .intel_model = 0x0D,  // Dothan (probably)
      .filename = "/etc/perf_commands/i686/default.txt" },
    { .arch = "i686",
      .model_name = "Intel(R) Pentium(TM) M 705 @ 1.5GHz",
      .intel_model = 0x09,  // Banias
      .filename = "/etc/perf_commands/i686/default.txt" },

    // ARMv7
    { .arch = "armv7l",
      .model_name = "ARMv7 Processor rev 3 (v7l)",
      .intel_model = 0,
      .filename = "/etc/perf_commands/armv7l/default.txt" },
    { .arch = "armv7l",
      .model_name = "ARMv7 Processor rev 4 (v7l)",
      .intel_model = 0,
      .filename = "/etc/perf_commands/armv7l/default.txt" },

    // Misc.
    { .arch = "MIPS",
      .model_name = "blah",
      .intel_model = 0,
      .filename = "/etc/perf_commands/unknown.txt" },
    { .arch = "AVR",
      .model_name = "blah",
      .intel_model = 0,
      .filename = "/etc/perf_commands/unknown.txt" },
    { .arch = "MC68000",
      .model_name = "blah",
      .intel_model = 0,
      .filename = "/etc/perf_commands/unknown.txt" },
  };

  for (const auto& test_case : kOddsFileTestCases) {
    // Instantiate a PerfTool with custom inputs.
    // |random_selector| will be deleted by PerfTool's destructor.
    MockRandomSelector* random_selector = new MockRandomSelector;
    std::unique_ptr<MockRandomSelector> random_selector_ptr(random_selector);
    PerfTool perf_tool(
        MakeMockCPUInfo(test_case.model_name, test_case.intel_model),
        random_selector_ptr.release(),
        MakeMockUnameFunc(test_case.arch));

    // Check if PerfTool has passed the correct path to our MockRandomSelector.
    EXPECT_EQ(test_case.filename, random_selector->filename());
  }
}

}  // namespace debugd
