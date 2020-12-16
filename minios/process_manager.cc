// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/process_manager.h"

using std::string;
using std::vector;

std::unique_ptr<brillo::Process> ProcessManager::CreateProcess(
    const vector<string>& cmd) {
  std::unique_ptr<brillo::Process> process(new brillo::ProcessImpl);
  for (const auto& arg : cmd)
    process->AddArg(arg);
  return process;
}

int ProcessManager::RunCommand(const vector<string>& cmd,
                               const string& input_file,
                               const string& output_file) {
  auto process = CreateProcess(cmd);
  if (!input_file.empty())
    process->RedirectInput(input_file);
  if (!output_file.empty())
    process->RedirectOutput(output_file);
  return process->Run();
}
