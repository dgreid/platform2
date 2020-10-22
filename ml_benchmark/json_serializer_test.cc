// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml_benchmark/json_serializer.h"

#include <string>
#include <utility>

#include <gtest/gtest.h>

using chrome::ml_benchmark::BenchmarkResults;
using chrome::ml_benchmark::Metric;

namespace {

// Helps avoid having to do separate checks for key presence and value
// correctness.
base::Optional<std::string> GetStringKey(const base::Value& v,
                                         base::StringPiece key) {
  const std::string* result = v.FindStringKey(key);
  if (result)
    return *result;
  return base::nullopt;
}

}  // namespace

namespace ml_benchmark {

TEST(BenchmarkResultsToJson, Basics) {
  BenchmarkResults results;
  results.set_status(chrome::ml_benchmark::RUNTIME_ERROR);
  results.set_results_message("Test error");
  results.set_total_accuracy(42);

  const base::Optional<base::Value> json =
      ml_benchmark::BenchmarkResultsToJson(results);
  ASSERT_TRUE(json);
  EXPECT_EQ(json->FindIntKey("status"), chrome::ml_benchmark::RUNTIME_ERROR);
  EXPECT_EQ(GetStringKey(*json, "results_message"), "Test error");
  EXPECT_EQ(json->FindDoubleKey("total_accuracy"), 42);
}

TEST(BenchmarkResultsToJson, Percentiles) {
  BenchmarkResults results;
  auto& latency_map = *results.mutable_percentile_latencies_in_us();
  latency_map[50] = 1000;
  latency_map[90] = 2000;
  latency_map[95] = 3000;
  latency_map[99] = 4000;

  const base::Optional<base::Value> json =
      ml_benchmark::BenchmarkResultsToJson(results);
  ASSERT_TRUE(json);
  const base::Value* latencies = json->FindKeyOfType(
      "percentile_latencies_in_us", base::Value::Type::DICTIONARY);
  ASSERT_TRUE(json);
  EXPECT_EQ(latencies->FindIntKey("50"), 1000);
  EXPECT_EQ(latencies->FindIntKey("90"), 2000);
  EXPECT_EQ(latencies->FindIntKey("95"), 3000);
  EXPECT_EQ(latencies->FindIntKey("99"), 4000);
}

TEST(BenchmarkResultsToJson, Metrics) {
  BenchmarkResults results;

  {
    Metric* m = results.add_metrics();
    m->set_name("Multiple ms metric");
    m->set_units(Metric::MS);
    m->set_cardinality(Metric::MULTIPLE);

    m->add_values(1);
    m->add_values(2);
    m->add_values(3);
  }

  {
    Metric* m = results.add_metrics();
    m->set_name("Single unitless metric");
    m->set_direction(Metric::BIGGER_IS_BETTER);
    // UNITLESS + Cardinality::SINGLE by default.
    m->add_values(42);
  }

  const base::Optional<base::Value> json =
      ml_benchmark::BenchmarkResultsToJson(results);
  ASSERT_TRUE(json);
  const base::Value* metrics =
      json->FindKeyOfType("metrics", base::Value::Type::LIST);
  EXPECT_EQ(metrics->GetList().size(), 2);

  {
    const auto& m = metrics->GetList()[0];
    EXPECT_EQ(GetStringKey(m, "name"), "Multiple ms metric");
    EXPECT_EQ(GetStringKey(m, "units"), "ms");
    EXPECT_EQ(GetStringKey(m, "improvement_direction"), "smaller_is_better");
    EXPECT_EQ(GetStringKey(m, "cardinality"), "multiple");

    const base::Value* values =
        m.FindKeyOfType("values", base::Value::Type::LIST);
    ASSERT_TRUE(values);
    EXPECT_EQ(values->GetList().size(), 3);
    EXPECT_EQ(values->GetList()[0].GetDouble(), 1);
    EXPECT_EQ(values->GetList()[1].GetDouble(), 2);
    EXPECT_EQ(values->GetList()[2].GetDouble(), 3);
  }

  {
    const auto& m = metrics->GetList()[1];
    EXPECT_EQ(GetStringKey(m, "name"), "Single unitless metric");
    EXPECT_EQ(GetStringKey(m, "units"), "unitless");
    EXPECT_EQ(GetStringKey(m, "improvement_direction"), "bigger_is_better");
    EXPECT_EQ(GetStringKey(m, "cardinality"), "single");

    const base::Value* values =
        m.FindKeyOfType("values", base::Value::Type::LIST);
    ASSERT_TRUE(values);
    EXPECT_EQ(values->GetList().size(), 1);
    EXPECT_EQ(values->GetList()[0].GetDouble(), 42);
  }
}

TEST(BenchmarkResultsToJson, MetricsCardinality) {
  auto get_metrics_size =
      [](const BenchmarkResults& results) -> base::Optional<size_t> {
    const base::Optional<base::Value> json =
        ml_benchmark::BenchmarkResultsToJson(results);
    if (!json)
      return base::nullopt;

    const base::Value* metrics =
        json->FindKeyOfType("metrics", base::Value::Type::LIST);
    CHECK(metrics);
    const auto& m = metrics->GetList()[0];
    const base::Value* values =
        m.FindKeyOfType("values", base::Value::Type::LIST);
    CHECK(values);
    return values->GetList().size();
  };

  {
    BenchmarkResults results;
    Metric* m = results.add_metrics();
    m->set_cardinality(Metric::MULTIPLE);
    m->add_values(1);
    m->add_values(2);
    m->add_values(3);
    EXPECT_EQ(get_metrics_size(results), 3);
  }

  {
    BenchmarkResults results;
    Metric* m = results.add_metrics();
    m->set_cardinality(Metric::MULTIPLE);
    // No results is OK here.
    EXPECT_EQ(get_metrics_size(results), 0);
  }

  {
    BenchmarkResults results;
    Metric* m = results.add_metrics();
    m->set_cardinality(Metric::SINGLE);
    m->add_values(1);
    EXPECT_EQ(get_metrics_size(results), 1);
  }

  {
    BenchmarkResults results;
    Metric* m = results.add_metrics();
    m->set_cardinality(Metric::SINGLE);
    // Three results instead of a single one is not OK.
    m->add_values(1);
    m->add_values(2);
    m->add_values(3);
    EXPECT_EQ(get_metrics_size(results), base::nullopt);
  }

  {
    BenchmarkResults results;
    Metric* m = results.add_metrics();
    m->set_cardinality(Metric::SINGLE);
    // No results instead of a single one is not OK.
    EXPECT_EQ(get_metrics_size(results), base::nullopt);
  }
}

}  // namespace ml_benchmark
