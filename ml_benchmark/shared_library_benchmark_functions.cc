// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml_benchmark/shared_library_benchmark_functions.h"

#include <base/logging.h>

namespace {

constexpr char kBenchmarkFunctionName[] = "benchmark_start";
constexpr char kFreeBenchmarkFunctionName[] = "free_benchmark_results";

void* load_function_from_shared_lib(const base::ScopedNativeLibrary& library,
    const char* function_name,
    const char* library_path) {
  auto function_pointer = library.GetFunctionPointer(function_name);

  if (function_pointer == nullptr) {
    LOG(ERROR)  << "Unable to load " << function_name << " from "
                << library_path;
  }

  return function_pointer;
}

}  // namespace

namespace ml_benchmark {

SharedLibraryBenchmarkFunctions::SharedLibraryBenchmarkFunctions(
    const base::FilePath& path) {
  base::NativeLibrary library = base::LoadNativeLibrary(path, nullptr);

  if (library == nullptr) {
    LOG(ERROR) << "Failed to load driver from: " << path;
    return;
  }

  library_ = base::ScopedNativeLibrary(library);
  auto benchmark_function_pointer = reinterpret_cast<benchmark_function>(
        load_function_from_shared_lib(library_,
                                      kBenchmarkFunctionName,
                                      path.value().c_str()));

  if (benchmark_function_pointer == nullptr) {
    return;
  }

  auto free_results_function =
      reinterpret_cast<free_benchmark_results_function>(
        load_function_from_shared_lib(library_,
            kFreeBenchmarkFunctionName,
            path.value().c_str()));

  if (free_results_function == nullptr) {
    return;
  }

  benchmark_function_ = benchmark_function_pointer;
  free_benchmark_results_function_ = free_results_function;
  valid_ = true;
}

void SharedLibraryBenchmarkFunctions::FreeBenchmarkResults(
    void* results_bytes) {
  DCHECK(valid()) << "Attempted to call FreeBenchmarkResults without"
                     " loading from the shared library";

  free_benchmark_results_function_(results_bytes);
}

int32_t SharedLibraryBenchmarkFunctions::BenchmarkFunction(
    const void* config_bytes,
    int32_t config_bytes_size,
    void** results_bytes,
    int32_t* results_bytes_size) {
  DCHECK(valid()) << "Attempted to call BenchmarkFunction without"
                     " loading from the shared library";

  return benchmark_function_(config_bytes,
                             config_bytes_size,
                             results_bytes,
                             results_bytes_size);
}

}  // namespace ml_benchmark
