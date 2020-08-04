// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/logging.h>

#include "image-burner/image_burner.h"
#include "image-burner/image_burner_impl.h"
#include "image-burner/image_burner_utils.h"

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);

  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_ALL;
#if BASE_VER < 780000
  settings.log_file = "/var/log/image_burner.log";
#else
  settings.log_file_path = "/var/log/image_burner.log";
#endif
  settings.lock_log = logging::LOCK_LOG_FILE;
  settings.delete_old = logging::DELETE_OLD_LOG_FILE;
  logging::InitLogging(settings);

  imageburn::BurnWriter writer;
  imageburn::BurnReader reader;
  imageburn::BurnPathGetter path_getter;
  std::unique_ptr<imageburn::BurnerImpl> burner(
      new imageburn::BurnerImpl(&writer, &reader, NULL, &path_getter));

  imageburn::ImageBurnService service(burner.get());
  service.Initialize();
  burner->InitSignalSender(&service);

  service.Register(brillo::dbus::GetSystemBusConnection());
  service.Run();
  return 0;
}
