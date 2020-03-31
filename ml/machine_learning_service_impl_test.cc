// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/containers/flat_map.h>
#include <base/files/file_util.h>
#include <base/macros.h>
#include <base/run_loop.h>
#include <base/stl_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/binding.h>
#include <mojo/public/cpp/bindings/interface_request.h>

#include "ml/machine_learning_service_impl.h"
#include "ml/mojom/graph_executor.mojom.h"
#include "ml/mojom/machine_learning_service.mojom.h"
#include "ml/mojom/model.mojom.h"
#include "ml/tensor_view.h"
#include "ml/test_utils.h"

namespace ml {
namespace {

constexpr double kSearchRanker20190923TestInput[] = {
    0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1,
};

constexpr double kSmartDim20181115TestInput[] = {
    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

constexpr double kSmartDim20190221TestInput[] = {
    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

constexpr double kSmartDim20190521TestInput[] = {
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 1,
};

constexpr double kTopCat20190722TestInput[] = {
    1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0
};

using ::chromeos::machine_learning::mojom::BuiltinModelId;
using ::chromeos::machine_learning::mojom::BuiltinModelSpec;
using ::chromeos::machine_learning::mojom::BuiltinModelSpecPtr;
using ::chromeos::machine_learning::mojom::CreateGraphExecutorResult;
using ::chromeos::machine_learning::mojom::ExecuteResult;
using ::chromeos::machine_learning::mojom::FlatBufferModelSpec;
using ::chromeos::machine_learning::mojom::FlatBufferModelSpecPtr;
using ::chromeos::machine_learning::mojom::GraphExecutorPtr;
using ::chromeos::machine_learning::mojom::LoadModelResult;
using ::chromeos::machine_learning::mojom::MachineLearningServicePtr;
using ::chromeos::machine_learning::mojom::Model;
using ::chromeos::machine_learning::mojom::ModelPtr;
using ::chromeos::machine_learning::mojom::ModelRequest;
using ::chromeos::machine_learning::mojom::TensorPtr;
using ::testing::DoubleEq;
using ::testing::DoubleNear;
using ::testing::ElementsAre;

// A version of MachineLearningServiceImpl that loads from the testing model
// directory.
class MachineLearningServiceImplForTesting : public MachineLearningServiceImpl {
 public:
  // Pass a dummy callback and use the testing model directory.
  explicit MachineLearningServiceImplForTesting(
      mojo::ScopedMessagePipeHandle pipe)
      : MachineLearningServiceImpl(
            std::move(pipe), base::Closure(), GetTestModelDir()) {}
};

TEST(MachineLearningServiceImplTest, TestBadModel) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  // Set up model spec to specify an invalid model.
  BuiltinModelSpecPtr spec = BuiltinModelSpec::New();
  spec->id = BuiltinModelId::UNSUPPORTED_UNKNOWN;

  // Load model.
  ModelPtr model;
  bool model_callback_done = false;
  ml_service->LoadBuiltinModel(
      std::move(spec), mojo::MakeRequest(&model),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::MODEL_SPEC_ERROR);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
}

TEST(ModelLoadAndInferenceTest, TestModel) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  // Set up model spec.
  BuiltinModelSpecPtr spec = BuiltinModelSpec::New();
  spec->id = BuiltinModelId::TEST_MODEL;

  // Load model.
  ModelPtr model;
  bool model_callback_done = false;
  ml_service->LoadBuiltinModel(
      std::move(spec), mojo::MakeRequest(&model),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
  ASSERT_TRUE(model.is_bound());

  // Get graph executor.
  GraphExecutorPtr graph_executor;
  bool ge_callback_done = false;
  model->CreateGraphExecutor(
      mojo::MakeRequest(&graph_executor),
      base::Bind(
          [](bool* ge_callback_done, const CreateGraphExecutorResult result) {
            EXPECT_EQ(result, CreateGraphExecutorResult::OK);
            *ge_callback_done = true;
          },
          &ge_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ge_callback_done);
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace("x", NewTensor<double>({1}, {0.5}));
  inputs.emplace("y", NewTensor<double>({1}, {0.25}));
  std::vector<std::string> outputs({"z"});

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      base::Bind(
          [](bool* infer_callback_done, const ExecuteResult result,
             base::Optional<std::vector<TensorPtr>> outputs) {
            // Check that the inference succeeded and gives the expected number
            // of outputs.
            EXPECT_EQ(result, ExecuteResult::OK);
            ASSERT_TRUE(outputs.has_value());
            ASSERT_EQ(outputs->size(), 1);

            // Check that the output tensor has the right type and format.
            const TensorView<double> out_tensor((*outputs)[0]);
            EXPECT_TRUE(out_tensor.IsValidType());
            EXPECT_TRUE(out_tensor.IsValidFormat());

            // Check the output tensor has the expected shape and values.
            EXPECT_THAT(out_tensor.GetShape(), ElementsAre(1));
            EXPECT_THAT(out_tensor.GetValues(), ElementsAre(DoubleEq(0.75)));

            *infer_callback_done = true;
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Smart Dim (20181115) model file loads correctly and produces
// the expected inference result.
TEST(ModelLoadAndInferenceTest, SmartDim20181115) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  // Set up model spec.
  BuiltinModelSpecPtr spec = BuiltinModelSpec::New();
  spec->id = BuiltinModelId::SMART_DIM_20181115;

  // Load model.
  ModelPtr model;
  bool model_callback_done = false;
  ml_service->LoadBuiltinModel(
      std::move(spec), mojo::MakeRequest(&model),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
  ASSERT_TRUE(model.is_bound());

  // Get graph executor.
  GraphExecutorPtr graph_executor;
  bool ge_callback_done = false;
  model->CreateGraphExecutor(
      mojo::MakeRequest(&graph_executor),
      base::Bind(
          [](bool* ge_callback_done, const CreateGraphExecutorResult result) {
            EXPECT_EQ(result, CreateGraphExecutorResult::OK);
            *ge_callback_done = true;
          },
          &ge_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ge_callback_done);
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace(
      "input", NewTensor<double>(
                   {1, base::size(kSmartDim20181115TestInput)},
                   std::vector<double>(std::begin(kSmartDim20181115TestInput),
                                       std::end(kSmartDim20181115TestInput))));
  std::vector<std::string> outputs({"output"});

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      base::Bind(
          [](bool* infer_callback_done, const ExecuteResult result,
             base::Optional<std::vector<TensorPtr>> outputs) {
            // Check that the inference succeeded and gives the expected number
            // of outputs.
            EXPECT_EQ(result, ExecuteResult::OK);
            ASSERT_TRUE(outputs.has_value());
            ASSERT_EQ(outputs->size(), 1);

            // Check that the output tensor has the right type and format.
            const TensorView<double> out_tensor((*outputs)[0]);
            EXPECT_TRUE(out_tensor.IsValidType());
            EXPECT_TRUE(out_tensor.IsValidFormat());

            // Check the output tensor has the expected shape and values.
            EXPECT_THAT(out_tensor.GetShape(), ElementsAre(1, 1));
            EXPECT_THAT(out_tensor.GetValues(),
                        ElementsAre(DoubleNear(-3.36311, 0.1)));

            *infer_callback_done = true;
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Smart Dim (20190221) model file loads correctly and produces
// the expected inference result.
TEST(ModelLoadAndInferenceTest, SmartDim20190221) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  // Set up model spec.
  BuiltinModelSpecPtr spec = BuiltinModelSpec::New();
  spec->id = BuiltinModelId::SMART_DIM_20190221;

  // Load model.
  ModelPtr model;
  bool model_callback_done = false;
  ml_service->LoadBuiltinModel(
      std::move(spec), mojo::MakeRequest(&model),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
  ASSERT_TRUE(model.is_bound());

  // Get graph executor.
  GraphExecutorPtr graph_executor;
  bool ge_callback_done = false;
  model->CreateGraphExecutor(
      mojo::MakeRequest(&graph_executor),
      base::Bind(
          [](bool* ge_callback_done, const CreateGraphExecutorResult result) {
            EXPECT_EQ(result, CreateGraphExecutorResult::OK);
            *ge_callback_done = true;
          },
          &ge_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ge_callback_done);
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace(
      "input", NewTensor<double>(
                   {1, base::size(kSmartDim20190221TestInput)},
                   std::vector<double>(std::begin(kSmartDim20190221TestInput),
                                       std::end(kSmartDim20190221TestInput))));
  std::vector<std::string> outputs({"output"});

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      base::Bind(
          [](bool* infer_callback_done, const ExecuteResult result,
             base::Optional<std::vector<TensorPtr>> outputs) {
            // Check that the inference succeeded and gives the expected number
            // of outputs.
            EXPECT_EQ(result, ExecuteResult::OK);
            ASSERT_TRUE(outputs.has_value());
            ASSERT_EQ(outputs->size(), 1);

            // Check that the output tensor has the right type and format.
            const TensorView<double> out_tensor((*outputs)[0]);
            EXPECT_TRUE(out_tensor.IsValidType());
            EXPECT_TRUE(out_tensor.IsValidFormat());

            // Check the output tensor has the expected shape and values.
            EXPECT_THAT(out_tensor.GetShape(), ElementsAre(1, 1));
            EXPECT_THAT(out_tensor.GetValues(),
                        ElementsAre(DoubleNear(-0.900591, 0.1)));

            *infer_callback_done = true;
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Smart Dim (20190521) model file loads correctly and produces
// the expected inference result.
TEST(ModelLoadAndInferenceTest, SmartDim20190521) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  // Set up model spec.
  BuiltinModelSpecPtr spec = BuiltinModelSpec::New();
  spec->id = BuiltinModelId::SMART_DIM_20190521;

  // Load model.
  ModelPtr model;
  bool model_callback_done = false;
  ml_service->LoadBuiltinModel(
      std::move(spec), mojo::MakeRequest(&model),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
  ASSERT_TRUE(model.is_bound());

  // Get graph executor.
  GraphExecutorPtr graph_executor;
  bool ge_callback_done = false;
  model->CreateGraphExecutor(
      mojo::MakeRequest(&graph_executor),
      base::Bind(
          [](bool* ge_callback_done, const CreateGraphExecutorResult result) {
            EXPECT_EQ(result, CreateGraphExecutorResult::OK);
            *ge_callback_done = true;
          },
          &ge_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ge_callback_done);
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace(
      "input", NewTensor<double>(
                   {1, base::size(kSmartDim20190521TestInput)},
                   std::vector<double>(std::begin(kSmartDim20190521TestInput),
                                       std::end(kSmartDim20190521TestInput))));
  std::vector<std::string> outputs({"output"});

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      base::Bind(
          [](bool* infer_callback_done, const ExecuteResult result,
             base::Optional<std::vector<TensorPtr>> outputs) {
            // Check that the inference succeeded and gives the expected number
            // of outputs.
            EXPECT_EQ(result, ExecuteResult::OK);
            ASSERT_TRUE(outputs.has_value());
            ASSERT_EQ(outputs->size(), 1);

            // Check that the output tensor has the right type and format.
            const TensorView<double> out_tensor((*outputs)[0]);
            EXPECT_TRUE(out_tensor.IsValidType());
            EXPECT_TRUE(out_tensor.IsValidFormat());

            // Check the output tensor has the expected shape and values.
            EXPECT_THAT(out_tensor.GetShape(), ElementsAre(1, 1));
            EXPECT_THAT(out_tensor.GetValues(),
                        ElementsAre(DoubleNear(0.66962254, 0.1)));

            *infer_callback_done = true;
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Top Cat (20190722) model file loads correctly and produces
// the expected inference result.
TEST(ModelLoadAndInferenceTest, TopCat20190722) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  // Set up model spec.
  BuiltinModelSpecPtr spec = BuiltinModelSpec::New();
  spec->id = BuiltinModelId::TOP_CAT_20190722;

  // Load model.
  ModelPtr model;
  bool model_callback_done = false;
  ml_service->LoadBuiltinModel(
      std::move(spec), mojo::MakeRequest(&model),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
  ASSERT_TRUE(model.is_bound());

  // Get graph executor.
  GraphExecutorPtr graph_executor;
  bool ge_callback_done = false;
  model->CreateGraphExecutor(
      mojo::MakeRequest(&graph_executor),
      base::Bind(
          [](bool* ge_callback_done, const CreateGraphExecutorResult result) {
            EXPECT_EQ(result, CreateGraphExecutorResult::OK);
            *ge_callback_done = true;
          },
          &ge_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ge_callback_done);
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace("input",
                 NewTensor<double>(
                     {1, base::size(kTopCat20190722TestInput)},
                     std::vector<double>(std::begin(kTopCat20190722TestInput),
                                         std::end(kTopCat20190722TestInput))));
  std::vector<std::string> outputs({"output"});

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      base::Bind(
          [](bool* infer_callback_done, const ExecuteResult result,
             base::Optional<std::vector<TensorPtr>> outputs) {
            // Check that the inference succeeded and gives the expected number
            // of outputs.
            EXPECT_EQ(result, ExecuteResult::OK);
            ASSERT_TRUE(outputs.has_value());
            ASSERT_EQ(outputs->size(), 1);

            // Check that the output tensor has the right type and format.
            const TensorView<double> out_tensor((*outputs)[0]);
            EXPECT_TRUE(out_tensor.IsValidType());
            EXPECT_TRUE(out_tensor.IsValidFormat());

            // Check the output tensor has the expected shape and values.
            EXPECT_THAT(out_tensor.GetShape(), ElementsAre(1, 1));
            EXPECT_THAT(out_tensor.GetValues(),
                        ElementsAre(DoubleNear(-3.02972, 0.1)));

            *infer_callback_done = true;
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Search Ranker (20190923) model file loads correctly and
// produces the expected inference result.
TEST(ModelLoadAndInferenceTest, SearchRanker20190923) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  // Set up model spec.
  BuiltinModelSpecPtr spec = BuiltinModelSpec::New();
  spec->id = BuiltinModelId::SEARCH_RANKER_20190923;

  // Load model.
  ModelPtr model;
  bool model_callback_done = false;
  ml_service->LoadBuiltinModel(
      std::move(spec), mojo::MakeRequest(&model),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
  ASSERT_TRUE(model.is_bound());

  // Get graph executor.
  GraphExecutorPtr graph_executor;
  bool ge_callback_done = false;
  model->CreateGraphExecutor(
      mojo::MakeRequest(&graph_executor),
      base::Bind(
          [](bool* ge_callback_done, const CreateGraphExecutorResult result) {
            EXPECT_EQ(result, CreateGraphExecutorResult::OK);
            *ge_callback_done = true;
          },
          &ge_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ge_callback_done);
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace("input", NewTensor<double>(
                              {1, base::size(kSearchRanker20190923TestInput)},
                              std::vector<double>(
                                  std::begin(kSearchRanker20190923TestInput),
                                  std::end(kSearchRanker20190923TestInput))));
  std::vector<std::string> outputs({"output"});

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      base::Bind(
          [](bool* infer_callback_done, const ExecuteResult result,
             base::Optional<std::vector<TensorPtr>> outputs) {
            // Check that the inference succeeded and gives the expected number
            // of outputs.
            EXPECT_EQ(result, ExecuteResult::OK);
            ASSERT_TRUE(outputs.has_value());
            ASSERT_EQ(outputs->size(), 1);

            // Check that the output tensor has the right type and format.
            const TensorView<double> out_tensor((*outputs)[0]);
            EXPECT_TRUE(out_tensor.IsValidType());
            EXPECT_TRUE(out_tensor.IsValidFormat());

            // Check the output tensor has the expected shape and values.
            EXPECT_THAT(out_tensor.GetShape(), ElementsAre(1));
            EXPECT_THAT(out_tensor.GetValues(),
                        ElementsAre(DoubleNear(0.658488, 0.01)));

            *infer_callback_done = true;
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests loading an empty model througth the downloaded model api.
TEST(LoadModelStringTest, EmptyModelString) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  FlatBufferModelSpecPtr spec = FlatBufferModelSpec::New();
  spec->model_string = "";
  spec->inputs["x"] = 1;
  spec->inputs["y"] = 2;
  spec->outputs["z"] = 0;
  spec->metrics_model_name = "TestModel";

  // Load model from an empty model string.
  ModelPtr model;
  bool model_callback_done = false;
  ml_service->LoadFlatBufferModel(
      std::move(spec), mojo::MakeRequest(&model),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::LOAD_MODEL_ERROR);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
}

// Tests loading a bad model string througth the downloaded model api.
TEST(LoadModelStringTest, BadModelString) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  FlatBufferModelSpecPtr spec = FlatBufferModelSpec::New();
  spec->model_string = "bad model string";
  spec->inputs["x"] = 1;
  spec->inputs["y"] = 2;
  spec->outputs["z"] = 0;
  spec->metrics_model_name = "TestModel";

  // Load model from an empty model string.
  ModelPtr model;
  bool model_callback_done = false;
  ml_service->LoadFlatBufferModel(
      std::move(spec), mojo::MakeRequest(&model),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::LOAD_MODEL_ERROR);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
}

// Tests loading Smart Dim model througth the downloaded model api.
TEST(LoadModelStringAndInferenceTest, SmartDim20181115ModelString) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  // Load SmartDim model into string.
  std::string model_string;
  ASSERT_TRUE(base::ReadFileToString(
      base::FilePath(GetTestModelDir() +
                     "mlservice-model-smart_dim-20181115.tflite"),
      &model_string));

  FlatBufferModelSpecPtr spec = FlatBufferModelSpec::New();
  spec->model_string = std::move(model_string);
  spec->inputs["input"] = 3;
  spec->outputs["output"] = 4;
  spec->metrics_model_name = "SmartDimModel";

  // Load model.
  ModelPtr model;
  bool model_callback_done = false;
  ml_service->LoadFlatBufferModel(
      std::move(spec), mojo::MakeRequest(&model),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
  ASSERT_NE(model.get(), nullptr);

  // Get graph executor.
  GraphExecutorPtr graph_executor;
  bool ge_callback_done = false;
  model->CreateGraphExecutor(
      mojo::MakeRequest(&graph_executor),
      base::Bind(
          [](bool* ge_callback_done, const CreateGraphExecutorResult result) {
            EXPECT_EQ(result, CreateGraphExecutorResult::OK);
            *ge_callback_done = true;
          },
          &ge_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ge_callback_done);
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace(
      "input", NewTensor<double>(
                   {1, base::size(kSmartDim20181115TestInput)},
                   std::vector<double>(std::begin(kSmartDim20181115TestInput),
                                       std::end(kSmartDim20181115TestInput))));
  std::vector<std::string> outputs({"output"});

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      base::Bind(
          [](bool* infer_callback_done, const ExecuteResult result,
             base::Optional<std::vector<TensorPtr>> outputs) {
            // Check that the inference succeeded and gives the expected number
            // of outputs.
            EXPECT_EQ(result, ExecuteResult::OK);
            ASSERT_TRUE(outputs.has_value());
            ASSERT_EQ(outputs->size(), 1);

            // Check that the output tensor has the right type and format.
            const TensorView<double> out_tensor((*outputs)[0]);
            EXPECT_TRUE(out_tensor.IsValidType());
            EXPECT_TRUE(out_tensor.IsValidFormat());

            // Check the output tensor has the expected shape and values.
            EXPECT_THAT(out_tensor.GetShape(), ElementsAre(1, 1));
            EXPECT_THAT(out_tensor.GetValues(),
                        ElementsAre(DoubleNear(-3.36311, 0.1)));
            *infer_callback_done = true;
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

}  // namespace
}  // namespace ml
