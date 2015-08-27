// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_PERF_TOOL_H_
#define DEBUGD_SRC_PERF_TOOL_H_

#include <stdint.h>
#include <sys/utsname.h>

#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>
#include <dbus-c++/dbus.h>

#include "debugd/src/cpu_info_parser.h"

namespace debugd {

class RandomSelector;

class PerfTool {
 public:
  // For injection of uname(2)
  typedef std::function<int(struct utsname*)> UnameFunc;

  PerfTool();
  // This is a special constructor for testing that takes in CPUInfoParser and
  // RandomSelector args and allows injection of uname(2). It takes ownership of
  // |random_selector|.
  PerfTool(const CPUInfoParser& cpu_info,
           RandomSelector* random_selector,
           UnameFunc uname_func);
  ~PerfTool() = default;

  // Runs the perf tool with the request command for |duration_secs| seconds
  // and returns either a perf_data or perf_stat protobuf in serialized form.
  int GetPerfOutput(const uint32_t& duration_secs,
                    const std::vector<std::string>& perf_args,
                    std::vector<uint8_t>* perf_data,
                    std::vector<uint8_t>* perf_stat,
                    DBus::Error* error);

  // Randomly runs the perf tool in various modes and collects various events
  // for |duration_secs| seconds and returns either a perf_data or perf_stat
  // protobuf in binary form.
  int GetRandomPerfOutput(const uint32_t& duration_secs,
                          std::vector<uint8_t>* perf_data,
                          std::vector<uint8_t>* perf_stat,
                          DBus::Error* error);

  // Randomly runs the perf tool in various modes and collects various events
  // for |duration_secs| seconds and returns a protobuf containing the collected
  // data.
  std::vector<uint8_t> GetRichPerfData(const uint32_t& duration_secs,
                                       DBus::Error* error);

 private:
  // Helper function that runs perf for a given |duration_secs| returning the
  // collected data in |data_string|. Return value is the status from running
  // perf.
  int GetPerfOutputHelper(const uint32_t& duration_secs,
                          const std::vector<std::string>& perf_args,
                          DBus::Error* error,
                          std::string* data_string);

  std::unique_ptr<RandomSelector> random_selector_;

  DISALLOW_COPY_AND_ASSIGN(PerfTool);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_PERF_TOOL_H_
