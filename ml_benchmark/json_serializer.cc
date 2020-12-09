// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml_benchmark/json_serializer.h"

#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/values.h>
#include <brillo/file_utils.h>

#include <string>
#include <utility>

using chrome::ml_benchmark::BenchmarkResults;
using chrome::ml_benchmark::Metric;

namespace {

// Maps to |tast/common/perf/perf.go| |supportedUnits|.
base::Optional<std::string> metric_units(const Metric::Units u) {
  switch (u) {
    case Metric::UNITLESS:
      return "unitless";
    case Metric::BYTES:
      return "bytes";
    case Metric::JOULES:
      return "J";
    case Metric::WATTS:
      return "W";
    case Metric::COUNT:
      return "count";
    case Metric::MS:
      return "ms";
    case Metric::NPERCENT:
      return "n%";
    case Metric::SIGMA:
      return "sigma";
    case Metric::TS_MS:
      return "tsMs";
    default:
      LOG(ERROR) << "Unhandled unit: " << u;
      return base::nullopt;
  }
}

// Maps to |mlbenchmark/scenario.go| |ImprovementDirection|.
base::Optional<std::string> metric_direction(const Metric::Direction d) {
  switch (d) {
    case Metric::SMALLER_IS_BETTER:
      return "smaller_is_better";
    case Metric::BIGGER_IS_BETTER:
      return "bigger_is_better";
    default:
      LOG(ERROR) << "Unhandled direction: " << d;
      return base::nullopt;
  }
}

// Maps to |mlbenchmark/scenario.go| |Cardinality|.
base::Optional<std::string> metric_cardinality(const Metric::Cardinality c) {
  switch (c) {
    case Metric::SINGLE:
      return "single";
    case Metric::MULTIPLE:
      return "multiple";
    default:
      LOG(ERROR) << "Unhandled cardinality: " << c;
      return base::nullopt;
  }
}

}  // namespace

namespace ml_benchmark {

base::Optional<base::Value> BenchmarkResultsToJson(
    const BenchmarkResults& results) {
  base::Value doc(base::Value::Type::DICTIONARY);
  doc.SetKey("status", base::Value(results.status()));
  doc.SetKey("results_message", base::Value(results.results_message()));
  doc.SetKey("total_accuracy",
             base::Value(static_cast<double>(results.total_accuracy())));

  base::Value percentiles(base::Value::Type::DICTIONARY);
  for (const auto& latencies : results.percentile_latencies_in_us()) {
    std::string percentile = std::to_string(latencies.first);
    percentiles.SetKey(percentile,
                       base::Value(static_cast<int>(latencies.second)));
  }
  doc.SetKey("percentile_latencies_in_us", std::move(percentiles));

  base::Value metrics(base::Value::Type::LIST);
  for (const auto& m : results.metrics()) {
    base::Value metric(base::Value::Type::DICTIONARY);
    metric.SetKey("name", base::Value(m.name()));
    const auto direction = metric_direction(m.direction());
    if (!direction)
      return base::nullopt;
    metric.SetKey("improvement_direction", base::Value(*direction));
    const auto units = metric_units(m.units());
    if (!units)
      return base::nullopt;
    metric.SetKey("units", base::Value(*units));
    const auto cardinality = metric_cardinality(m.cardinality());
    if (!cardinality)
      return base::nullopt;
    metric.SetKey("cardinality", base::Value(*cardinality));

    if (m.cardinality() == Metric::SINGLE && m.values().size() != 1) {
      LOG(ERROR) << "Single cardinality metrics should contain a single value. "
                 << m.values().size() << " values found instead for metric "
                 << m.name();
      return base::nullopt;
    }
    base::Value values(base::Value::Type::LIST);
    for (const auto& v : m.values()) {
      values.Append(base::Value(v));
    }
    metric.SetKey("values", std::move(values));

    metrics.Append(std::move(metric));
  }
  doc.SetKey("metrics", std::move(metrics));

  return doc;
}

void WriteResultsToPath(const BenchmarkResults& results,
                        const base::FilePath& output_path) {
  base::Optional<base::Value> doc = BenchmarkResultsToJson(results);
  if (!doc) {
    return;
  }

  std::string results_string;
  if (!base::JSONWriter::Write(*doc, &results_string)) {
    LOG(ERROR) << "Unable to serialize benchmarking results.";
    return;
  }
  constexpr mode_t kFileRWMode = 0644;
  if (!brillo::WriteToFileAtomic(output_path, results_string.c_str(),
                                 results_string.size(), kFileRWMode)) {
    LOG(ERROR) << "Unable to write out the benchmarking results";
  }
}

}  // namespace ml_benchmark
