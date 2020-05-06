// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "dlcservice/error.h"
#include "dlcservice/metrics.h"

namespace dlcservice {

class MetricsTest : public testing::Test {
 public:
  MetricsTest() = default;

 private:
  void SetUp() override {
    auto mock_metrics_library =
        std::make_unique<testing::StrictMock<MetricsLibraryMock>>();
    metrics_library_ = mock_metrics_library.get();
    metrics_ = std::make_unique<Metrics>(std::move(mock_metrics_library));
  }

 protected:
  MetricsLibraryMock* metrics_library_;
  std::unique_ptr<Metrics> metrics_;

 private:
  MetricsTest(const MetricsTest&) = delete;
  MetricsTest& operator=(const MetricsTest&) = delete;
};

TEST_F(MetricsTest, Init) {
  EXPECT_CALL(*metrics_library_, Init());
  metrics_->Init();
}

}  // namespace dlcservice
