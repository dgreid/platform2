// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/memory/memory.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>
#include <re2/re2.h>

#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/cros_healthd/routines/memory/memory_constants.h"

namespace diagnostics {

namespace {

namespace executor_ipc = chromeos::cros_healthd_executor::mojom;
namespace mojo_ipc = chromeos::cros_healthd::mojom;

// Approximate number of microseconds per byte of memory tested. Derived from
// testing on a nami device.
constexpr double kMicrosecondsPerByte = 0.20;

// Regex to parse out the version of memtester used.
constexpr char kMemtesterVersionRegex[] = R"(memtester version (.+))";
// Regex to parse out the amount of memory tested.
constexpr char kMemtesterBytesTestedRegex[] = R"(got  \d+MB \((\d+) bytes\))";
// Regex to parse out a particular subtest and its result.
constexpr char kMemtesterSubtestRegex[] = R"((.+)\s*: (.+))";
// Failure messages in subtests will begin with this pattern.
constexpr char kMemtesterSubtestFailurePattern[] = "FAILURE:";

// Takes a |raw_string|, potentially with backspace characters ('\b'), and
// processes the backspaces in the string like the console would. For example,
// if |raw_string| was "Hello, Worlb\bd\n", this function would return
// "Hello, World\n". This function operates a single character at a time, and
// should only be used for small inputs.
std::string ProcessBackspaces(const std::string& raw_string) {
  std::string processed;
  for (const char& c : raw_string) {
    if (c == '\b') {
      // std::string::pop_back() causes undefined behavior if the string is
      // empty. We never expect to call this method on an empty string - that
      // would indicate |raw_string| was invalid.
      DCHECK(!processed.empty());
      processed.pop_back();
    } else {
      processed.push_back(c);
    }
  }

  return processed;
}

}  // namespace

MemoryRoutine::MemoryRoutine(Context* context,
                             const base::TickClock* tick_clock)
    : context_(context),
      status_(mojo_ipc::DiagnosticRoutineStatusEnum::kReady) {
  DCHECK(context_);

  if (tick_clock) {
    tick_clock_ = tick_clock;
  } else {
    default_tick_clock_ = std::make_unique<base::DefaultTickClock>();
    tick_clock_ = default_tick_clock_.get();
  }
  DCHECK(tick_clock_);
}

MemoryRoutine::~MemoryRoutine() = default;

void MemoryRoutine::Start() {
  DCHECK_EQ(status_, mojo_ipc::DiagnosticRoutineStatusEnum::kReady);

  // Estimate the routine's duration based on the amount of free memory.
  expected_duration_us_ =
      base::SysInfo::AmountOfAvailablePhysicalMemory() * kMicrosecondsPerByte;
  start_ticks_ = tick_clock_->NowTicks();

  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  status_message_ = kMemoryRoutineRunningMessage;
  context_->executor()->RunMemtester(base::BindOnce(
      &MemoryRoutine::DetermineRoutineResult, weak_ptr_factory_.GetWeakPtr()));
}

// The memory routine can only be started.
void MemoryRoutine::Resume() {}
void MemoryRoutine::Cancel() {}

void MemoryRoutine::PopulateStatusUpdate(mojo_ipc::RoutineUpdate* response,
                                         bool include_output) {
  DCHECK(response);

  // Because the memory routine is non-interactive, we will never include a user
  // message.
  mojo_ipc::NonInteractiveRoutineUpdate update;
  update.status = status_;
  update.status_message = status_message_;

  response->routine_update_union->set_noninteractive_update(update.Clone());

  if (include_output && !output_dict_.DictEmpty()) {
    std::string json;
    base::JSONWriter::WriteWithOptions(
        output_dict_, base::JSONWriter::Options::OPTIONS_PRETTY_PRINT, &json);
    response->output =
        CreateReadOnlySharedMemoryRegionMojoHandle(base::StringPiece(json));
  }

  // If the routine has finished, set the progress percent to 100 and don't take
  // the amount of time ran into account.
  if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kPassed ||
      status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kFailed) {
    response->progress_percent = 100;
    return;
  }

  if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kReady) {
    // The routine has not started.
    response->progress_percent = 0;
    return;
  }

  // Cap the progress at 99, in case it's taking longer than the estimated
  // time.
  base::TimeDelta elapsed_time = tick_clock_->NowTicks() - start_ticks_;
  response->progress_percent =
      std::min<int64_t>(99, static_cast<int64_t>(elapsed_time.InMicroseconds() /
                                                 expected_duration_us_ * 100));
}

mojo_ipc::DiagnosticRoutineStatusEnum MemoryRoutine::GetStatus() {
  return status_;
}

void MemoryRoutine::DetermineRoutineResult(
    executor_ipc::ProcessResultPtr process) {
  ParseMemtesterOutput(process->out);

  int32_t ret = process->return_code;
  if (ret == EXIT_SUCCESS) {
    status_message_ = kMemoryRoutineSucceededMessage;
    status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
    return;
  }

  std::string status_message;
  if (ret & MemtesterErrorCodes::kAllocatingLockingInvokingError)
    status_message += kMemoryRoutineAllocatingLockingInvokingFailureMessage;

  if (ret & MemtesterErrorCodes::kStuckAddressTestError)
    status_message += kMemoryRoutineStuckAddressTestFailureMessage;

  if (ret & MemtesterErrorCodes::kOtherTestError)
    status_message += kMemoryRoutineOtherTestFailureMessage;

  status_message_ = std::move(status_message);
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
}

void MemoryRoutine::ParseMemtesterOutput(const std::string& raw_output) {
  std::vector<std::string> lines = base::SplitString(
      raw_output, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  // The following strings are used to hold values matched from regexes.
  std::string version;
  std::string bytes_tested_str;
  std::string subtest_name;
  std::string subtest_result;
  // Holds the integer value for the number of bytes tested, converted from
  // |bytes_tested_str|.
  uint64_t bytes_tested;
  // Holds the results of all subtests.
  base::Value subtest_dict(base::Value::Type::DICTIONARY);
  // Holds all the parsed output from memtester.
  base::Value result_dict(base::Value::Type::DICTIONARY);
  for (int index = 0; index < lines.size(); index++) {
    std::string line = ProcessBackspaces(lines[index]);

    if (RE2::FullMatch(line, kMemtesterVersionRegex, &version)) {
      result_dict.SetStringKey("memtesterVersion", version);
    } else if (RE2::PartialMatch(line, kMemtesterBytesTestedRegex,
                                 &bytes_tested_str) &&
               base::StringToUint64(bytes_tested_str, &bytes_tested)) {
      result_dict.SetIntKey("bytesTested", static_cast<int>(bytes_tested));
    } else if (RE2::FullMatch(line, kMemtesterSubtestRegex, &subtest_name,
                              &subtest_result) &&
               !base::StartsWith(line, kMemtesterSubtestFailurePattern,
                                 base::CompareCase::SENSITIVE)) {
      // Process |subtest_name| so it's formatted like a JSON key - remove
      // spaces and make sure the first letter is lowercase. For example, Stuck
      // Address should become stuckAddress.
      base::RemoveChars(subtest_name, " ", &subtest_name);
      subtest_name[0] = std::tolower(subtest_name[0]);

      subtest_dict.SetStringKey(subtest_name, subtest_result);
    }
  }

  if (!subtest_dict.DictEmpty())
    result_dict.SetKey("subtests", std::move(subtest_dict));
  if (!result_dict.DictEmpty())
    output_dict_.SetKey("resultDetails", std::move(result_dict));
}

}  // namespace diagnostics
