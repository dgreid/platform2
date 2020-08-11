// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/utils.h"

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "chrome/knowledge/federated/example.pb.h"
#include "chrome/knowledge/federated/feature.pb.h"
#include "federated/mojom/example.mojom.h"
#include "federated/test_utils.h"

namespace federated {
namespace {
using chromeos::federated::mojom::Example;
using chromeos::federated::mojom::ExamplePtr;
using chromeos::federated::mojom::Features;
using testing::ElementsAre;

TEST(UtilsTest, ConvertToTensorFlowExampleProto) {
  auto example = CreateExamplePtr();

  tensorflow::Example tf_example_converted =
      ConvertToTensorFlowExampleProto(example);
  const auto& tf_feature_map = tf_example_converted.features().feature();

  EXPECT_EQ(tf_feature_map.size(), 4);

  EXPECT_TRUE(tf_feature_map.contains("int_feature1"));
  const auto& int_feature1 = tf_feature_map.at("int_feature1");
  EXPECT_TRUE(int_feature1.has_int64_list() && !int_feature1.has_float_list() &&
              !int_feature1.has_bytes_list());
  EXPECT_THAT(int_feature1.int64_list().value(), ElementsAre(1, 2, 3, 4, 5));

  EXPECT_TRUE(tf_feature_map.contains("int_feature2"));
  const auto& int_feature2 = tf_feature_map.at("int_feature2");
  EXPECT_TRUE(int_feature2.has_int64_list() && !int_feature2.has_float_list() &&
              !int_feature2.has_bytes_list());
  EXPECT_THAT(int_feature2.int64_list().value(),
              ElementsAre(10, 20, 30, 40, 50));

  EXPECT_TRUE(tf_feature_map.contains("float_feature1"));
  const auto& float_feature = tf_feature_map.at("float_feature1");
  EXPECT_TRUE(!float_feature.has_int64_list() &&
              float_feature.has_float_list() &&
              !float_feature.has_bytes_list());
  EXPECT_THAT(float_feature.float_list().value(),
              ElementsAre(1.1, 2.1, 3.1, 4.1, 5.1));

  EXPECT_TRUE(tf_feature_map.contains("string_feature1"));
  const auto& string_feature = tf_feature_map.at("string_feature1");
  EXPECT_TRUE(!string_feature.has_int64_list() &&
              !string_feature.has_float_list() &&
              string_feature.has_bytes_list());
  EXPECT_THAT(string_feature.bytes_list().value(),
              ElementsAre("abc", "123", "xyz"));
}

}  // namespace
}  // namespace federated
