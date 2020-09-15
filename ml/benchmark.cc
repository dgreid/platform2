// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/benchmark.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/containers/flat_map.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/run_loop.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/message_loops/base_message_loop.h>
#include <base/message_loop/message_loop_current.h>
#include <google/protobuf/text_format.h>
#include <mojo/core/core.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "ml/benchmark.pb.h"
#include "ml/model_impl.h"
#include "ml/mojom/graph_executor.mojom.h"
#include "ml/mojom/machine_learning_service.mojom.h"
#include "ml/mojom/model.mojom.h"
#include "ml/test_utils.h"
#include "proto/benchmark_config.pb.h"

using ::chrome::ml_benchmark::BenchmarkResults;
using ::chrome::ml_benchmark::BenchmarkReturnStatus;
using ::chrome::ml_benchmark::CrOSBenchmarkConfig;
using ::chromeos::machine_learning::mojom::CreateGraphExecutorResult;
using ::chromeos::machine_learning::mojom::ExecuteResult;
using ::chromeos::machine_learning::mojom::GraphExecutor;
using ::chromeos::machine_learning::mojom::LoadModelResult;
using ::chromeos::machine_learning::mojom::Model;
using ::chromeos::machine_learning::mojom::TensorPtr;
using ::google::protobuf::TextFormat;

using Example = ml::ExpectedInputOutput::Example;
using Feature = ml::ExpectedInputOutput::Example::Feature;

namespace ml {
namespace {

// Percentiles for latency.
constexpr int kLatencyPercentile[] = {50, 90, 95, 99};
// Use a fake model name for benchmark runs.
constexpr char kMlBenchmarkMetricsName[] = "benchmark_model";

// The accumulative result of an inference test run.
struct AccumulativeResult {
  // Set to true if any inference fails.
  bool has_failure = false;
  // Total error for all inference.
  float total_error = 0.0;
  // Time of each run.
  std::vector<int64_t> times_in_us;
};

// Serialize `results` into results_data and returns results.status().
int32_t SerializeResults(const BenchmarkResults& results,
                         void** results_data,
                         int32_t* results_size) {
  if (results.status() != BenchmarkReturnStatus::OK) {
    LOG(ERROR) << "result with error: " << results.DebugString();
  }
  const std::string result_pb = results.SerializeAsString();
  CHECK(!result_pb.empty());
  const int size = result_pb.size();
  // Will be released by the caller.
  char* const data = new char[size];
  result_pb.copy(data, size);
  *results_data = data;
  *results_size = size;
  return results.status();
}

void InitializeOnce() {
  if (!base::MessageLoopCurrent::IsSet()) {
    (new brillo::BaseMessageLoop())->SetAsCurrent();
  }
  if (!mojo::core::Core::Get()) {
    mojo::core::Init();
  }
}

// Constructs `model` based on tflite_config; returns whether the construction
// is successful.
bool ConstructModel(const FlatBufferModelSpecProto& model_proto,
                    mojo::Remote<Model>* const model) {
  auto model_string_impl =
      std::make_unique<std::string>(model_proto.model_string());

  // Step 1 builds the FlatBufferModel.
  std::unique_ptr<tflite::FlatBufferModel> flat_buffer_model =
      tflite::FlatBufferModel::BuildFromBuffer(model_string_impl->c_str(),
                                               model_string_impl->length());

  if (flat_buffer_model == nullptr) {
    return false;
  }

  // Step 2 constructs the ModelImpl.
  ModelImpl::Create(
      std::map<std::string, int>(model_proto.required_inputs().begin(),
                                 model_proto.required_inputs().end()),
      std::map<std::string, int>(model_proto.required_outputs().begin(),
                                 model_proto.required_outputs().end()),
      std::move(flat_buffer_model), std::move(model_string_impl),
      model->BindNewPipeAndPassReceiver(), kMlBenchmarkMetricsName);

  return true;
}

// Constructs `graph_executor`; returns whether the construction is successful.
bool ConstructGraphExecutor(const mojo::Remote<Model>& model,
                            mojo::Remote<GraphExecutor>* const graph_executor) {
  bool succeeded = false;
  model->CreateGraphExecutor(
      graph_executor->BindNewPipeAndPassReceiver(),
      base::Bind(
          [](bool* succeeded, const CreateGraphExecutorResult result) {
            *succeeded = result == CreateGraphExecutorResult::OK;
          },
          &succeeded));
  // Block until CreateGraphExecutor finishes.
  base::RunLoop().RunUntilIdle();
  return succeeded;
}

// Converts ExpectedInputOutput::Example into tensor map.
base::flat_map<std::string, TensorPtr> TensorMapFromInput(
    const Example& input) {
  base::flat_map<std::string, TensorPtr> input_map;

  // Loop over each feature.
  for (const auto& pair : input.features().feature()) {
    switch (pair.second.kind_case()) {
      case Feature::kFloatList: {
        // For FloatList, make a (1, n) tensor with the value.
        const auto& float_values = pair.second.float_list().value();
        if (float_values.size() == 1) {
          input_map[pair.first] = NewTensor<double>({1}, {float_values.at(0)});
        } else {
          input_map[pair.first] = NewTensor<double>(
              {1, float_values.size()},
              std::vector<double>(float_values.begin(), float_values.end()));
        }
      } break;
      case Feature::kInt64List: {
        // For Int64List, make a (1, n) tensor with the value.
        const auto& int_values = pair.second.int64_list().value();
        if (int_values.size() == 1) {
          input_map[pair.first] = NewTensor<int64_t>({1}, {int_values.at(0)});
        } else {
          input_map[pair.first] = NewTensor<int64_t>(
              {1, int_values.size()},
              std::vector<int64_t>(int_values.begin(), int_values.end()));
        }
      } break;
      default:
        LOG(ERROR) << "InputType not supported.";
        NOTREACHED();
        break;
    }
  }
  return input_map;
}

// Converts the `accumulative_result` into BenchmarkResults.
BenchmarkResults ToBenchmarkResults(AccumulativeResult* accumulative_result) {
  BenchmarkResults benchmark_result;
  benchmark_result.set_status(BenchmarkReturnStatus::OK);
  // Sets average accuracy.
  benchmark_result.set_total_accuracy(accumulative_result->total_error /
                                      accumulative_result->times_in_us.size());

  // Sorts all times_in_us for all the successful runs.
  std::sort(accumulative_result->times_in_us.begin(),
            accumulative_result->times_in_us.end());

  // Gets percentile for times_in_us.
  for (const int i : kLatencyPercentile) {
    const int pos = i * accumulative_result->times_in_us.size() / 100;
    CHECK(pos < accumulative_result->times_in_us.size())
        << "percentile can't be 100";
    (*benchmark_result.mutable_percentile_latencies_in_us())[i] =
        accumulative_result->times_in_us[pos];
  }

  return benchmark_result;
}

BenchmarkResults InferenceForTfliteModel(
    const TfliteBenchmarkConfig& tflite_config,
    const FlatBufferModelSpecProto& model_proto,
    const ExpectedInputOutput& input_output) {
  // Initialization for the first time.
  InitializeOnce();

  BenchmarkResults benchmark_result;

  // Step 1: construct the model.
  mojo::Remote<Model> model;
  if (!ConstructModel(model_proto, &model)) {
    benchmark_result.set_status(BenchmarkReturnStatus::INITIALIZATION_FAILED);
    benchmark_result.set_results_message(
        "Can't construct the Model from the model file.");
    return benchmark_result;
  }

  // Step 2: construct the graph executor.
  mojo::Remote<GraphExecutor> graph_executor;
  if (!ConstructGraphExecutor(model, &graph_executor)) {
    benchmark_result.set_status(BenchmarkReturnStatus::INITIALIZATION_FAILED);
    benchmark_result.set_results_message(
        "Can't construct the GraphExecutor from the model.");
    return benchmark_result;
  }

  // Step 3: run inference multiple times.
  std::vector<std::string> output_name;
  for (const auto& pair : model_proto.required_outputs()) {
    output_name.push_back(pair.first);
  }

  AccumulativeResult accumulative_result;
  for (int i = 0; i < tflite_config.num_runs(); ++i) {
    // Starts the timer.
    const std::clock_t start_time = std::clock();
    // Run infernce.
    graph_executor->Execute(
        TensorMapFromInput(input_output.input()), output_name,
        base::Bind(
            [](AccumulativeResult* accumulative_result,
               const std::vector<std::string>* const output_name,
               const Example* const example, ExecuteResult result,
               base::Optional<std::vector<TensorPtr>> outputs) {
              // Check that the inference run successfully.
              if (result != ExecuteResult::OK || !outputs.has_value() ||
                  outputs->size() != 1) {
                accumulative_result->has_failure = true;
                return;
              }

              // Check that the output tensor has the right type and format.
              for (int i = 0; i < output_name->size(); ++i) {
                const auto& expected_values = example->features()
                                                  .feature()
                                                  .at(output_name->at(i))
                                                  .float_list()
                                                  .value();
                const TensorView<double> out_tensor((*outputs)[i]);
                if (!out_tensor.IsValidType() || !out_tensor.IsValidFormat() ||
                    (out_tensor.GetShape() !=
                         std::vector<int64_t>({1, expected_values.size()}) &&
                     out_tensor.GetShape() !=
                         std::vector<int64_t>({expected_values.size()}))) {
                  accumulative_result->has_failure = true;
                  return;
                }
                for (int j = 0; j < expected_values.size(); ++j) {
                  // Only record metrics when the inference run successfully.
                  accumulative_result->total_error += std::abs(
                      out_tensor.GetValues()[j] - expected_values.at(j));
                }
              }
            },
            &accumulative_result, &output_name,
            &input_output.expected_output()));
    base::RunLoop().RunUntilIdle();

    // Inference should always succeed; return error otherwise.
    if (accumulative_result.has_failure) {
      benchmark_result.set_status(BenchmarkReturnStatus::RUNTIME_ERROR);
      benchmark_result.set_results_message("Inference error.");
      return benchmark_result;
    }

    // Records time.
    const int64_t cpu_time_us = static_cast<int64_t>(
        (std::clock() - start_time) * 1000000.0 / CLOCKS_PER_SEC);
    accumulative_result.times_in_us.push_back(cpu_time_us);
  }

  // Converts accumulative_result into BenchmarkResults.
  return ToBenchmarkResults(&accumulative_result);
}

}  // namespace
}  // namespace ml

int32_t benchmark_start(const void* config_bytes,
                        int32_t config_bytes_size,
                        void** results_bytes,
                        int32_t* results_bytes_size) {
  CHECK(config_bytes);
  CHECK(results_bytes);
  CHECK(results_bytes_size);

  BenchmarkResults result;

  // Step 1 De-serialize the CrOSBenchmarkConfig.
  CrOSBenchmarkConfig benchmark_config;
  if (!benchmark_config.ParseFromArray(config_bytes, config_bytes_size)) {
    result.set_status(BenchmarkReturnStatus::INCORRECT_CONFIGURATION);
    result.set_results_message("Can't parse CrOSBenchmarkConfig.");
    return ml::SerializeResults(result, results_bytes, results_bytes_size);
  }

  // Step 2 Parse the TfliteBenchmarkConfig
  ml::TfliteBenchmarkConfig tflite_config;
  if (!TextFormat::ParseFromString(benchmark_config.driver_config(),
                                   &tflite_config)) {
    result.set_status(BenchmarkReturnStatus::INCORRECT_CONFIGURATION);
    result.set_results_message("Can't parse TfliteBenchmarkConfig.");
    return ml::SerializeResults(result, results_bytes, results_bytes_size);
  }

  // Step 3 Parse the FlatBufferModelSpecProto.
  ml::FlatBufferModelSpecProto model_proto;
  std::string model_buf;
  if (!base::ReadFileToString(
          base::FilePath(tflite_config.tflite_model_filepath()), &model_buf)) {
    result.set_status(BenchmarkReturnStatus::INITIALIZATION_FAILED);
    result.set_results_message(tflite_config.tflite_model_filepath() +
                               " can't be read.");
    return ml::SerializeResults(result, results_bytes, results_bytes_size);
  }
  if (!model_proto.ParseFromString(model_buf)) {
    result.set_status(BenchmarkReturnStatus::INITIALIZATION_FAILED);
    result.set_results_message("Can't parse FlatBufferModelSpecProto");
    return ml::SerializeResults(result, results_bytes, results_bytes_size);
  }

  // Step 4 Parse the ExpectedInputOutput.
  ml::ExpectedInputOutput input_output;
  std::string input_buf;
  if (!base::ReadFileToString(
          base::FilePath(tflite_config.input_output_filepath()), &input_buf)) {
    result.set_status(BenchmarkReturnStatus::INITIALIZATION_FAILED);
    result.set_results_message(tflite_config.input_output_filepath() +
                               " can't be read.");
    return ml::SerializeResults(result, results_bytes, results_bytes_size);
  }
  if (!input_output.ParseFromString(input_buf)) {
    result.set_status(BenchmarkReturnStatus::INITIALIZATION_FAILED);
    result.set_results_message("Can't parse ExpectedInputOutput");
    return ml::SerializeResults(result, results_bytes, results_bytes_size);
  }

  // Step 5 runs InferenceForTfliteModel with the tflite_config,
  result =
      ml::InferenceForTfliteModel(tflite_config, model_proto, input_output);
  return ml::SerializeResults(result, results_bytes, results_bytes_size);
}

void free_benchmark_results(void* results_bytes) {
  delete[] static_cast<char*>(results_bytes);
}
