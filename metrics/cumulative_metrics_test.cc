// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "base/at_exit.h"
#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/system/sys_info.h"
#include "base/task/single_thread_task_executor.h"
#include "base/threading/thread_local.h"
#include "base/time/time.h"
#include "metrics/cumulative_metrics.h"

using chromeos_metrics::CumulativeMetrics;

namespace chromeos_metrics {

namespace {

base::ThreadLocalPointer<base::RunLoop> tls_run_loop;

constexpr char kMetricNameX[] = "x.pi";
constexpr char kMetricNameY[] = "y.pi";
constexpr char kMetricNameZ[] = "z.pi";
constexpr int kTotalReportCount = 3;

static int accumulator_update_partial_count = 0;
static int accumulator_update_total_count = 0;
static int accumulator_report_count = 0;

}  // namespace

class CumulativeMetricsTest : public testing::Test {};

static void UpdateAccumulators(CumulativeMetrics* cm) {
  cm->Add(kMetricNameX, 111);
  cm->Add(kMetricNameY, 222);
  cm->Max(kMetricNameZ, 333);
  accumulator_update_partial_count++;
  accumulator_update_total_count++;
}

static void ReportAccumulators(CumulativeMetrics* cm) {
  // The first call is done at initialization, to possibly report metrics
  // accumulated in the previous cycle.  We ignore it because we want to
  // test through at least one cycle.
  if (accumulator_report_count >= 1) {
    EXPECT_EQ(cm->Get(kMetricNameX), 111 * accumulator_update_partial_count);
    EXPECT_EQ(cm->Get(kMetricNameY), 222 * accumulator_update_partial_count);
    EXPECT_EQ(cm->Get(kMetricNameZ), 333);
  }
  // Quit loop.
  if (accumulator_report_count == kTotalReportCount) {
    tls_run_loop.Get()->Quit();
  }
  accumulator_update_partial_count = 0;
  accumulator_report_count += 1;
}

TEST_F(CumulativeMetricsTest, TestLoop) {
  base::SingleThreadTaskExecutor task_executor_;
  base::RunLoop run_loop;
  tls_run_loop.Set(&run_loop);

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath pi_path = temp_dir.GetPath().Append("cumulative");
  ASSERT_TRUE(base::CreateDirectory(pi_path));

  std::vector<std::string> names = {kMetricNameX, kMetricNameY, kMetricNameZ};
  CumulativeMetrics cm(pi_path, names, base::TimeDelta::FromMilliseconds(100),
                       base::Bind(&UpdateAccumulators),
                       base::TimeDelta::FromMilliseconds(500),
                       base::Bind(&ReportAccumulators));

  run_loop.Run();

  // We don't want to rely on a precise number of calls to the update or report
  // callbacks because load on the buildbot can vary a lot, and also there are
  // uncertainties as to when the QuitNow() is effective.  But we expect at
  // least kTotalReportCount + 1 calls, no matter how loaded the system is,
  // because QuitNow() is called only after that many reports.
  EXPECT_GE(accumulator_report_count, kTotalReportCount + 1);
  // We also expect at least kTotalReportCount calls to the update callback.
  EXPECT_GE(accumulator_update_total_count, kTotalReportCount);
}

}  // namespace chromeos_metrics
