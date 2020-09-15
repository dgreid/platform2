// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <google/protobuf/text_format.h>
#include <gtest/gtest.h>

#include "ml/benchmark.h"
#include "ml/benchmark.pb.h"
#include "ml/mojom/model.mojom.h"
#include "proto/benchmark_config.pb.h"

namespace ml {

using ::chrome::ml_benchmark::BenchmarkResults;
using ::chrome::ml_benchmark::BenchmarkReturnStatus;
using ::chrome::ml_benchmark::CrOSBenchmarkConfig;
using ::google::protobuf::TextFormat;

// Test model
constexpr char kSmartDim20181115ModelFile[] =
    "/opt/google/chrome/ml_models/mlservice-model-test_add-20180914.tflite";

// Test input.
constexpr char kModelProtoText[] = R"(
  required_inputs: {
    key: "x"
    value: 1
  }
  required_inputs: {
    key: "y"
    value: 2
  }
  required_outputs: {
    key: "z"
    value: 0
  }
)";
constexpr char kInputOutputText[] = R"(
  input: {
    features: {
      feature: {
        key: "x"
        value: {
          float_list: { value:[ 0.5 ] }
        }
      }
      feature: {
        key: "y"
        value: {
          float_list: { value:[ 0.25 ] }
        }
      }
    }
  }
  expected_output:{
    features: {
      feature: {
        key: "z"
        value: {
          float_list: { value: [ 0.75 ] }
        }
      }
    }
  }
)";

class MlBenchmarkTest : public ::testing::Test {
 public:
  MlBenchmarkTest() {
    // Set benchmark_config_;
    CHECK(temp_dir_.CreateUniqueTempDir());
    const base::FilePath tflite_model_filepath =
        temp_dir_.GetPath().Append("model.pb");
    const base::FilePath input_output_filepath =
        temp_dir_.GetPath().Append("input_output.pb");
    TfliteBenchmarkConfig tflite_config;
    tflite_config.set_tflite_model_filepath(tflite_model_filepath.value());
    tflite_config.set_input_output_filepath(input_output_filepath.value());
    tflite_config.set_num_runs(100);
    TextFormat::PrintToString(tflite_config,
                              benchmark_config_.mutable_driver_config());

    // Set FlatBufferModelSpecProto;
    FlatBufferModelSpecProto model_proto;
    CHECK(TextFormat::ParseFromString(kModelProtoText, &model_proto));
    base::ReadFileToString(base::FilePath(kSmartDim20181115ModelFile),
                           model_proto.mutable_model_string());
    const std::string model_content = model_proto.SerializeAsString();
    base::WriteFile(tflite_model_filepath, model_content.data(),
                    model_content.size());

    // Set ExpectedInputOutput.
    ExpectedInputOutput input_output;
    CHECK(TextFormat::ParseFromString(kInputOutputText, &input_output));
    const std::string input_content = input_output.SerializeAsString();
    base::WriteFile(input_output_filepath, input_content.data(),
                    input_content.size());
  }

 protected:
  // Temporary directory containing a file used for the file mechanism.
  base::ScopedTempDir temp_dir_;

  CrOSBenchmarkConfig benchmark_config_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MlBenchmarkTest);
};

TEST_F(MlBenchmarkTest, TfliteModelTest) {
  // Step 1 run benchmark_start.
  const std::string config = benchmark_config_.SerializeAsString();
  void* results_data = nullptr;
  int results_size = 0;
  EXPECT_EQ(benchmark_start(config.c_str(), config.size(), &results_data,
                            &results_size),
            BenchmarkReturnStatus::OK);

  // Step 2 check results.
  BenchmarkResults results;
  CHECK(results.ParseFromArray(results_data, results_size));
  free_benchmark_results(results_data);
  EXPECT_EQ(results.status(), BenchmarkReturnStatus::OK);
  EXPECT_LT(results.total_accuracy(), 1e-5);
}

}  // namespace ml
