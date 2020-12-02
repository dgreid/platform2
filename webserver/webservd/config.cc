// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webservd/config.h"

#include <utility>

#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/values.h>
#include <brillo/errors/error_codes.h>

#include "webservd/error_codes.h"

namespace webservd {

const char kDefaultLogDirectory[] = "/var/log/webservd";

namespace {

const char kLogDirectoryKey[] = "log_directory";
const char kProtocolHandlersKey[] = "protocol_handlers";
const char kNameKey[] = "name";
const char kPortKey[] = "port";
const char kUseTLSKey[] = "use_tls";
const char kInterfaceKey[] = "interface";

// Default configuration for the web server.
const char kDefaultConfig[] = R"({
  "protocol_handlers": [
    {
      "name": "http",
      "port": 80,
      "use_tls": false
    },
    {
      "name": "https",
      "port": 443,
      "use_tls": true
    }
  ]
})";

bool LoadHandlerConfig(const base::Value& handler_value,
                       Config::ProtocolHandler* handler_config,
                       brillo::ErrorPtr* error) {
  base::Optional<int> port = handler_value.FindIntKey(kPortKey);
  if (!port.has_value()) {
    brillo::Error::AddTo(error, FROM_HERE, webservd::errors::kDomain,
                         webservd::errors::kInvalidConfig, "Port is missing");
    return false;
  }
  if (*port < 1 || *port > 0xFFFF) {
    brillo::Error::AddToPrintf(error, FROM_HERE, webservd::errors::kDomain,
                               webservd::errors::kInvalidConfig,
                               "Invalid port value: %d", *port);
    return false;
  }
  handler_config->port = *port;

  // Allow "use_tls" to be omitted, so not returning an error here.
  base::Optional<bool> use_tls = handler_value.FindBoolKey(kUseTLSKey);
  if (use_tls.has_value())
    handler_config->use_tls = *use_tls;

  // "interface" is also optional.
  const std::string* interface_name =
      handler_value.FindStringKey(kInterfaceKey);
  if (interface_name != nullptr)
    handler_config->interface_name = *interface_name;

  return true;
}

}  // anonymous namespace

Config::ProtocolHandler::~ProtocolHandler() {
  if (socket_fd != -1)
    close(socket_fd);
}

void LoadDefaultConfig(Config* config) {
  LOG(INFO) << "Loading default server configuration...";
  CHECK(LoadConfigFromString(kDefaultConfig, config, nullptr));
}

bool LoadConfigFromFile(const base::FilePath& json_file_path, Config* config) {
  std::string config_json;
  LOG(INFO) << "Loading server configuration from " << json_file_path.value();
  return base::ReadFileToString(json_file_path, &config_json) &&
         LoadConfigFromString(config_json, config, nullptr);
}

bool LoadConfigFromString(const std::string& config_json,
                          Config* config,
                          brillo::ErrorPtr* error) {
  auto result = base::JSONReader::ReadAndReturnValueWithError(
      config_json, base::JSON_ALLOW_TRAILING_COMMAS);

  if (!result.value) {
    brillo::Error::AddToPrintf(error, FROM_HERE, brillo::errors::json::kDomain,
                               brillo::errors::json::kParseError,
                               "Error parsing server configuration: %s",
                               result.error_message.c_str());
    return false;
  }

  if (!result.value->is_dict()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::json::kDomain,
                         brillo::errors::json::kObjectExpected,
                         "JSON object is expected.");
    return false;
  }

  // "log_directory" is optional
  if (result.value->FindStringKey(kLogDirectoryKey)) {
    config->log_directory = *result.value->FindStringKey(kLogDirectoryKey);
  }

  const base::Value* protocol_handlers =
      result.value->FindListKey(kProtocolHandlersKey);
  if (protocol_handlers) {
    for (const base::Value& handler_value : protocol_handlers->GetList()) {
      if (!handler_value.is_dict()) {
        brillo::Error::AddTo(
            error, FROM_HERE, brillo::errors::json::kDomain,
            brillo::errors::json::kObjectExpected,
            "Protocol handler definition must be a JSON object");
        return false;
      }

      const std::string* name = handler_value.FindStringKey(kNameKey);
      if (name == nullptr) {
        brillo::Error::AddTo(
            error, FROM_HERE, errors::kDomain, errors::kInvalidConfig,
            "Protocol handler definition must include its name");
        return false;
      }

      Config::ProtocolHandler handler_config;
      handler_config.name = *name;
      if (!LoadHandlerConfig(handler_value, &handler_config, error)) {
        brillo::Error::AddToPrintf(
            error, FROM_HERE, errors::kDomain, errors::kInvalidConfig,
            "Unable to parse config for protocol handler '%s'", name->c_str());
        return false;
      }
      config->protocol_handlers.push_back(std::move(handler_config));
    }
  }
  return true;
}

}  // namespace webservd
