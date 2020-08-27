// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SALSA_TRY_TOUCH_EXPERIMENT_SALSA_EXPERIMENT_RUNNER_H_
#define SALSA_TRY_TOUCH_EXPERIMENT_SALSA_EXPERIMENT_RUNNER_H_

#include <string>

#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <ncurses.h>

#include "salsa/try_touch_experiment/experiment.h"

class SalsaExperimentRunner {
 public:
  bool LoadExperiment(const std::string& exp_string);
  void run() const;

 private:
  static void StartCurses();
  static void EndCurses();
  std::string Decode(const std::string& exp_string) const;

  Experiment exp_;
};

#endif  // SALSA_TRY_TOUCH_EXPERIMENT_SALSA_EXPERIMENT_RUNNER_H_
