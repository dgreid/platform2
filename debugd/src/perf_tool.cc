// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/perf_tool.h"

#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <base/bind.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>

#include "debugd/src/error_utils.h"
#include "debugd/src/process_with_output.h"

namespace debugd {

namespace {

const char kUnsupportedPerfToolErrorName[] =
    "org.chromium.debugd.error.UnsupportedPerfTool";
const char kProcessErrorName[] = "org.chromium.debugd.error.RunProcess";
const char kStopProcessErrorName[] = "org.chromium.debugd.error.StopProcess";

const char kArgsError[] =
    "perf_args must begin with {\"perf\", \"record\"}, "
    " {\"perf\", \"stat\"}, or {\"perf\", \"mem\"}";

// Location of quipper on ChromeOS.
const char kQuipperLocation[] = "/usr/bin/quipper";

enum PerfSubcommand {
  PERF_COMMAND_RECORD,
  PERF_COMMAND_STAT,
  PERF_COMMAND_MEM,
  PERF_COMMAND_UNSUPPORTED,
};

// Returns one of the above enums given an vector of perf arguments, starting
// with "perf" itself in |args[0]|.
PerfSubcommand GetPerfSubcommandType(const std::vector<std::string>& args) {
  if (args[0] == "perf" && args.size() > 1) {
    if (args[1] == "record")
      return PERF_COMMAND_RECORD;
    if (args[1] == "stat")
      return PERF_COMMAND_STAT;
    if (args[1] == "mem")
      return PERF_COMMAND_MEM;
  }

  return PERF_COMMAND_UNSUPPORTED;
}

void AddQuipperArguments(brillo::Process* process,
                         const uint32_t duration_secs,
                         const std::vector<std::string>& perf_args) {
  process->AddArg(kQuipperLocation);
  process->AddArg(base::StringPrintf("%u", duration_secs));
  for (const auto& arg : perf_args) {
    process->AddArg(arg);
  }
}

}  // namespace

PerfTool::PerfTool() {
  signal_handler_.Init();
  process_reaper_.Register(&signal_handler_);
}

bool PerfTool::GetPerfOutput(uint32_t duration_secs,
                             const std::vector<std::string>& perf_args,
                             std::vector<uint8_t>* perf_data,
                             std::vector<uint8_t>* perf_stat,
                             int32_t* status,
                             brillo::ErrorPtr* error) {
  PerfSubcommand subcommand = GetPerfSubcommandType(perf_args);
  if (subcommand == PERF_COMMAND_UNSUPPORTED) {
    DEBUGD_ADD_ERROR(error, kUnsupportedPerfToolErrorName, kArgsError);
    return false;
  }

  // This whole method is synchronous, so we create a subprocess, let it run to
  // completion, then gather up its output to return it.
  ProcessWithOutput process;
  process.SandboxAs("root", "root");
  if (!process.Init()) {
    DEBUGD_ADD_ERROR(error, kProcessErrorName,
                     "Process initialization failure.");
    return false;
  }

  AddQuipperArguments(&process, duration_secs, perf_args);

  std::string output_string;
  *status = process.Run();
  if (*status != 0) {
    output_string =
        base::StringPrintf("<process exited with status: %d>", *status);
  } else {
    process.GetOutput(&output_string);
  }

  switch (subcommand) {
    case PERF_COMMAND_RECORD:
    case PERF_COMMAND_MEM:
      perf_data->assign(output_string.begin(), output_string.end());
      break;
    case PERF_COMMAND_STAT:
      perf_stat->assign(output_string.begin(), output_string.end());
      break;
    default:
      // Discard the output.
      break;
  }

  return true;
}

void PerfTool::OnQuipperProcessExited(const siginfo_t& siginfo) {
  // Called after SIGCHLD has been received from the signalfd file descriptor.
  // Wait() for the child process wont' block. It'll just reap the zombie child
  // process.
  quipper_process_->Wait();
  quipper_process_ = nullptr;
  quipper_process_output_fd_.reset();

  profiler_session_id_.reset();
}

bool PerfTool::GetPerfOutputFd(uint32_t duration_secs,
                               const std::vector<std::string>& perf_args,
                               const base::ScopedFD& stdout_fd,
                               uint64_t* session_id,
                               brillo::ErrorPtr* error) {
  PerfSubcommand subcommand = GetPerfSubcommandType(perf_args);
  if (subcommand == PERF_COMMAND_UNSUPPORTED) {
    DEBUGD_ADD_ERROR(error, kUnsupportedPerfToolErrorName, kArgsError);
    return false;
  }

  if (quipper_process_) {
    // Do not run multiple sessions at the same time. Attempt to start another
    // profiler session using this method yields a DBus error. Note that
    // starting another session using GetPerfOutput() will still succeed.
    DEBUGD_ADD_ERROR(error, kProcessErrorName, "Existing perf tool running.");
    return false;
  }

  DCHECK(!profiler_session_id_);

  quipper_process_ = std::make_unique<SandboxedProcess>();
  quipper_process_->SandboxAs("root", "root");
  if (!quipper_process_->Init()) {
    DEBUGD_ADD_ERROR(error, kProcessErrorName,
                     "Process initialization failure.");
    return false;
  }

  AddQuipperArguments(quipper_process_.get(), duration_secs, perf_args);
  quipper_process_->BindFd(stdout_fd.get(), 1);

  if (!quipper_process_->Start()) {
    DEBUGD_ADD_ERROR(error, kProcessErrorName, "Process start failure.");
    return false;
  }
  DCHECK_GT(quipper_process_->pid(), 0);

  process_reaper_.WatchForChild(
      FROM_HERE, quipper_process_->pid(),
      base::Bind(&PerfTool::OnQuipperProcessExited, base::Unretained(this)));

  // When GetPerfOutputFd() is used to run the perf tool, the user will read
  // from the read end of |stdout_fd| until the write end is closed.  At that
  // point, it may make another call to GetPerfOutputFd() and expect that will
  // start another perf run. |stdout_fd| will be closed when the last process
  // holding it exits, which is minijail0 in this case. However, the kernel
  // closes fds before signaling process exit. Therefore, it's possible for
  // |stdout_fd| to be closed and the user tries to run another
  // GetPerfOutputFd() before we're signaled of the process exit. To mitigate
  // this, hold on to a dup() of |stdout_fd| until we're signaled that the
  // process has exited. This guarantees that the caller can make a new
  // GetPerfOutputFd() call when it finishes reading the output.
  quipper_process_output_fd_.reset(dup(stdout_fd.get()));
  DCHECK(quipper_process_output_fd_.is_valid());

  // Generate an opaque, pseudo-unique, session ID using time and process ID.
  profiler_session_id_ = *session_id =
      static_cast<uint64_t>(base::Time::Now().ToTimeT()) << 32 |
      (quipper_process_->pid() & 0xffffffff);

  return true;
}

bool PerfTool::StopPerf(uint64_t session_id, brillo::ErrorPtr* error) {
  if (!profiler_session_id_) {
    DEBUGD_ADD_ERROR(error, kStopProcessErrorName, "Perf tool not started");
    return false;
  }

  if (profiler_session_id_ != session_id) {
    // Session ID mismatch: return a failure without affecting the existing
    // profiler session.
    DEBUGD_ADD_ERROR(error, kStopProcessErrorName,
                     "Invalid profile session id.");
    return false;
  }

  // Stop by sending SIGINT to the profiler session. The sandboxed quipper
  // process will be reaped in OnQuipperProcessExited().
  if (quipper_process_) {
    DCHECK_GT(quipper_process_->pid(), 0);
    if (kill(quipper_process_->pid(), SIGINT) != 0) {
      PLOG(WARNING) << "Failed to stop the profiler session.";
    }
  }

  return true;
}

}  // namespace debugd
