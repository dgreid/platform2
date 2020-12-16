// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_PROCESS_MANAGER_H_
#define MINIOS_PROCESS_MANAGER_H_

#include <memory>
#include <string>
#include <vector>

#include <brillo/process/process.h>

class ProcessManager {
 public:
  ProcessManager() = default;
  ~ProcessManager() = default;

  // Runs the command line with input and output redirected and returns the exit
  // code. Input and output files will be ignored if strings are empty.
  int RunCommand(const std::vector<std::string>& cmd,
                 const std::string& intput_file,
                 const std::string& output_file);

 private:
  ProcessManager(const ProcessManager&) = delete;
  ProcessManager& operator=(const ProcessManager&) = delete;

  std::unique_ptr<brillo::Process> CreateProcess(
      const std::vector<std::string>& cmd);
};

#endif  // MINIOS_PROCESS_MANAGER_H_
