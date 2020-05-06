// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_METRICS_H_
#define DLCSERVICE_METRICS_H_

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <metrics/metrics_library.h>

#include "dlcservice/error.h"

namespace dlcservice {

// Performs UMA metrics logging for the dlcservice daemon.
class Metrics {
 public:
  explicit Metrics(std::unique_ptr<MetricsLibraryInterface> metrics_library)
      : metrics_library_(std::move(metrics_library)) {}

  ~Metrics() = default;

  // Initializes the class.
  void Init();

 private:
  std::unique_ptr<MetricsLibraryInterface> metrics_library_;

  // Not copyable or movable.
  Metrics(const Metrics&) = delete;
  Metrics& operator=(const Metrics&) = delete;
};

}  // namespace dlcservice

#endif  // DLCSERVICE_METRICS_H_
