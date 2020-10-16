// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/process/process_with_output.h"

#include <signal.h>

#include <base/files/file_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <brillo/errors/error_codes.h>

#include "diagnostics/cros_healthd/utils/helper_tool_utils.h"

namespace diagnostics {

namespace {

const char kDBusErrorString[] = "org.chromium.health.error.RunProcess";
const char kInitErrorString[] = "Process initialization failure.";
const char kStartErrorString[] = "Process start failure.";
const char kInputErrorString[] = "Process input write failure.";

}  // namespace

ProcessWithOutput::ProcessWithOutput() : separate_stderr_(false) {}

ProcessWithOutput::~ProcessWithOutput() {
  outfile_.reset();
  errfile_.reset();

  if (!outfile_path_.empty())
    base::DeleteFile(outfile_path_, false);  // not recursive
  if (!errfile_path_.empty())
    base::DeleteFile(errfile_path_, false);
}

bool ProcessWithOutput::Init() {
  return Init({});
}

bool ProcessWithOutput::Init(
    const std::vector<std::string>& minijail_extra_args) {
  if (!SandboxedProcess::Init(minijail_extra_args)) {
    return false;
  }

  outfile_ = base::CreateAndOpenTemporaryStream(&outfile_path_);
  if (!outfile_.get()) {
    return false;
  }
  if (separate_stderr_) {
    errfile_ = base::CreateAndOpenTemporaryStream(&errfile_path_);
    if (!errfile_.get()) {
      return false;
    }
  }

  // We can't just RedirectOutput to the file we just created, since
  // RedirectOutput uses O_CREAT | O_EXCL to open the target file (i.e., it'll
  // fail if the file already exists). We can't CreateTemporaryFile() and then
  // use that filename, since we'd have to remove it before using
  // RedirectOutput, which exposes us to a /tmp race. Instead, bind outfile_'s
  // fd to the subprocess's stdout and stderr.
  BindFd(fileno(outfile_.get()), STDOUT_FILENO);
  BindFd(fileno(separate_stderr_ ? errfile_.get() : outfile_.get()),
         STDERR_FILENO);
  return true;
}

bool ProcessWithOutput::GetOutputLines(std::vector<std::string>* output) const {
  std::string contents;
  if (!GetOutput(&contents))
    return false;

  // If the file contains "a\nb\n", base::SplitString() will return a vector
  // {"a", "b", ""} because it treats "\n" as a delimiter, not an EOL
  // character.  Removing the final "\n" fixes this.
  if (base::EndsWith(contents, "\n", base::CompareCase::SENSITIVE)) {
    contents.pop_back();
  }

  *output = base::SplitString(contents, "\n", base::KEEP_WHITESPACE,
                              base::SPLIT_WANT_ALL);
  return true;
}

bool ProcessWithOutput::GetOutput(std::string* output) const {
  return base::ReadFileToString(outfile_path_, output);
}

bool ProcessWithOutput::GetError(std::string* error) {
  return base::ReadFileToString(errfile_path_, error);
}

int ProcessWithOutput::RunProcess(const std::string& command,
                                  const ArgList& arguments,
                                  bool requires_root,
                                  const std::string* stdin,
                                  std::string* stdout,
                                  std::string* stderr,
                                  brillo::ErrorPtr* error) {
  ProcessWithOutput process;
  if (requires_root) {
    process.SandboxAs("root", "root");
  }
  return DoRunProcess(command, arguments, stdin, stdout, stderr, error,
                      &process);
}

int ProcessWithOutput::DoRunProcess(const std::string& command,
                                    const ArgList& arguments,
                                    const std::string* stdin,
                                    std::string* stdout,
                                    std::string* stderr,
                                    brillo::ErrorPtr* error,
                                    ProcessWithOutput* process) {
  process->set_separate_stderr(true);
  if (!process->Init()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kDBusErrorString, kInitErrorString);
    return kRunError;
  }

  process->AddArg(command);
  for (const auto& argument : arguments) {
    process->AddArg(argument);
  }

  int result = kRunError;
  if (stdin) {
    process->RedirectUsingPipe(STDIN_FILENO, true);
    if (process->Start()) {
      int stdin_fd = process->GetPipe(STDIN_FILENO);
      // Kill the process if writing to or closing the pipe fails.
      if (!base::WriteFileDescriptor(stdin_fd, stdin->c_str(),
                                     stdin->length()) ||
          IGNORE_EINTR(close(stdin_fd)) < 0) {
        process->Kill(SIGKILL, 0);
        brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                             kDBusErrorString, kInputErrorString);
      }
      result = process->Wait();
    } else {
      brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                           kDBusErrorString, kStartErrorString);
    }
  } else {
    result = process->Run();
  }

  if (stdout)
    process->GetOutput(stdout);

  if (stderr)
    process->GetError(stderr);

  return result;
}

}  // namespace diagnostics
