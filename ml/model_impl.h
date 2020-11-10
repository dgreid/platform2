// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_MODEL_IMPL_H_
#define ML_MODEL_IMPL_H_

#include <list>
#include <map>
#include <memory>
#include <string>

#include <base/macros.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <tensorflow/lite/model.h>

#include "ml/graph_executor_impl.h"
#include "ml/mojom/model.mojom.h"

namespace ml {

// Holds 4-byte aligned char[] data suitable for a flatbuffer model.
class AlignedModelData {
 public:
  // Constructs from a std::string. If its .c_str() is not 4-byte aligned, an
  // aligned copy is made.
  explicit AlignedModelData(std::string model_str);

  ~AlignedModelData();

  AlignedModelData(const AlignedModelData&) = delete;
  AlignedModelData& operator=(const AlignedModelData&) = delete;

  // The start of the model data. The result will be 4-byte aligned.
  const char* data() const;
  // The length of the buffer starting at `data()`.
  size_t size() const;

 private:
  // Original std::string containing model data. May be empty.
  std::unique_ptr<std::string> original_model_str_;
  // Aligned copy of the original std::string. May be empty.
  std::unique_ptr<char[]> aligned_copy_;
  size_t aligned_copy_size_;
};

// Holds a TensorFlow lite graph and produces GraphExecutors that may run the
// graph.
//
// All GraphExecutors created by a ModelImpl reference its model definition (and
// hence may not outlive the ModelImpl). Multiple such GraphExecutors may be
// used concurrently from different sequences.
class ModelImpl : public chromeos::machine_learning::mojom::Model {
 public:
  // Creates an instance bound to `receiver`.
  //
  // The `required_inputs` and `required_outputs` arguments specify a mapping
  // from required input / output tensor names to their indices in the TF lite
  // graph, and must outlive this object.
  // `model_data` is backing data for `model` which this class will take
  // ownership of. It will be destroyed *after* `model`.
  //
  // The RAM of the returned model is not owned by the caller. The model object
  // will be deleted when the corresponding mojo connection is closed.
  static ModelImpl* Create(
      std::map<std::string, int> required_inputs,
      std::map<std::string, int> required_outputs,
      std::unique_ptr<tflite::FlatBufferModel> model,
      std::unique_ptr<AlignedModelData> model_data,
      mojo::PendingReceiver<chromeos::machine_learning::mojom::Model> receiver,
      const std::string& metrics_model_name);

  // Use when constructed from file where no need to pass the `model_string`.
  // The RAM of the returned model is not owned by the caller. The model object
  // will be deleted when the corresponding mojo connection is closed.
  static ModelImpl* Create(
      std::map<std::string, int> required_inputs,
      std::map<std::string, int> required_outputs,
      std::unique_ptr<tflite::FlatBufferModel> model,
      mojo::PendingReceiver<chromeos::machine_learning::mojom::Model> receiver,
      const std::string& metrics_model_name);

  int num_graph_executors_for_testing() const;

 private:
  // Constructor is private, call `Create` to create objects.
  ModelImpl(
      std::map<std::string, int> required_inputs,
      std::map<std::string, int> required_outputs,
      std::unique_ptr<tflite::FlatBufferModel> model,
      std::unique_ptr<AlignedModelData> model_data,
      mojo::PendingReceiver<chromeos::machine_learning::mojom::Model> receiver,
      const std::string& metrics_model_name);
  ModelImpl(const ModelImpl&) = delete;
  ModelImpl& operator=(const ModelImpl&) = delete;

  void set_disconnect_handler(base::Closure disconnect_handler);

  // chromeos::machine_learning::mojom::Model:
  void CreateGraphExecutor(
      mojo::PendingReceiver<chromeos::machine_learning::mojom::GraphExecutor>
          receiver,
      CreateGraphExecutorCallback callback) override;
  void CreateGraphExecutorWithOptions(
      chromeos::machine_learning::mojom::GraphExecutorOptionsPtr options,
      mojo::PendingReceiver<chromeos::machine_learning::mojom::GraphExecutor>
          receiver,
      CreateGraphExecutorCallback callback) override;

  // Remove a graph executor from our hosted set.
  void EraseGraphExecutor(std::list<GraphExecutorImpl>::const_iterator it);

  const std::map<std::string, int> required_inputs_;
  const std::map<std::string, int> required_outputs_;

  // Must be above `model_`.
  const std::unique_ptr<AlignedModelData> model_data_;

  const std::unique_ptr<tflite::FlatBufferModel> model_;

  mojo::Receiver<chromeos::machine_learning::mojom::Model> receiver_;

  // Emulate a strongly bound receiver set: hold a set of GraphExecutors,
  // specific elements of which are erased on connection closure.
  //
  // That is, when a pipe to a GraphExecutorImpl closes, that object is removed
  // from this set (by its binding disconnection handler). Further, when a
  // ModelImpl is destroyed, its entire collection of GraphExecutorImpls is also
  // destroyed.
  std::list<GraphExecutorImpl> graph_executors_;

  // Model name as it should appear in UMA histogram names.
  const std::string metrics_model_name_;
};

}  // namespace ml

#endif  // ML_MODEL_IMPL_H_
