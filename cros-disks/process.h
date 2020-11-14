// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_PROCESS_H_
#define CROS_DISKS_PROCESS_H_

#include <string>
#include <utility>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/strings/string_piece.h>

#include <gtest/gtest_prod.h>

namespace cros_disks {

// A base class for executing a process.
//
// TODO(crbug.com/1003654) This base class is not feature complete yet.
class Process {
 public:
  // Invalid process ID assigned to a process that has not started.
  static const pid_t kInvalidProcessId;

  static const int kInvalidFD;

  Process(const Process&) = delete;
  Process& operator=(const Process&) = delete;

  virtual ~Process();

  // Adds an argument to the end of the argument list.
  // Precondition: Start() has not been called yet.
  void AddArgument(std::string argument);

  // Adds a variable to the environment that will be passed to the process.
  // Precondition: Start() has not been called yet.
  // Precondition: `name` is not empty and doesn't contain '='.
  void AddEnvironmentVariable(base::StringPiece name, base::StringPiece value);

  // Sets the string to pass to the process stdin.
  // Might be silently truncated if it doesn't fit in a pipe's buffer.
  // Precondition: Start() has not been called yet.
  void SetStdIn(std::string input) { input_ = std::move(input); }

  // Starts the process. The started process has its stdin, stdout and stderr
  // connected to /dev/null. Returns true in case of success. Once started, the
  // process can be waiting for to finish using Wait().
  bool Start();

  // Waits for the process to finish and returns its exit status.
  int Wait();

  // Checks if the process finished.
  bool IsFinished();

  // Starts a process, captures its output and waits for it to finish. Returns
  // the same exit status as Wait().
  //
  // Precondition: output is non-null
  int Run(std::vector<std::string>* output);

  pid_t pid() const { return pid_; }

  const std::vector<std::string>& arguments() const { return arguments_; }
  const std::vector<std::string>& environment() const { return environment_; }
  const std::string& input() const { return input_; }

 protected:
  Process();

  // Gets the arguments used to start the process. This method calls
  // BuildArgumentsArray() to build |arguments_array_| only once (i.e. when
  // |arguments_array_| is empty). Once |arguments_array_| is built, subsequent
  // calls to AddArgument() are not allowed. The returned array of arguments is
  // owned by this Process object.
  char* const* GetArguments();

  // Gets the environment to pass to the subprocess. The returned array of
  // environment variables is owned by this Process object.
  char* const* GetEnvironment();

  // Starts a process, and connects to its stdin, stdout and stderr the given
  // file descriptors.
  //
  // Returns the PID of the started process, or -1 in case of error.
  virtual pid_t StartImpl(base::ScopedFD in_fd,
                          base::ScopedFD out_fd,
                          base::ScopedFD err_fd) = 0;

  // Once either WaitImpl() or WaitNonBlockingImpl() has returned a nonnegative
  // exit status, none of these methods is called again.

  // Waits for the process to finish and returns its nonnegative exit status.
  virtual int WaitImpl() = 0;

  // Checks if the process has finished and returns its nonnegative exit status,
  // or -1 if the process is still running.
  virtual int WaitNonBlockingImpl() = 0;

 private:
  // Starts the process. The started process has its stdin, stdout and stderr
  // redirected to the given file descriptors. Returns true in case of success.
  bool Start(base::ScopedFD in_fd,
             base::ScopedFD out_fd,
             base::ScopedFD err_fd);

  // Waits for process to finish collecting process' stdout and stderr
  // output and fills interleaved version of it.
  void Communicate(std::vector<std::string>* output,
                   base::ScopedFD out_fd,
                   base::ScopedFD err_fd);

  // Builds |arguments_array_| from |arguments_|. Existing values of
  // |arguments_array_| are overridden.
  void BuildArgumentsArray();

  bool finished() const { return status_ >= 0; }

  // Process arguments.
  std::vector<std::string> arguments_;
  std::vector<char*> arguments_array_;

  // Extra environment variables.
  std::vector<std::string> environment_;

  // Full environment for the subprocess.
  std::vector<char*> environment_array_;

  // String to pass to the process stdin.
  std::string input_;

  // Process ID (default to kInvalidProcessId when the process has not started).
  pid_t pid_ = kInvalidProcessId;

  // Exit status. A nonnegative value indicates that the process has finished.
  int status_ = -1;

  FRIEND_TEST(ProcessTest, GetArguments);
  FRIEND_TEST(ProcessTest, GetArgumentsWithNoArgumentsAdded);
};

}  // namespace cros_disks

#endif  // CROS_DISKS_PROCESS_H_
