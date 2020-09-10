// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This is a utility to clear internal crypto entropy (if applicable) from
// |BiometricsManager|s, so as to render useless templates and other user data
// encrypted with old secrets.

#include <sys/types.h>
#include <unistd.h>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/logging.h>
#include <base/process/process.h>
#include <base/task/single_thread_task_executor.h>
#include <base/time/time.h>
#include <brillo/flag_helper.h>
#include <dbus/bus.h>

#include "biod/biod_version.h"
#include "biod/cros_fp_biometrics_manager.h"
#include "biod/cros_fp_device.h"
#include "biod/power_button_filter.h"

namespace {

static int64_t kTimeoutSeconds = 30;

constexpr char kHelpMessage[] = "bio_wash resets the SBP.";

int DoBioWash(const bool factory_init = false) {
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  base::FileDescriptorWatcher watcher(task_executor.task_runner());
  std::vector<std::unique_ptr<biod::BiometricsManager>> managers;
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  // It's o.k to not connect to the bus as we don't really care about D-Bus
  // events for BioWash.
  auto bus = base::MakeRefCounted<dbus::Bus>(options);
  auto biod_metrics = std::make_unique<biod::BiodMetrics>();
  // Add all the possible BiometricsManagers available.
  auto cros_fp_bio = std::make_unique<biod::CrosFpBiometricsManager>(
      biod::PowerButtonFilter::Create(bus),
      biod::CrosFpDevice::Create(biod_metrics.get(),
                                 std::make_unique<biod::EcCommandFactory>()),
      std::move(biod_metrics));
  if (cros_fp_bio) {
    managers.emplace_back(std::move(cros_fp_bio));
  }

  if (managers.empty()) {
    LOG(ERROR) << "No biometrics managers instantiated correctly.";
    return -1;
  }

  int ret = 0;
  for (const auto& biometrics_manager : managers) {
    if (!biometrics_manager->ResetEntropy(factory_init)) {
      LOG(ERROR) << "Failed to reset entropy for sensor type: "
                 << biometrics_manager->GetType();
      ret = -1;
    }
  }

  return ret;
}

}  // namespace

int main(int argc, char* argv[]) {
  DEFINE_bool(factory_init, false, "First time initialisation in the factory.");

  brillo::FlagHelper::Init(argc, argv, kHelpMessage);

  biod::LogVersion();

  pid_t pid;
  pid = fork();

  if (pid == -1) {
    PLOG(ERROR) << "Failed to fork child process for bio_wash.";
    return -1;
  }

  if (pid == 0) {
    return DoBioWash(FLAGS_factory_init);
  }

  auto process = base::Process::Open(pid);
  int exit_code;
  if (!process.WaitForExitWithTimeout(
          base::TimeDelta::FromSeconds(kTimeoutSeconds), &exit_code)) {
    LOG(ERROR) << "Bio wash timeout out, exit code: " << exit_code;
    process.Terminate(-1, false);
  }

  return exit_code;
}
