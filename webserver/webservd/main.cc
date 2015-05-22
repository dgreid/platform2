// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <sysexits.h>

#include <base/command_line.h>
#include <base/files/file_util.h>
#include <chromeos/dbus/async_event_sequencer.h>
#include <chromeos/dbus/exported_object_manager.h>
#include <chromeos/daemons/dbus_daemon.h>
#include <chromeos/flag_helper.h>
#include <chromeos/minijail/minijail.h>
#include <chromeos/syslog_logging.h>

#include "webserver/webservd/config.h"
#include "webserver/webservd/log_manager.h"
#include "webserver/webservd/server.h"
#include "webserver/webservd/utils.h"

using chromeos::dbus_utils::AsyncEventSequencer;

namespace {

const char kDefaultConfigFilePath[] = "/etc/webservd/config";
const char kServiceName[] = "org.chromium.WebServer";
const char kRootServicePath[] = "/org/chromium/WebServer";
const char kWebServerUserName[] = "webservd";
const char kWebServerGroupName[] = "webservd";

class Daemon final : public chromeos::DBusServiceDaemon {
 public:
  explicit Daemon(webservd::Config config)
      : DBusServiceDaemon{kServiceName, kRootServicePath},
        config_{std::move(config)} {}

 protected:
  void RegisterDBusObjectsAsync(AsyncEventSequencer* sequencer) override {
    webservd::LogManager::Init(base::FilePath{config_.log_directory});
    server_.reset(new webservd::Server{object_manager_.get(), config_});
    server_->RegisterAsync(
        sequencer->GetHandler("Server.RegisterAsync() failed.", true));
  }

  void OnShutdown(int* return_code) override {
    server_.reset();
  }

 private:
  webservd::Config config_;
  std::unique_ptr<webservd::Server> server_;

  DISALLOW_COPY_AND_ASSIGN(Daemon);
};

}  // namespace

int main(int argc, char* argv[]) {
  DEFINE_bool(log_to_stderr, false, "log trace messages to stderr as well");
  DEFINE_string(config_path, "",
                "path to a file containing server configuration");
  DEFINE_bool(debug, false,
              "return debug error information in web requests");
  chromeos::FlagHelper::Init(argc, argv, "Brillo web server daemon");

  int flags = chromeos::kLogToSyslog;
  if (FLAGS_log_to_stderr)
    flags |= chromeos::kLogToStderr;
  chromeos::InitLog(flags | chromeos::kLogHeader);

  webservd::Config config;
  base::FilePath default_file_path{kDefaultConfigFilePath};
  if (!FLAGS_config_path.empty()) {
    // In tests, we'll override the board specific and default configurations
    // with a test specific configuration.
    webservd::LoadConfigFromFile(base::FilePath{FLAGS_config_path}, &config);
  } else if (base::PathExists(default_file_path)) {
    // Some boards have a configuration they will want to use to override
    // our defaults.  Part of our interface is to look for this in a
    // standard location.
    CHECK(webservd::LoadConfigFromFile(default_file_path, &config));
  } else {
    webservd::LoadDefaultConfig(&config);
  }

  // For protocol handlers bound to specific network interfaces, we need root
  // access to create those bound sockets. Do that here before we drop
  // privileges.
  for (auto& handler_config : config.protocol_handlers) {
    if (!handler_config.interface_name.empty()) {
      int socket_fd =
          webservd::CreateNetworkInterfaceSocket(handler_config.interface_name);
      if (socket_fd < 0) {
        LOG(ERROR) << "Failed to create a socket for network interface "
                   << handler_config.interface_name;
        return EX_SOFTWARE;
      }
      handler_config.socket_fd = socket_fd;
    }
  }

  config.use_debug = FLAGS_debug;
  Daemon daemon{std::move(config)};

  // Drop privileges and use 'webservd' user. We need to do this after Daemon
  // object is constructed since it creates an instance of base::AtExitManager
  // which is required for chromeos::Minijail::GetInstance() to work.
  chromeos::Minijail* minijail_instance = chromeos::Minijail::GetInstance();
  minijail* jail = minijail_instance->New();
  minijail_instance->DropRoot(jail, kWebServerUserName, kWebServerGroupName);
  // Permissions needed for the daemon to allow it to bind to ports like TCP
  // 80.
  minijail_instance->UseCapabilities(jail, CAP_TO_MASK(CAP_NET_BIND_SERVICE));
  minijail_enter(jail);
  minijail_instance->Destroy(jail);
  return daemon.Run();
}
