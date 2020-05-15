// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/command_line.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/native_library.h>
#include <base/scoped_native_library.h>
#include <brillo/proto_file_io.h>

#include "proto/benchmark_config.pb.h"

using BenchmarkConfig = chrome::ml_benchmark::CrOSBenchmarkConfig;
using BenchmarkResults = chrome::ml_benchmark::BenchmarkResults;
typedef void (*benchmark_fn)(const BenchmarkConfig* config,
                             BenchmarkResults* results);

namespace {

constexpr char kWorkspacePath[] = "workspace_path";
constexpr char kConfigFilePath[] = "benchmark.config";
constexpr char kBenchmarkFunctionName[] = "benchmark_start";

// TODO(franklinh) - Drive this using the config file instead
constexpr char kSodaDriverName[] = "SoDA";
constexpr char kSodaDriverPath[] = "libsoda_benchmark_driver.so";

void benchmark_and_report_results(const std::string& driver_name,
                                  const base::FilePath& driver_file_path,
                                  const BenchmarkConfig& config) {
  base::NativeLibrary test_driver_library =
      base::LoadNativeLibrary(driver_file_path, nullptr);

  if (test_driver_library == nullptr) {
    LOG(ERROR) << "Failed to load " << driver_name << " driver\n";
    return;
  }

  base::ScopedNativeLibrary test_driver(test_driver_library);

  auto benchmark_function = reinterpret_cast<benchmark_fn>(
      test_driver.GetFunctionPointer(kBenchmarkFunctionName));

  if (benchmark_function == nullptr) {
    LOG(ERROR) << "Unable to load the benchmark function from the "
               << driver_name << " driver\n";
    return;
  }

  LOG(INFO) << "Starting the " << driver_name << " benchmark\n";
  BenchmarkResults results;
  benchmark_function(&config, &results);

  if (results.status() == chrome::ml_benchmark::OK) {
    LOG(INFO) << driver_name << " finished\n";
    LOG(INFO) << results.results_message();
  } else {
    LOG(ERROR) << driver_name << " failed execution\n";
    LOG(ERROR) << "Reason: " << results.results_message();
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);

  auto* args = base::CommandLine::ForCurrentProcess();

  base::FilePath workspace_path(".");
  if (args->HasSwitch(kWorkspacePath)) {
    workspace_path = args->GetSwitchValuePath(kWorkspacePath);
  }

  base::FilePath workspace_config_path = workspace_path.Append(kConfigFilePath);

  BenchmarkConfig benchmark_config;

  CHECK(brillo::ReadTextProtobuf(workspace_config_path, &benchmark_config))
      << "Could not read the benchmark config file";

  // Execute benchmarks
  if (benchmark_config.has_soda_config()) {
    base::FilePath soda_path(kSodaDriverPath);

    benchmark_and_report_results(kSodaDriverName, soda_path, benchmark_config);
  }
}
