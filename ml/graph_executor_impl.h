// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_GRAPH_EXECUTOR_IMPL_H_
#define ML_GRAPH_EXECUTOR_IMPL_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/callback_forward.h>
#include <base/containers/flat_map.h>
#include <base/macros.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <tensorflow/lite/model.h>

#include "ml/mojom/graph_executor.mojom.h"

namespace ml {

// Allows execution of TensorFlow lite graphs using input / output specified
// with Mojo types.
//
// Holds as little state as possible (with the remainder living in the parent
// Model object and shared between all sibling GraphExecutors). Hence, a
// GraphExecutor becomes invalid when its parent Model object is destroyed.
//
// A given GraphExecutorImpl may not be used concurrently from different
// sequences.
class GraphExecutorImpl
    : public chromeos::machine_learning::mojom::GraphExecutor {
 public:
  // Creates an instance bound to `receiver`.
  //
  // The `required_inputs` and `required_outputs` arguments specify a mapping
  // from required input / output tensor names to their indices in the TF lite
  // graph, and must outlive this object.
  //
  // UMA metrics will be logged with the specified `metrics_model_name`.
  //
  // As is standard, `interpreter` must outlive the model with which it was
  // constructed.
  GraphExecutorImpl(
      const std::map<std::string, int>& required_inputs,
      const std::map<std::string, int>& required_outputs,
      std::unique_ptr<tflite::Interpreter> interpreter,
      mojo::PendingReceiver<chromeos::machine_learning::mojom::GraphExecutor>
          receiver,
      const std::string& metrics_model_name);
  GraphExecutorImpl(const GraphExecutorImpl&) = delete;
  GraphExecutorImpl& operator=(const GraphExecutorImpl&) = delete;

  void set_disconnect_handler(base::Closure disconnect_handler);

 private:
  // chromeos::machine_learning::mojom::GraphExecutor:
  void Execute(
      base::flat_map<std::string, chromeos::machine_learning::mojom::TensorPtr>
          inputs,
      const std::vector<std::string>& output_names,
      ExecuteCallback callback);

  const std::map<std::string, int>& required_inputs_;
  const std::map<std::string, int>& required_outputs_;

  const std::unique_ptr<tflite::Interpreter> interpreter_;

  mojo::Receiver<chromeos::machine_learning::mojom::GraphExecutor> receiver_;

  // Model name as it should appear in UMA histogram names.
  const std::string metrics_model_name_;
};

}  // namespace ml

#endif  // ML_GRAPH_EXECUTOR_IMPL_H_
