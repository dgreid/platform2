// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This class is most definitely NOT re-entrant.

#include "login_manager/generator_job.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include <map>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

#include "login_manager/system_utils.h"

namespace login_manager {
namespace {
const char kKeygenExecutable[] = "/sbin/keygen";
}  // namespace

GeneratorJobFactoryInterface::~GeneratorJobFactoryInterface() {}

GeneratorJob::Factory::Factory() {}
GeneratorJob::Factory::~Factory() {}

scoped_ptr<GeneratorJobInterface> GeneratorJob::Factory::Create(
    const std::string& filename,
    const base::FilePath& user_path,
    uid_t desired_uid,
    SystemUtils* utils) {
  return scoped_ptr<GeneratorJobInterface>(new GeneratorJob(filename,
                                                            user_path,
                                                            desired_uid,
                                                            utils));
}

GeneratorJob::GeneratorJob(const std::string& filename,
                           const base::FilePath& user_path,
                           uid_t desired_uid,
                           SystemUtils* utils)
    : filename_(filename),
      user_path_(user_path.value()),
      system_(utils),
      subprocess_(desired_uid, system_) {
}

GeneratorJob::~GeneratorJob() {}

bool GeneratorJob::RunInBackground() {
  std::vector<std::string> argv;
  argv.push_back(kKeygenExecutable);
  argv.push_back(filename_);
  argv.push_back(user_path_);

  return subprocess_.ForkAndExec(argv, std::map<std::string, std::string>());
}

void GeneratorJob::KillEverything(int signal, const std::string& message) {
  if (subprocess_.pid() < 0)
    return;
  subprocess_.KillEverything(signal);
}

void GeneratorJob::Kill(int signal, const std::string& message) {
  if (subprocess_.pid() < 0)
    return;
  subprocess_.Kill(signal);
}

void GeneratorJob::WaitAndAbort(base::TimeDelta timeout) {
  if (subprocess_.pid() < 0)
    return;
  if (!system_->ProcessGroupIsGone(subprocess_.pid(), timeout))
    KillEverything(SIGABRT, std::string());
  else
    DLOG(INFO) << "Cleaned up child " << subprocess_.pid();
}

const std::string GeneratorJob::GetName() const {
  base::FilePath exec_file(kKeygenExecutable);
  return exec_file.BaseName().value();
}

}  // namespace login_manager
