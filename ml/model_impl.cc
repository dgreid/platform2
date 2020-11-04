// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/model_impl.h"

#include <algorithm>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <tensorflow/lite/context.h>
#include <tensorflow/lite/delegates/nnapi/nnapi_delegate.h>
#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/kernels/register.h>

#include "ml/machine_learning_service_impl.h"
#include "ml/request_metrics.h"

namespace {

// Callback for self-owned ModelImpl's to delete themselves upon disconnection.
void DeleteModelImpl(const ml::ModelImpl* const model_impl) {
  delete model_impl;
}

}  // namespace

namespace ml {

using ::chromeos::machine_learning::mojom::CreateGraphExecutorResult;
using ::chromeos::machine_learning::mojom::GraphExecutor;
using ::chromeos::machine_learning::mojom::GraphExecutorOptions;
using ::chromeos::machine_learning::mojom::GraphExecutorOptionsPtr;
using ::chromeos::machine_learning::mojom::Model;

// Base name for UMA metrics related to CreateGraphExecutor calls
constexpr char kMetricsRequestName[] = "CreateGraphExecutorResult";

AlignedModelData::AlignedModelData(std::string model_str) {
  if (reinterpret_cast<std::uintptr_t>(model_str.c_str()) % 4 == 0) {
    // `model_str` is aligned. Keep it.
    original_model_str_ = std::make_unique<std::string>(std::move(model_str));
    aligned_copy_ = nullptr;
    aligned_copy_size_ = 0;
  } else {
    // `model_str` is unaligned. Discard it and make an aligned copy.
    aligned_copy_.reset(new char[model_str.size()]);
    std::copy(model_str.begin(), model_str.end(), aligned_copy_.get());
    aligned_copy_size_ = model_str.size();
  }
}

const char* AlignedModelData::data() const {
  return aligned_copy_ ? aligned_copy_.get() : original_model_str_->c_str();
}

size_t AlignedModelData::size() const {
  return aligned_copy_ ? aligned_copy_size_ : original_model_str_->size();
}

AlignedModelData::~AlignedModelData() = default;

ModelImpl* ModelImpl::Create(std::map<std::string, int> required_inputs,
                             std::map<std::string, int> required_outputs,
                             std::unique_ptr<tflite::FlatBufferModel> model,
                             std::unique_ptr<AlignedModelData> model_data,
                             mojo::PendingReceiver<Model> receiver,
                             const std::string& metrics_model_name) {
  auto model_impl = new ModelImpl(
      std::move(required_inputs), std::move(required_outputs), std::move(model),
      std::move(model_data), std::move(receiver), metrics_model_name);
  // Use a disconnection handler to strongly bind `model_impl` to `receiver`.
  model_impl->set_disconnect_handler(
      base::Bind(&DeleteModelImpl, base::Unretained(model_impl)));

  return model_impl;
}

ModelImpl* ModelImpl::Create(std::map<std::string, int> required_inputs,
                             std::map<std::string, int> required_outputs,
                             std::unique_ptr<tflite::FlatBufferModel> model,
                             mojo::PendingReceiver<Model> receiver,
                             const std::string& metrics_model_name) {
  auto model_impl = new ModelImpl(
      std::move(required_inputs), std::move(required_outputs), std::move(model),
      nullptr, std::move(receiver), metrics_model_name);
  // Use a disconnection handler to strongly bind `model_impl` to `receiver`.
  model_impl->set_disconnect_handler(
      base::Bind(&DeleteModelImpl, base::Unretained(model_impl)));

  return model_impl;
}

ModelImpl::ModelImpl(std::map<std::string, int> required_inputs,
                     std::map<std::string, int> required_outputs,
                     std::unique_ptr<tflite::FlatBufferModel> model,
                     std::unique_ptr<AlignedModelData> model_data,
                     mojo::PendingReceiver<Model> receiver,
                     const std::string& metrics_model_name)
    : required_inputs_(std::move(required_inputs)),
      required_outputs_(std::move(required_outputs)),
      model_data_(std::move(model_data)),
      model_(std::move(model)),
      receiver_(this, std::move(receiver)),
      metrics_model_name_(metrics_model_name) {}

void ModelImpl::set_disconnect_handler(base::Closure disconnect_handler) {
  receiver_.set_disconnect_handler(std::move(disconnect_handler));
}

int ModelImpl::num_graph_executors_for_testing() const {
  return graph_executors_.size();
}

void ModelImpl::CreateGraphExecutor(
    mojo::PendingReceiver<GraphExecutor> receiver,
    CreateGraphExecutorCallback callback) {
  auto options = GraphExecutorOptions::New(/*use_nnapi=*/false);
  CreateGraphExecutorWithOptions(std::move(options), std::move(receiver),
                                 std::move(callback));
}

void ModelImpl::CreateGraphExecutorWithOptions(
    GraphExecutorOptionsPtr options,
    mojo::PendingReceiver<GraphExecutor> receiver,
    CreateGraphExecutorCallback callback) {
  DCHECK(!metrics_model_name_.empty());

  RequestMetrics request_metrics(metrics_model_name_, kMetricsRequestName);
  request_metrics.StartRecordingPerformanceMetrics();

  if (model_ == nullptr) {
    LOG(ERROR) << "Null model provided.";
    std::move(callback).Run(
        CreateGraphExecutorResult::MODEL_INTERPRETATION_ERROR);
    request_metrics.RecordRequestEvent(
        CreateGraphExecutorResult::MODEL_INTERPRETATION_ERROR);
    return;
  }

  // Instantiate interpreter.
  tflite::ops::builtin::BuiltinOpResolver resolver;
  std::unique_ptr<tflite::Interpreter> interpreter;
  const TfLiteStatus resolve_status =
      tflite::InterpreterBuilder(*model_, resolver)(&interpreter);
  if (resolve_status != kTfLiteOk || !interpreter) {
    LOG(ERROR) << "Could not resolve model ops.";
    std::move(callback).Run(
        CreateGraphExecutorResult::MODEL_INTERPRETATION_ERROR);
    request_metrics.RecordRequestEvent(
        CreateGraphExecutorResult::MODEL_INTERPRETATION_ERROR);
    return;
  }

  // If requested, load and apply NNAPI
  if (options->use_nnapi) {
    TfLiteDelegate* delegate = tflite::NnApiDelegate();
    if (!delegate) {
      LOG(ERROR) << "NNAPI requested but not available.";
      std::move(callback).Run(CreateGraphExecutorResult::NNAPI_UNAVAILABLE);
      request_metrics.RecordRequestEvent(
          CreateGraphExecutorResult::NNAPI_UNAVAILABLE);
      return;
    }
    if (interpreter->ModifyGraphWithDelegate(delegate) != kTfLiteOk) {
      LOG(ERROR) << "Could not use NNAPI delegate.";
      std::move(callback).Run(CreateGraphExecutorResult::NNAPI_USE_ERROR);
      request_metrics.RecordRequestEvent(
          CreateGraphExecutorResult::NNAPI_USE_ERROR);
      return;
    }
  }

  // Allocate memory for tensors.
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    std::move(callback).Run(CreateGraphExecutorResult::MEMORY_ALLOCATION_ERROR);
    request_metrics.RecordRequestEvent(
        CreateGraphExecutorResult::MEMORY_ALLOCATION_ERROR);
    return;
  }

  // Add graph executor and schedule its deletion on pipe closure.
  graph_executors_.emplace_front(required_inputs_, required_outputs_,
                                 std::move(interpreter), std::move(receiver),
                                 metrics_model_name_);
  graph_executors_.front().set_disconnect_handler(
      base::Bind(&ModelImpl::EraseGraphExecutor, base::Unretained(this),
                 graph_executors_.begin()));

  std::move(callback).Run(CreateGraphExecutorResult::OK);
  request_metrics.FinishRecordingPerformanceMetrics();
  request_metrics.RecordRequestEvent(CreateGraphExecutorResult::OK);
}

void ModelImpl::EraseGraphExecutor(
    const std::list<GraphExecutorImpl>::const_iterator it) {
  graph_executors_.erase(it);
}

}  // namespace ml
