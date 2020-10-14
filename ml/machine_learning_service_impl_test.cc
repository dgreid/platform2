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
#include <mojo/public/cpp/bindings/remote.h>

#include "ml/handwriting.h"
#include "ml/handwriting_proto_mojom_conversion.h"
#include "ml/machine_learning_service_impl.h"
#include "ml/mojom/graph_executor.mojom.h"
#include "ml/mojom/handwriting_recognizer.mojom.h"
#include "ml/mojom/machine_learning_service.mojom.h"
#include "ml/mojom/model.mojom.h"
#include "ml/mojom/soda.mojom.h"
#include "ml/mojom/text_classifier.mojom.h"
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

constexpr double kSmartDim20200206TestInput[] = {
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
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
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
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};

// Points that are used to generate a stroke for handwriting.
constexpr float kHandwritingTestPoints[23][2] = {
    {1.928, 0.827}, {1.828, 0.826}, {1.73, 0.858},  {1.667, 0.901},
    {1.617, 0.955}, {1.567, 1.043}, {1.548, 1.148}, {1.569, 1.26},
    {1.597, 1.338}, {1.641, 1.408}, {1.688, 1.463}, {1.783, 1.473},
    {1.853, 1.418}, {1.897, 1.362}, {1.938, 1.278}, {1.968, 1.204},
    {1.999, 1.112}, {2.003, 1.004}, {1.984, 0.905}, {1.988, 1.043},
    {1.98, 1.178},  {1.976, 1.303}, {1.984, 1.415},
};

constexpr char kTextClassifierTestInput[] =
    "user.name@gmail.com. 123 George Street. unfathomable. 12pm. 350°F";

using ::chromeos::machine_learning::mojom::BuiltinModelId;
using ::chromeos::machine_learning::mojom::BuiltinModelSpec;
using ::chromeos::machine_learning::mojom::BuiltinModelSpecPtr;
using ::chromeos::machine_learning::mojom::CodepointSpan;
using ::chromeos::machine_learning::mojom::CodepointSpanPtr;
using ::chromeos::machine_learning::mojom::CreateGraphExecutorResult;
using ::chromeos::machine_learning::mojom::ExecuteResult;
using ::chromeos::machine_learning::mojom::FlatBufferModelSpec;
using ::chromeos::machine_learning::mojom::FlatBufferModelSpecPtr;
using ::chromeos::machine_learning::mojom::GraphExecutor;
using ::chromeos::machine_learning::mojom::HandwritingRecognitionQuery;
using ::chromeos::machine_learning::mojom::HandwritingRecognitionQueryPtr;
using ::chromeos::machine_learning::mojom::HandwritingRecognizer;
using ::chromeos::machine_learning::mojom::HandwritingRecognizerResult;
using ::chromeos::machine_learning::mojom::HandwritingRecognizerResultPtr;
using ::chromeos::machine_learning::mojom::HandwritingRecognizerSpec;
using ::chromeos::machine_learning::mojom::LoadHandwritingModelResult;
using ::chromeos::machine_learning::mojom::LoadModelResult;
using ::chromeos::machine_learning::mojom::MachineLearningService;
using ::chromeos::machine_learning::mojom::Model;
using ::chromeos::machine_learning::mojom::SodaClient;
using ::chromeos::machine_learning::mojom::SodaConfig;
using ::chromeos::machine_learning::mojom::SodaRecognizer;
using ::chromeos::machine_learning::mojom::TensorPtr;
using ::chromeos::machine_learning::mojom::TextAnnotationPtr;
using ::chromeos::machine_learning::mojom::TextAnnotationRequest;
using ::chromeos::machine_learning::mojom::TextAnnotationRequestPtr;
using ::chromeos::machine_learning::mojom::TextClassifier;
using ::chromeos::machine_learning::mojom::TextLanguagePtr;
using ::chromeos::machine_learning::mojom::TextSuggestSelectionRequest;
using ::chromeos::machine_learning::mojom::TextSuggestSelectionRequestPtr;
using ::testing::DoubleEq;
using ::testing::DoubleNear;
using ::testing::ElementsAre;
using ::testing::StrictMock;

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

// A simple SODA client for testing.
class MockSodaClientImpl
    : public chromeos::machine_learning::mojom::SodaClient {
 public:
  MOCK_METHOD(void, OnStop, (), (override));
  MOCK_METHOD(void, OnStart, (), (override));
  MOCK_METHOD(
      void,
      OnSpeechRecognizerEvent,
      (chromeos::machine_learning::mojom::SpeechRecognizerEventPtr event),
      (override));
};

// Loads builtin model specified by `model_id`, binding the impl to `model`.
// Returns true on success.
bool LoadBuiltinModelForTesting(
    const mojo::Remote<MachineLearningService>& ml_service,
    BuiltinModelId model_id,
    mojo::Remote<Model>* model) {
  // Set up model spec.
  BuiltinModelSpecPtr spec = BuiltinModelSpec::New();
  spec->id = model_id;

  bool model_callback_done = false;
  ml_service->LoadBuiltinModel(
      std::move(spec), model->BindNewPipeAndPassReceiver(),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  return model_callback_done;
}

// Loads flatbuffer model specified by `spec`, binding the impl to `model`.
// Returns true on success.
bool LoadFlatBufferModelForTesting(
    const mojo::Remote<MachineLearningService>& ml_service,
    FlatBufferModelSpecPtr spec,
    mojo::Remote<Model>* model) {
  bool model_callback_done = false;
  ml_service->LoadFlatBufferModel(
      std::move(spec), model->BindNewPipeAndPassReceiver(),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  return model_callback_done;
}

// Creates graph executor of `model`, binding the impl to `graph_executor`.
// Returns true on success.
bool CreateGraphExecutorForTesting(
    const mojo::Remote<Model>& model,
    mojo::Remote<GraphExecutor>* graph_executor) {
  bool ge_callback_done = false;
  model->CreateGraphExecutor(
      graph_executor->BindNewPipeAndPassReceiver(),
      base::Bind(
          [](bool* ge_callback_done, const CreateGraphExecutorResult result) {
            EXPECT_EQ(result, CreateGraphExecutorResult::OK);
            *ge_callback_done = true;
          },
          &ge_callback_done));
  base::RunLoop().RunUntilIdle();
  return ge_callback_done;
}

// Checks that `result` is OK and that `outputs` contains a tensor matching
// `expected_shape` and `expected_value`. Sets `infer_callback_done` to true so
// that this function can be used to verify that a Mojo callback has been run.
// TODO(alanlxl): currently the output size of all models are 1, and value types
// are all double. Parameterization may be necessary for future models.
void CheckOutputTensor(const std::vector<int64_t> expected_shape,
                       const double expected_value,
                       bool* infer_callback_done,
                       ExecuteResult result,
                       base::Optional<std::vector<TensorPtr>> outputs) {
  // Check that the inference succeeded and gives the expected number
  // of outputs.
  EXPECT_EQ(result, ExecuteResult::OK);
  ASSERT_TRUE(outputs.has_value());
  // currently all the models here has the same output size 1.
  ASSERT_EQ(outputs->size(), 1);

  // Check that the output tensor has the right type and format.
  const TensorView<double> out_tensor((*outputs)[0]);
  EXPECT_TRUE(out_tensor.IsValidType());
  EXPECT_TRUE(out_tensor.IsValidFormat());

  // Check the output tensor has the expected shape and values.
  EXPECT_EQ(out_tensor.GetShape(), expected_shape);
  EXPECT_THAT(out_tensor.GetValues(),
              ElementsAre(DoubleNear(expected_value, 1e-5)));
  *infer_callback_done = true;
}

// Tests that Clone() connects to a working impl.
TEST(MachineLearningServiceImplTest, Clone) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  // Call Clone to bind another MachineLearningService.
  mojo::Remote<MachineLearningService> ml_service_2;
  ml_service->Clone(ml_service_2.BindNewPipeAndPassReceiver());

  // Verify that the new MachineLearningService works with a simple call:
  // Loading the TEST_MODEL.
  BuiltinModelSpecPtr spec = BuiltinModelSpec::New();
  spec->id = BuiltinModelId::TEST_MODEL;
  mojo::Remote<Model> model;
  bool model_callback_done = false;
  ml_service_2->LoadBuiltinModel(
      std::move(spec), model.BindNewPipeAndPassReceiver(),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(model_callback_done);
  EXPECT_TRUE(model.is_bound());
}

TEST(MachineLearningServiceImplTest, TestBadModel) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  // Set up model spec to specify an invalid model.
  BuiltinModelSpecPtr spec = BuiltinModelSpec::New();
  spec->id = BuiltinModelId::UNSUPPORTED_UNKNOWN;

  // Load model.
  mojo::Remote<Model> model;
  bool model_callback_done = false;
  ml_service->LoadBuiltinModel(
      std::move(spec), model.BindNewPipeAndPassReceiver(),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::MODEL_SPEC_ERROR);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
}

// Tests loading an empty model through the downloaded model api.
TEST(MachineLearningServiceImplTest, EmptyModelString) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  FlatBufferModelSpecPtr spec = FlatBufferModelSpec::New();
  spec->model_string = "";
  spec->inputs["x"] = 1;
  spec->inputs["y"] = 2;
  spec->outputs["z"] = 0;
  spec->metrics_model_name = "TestModel";

  // Load model from an empty model string.
  mojo::Remote<Model> model;
  bool model_callback_done = false;
  ml_service->LoadFlatBufferModel(
      std::move(spec), model.BindNewPipeAndPassReceiver(),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::LOAD_MODEL_ERROR);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
}

// Tests loading a bad model string through the downloaded model api.
TEST(MachineLearningServiceImplTest, BadModelString) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  FlatBufferModelSpecPtr spec = FlatBufferModelSpec::New();
  spec->model_string = "bad model string";
  spec->inputs["x"] = 1;
  spec->inputs["y"] = 2;
  spec->outputs["z"] = 0;
  spec->metrics_model_name = "TestModel";

  // Load model from an empty model string.
  mojo::Remote<Model> model;
  bool model_callback_done = false;
  ml_service->LoadFlatBufferModel(
      std::move(spec), model.BindNewPipeAndPassReceiver(),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::LOAD_MODEL_ERROR);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
}

// Tests loading TEST_MODEL through the builtin model api.
TEST(MachineLearningServiceImplTest, TestModel) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  // Leave loading model and creating graph executor inline here to demonstrate
  // the usage details.
  // Set up model spec.
  BuiltinModelSpecPtr spec = BuiltinModelSpec::New();
  spec->id = BuiltinModelId::TEST_MODEL;

  // Load model.
  mojo::Remote<Model> model;
  bool model_callback_done = false;
  ml_service->LoadBuiltinModel(
      std::move(spec), model.BindNewPipeAndPassReceiver(),
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
  mojo::Remote<GraphExecutor> graph_executor;
  bool ge_callback_done = false;
  model->CreateGraphExecutor(
      graph_executor.BindNewPipeAndPassReceiver(),
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
  std::vector<int64_t> expected_shape{1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(std::move(inputs), std::move(outputs),
                          base::Bind(&CheckOutputTensor, expected_shape, 0.75,
                                     &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests loading TEST_MODEL through the downloaded model api.
TEST(MachineLearningServiceImplTest, TestModelString) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  // Load the TEST_MODEL model file into string.
  std::string model_string;
  ASSERT_TRUE(base::ReadFileToString(
      base::FilePath(GetTestModelDir() +
                     "mlservice-model-test_add-20180914.tflite"),
      &model_string));

  FlatBufferModelSpecPtr spec = FlatBufferModelSpec::New();
  spec->model_string = std::move(model_string);
  spec->inputs["x"] = 1;
  spec->inputs["y"] = 2;
  spec->outputs["z"] = 0;
  spec->metrics_model_name = "TestModel";

  // Load model.
  mojo::Remote<Model> model;
  ASSERT_TRUE(
      LoadFlatBufferModelForTesting(ml_service, std::move(spec), &model));
  ASSERT_NE(model.get(), nullptr);
  ASSERT_TRUE(model.is_bound());

  // Get graph executor.
  mojo::Remote<GraphExecutor> graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace("x", NewTensor<double>({1}, {0.5}));
  inputs.emplace("y", NewTensor<double>({1}, {0.25}));
  std::vector<std::string> outputs({"z"});
  std::vector<int64_t> expected_shape{1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(std::move(inputs), std::move(outputs),
                          base::Bind(&CheckOutputTensor, expected_shape, 0.75,
                                     &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Smart Dim (20181115) model file loads correctly and produces
// the expected inference result.
TEST(BuiltinModelInferenceTest, SmartDim20181115) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  // Load model.
  mojo::Remote<Model> model;
  ASSERT_TRUE(LoadBuiltinModelForTesting(
      ml_service, BuiltinModelId::SMART_DIM_20181115, &model));
  ASSERT_TRUE(model.is_bound());

  // Get graph executor.
  mojo::Remote<GraphExecutor> graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace(
      "input", NewTensor<double>(
                   {1, base::size(kSmartDim20181115TestInput)},
                   std::vector<double>(std::begin(kSmartDim20181115TestInput),
                                       std::end(kSmartDim20181115TestInput))));
  std::vector<std::string> outputs({"output"});
  std::vector<int64_t> expected_shape{1L, 1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(std::move(inputs), std::move(outputs),
                          base::Bind(&CheckOutputTensor, expected_shape,
                                     -3.36311, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Smart Dim (20190221) model file loads correctly and produces
// the expected inference result.
TEST(BuiltinModelInferenceTest, SmartDim20190221) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  // Load model and create graph executor.
  mojo::Remote<Model> model;
  ASSERT_TRUE(LoadBuiltinModelForTesting(
      ml_service, BuiltinModelId::SMART_DIM_20190221, &model));
  ASSERT_TRUE(model.is_bound());

  mojo::Remote<GraphExecutor> graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace(
      "input", NewTensor<double>(
                   {1, base::size(kSmartDim20190221TestInput)},
                   std::vector<double>(std::begin(kSmartDim20190221TestInput),
                                       std::end(kSmartDim20190221TestInput))));
  std::vector<std::string> outputs({"output"});
  std::vector<int64_t> expected_shape{1L, 1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(std::move(inputs), std::move(outputs),
                          base::Bind(&CheckOutputTensor, expected_shape,
                                     -0.900591, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Smart Dim (20190521) model file loads correctly and produces
// the expected inference result.
TEST(BuiltinModelInferenceTest, SmartDim20190521) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  // Load model and create graph executor.
  mojo::Remote<Model> model;
  ASSERT_TRUE(LoadBuiltinModelForTesting(
      ml_service, BuiltinModelId::SMART_DIM_20190521, &model));
  ASSERT_TRUE(model.is_bound());

  mojo::Remote<GraphExecutor> graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace(
      "input", NewTensor<double>(
                   {1, base::size(kSmartDim20190521TestInput)},
                   std::vector<double>(std::begin(kSmartDim20190521TestInput),
                                       std::end(kSmartDim20190521TestInput))));
  std::vector<std::string> outputs({"output"});
  std::vector<int64_t> expected_shape{1L, 1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(std::move(inputs), std::move(outputs),
                          base::Bind(&CheckOutputTensor, expected_shape,
                                     0.66962254, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Top Cat (20190722) model file loads correctly and produces
// the expected inference result.
TEST(BuiltinModelInferenceTest, TopCat20190722) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  // Load model and create graph executor.
  mojo::Remote<Model> model;
  ASSERT_TRUE(LoadBuiltinModelForTesting(
      ml_service, BuiltinModelId::TOP_CAT_20190722, &model));
  ASSERT_TRUE(model.is_bound());

  mojo::Remote<GraphExecutor> graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace("input",
                 NewTensor<double>(
                     {1, base::size(kTopCat20190722TestInput)},
                     std::vector<double>(std::begin(kTopCat20190722TestInput),
                                         std::end(kTopCat20190722TestInput))));
  std::vector<std::string> outputs({"output"});
  std::vector<int64_t> expected_shape{1L, 1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(std::move(inputs), std::move(outputs),
                          base::Bind(&CheckOutputTensor, expected_shape,
                                     -3.02972, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Search Ranker (20190923) model file loads correctly and
// produces the expected inference result.
TEST(BuiltinModelInferenceTest, SearchRanker20190923) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  // Load model and create graph executor.
  mojo::Remote<Model> model;
  ASSERT_TRUE(LoadBuiltinModelForTesting(
      ml_service, BuiltinModelId::SEARCH_RANKER_20190923, &model));
  ASSERT_TRUE(model.is_bound());

  mojo::Remote<GraphExecutor> graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace("input", NewTensor<double>(
                              {1, base::size(kSearchRanker20190923TestInput)},
                              std::vector<double>(
                                  std::begin(kSearchRanker20190923TestInput),
                                  std::end(kSearchRanker20190923TestInput))));
  std::vector<std::string> outputs({"output"});
  std::vector<int64_t> expected_shape{1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(std::move(inputs), std::move(outputs),
                          base::Bind(&CheckOutputTensor, expected_shape,
                                     0.658488, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Smart Dim (20200206) model file loads correctly and
// produces the expected inference result.
TEST(DownloadableModelInferenceTest, SmartDim20200206) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  // Load SmartDim model into string.
  std::string model_string;
  ASSERT_TRUE(base::ReadFileToString(
      base::FilePath(GetTestModelDir() +
                     "mlservice-model-smart_dim-20200206-downloadable.tflite"),
      &model_string));

  FlatBufferModelSpecPtr spec = FlatBufferModelSpec::New();
  spec->model_string = std::move(model_string);
  spec->inputs["input"] = 0;
  spec->outputs["output"] = 6;
  spec->metrics_model_name = "SmartDimModel_20200206";

  // Load model.
  mojo::Remote<Model> model;
  ASSERT_TRUE(
      LoadFlatBufferModelForTesting(ml_service, std::move(spec), &model));
  ASSERT_NE(model.get(), nullptr);
  ASSERT_TRUE(model.is_bound());

  // Get graph executor.
  mojo::Remote<GraphExecutor> graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace(
      "input", NewTensor<double>(
                   {1, base::size(kSmartDim20200206TestInput)},
                   std::vector<double>(std::begin(kSmartDim20200206TestInput),
                                       std::end(kSmartDim20200206TestInput))));
  std::vector<std::string> outputs({"output"});
  std::vector<int64_t> expected_shape{1L, 1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(std::move(inputs), std::move(outputs),
                          base::Bind(&CheckOutputTensor, expected_shape,
                                     -1.07195, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests loading text classifier only.
TEST(LoadTextClassifierTest, NoInference) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  mojo::Remote<TextClassifier> text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      text_classifier.BindNewPipeAndPassReceiver(),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
}

// Tests text classifier annotator for empty string.
TEST(TextClassifierAnnotateTest, EmptyString) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  mojo::Remote<TextClassifier> text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      text_classifier.BindNewPipeAndPassReceiver(),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);

  TextAnnotationRequestPtr request = TextAnnotationRequest::New();
  request->text = "";
  bool infer_callback_done = false;
  text_classifier->Annotate(std::move(request),
                            base::Bind(
                                [](bool* infer_callback_done,
                                   std::vector<TextAnnotationPtr> annotations) {
                                  *infer_callback_done = true;
                                  EXPECT_EQ(annotations.size(), 0);
                                },
                                &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests text classifier annotator for a complex string.
TEST(TextClassifierAnnotateTest, ComplexString) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  mojo::Remote<TextClassifier> text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      text_classifier.BindNewPipeAndPassReceiver(),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);

  TextAnnotationRequestPtr request = TextAnnotationRequest::New();
  request->text = kTextClassifierTestInput;
  bool infer_callback_done = false;
  text_classifier->Annotate(
      std::move(request),
      base::Bind(
          [](bool* infer_callback_done,
             std::vector<TextAnnotationPtr> annotations) {
            *infer_callback_done = true;
            EXPECT_EQ(annotations.size(), 5);
            EXPECT_EQ(annotations[0]->start_offset, 0);
            EXPECT_EQ(annotations[0]->end_offset, 19);
            ASSERT_GE(annotations[0]->entities.size(), 1);
            EXPECT_EQ(annotations[0]->entities[0]->name, "email");
            EXPECT_EQ(annotations[0]->entities[0]->data->get_string_value(),
                      "user.name@gmail.com");
            EXPECT_EQ(annotations[1]->start_offset, 21);
            EXPECT_EQ(annotations[1]->end_offset, 38);
            ASSERT_GE(annotations[1]->entities.size(), 1);
            EXPECT_EQ(annotations[1]->entities[0]->name, "address");
            EXPECT_EQ(annotations[1]->entities[0]->data->get_string_value(),
                      "123 George Street");
            EXPECT_EQ(annotations[2]->start_offset, 40);
            EXPECT_EQ(annotations[2]->end_offset, 52);
            ASSERT_GE(annotations[2]->entities.size(), 1);
            EXPECT_EQ(annotations[2]->entities[0]->name, "dictionary");
            EXPECT_EQ(annotations[2]->entities[0]->data->get_string_value(),
                      "unfathomable");
            EXPECT_EQ(annotations[3]->start_offset, 54);
            EXPECT_EQ(annotations[3]->end_offset, 59);
            ASSERT_GE(annotations[3]->entities.size(), 1);
            EXPECT_EQ(annotations[3]->entities[0]->name, "datetime");
            EXPECT_EQ(annotations[3]->entities[0]->data->get_string_value(),
                      "12pm.");
            EXPECT_EQ(annotations[4]->start_offset, 60);
            EXPECT_EQ(annotations[4]->end_offset, 65);
            ASSERT_GE(annotations[4]->entities.size(), 1);
            EXPECT_EQ(annotations[4]->entities[0]->name, "unit");
            EXPECT_EQ(annotations[4]->entities[0]->data->get_string_value(),
                      "350°F");
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests text classifier selection suggestion for an empty string.
// In this situation, text classifier will return the input span.
TEST(TextClassifierSelectionTest, EmptyString) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  mojo::Remote<TextClassifier> text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      text_classifier.BindNewPipeAndPassReceiver(),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);

  TextSuggestSelectionRequestPtr request = TextSuggestSelectionRequest::New();
  request->text = "";
  request->user_selection = CodepointSpan::New();
  request->user_selection->start_offset = 1;
  request->user_selection->end_offset = 2;
  bool infer_callback_done = false;
  text_classifier->SuggestSelection(
      std::move(request),
      base::Bind(
          [](bool* infer_callback_done, CodepointSpanPtr suggested_span) {
            *infer_callback_done = true;
            EXPECT_EQ(suggested_span->start_offset, 1);
            EXPECT_EQ(suggested_span->end_offset, 2);
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests text classifier selection suggestion for a complex string.
TEST(TextClassifierSelectionTest, ComplexString) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  mojo::Remote<TextClassifier> text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      text_classifier.BindNewPipeAndPassReceiver(),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);

  TextSuggestSelectionRequestPtr request = TextSuggestSelectionRequest::New();
  request->text = kTextClassifierTestInput;
  request->user_selection = CodepointSpan::New();
  request->user_selection->start_offset = 25;
  request->user_selection->end_offset = 26;
  bool infer_callback_done = false;
  text_classifier->SuggestSelection(
      std::move(request),
      base::Bind(
          [](bool* infer_callback_done, CodepointSpanPtr suggested_span) {
            *infer_callback_done = true;
            EXPECT_EQ(suggested_span->start_offset, 21);
            EXPECT_EQ(suggested_span->end_offset, 38);
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests text classifier selection suggestion with wrong inputs.
// In this situation, text classifier will return the input span.
TEST(TextClassifierSelectionTest, WrongInput) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  mojo::Remote<TextClassifier> text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      text_classifier.BindNewPipeAndPassReceiver(),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);

  TextSuggestSelectionRequestPtr request = TextSuggestSelectionRequest::New();
  request->text = kTextClassifierTestInput;
  request->user_selection = CodepointSpan::New();
  request->user_selection->start_offset = 30;
  request->user_selection->end_offset = 26;
  bool infer_callback_done = false;
  text_classifier->SuggestSelection(
      std::move(request),
      base::Bind(
          [](bool* infer_callback_done, CodepointSpanPtr suggested_span) {
            *infer_callback_done = true;
            EXPECT_EQ(suggested_span->start_offset, 30);
            EXPECT_EQ(suggested_span->end_offset, 26);
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests text classifier language identification with some valid inputs.
TEST(TextClassifierLangIdTest, ValidInput) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  mojo::Remote<TextClassifier> text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      text_classifier.BindNewPipeAndPassReceiver(),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);

  bool infer_callback_done = false;
  text_classifier->FindLanguages(
      "Bonjour",
      base::Bind(
          [](bool* infer_callback_done, std::vector<TextLanguagePtr> result) {
            *infer_callback_done = true;
            ASSERT_GT(result.size(), 0);
            EXPECT_EQ(result[0]->locale, "fr");
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests text classifier language identification with empty input.
// Empty input should produce empty result.
TEST(TextClassifierLangIdTest, EmptyInput) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  mojo::Remote<TextClassifier> text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      text_classifier.BindNewPipeAndPassReceiver(),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);

  bool infer_callback_done = false;
  text_classifier->FindLanguages(
      "",
      base::Bind(
          [](bool* infer_callback_done, std::vector<TextLanguagePtr> result) {
            *infer_callback_done = true;
            EXPECT_EQ(result.size(), 0);
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Test class for HandwritingRecognizerTest.
class HandwritingRecognizerTest : public testing::Test {
 protected:
  void SetUp() override {
    // Nothing to test on an unsupported platform.
    if (!ml::HandwritingLibrary::IsHandwritingLibraryUnitTestSupported()) {
      return;
    }
    // Set ml_service.
    ml_service_impl_ = std::make_unique<MachineLearningServiceImplForTesting>(
        ml_service_.BindNewPipeAndPassReceiver().PassPipe());

    // Set default request.
    request_.set_max_num_results(1);
    auto& stroke = *request_.mutable_ink()->add_strokes();
    for (int i = 0; i < 23; ++i) {
      auto& point = *stroke.add_points();
      point.set_x(kHandwritingTestPoints[i][0]);
      point.set_y(kHandwritingTestPoints[i][1]);
    }
  }

  // recognizer_ should be loaded successfully for this `language`.
  // Using new API (LoadHandwritingModelWithSpec) if use_load_handwriting_model
  // is true.
  void LoadRecognizerWithLanguage(
      const std::string& langauge,
      const bool use_load_handwriting_model = false) {
    bool model_callback_done = false;
    if (use_load_handwriting_model) {
      ml_service_->LoadHandwritingModel(
          HandwritingRecognizerSpec::New(langauge),
          recognizer_.BindNewPipeAndPassReceiver(),
          base::Bind(
              [](bool* model_callback_done,
                 const LoadHandwritingModelResult result) {
                ASSERT_EQ(result, LoadHandwritingModelResult::OK);
                *model_callback_done = true;
              },
              &model_callback_done));
    } else {
      ml_service_->LoadHandwritingModelWithSpec(
          HandwritingRecognizerSpec::New(langauge),
          recognizer_.BindNewPipeAndPassReceiver(),
          base::Bind(
              [](bool* model_callback_done, const LoadModelResult result) {
                ASSERT_EQ(result, LoadModelResult::OK);
                *model_callback_done = true;
              },
              &model_callback_done));
    }
    base::RunLoop().RunUntilIdle();
    ASSERT_TRUE(model_callback_done);
    ASSERT_TRUE(recognizer_.is_bound());
  }

  // Recognizing on the request_ should produce expected text and score.
  void ExpectRecognizeResult(const std::string& text, const float score) {
    // Perform inference.
    bool infer_callback_done = false;
    recognizer_->Recognize(
        HandwritingRecognitionQueryFromProtoForTesting(request_),
        base::Bind(
            [](bool* infer_callback_done, const std::string& text,
               const float score, const HandwritingRecognizerResultPtr result) {
              // Check that the inference succeeded and gives
              // the expected number of outputs.
              EXPECT_EQ(result->status,
                        HandwritingRecognizerResult::Status::OK);
              ASSERT_EQ(result->candidates.size(), 1);
              EXPECT_EQ(result->candidates.at(0)->text, text);
              EXPECT_FLOAT_EQ(result->candidates.at(0)->score, score);
              *infer_callback_done = true;
            },
            &infer_callback_done, text, score));
    base::RunLoop().RunUntilIdle();
    ASSERT_TRUE(infer_callback_done);
  }

  std::unique_ptr<MachineLearningServiceImplForTesting> ml_service_impl_;
  mojo::Remote<MachineLearningService> ml_service_;
  mojo::Remote<HandwritingRecognizer> recognizer_;
  chrome_knowledge::HandwritingRecognizerRequest request_;
};

// Tests that the HandwritingRecognizer recognition returns expected scores.
TEST_F(HandwritingRecognizerTest, GetExpectedScores) {
  // Nothing to test on an unsupported platform.
  if (!ml::HandwritingLibrary::IsHandwritingLibraryUnitTestSupported()) {
    return;
  }

  // Load Recognizer successfully.
  LoadRecognizerWithLanguage("en");

  // Run Recognition on the default request_.
  ExpectRecognizeResult("a", 0.50640869f);

  // Modify the request_ by setting fake time.
  for (int i = 0; i < 23; ++i) {
    request_.mutable_ink()->mutable_strokes(0)->mutable_points(i)->set_t(i * i *
                                                                         100);
  }
  ExpectRecognizeResult("a", 0.51218414f);
}

// Tests that the LoadHandwritingModel also perform as expected.
TEST_F(HandwritingRecognizerTest, LoadHandwritingModel) {
  // Nothing to test on an unsupported platform.
  if (!ml::HandwritingLibrary::IsHandwritingLibraryUnitTestSupported()) {
    return;
  }

  // Load Recognizer successfully.
  LoadRecognizerWithLanguage("en", true);

  // Clear the ink inside request.
  request_.clear_ink();

  // Perform inference should return an error.
  bool infer_callback_done = false;
  recognizer_->Recognize(
      HandwritingRecognitionQueryFromProtoForTesting(request_),
      base::Bind(
          [](bool* infer_callback_done,
             const HandwritingRecognizerResultPtr result) {
            // Check that the inference failed.
            EXPECT_EQ(result->status,
                      HandwritingRecognizerResult::Status::ERROR);
            EXPECT_EQ(result->candidates.size(), 0);
            *infer_callback_done = true;
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the HandwritingRecognizer Recognition should fail on empty ink.
TEST_F(HandwritingRecognizerTest, FailOnEmptyInk) {
  // Nothing to test on an unsupported platform.
  if (!ml::HandwritingLibrary::IsHandwritingLibraryUnitTestSupported()) {
    return;
  }

  // Load Recognizer successfully.
  LoadRecognizerWithLanguage("en");

  // Clear the ink inside request.
  request_.clear_ink();

  // Perform inference should return an error.
  bool infer_callback_done = false;
  recognizer_->Recognize(
      HandwritingRecognitionQueryFromProtoForTesting(request_),
      base::Bind(
          [](bool* infer_callback_done,
             const HandwritingRecognizerResultPtr result) {
            // Check that the inference failed.
            EXPECT_EQ(result->status,
                      HandwritingRecognizerResult::Status::ERROR);
            EXPECT_EQ(result->candidates.size(), 0);
            *infer_callback_done = true;
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

MATCHER_P(StructPtrEq, n, "") {
  return n.get().Equals(arg);
}

// Tests the SODA CrOS mojo callback for the dummy implementation can return
// expected error string.
TEST(SODARecognizerTest, DummyImplMojoCallback) {
#ifdef USE_ONDEVICE_SPEECH
  return;
#else
  StrictMock<MockSodaClientImpl> soda_client_impl;
  mojo::Receiver<SodaClient> soda_client(&soda_client_impl);
  auto soda_config = SodaConfig::New();
  mojo::Remote<SodaRecognizer> soda_recognizer;

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver().PassPipe());

  ml_service->LoadSpeechRecognizer(std::move(soda_config),
                                   soda_client.BindNewPipeAndPassRemote(),
                                   soda_recognizer.BindNewPipeAndPassReceiver(),
                                   base::BindOnce([](LoadModelResult) {}));
  chromeos::machine_learning::mojom::SpeechRecognizerEventPtr event =
      chromeos::machine_learning::mojom::SpeechRecognizerEvent::New();
  chromeos::machine_learning::mojom::FinalResultPtr final_result =
      chromeos::machine_learning::mojom::FinalResult::New();
  final_result->final_hypotheses.push_back(
      "On-device speech is not supported.");
  final_result->endpoint_reason =
      chromeos::machine_learning::mojom::EndpointReason::ENDPOINT_UNKNOWN;
  event->set_final_result(std::move(final_result));

  // TODO(robsc): Update this unittest to use regular Eq() once
  // https://chromium-review.googlesource.com/c/chromium/src/+/2456184 is
  // submitted.
  EXPECT_CALL(soda_client_impl,
              OnSpeechRecognizerEvent(StructPtrEq(std::ref(event))))
      .Times(1);
  soda_recognizer->Start();
  base::RunLoop().RunUntilIdle();

  EXPECT_CALL(soda_client_impl,
              OnSpeechRecognizerEvent(StructPtrEq(std::ref(event))))
      .Times(1);
  soda_recognizer->AddAudio({});
  base::RunLoop().RunUntilIdle();

  EXPECT_CALL(soda_client_impl,
              OnSpeechRecognizerEvent(StructPtrEq(std::ref(event))))
      .Times(1);
  soda_recognizer->MarkDone();
  base::RunLoop().RunUntilIdle();

  EXPECT_CALL(soda_client_impl,
              OnSpeechRecognizerEvent(StructPtrEq(std::ref(event))))
      .Times(1);
  soda_recognizer->Stop();
  base::RunLoop().RunUntilIdle();
#endif
}

}  // namespace
}  // namespace ml
