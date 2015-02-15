// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <sysexits.h>

#include <base/command_line.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/strings/stringprintf.h>
#include <base/values.h>
#include <chromeos/any.h>
#include <chromeos/daemons/dbus_daemon.h>
#include <chromeos/data_encoding.h>
#include <chromeos/dbus/data_serialization.h>
#include <chromeos/dbus/dbus_method_invoker.h>
#include <chromeos/errors/error.h>
#include <chromeos/variant_dictionary.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <dbus/object_manager.h>
#include <dbus/values_util.h>

#include "buffet/dbus-proxies.h"

using chromeos::Error;
using chromeos::ErrorPtr;

namespace {

void usage() {
  printf(R"(Possible commands:
  - TestMethod <message>
  - StartDevice
  - CheckDeviceRegistered
  - GetDeviceInfo
  - RegisterDevice param1=val1&param2=val2...
  - AddCommand '{"name":"command_name","parameters":{}}'
  - UpdateState prop_name prop_value
  - GetState
  - PendingCommands
)");
}

// Helpers for JsonToAny().
template<typename T>
chromeos::Any GetJsonValue(const base::Value& json,
                           bool(base::Value::*fnc)(T*) const) {
  T val;
  CHECK((json.*fnc)(&val));
  return val;
}

template<typename T>
chromeos::Any GetJsonList(const base::ListValue& list);  // Prototype.

// Converts a JSON value into an Any so it can be sent over D-Bus using
// UpdateState D-Bus method from Buffet.
chromeos::Any JsonToAny(const base::Value& json) {
  chromeos::Any prop_value;
  switch (json.GetType()) {
    case base::Value::TYPE_NULL:
      prop_value = nullptr;
      break;
    case base::Value::TYPE_BOOLEAN:
      prop_value = GetJsonValue<bool>(json, &base::Value::GetAsBoolean);
      break;
    case base::Value::TYPE_INTEGER:
      prop_value = GetJsonValue<int>(json, &base::Value::GetAsInteger);
      break;
    case base::Value::TYPE_DOUBLE:
      prop_value = GetJsonValue<double>(json, &base::Value::GetAsDouble);
      break;
    case base::Value::TYPE_STRING:
      prop_value = GetJsonValue<std::string>(json, &base::Value::GetAsString);
      break;
    case base::Value::TYPE_BINARY:
      LOG(FATAL) << "Binary values should not happen";
      break;
    case base::Value::TYPE_DICTIONARY: {
      const base::DictionaryValue* dict = nullptr;  // Still owned by |json|.
      CHECK(json.GetAsDictionary(&dict));
      chromeos::VariantDictionary var_dict;
      base::DictionaryValue::Iterator it(*dict);
      while (!it.IsAtEnd()) {
        var_dict.emplace(it.key(), JsonToAny(it.value()));
        it.Advance();
      }
      prop_value = var_dict;
      break;
    }
    case base::Value::TYPE_LIST: {
      const base::ListValue* list = nullptr;  // Still owned by |json|.
      CHECK(json.GetAsList(&list));
      CHECK(!list->empty()) << "Unable to deduce the type of list elements.";
      switch ((*list->begin())->GetType()) {
        case base::Value::TYPE_BOOLEAN:
          prop_value = GetJsonList<bool>(*list);
          break;
        case base::Value::TYPE_INTEGER:
          prop_value = GetJsonList<int>(*list);
          break;
        case base::Value::TYPE_DOUBLE:
          prop_value = GetJsonList<double>(*list);
          break;
        case base::Value::TYPE_STRING:
          prop_value = GetJsonList<std::string>(*list);
          break;
        case base::Value::TYPE_DICTIONARY:
          prop_value = GetJsonList<chromeos::VariantDictionary>(*list);
          break;
        default:
          LOG(FATAL) << "Unsupported JSON value type for list element: "
                     << (*list->begin())->GetType();
      }
      break;
    }
    default:
      LOG(FATAL) << "Unexpected JSON value type: " << json.GetType();
      break;
  }
  return prop_value;
}

template<typename T>
chromeos::Any GetJsonList(const base::ListValue& list) {
  std::vector<T> val;
  val.reserve(list.GetSize());
  for (const base::Value* v : list)
    val.push_back(JsonToAny(*v).Get<T>());
  return val;
}

class Daemon : public chromeos::DBusDaemon {
 public:
  Daemon() = default;

 protected:
  int OnInit() override {
    int return_code = chromeos::DBusDaemon::OnInit();
    if (return_code != EX_OK)
      return return_code;

    object_manager_.reset(new org::chromium::Buffet::ObjectManagerProxy{bus_});
    auto manager_instances = object_manager_->GetManagerInstances();
    if (manager_instances.empty()) {
      fprintf(stderr, "Buffet daemon was offline.");
      return EX_UNAVAILABLE;
    }
    manager_proxy_ = manager_instances.front();

    auto args = CommandLine::ForCurrentProcess()->GetArgs();

    // Pop the command off of the args list.
    std::string command = args.front();
    args.erase(args.begin());

    if (command.compare("TestMethod") == 0) {
      if (args.empty() || CheckArgs(command, args, 1)) {
        std::string message;
        if (!args.empty())
          message = args.back();
        PostTask(&Daemon::CallTestMethod, message);
      }
    } else if (command.compare("StartDevice") == 0 ||
               command.compare("sd") == 0) {
      if (CheckArgs(command, args, 0))
        PostTask(&Daemon::CallStartDevice);
    } else if (command.compare("CheckDeviceRegistered") == 0 ||
               command.compare("cr") == 0) {
      if (CheckArgs(command, args, 0))
        PostTask(&Daemon::CallCheckDeviceRegistered);
    } else if (command.compare("GetDeviceInfo") == 0 ||
               command.compare("di") == 0) {
      if (CheckArgs(command, args, 0))
        PostTask(&Daemon::CallGetDeviceInfo);
    } else if (command.compare("RegisterDevice") == 0 ||
               command.compare("rd") == 0) {
      if (args.empty() || CheckArgs(command, args, 1)) {
        std::string dict;
        if (!args.empty())
          dict = args.back();
        PostTask(&Daemon::CallRegisterDevice, dict);
      }
    } else if (command.compare("UpdateState") == 0 ||
               command.compare("us") == 0) {
      if (CheckArgs(command, args, 2))
        PostTask(&Daemon::CallUpdateState, args.front(), args.back());
    } else if (command.compare("GetState") == 0 ||
               command.compare("gs") == 0) {
      if (CheckArgs(command, args, 0))
        PostTask(&Daemon::CallGetState);
    } else if (command.compare("AddCommand") == 0 ||
               command.compare("ac") == 0) {
      if (CheckArgs(command, args, 1))
        PostTask(&Daemon::CallAddCommand, args.back());
    } else if (command.compare("PendingCommands") == 0 ||
               command.compare("pc") == 0) {
      if (CheckArgs(command, args, 0))
        // CallGetPendingCommands relies on ObjectManager but it is being
        // initialized asynchronously without a way to get a callback when
        // it is ready to be used. So, just wait a bit before calling its
        // methods.
        PostDelayedTask(&Daemon::CallGetPendingCommands,
                        base::TimeDelta::FromMilliseconds(100));
    } else {
      fprintf(stderr, "Unknown command: '%s'\n", command.c_str());
      usage();
      Quit();
      return EX_USAGE;
    }
    return EX_OK;
  }

  void OnShutdown(int* return_code) override {
    if (*return_code == EX_OK)
      *return_code = exit_code_;
  }

 private:
  void ReportError(Error* error) {
    fprintf(stderr, "Failed to receive a response: %s\n",
            error->GetMessage().c_str());
    exit_code_ = EX_UNAVAILABLE;
    Quit();
  }

  bool CheckArgs(const std::string& command,
                 const std::vector<std::string>& args,
                 size_t expected_arg_count) {
    if (args.size() == expected_arg_count)
      return true;
    fprintf(stderr, "Invalid number of arguments for command '%s'\n",
            command.c_str());
    usage();
    exit_code_ = EX_USAGE;
    Quit();
    return false;
  }

  template<typename... Args>
  void PostTask(void (Daemon::*fn)(const Args&...), const Args&... args) {
    base::MessageLoop::current()->PostTask(
        FROM_HERE, base::Bind(fn, base::Unretained(this), args...));
  }

  template<typename... Args>
  void PostDelayedTask(void (Daemon::*fn)(const Args&...),
                       const base::TimeDelta& delay,
                       const Args&... args) {
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE, base::Bind(fn, base::Unretained(this), args...), delay);
  }

  void CallTestMethod(const std::string& message) {
    ErrorPtr error;
    std::string response_message;
    if (!manager_proxy_->TestMethod(message, &response_message, &error)) {
      return ReportError(error.get());
    }
    printf("Received a response: %s\n", response_message.c_str());
    Quit();
  }

  void CallStartDevice() {
    ErrorPtr error;
    if (!manager_proxy_->StartDevice(&error)) {
      return ReportError(error.get());
    }
    Quit();
  }

  void CallCheckDeviceRegistered() {
    ErrorPtr error;
    std::string device_id;
    if (!manager_proxy_->CheckDeviceRegistered(&device_id, &error)) {
      return ReportError(error.get());
    }

    printf("Device ID: %s\n",
           device_id.empty() ? "<unregistered>" : device_id.c_str());
    Quit();
  }

  void CallGetDeviceInfo() {
    ErrorPtr error;
    std::string device_info;
    if (!manager_proxy_->GetDeviceInfo(&device_info, &error)) {
      return ReportError(error.get());
    }

    printf("Device Info: %s\n",
           device_info.empty() ? "<unregistered>" : device_info.c_str());
    Quit();
  }

  void CallRegisterDevice(const std::string& args) {
    chromeos::VariantDictionary params;
    if (!args.empty()) {
      auto key_values = chromeos::data_encoding::WebParamsDecode(args);
      for (const auto& pair : key_values) {
        params.insert(std::make_pair(pair.first, chromeos::Any(pair.second)));
      }
    }

    ErrorPtr error;
    std::string device_id;
    if (!manager_proxy_->RegisterDevice(params, &device_id, &error)) {
      return ReportError(error.get());
    }

    printf("Device registered: %s\n", device_id.c_str());
    Quit();
  }

  void CallUpdateState(const std::string& prop, const std::string& value) {
    ErrorPtr error;
    std::string error_message;
    std::unique_ptr<base::Value> json(base::JSONReader::ReadAndReturnError(
        value, base::JSON_PARSE_RFC, nullptr, &error_message));
    if (!json) {
      Error::AddTo(&error, FROM_HERE, chromeos::errors::json::kDomain,
                   chromeos::errors::json::kParseError, error_message);
      return ReportError(error.get());
    }

    chromeos::VariantDictionary property_set{{prop, JsonToAny(*json)}};
    if (!manager_proxy_->UpdateState(property_set, &error)) {
      return ReportError(error.get());
    }
    Quit();
  }

  void CallGetState() {
    std::string json;
    ErrorPtr error;
    if (!manager_proxy_->GetState(&json, &error)) {
      return ReportError(error.get());
    }
    printf("Device State: %s\n", json.c_str());
    Quit();
  }

  void CallAddCommand(const std::string& command) {
    ErrorPtr error;
    if (!manager_proxy_->AddCommand(command, &error)) {
      return ReportError(error.get());
    }
    Quit();
  }

  void CallGetPendingCommands() {
    printf("Pending commands:\n");
    for (auto* cmd : object_manager_->GetCommandInstances()) {
      printf("%10s (%-3d) - '%s' (id:%s)\n",
             cmd->status().c_str(),
             cmd->progress(),
             cmd->name().c_str(),
             cmd->id().c_str());
    }
    Quit();
  }

  std::unique_ptr<org::chromium::Buffet::ObjectManagerProxy> object_manager_;
  org::chromium::Buffet::ManagerProxy* manager_proxy_{nullptr};
  int exit_code_{EX_OK};

  DISALLOW_COPY_AND_ASSIGN(Daemon);
};

}  // anonymous namespace

int main(int argc, char** argv) {
  CommandLine::Init(argc, argv);
  CommandLine* cl = CommandLine::ForCurrentProcess();
  CommandLine::StringVector args = cl->GetArgs();
  if (args.empty()) {
    usage();
    return EX_USAGE;
  }

  Daemon daemon;
  return daemon.Run();
}
