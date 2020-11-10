// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <signal.h>
#include <sysexits.h>

#include <memory>
#include <string>

#include <base/command_line.h>
#include <base/files/file_util.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <brillo/dbus/exported_object_manager.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "webservd/config.h"
#include "webservd/log_manager.h"
#include "webservd/permission_broker_firewall.h"
#include "webservd/server.h"
#include "webservd/utils.h"

using brillo::dbus_utils::AsyncEventSequencer;

namespace {

const char kDefaultConfigFilePath[] = "/etc/webservd/config";

const char kServiceName[] = "org.chromium.WebServer";
const char kRootServicePath[] = "/org/chromium/WebServer";

class Daemon final : public brillo::DBusServiceDaemon {
 public:
  explicit Daemon(webservd::Config config)
      : DBusServiceDaemon{kServiceName, kRootServicePath},
        config_{std::move(config)} {}
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

 protected:
  void RegisterDBusObjectsAsync(AsyncEventSequencer* sequencer) override {
    webservd::LogManager::Init(base::FilePath{config_.log_directory});
    server_.reset(new webservd::Server{
        object_manager_.get(), config_,
        std::make_unique<webservd::PermissionBrokerFirewall>()});
    server_->RegisterAsync(
        sequencer->GetHandler("Server.RegisterAsync() failed.", true));
  }

  void OnShutdown(int* /* return_code */) override { server_.reset(); }

 private:
  webservd::Config config_;
  std::unique_ptr<webservd::Server> server_;
};

}  // namespace

int main(int argc, char* argv[]) {
  DEFINE_bool(log_to_stderr, false, "log trace messages to stderr as well");
  DEFINE_string(config_path, "",
                "path to a file containing server configuration");
  DEFINE_bool(debug, false, "return debug error information in web requests");
  DEFINE_bool(ipv6, true, "enable IPv6 support");
  brillo::FlagHelper::Init(argc, argv, "Brillo web server daemon");

  // From libmicrohttpd documentation, section 1.5 SIGPIPE:
  // ... portable code using MHD must install a SIGPIPE handler or explicitly
  // block the SIGPIPE signal.
  // This also applies to using pipes over D-Bus to pass request/response data
  // to/from remote request handlers. We handle errors from write operations on
  // sockets/pipes correctly, so SIGPIPE is just a pest.
  signal(SIGPIPE, SIG_IGN);

  int flags = brillo::kLogToSyslog;
  if (FLAGS_log_to_stderr)
    flags |= brillo::kLogToStderr;
  brillo::InitLog(flags | brillo::kLogHeader);

  webservd::Config config;
  config.use_ipv6 = FLAGS_ipv6;
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
  // access to create those bound sockets.
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

  return daemon.Run();
}
