// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <signal.h>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/threading/thread.h>
#include <brillo/daemons/daemon.h>
#include <brillo/flag_helper.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "iioservice/iioservice_simpleclient/daemon.h"
#include "iioservice/iioservice_simpleclient/observer_impl.h"
#include "iioservice/include/common.h"

namespace {

std::atomic<bool> daemon_running(false);
std::unique_ptr<iioservice::TestDaemon> exec_daemon;

void quit_daemon() {
  if (!daemon_running)
    return;

  daemon_running = false;
  LOG(INFO) << "Quiting daemon";
  exec_daemon->Quit();
}

void signal_handler_stop(int signal) {
  LOG(INFO) << "Signal: " << signal;

  quit_daemon();
}
}  // namespace

int main(int argc, char** argv) {
  DEFINE_int32(device_id, -1, "The IIO device id to test.");
  DEFINE_int32(device_type, 0,
               "The IIO device type to test. It follows the mojo interface's "
               "order: NONE: 0, ACCEL: 1, ANGLVEL: 2, LIGHT: 3, COUNT: 4, "
               "MAGN: 5, ANGL: 6, ACPI_ALS: 7, BARO: 8");
  DEFINE_string(channels, "", "Specify space separated channels to be enabled");
  DEFINE_double(frequency, -1.0, "frequency in Hz set to the device.");
  DEFINE_uint64(timeout, 1000, "Timeout for I/O operations. 0 as no timeout");

  brillo::FlagHelper::Init(argc, argv, "Chromium OS iioservice_simpleclient");
  logging::LoggingSettings settings;
  LOG_ASSERT(logging::InitLogging(settings));

  std::vector<std::string> channel_ids = base::SplitString(
      FLAGS_channels, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  if (FLAGS_device_id == -1 && FLAGS_device_type == 0) {
    LOG(ERROR)
        << "iioservice_simpleclient must be called with a sensor specified.";
    exit(1);
  }
  if (FLAGS_frequency < 0.0) {
    LOG(ERROR) << "iioservice_simpleclient must be called with frequency set.";
    exit(1);
  }
  if (channel_ids.empty()) {
    LOG(ERROR) << "iioservice_simpleclient must be called with at least one "
                  "channel enabled.";
    exit(1);
  }

  exec_daemon = std::make_unique<iioservice::TestDaemon>(
      FLAGS_device_id, static_cast<cros::mojom::DeviceType>(FLAGS_device_type),
      std::move(channel_ids), FLAGS_frequency, FLAGS_timeout);
  signal(SIGTERM, signal_handler_stop);
  signal(SIGINT, signal_handler_stop);
  daemon_running = true;
  exec_daemon->Run();
  daemon_running = false;
}
