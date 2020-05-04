// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <utility>

#include <base/at_exit.h>
#include <base/bind.h>
#include <base/logging.h>
#include <base/time/time.h>
#include <brillo/flag_helper.h>
#include <brillo/message_loops/base_message_loop.h>
#include <brillo/syslog_logging.h>

#include "diagnostics/cros_healthd_event_tool/event_subscriber.h"

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

enum class EventCategory {
  kPower,
  kBluetooth,
};

constexpr std::pair<const char*, EventCategory> kCategorySwitches[] = {
    {"power", EventCategory::kPower},
    {"bluetooth", EventCategory::kBluetooth},
};

// Create a stringified list of the category names for use in help.
std::string GetCategoryHelp() {
  std::stringstream ss;
  ss << "Category of events to subscribe to: [";
  const char* sep = "";
  for (auto pair : kCategorySwitches) {
    ss << sep << pair.first;
    sep = ", ";
  }
  ss << "]";
  return ss.str();
}

}  // namespace

// 'cros-health-event' command-line tool:
//
// Test driver for cros_healthd's event subscription. Supports subscribing to a
// single category of events at a time.
int main(int argc, char** argv) {
  std::string category_help = GetCategoryHelp();
  DEFINE_string(category, "", category_help.c_str());
  DEFINE_uint32(length_seconds, 10, "Number of seconds to listen for events.");
  brillo::FlagHelper::Init(argc, argv,
                           "event - Device event subscription tool.");
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  base::AtExitManager at_exit_manager;

  std::map<std::string, EventCategory> switch_to_category(
      std::begin(kCategorySwitches), std::end(kCategorySwitches));

  logging::InitLogging(logging::LoggingSettings());

  brillo::BaseMessageLoop message_loop;

  // Make sure at least one category is specified.
  if (FLAGS_category == "") {
    LOG(ERROR) << "No category specified.";
    return EXIT_FAILURE;
  }
  // Validate the category flag.
  auto iterator = switch_to_category.find(FLAGS_category);
  if (iterator == switch_to_category.end()) {
    LOG(ERROR) << "Invalid category: " << FLAGS_category;
    return EXIT_FAILURE;
  }

  // Subscribe to the specified category.
  diagnostics::EventSubscriber event_subscriber;
  switch (iterator->second) {
    case EventCategory::kPower:
      event_subscriber.SubscribeToPowerEvents();
      break;
    case EventCategory::kBluetooth:
      event_subscriber.SubscribeToBluetoothEvents();
      break;
  }

  // Schedule an exit after |FLAGS_length_seconds|.
  message_loop.PostDelayedTask(
      FROM_HERE,
      base::BindOnce([](brillo::BaseMessageLoop* loop) { loop->BreakLoop(); },
                     &message_loop),
      base::TimeDelta::FromSeconds(FLAGS_length_seconds));

  message_loop.Run();

  return EXIT_SUCCESS;
}
