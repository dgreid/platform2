// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/proto_file_io.h>

#include "ml_benchmark/shared_library_benchmark.h"
#include "ml_benchmark/shared_library_benchmark_functions.h"
#include "proto/benchmark_config.pb.h"

using chrome::ml_benchmark::AccelerationMode;
using chrome::ml_benchmark::CrOSBenchmarkConfig;
using chrome::ml_benchmark::SodaBenchmarkConfig;
using chrome::ml_benchmark::BenchmarkResults;
using ml_benchmark::SharedLibraryBenchmark;
using ml_benchmark::SharedLibraryBenchmarkFunctions;

namespace {

void benchmark_and_report_results(const std::string& driver_name,
                                  const base::FilePath& driver_file_path,
                                  const CrOSBenchmarkConfig& config) {
  auto functions = std::make_unique<SharedLibraryBenchmarkFunctions>(
      driver_file_path);
  if (functions == nullptr || !functions->valid()) {
    LOG(ERROR) << "Unable to load the " << driver_name << " benchmark";
    return;
  }

  LOG(INFO) << "Starting the " << driver_name << " benchmark";
  SharedLibraryBenchmark benchmark(std::move(functions));
  BenchmarkResults results;
  if (!benchmark.ExecuteBenchmark(config, &results)) {
    LOG(ERROR) << "Unable to execute the " << driver_name << " benchmark";
  }

  if (results.status() == chrome::ml_benchmark::OK) {
    LOG(INFO) << driver_name << " finished";
    LOG(INFO) << results.results_message();
    LOG(INFO) << "Accuracy: " << results.total_accuracy();
    LOG(INFO) << "Average Latency: " << results.average_latency_in_us()
              << " usec";
  } else {
    LOG(ERROR) << driver_name << " Encountered an error";
    LOG(ERROR) << "Reason: " << results.results_message();
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  DEFINE_string(workspace_path, ".", "Path to the driver workspace.");
  DEFINE_string(config_file_name, "benchmark.config",
      "Name of the driver configuration file.");
  DEFINE_string(driver_library_path, "libsoda_benchmark_driver.so",
      "Path to the driver shared library.");
  DEFINE_bool(use_nnapi, false, "Use NNAPI delegate.");

  brillo::FlagHelper::Init(argc, argv, "ML Benchmark runner");

  base::FilePath workspace_config_path =
      base::FilePath(FLAGS_workspace_path).Append(FLAGS_config_file_name);

  CrOSBenchmarkConfig benchmark_config;

  if (FLAGS_use_nnapi) {
    benchmark_config.set_acceleration_mode(AccelerationMode::NNAPI);
  }

  CHECK(base::ReadFileToString(workspace_config_path,
                               benchmark_config.mutable_driver_config()))
      << "Could not read the benchmark config file: " << workspace_config_path;

  base::FilePath driver_library(FLAGS_driver_library_path);

  benchmark_and_report_results(FLAGS_driver_library_path,
                               driver_library,
                               benchmark_config);

  LOG(INFO) << "Benchmark finished, exiting";
}
