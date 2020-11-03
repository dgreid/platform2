// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SIMPLE_ROUTINE_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SIMPLE_ROUTINE_H_

#include <string>

#include <base/callback.h>
#include <base/values.h>

#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

// Provides a convenient way to construct a simple routine. If your routine has
// any of the following features, this class should NOT be used:
// * User interaction - simple routines are all non-interactive.
// * Running subprocesses - use SubprocRoutine instead.
// * Long runtime - simple routines cannot be cancelled, so only short-lived
//                  routines should use this class.
//
// Adding a new simple routine could be done as follows:
//
// (Header file)
// std::unique_ptr<DiagnosticRoutine> CreateNewSimpleRoutine(Params params);
//
// (Implementation file)
// void DoRoutineWork(
//   Params params,
//   chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum* status,
//   std::string* status_message,
//   base::Value* output_dict) {
//     // Routine-specific logic goes here.
// }
//
// std::unique_ptr<DiagnosticRoutine> CreateNewSimpleRoutine(Params params) {
//   return std::make_unique<SimpleRoutine>(
//       base::BindOnce(&DoRoutineWork, Params));
// }
class SimpleRoutine final : public DiagnosticRoutine {
 public:
  using Task = base::OnceCallback<void(
      chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum* status,
      std::string* status_message,
      base::Value* output_dict)>;

  explicit SimpleRoutine(Task task);
  SimpleRoutine(const SimpleRoutine&) = delete;
  SimpleRoutine& operator=(const SimpleRoutine&) = delete;

  // DiagnosticRoutine overrides:
  ~SimpleRoutine() override;
  void Start() override;
  void Resume() override;
  void Cancel() override;
  void PopulateStatusUpdate(
      chromeos::cros_healthd::mojom::RoutineUpdate* response,
      bool include_output) override;
  chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum GetStatus()
      override;

 private:
  // Task encapsulating the logic of the routine to run.
  Task task_;

  chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum status_;
  std::string status_message_;
  base::Value output_dict_{base::Value::Type::DICTIONARY};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SIMPLE_ROUTINE_H_
