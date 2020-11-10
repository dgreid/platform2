// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <sysexits.h>

#include <base/cancelable_callback.h>
#include <base/command_line.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <base/strings/stringprintf.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/values.h>
#include <brillo/any.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/data_encoding.h>
#include <brillo/dbus/data_serialization.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/errors/error.h>
#include <brillo/strings/string_utils.h>
#include <brillo/variant_dictionary.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <dbus/object_manager.h>
#include <dbus/values_util.h>

#include "buffet/dbus-proxies.h"

using brillo::Error;
using brillo::ErrorPtr;
using org::chromium::Buffet::ManagerProxyInterface;

namespace {

void usage() {
  printf(R"(Possible commands:
  - TestMethod <message>
  - CheckDeviceRegistered
  - GetDeviceInfo
  - RegisterDevice param1=val1&param2=val2...
  - AddCommand '{"name":"command_name","parameters":{}}'
  - UpdateState prop_name prop_value
  - GetState
  - PendingCommands
  - SetCommandVisibility pkg1.cmd1[,pkg2.cm2,...] [all|cloud|local|none]
)");
}

// Helpers for JsonToAny().
template <typename T>
brillo::Any GetJsonValue(const base::Value& json,
                         bool (base::Value::*fnc)(T*) const) {
  T val;
  CHECK((json.*fnc)(&val));
  return val;
}

template <typename T>
brillo::Any GetJsonList(const base::ListValue& list);  // Prototype.

// Converts a JSON value into an Any so it can be sent over D-Bus using
// UpdateState D-Bus method from Buffet.
brillo::Any JsonToAny(const base::Value& json) {
  brillo::Any prop_value;
  switch (json.type()) {
    case base::Value::Type::NONE:
      prop_value = nullptr;
      break;
    case base::Value::Type::BOOLEAN:
      prop_value = GetJsonValue<bool>(json, &base::Value::GetAsBoolean);
      break;
    case base::Value::Type::INTEGER:
      prop_value = GetJsonValue<int>(json, &base::Value::GetAsInteger);
      break;
    case base::Value::Type::DOUBLE:
      prop_value = GetJsonValue<double>(json, &base::Value::GetAsDouble);
      break;
    case base::Value::Type::STRING:
      prop_value = GetJsonValue<std::string>(json, &base::Value::GetAsString);
      break;
    case base::Value::Type::BINARY:
      LOG(FATAL) << "Binary values should not happen";
      break;
    case base::Value::Type::DICTIONARY: {
      const base::DictionaryValue* dict = nullptr;  // Still owned by |json|.
      CHECK(json.GetAsDictionary(&dict));
      brillo::VariantDictionary var_dict;
      base::DictionaryValue::Iterator it(*dict);
      while (!it.IsAtEnd()) {
        var_dict.emplace(it.key(), JsonToAny(it.value()));
        it.Advance();
      }
      prop_value = var_dict;
      break;
    }
    case base::Value::Type::LIST: {
      const base::ListValue* list = nullptr;  // Still owned by |json|.
      CHECK(json.GetAsList(&list));
      CHECK(!list->empty()) << "Unable to deduce the type of list elements.";
      switch (list->begin()->type()) {
        case base::Value::Type::BOOLEAN:
          prop_value = GetJsonList<bool>(*list);
          break;
        case base::Value::Type::INTEGER:
          prop_value = GetJsonList<int>(*list);
          break;
        case base::Value::Type::DOUBLE:
          prop_value = GetJsonList<double>(*list);
          break;
        case base::Value::Type::STRING:
          prop_value = GetJsonList<std::string>(*list);
          break;
        case base::Value::Type::DICTIONARY:
          prop_value = GetJsonList<brillo::VariantDictionary>(*list);
          break;
        default:
          LOG(FATAL) << "Unsupported JSON value type for list element: "
                     << list->begin()->type();
      }
      break;
    }
    default:
      LOG(FATAL) << "Unexpected JSON value type: " << json.type();
      break;
  }
  return prop_value;
}

template <typename T>
brillo::Any GetJsonList(const base::ListValue& list) {
  std::vector<T> val;
  val.reserve(list.GetSize());
  for (const base::Value& v : list)
    val.push_back(JsonToAny(v).Get<T>());
  return val;
}

class Daemon final : public brillo::DBusDaemon {
 public:
  Daemon() = default;
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

 protected:
  int OnInit() override {
    int return_code = brillo::DBusDaemon::OnInit();
    if (return_code != EX_OK)
      return return_code;

    object_manager_.reset(new org::chromium::Buffet::ObjectManagerProxy{bus_});
    return_code = ScheduleActions();
    if (return_code == EX_USAGE) {
      usage();
    }
    return return_code;
  }

  void OnShutdown(int* return_code) override {
    if (*return_code == EX_OK)
      *return_code = exit_code_;
  }

 private:
  int ScheduleActions() {
    auto args = base::CommandLine::ForCurrentProcess()->GetArgs();

    // Pop the command off of the args list.
    std::string command = args.front();
    args.erase(args.begin());
    base::Callback<void(ManagerProxyInterface*)> job;
    if (command.compare("TestMethod") == 0) {
      if (!args.empty() && !CheckArgs(command, args, 1))
        return EX_USAGE;
      std::string message;
      if (!args.empty())
        message = args.back();
      job = base::Bind(&Daemon::CallTestMethod, weak_factory_.GetWeakPtr(),
                       message);
    } else if (command.compare("CheckDeviceRegistered") == 0 ||
               command.compare("cr") == 0) {
      if (!CheckArgs(command, args, 0))
        return EX_USAGE;
      job = base::Bind(&Daemon::CallCheckDeviceRegistered,
                       weak_factory_.GetWeakPtr());
    } else if (command.compare("RegisterDevice") == 0 ||
               command.compare("rd") == 0) {
      if (!args.empty() && !CheckArgs(command, args, 1))
        return EX_USAGE;
      std::string dict;
      if (!args.empty())
        dict = args.back();
      job = base::Bind(&Daemon::CallRegisterDevice, weak_factory_.GetWeakPtr(),
                       dict);
    } else if (command.compare("UpdateState") == 0 ||
               command.compare("us") == 0) {
      if (!CheckArgs(command, args, 2))
        return EX_USAGE;
      job = base::Bind(&Daemon::CallUpdateState, weak_factory_.GetWeakPtr(),
                       args.front(), args.back());
    } else if (command.compare("GetState") == 0 || command.compare("gs") == 0) {
      if (!CheckArgs(command, args, 0))
        return EX_USAGE;
      job = base::Bind(&Daemon::CallGetState, weak_factory_.GetWeakPtr());
    } else if (command.compare("AddCommand") == 0 ||
               command.compare("ac") == 0) {
      if (!CheckArgs(command, args, 1))
        return EX_USAGE;
      job = base::Bind(&Daemon::CallAddCommand, weak_factory_.GetWeakPtr(),
                       args.back());
    } else if (command.compare("PendingCommands") == 0 ||
               command.compare("pc") == 0) {
      if (!CheckArgs(command, args, 0))
        return EX_USAGE;
      // CallGetPendingCommands relies on ObjectManager but it is being
      // initialized asynchronously without a way to get a callback when
      // it is ready to be used. So, just wait a bit before calling its
      // methods.
      base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
          FROM_HERE,
          base::Bind(&Daemon::CallGetPendingCommands,
                     weak_factory_.GetWeakPtr()),
          base::TimeDelta::FromMilliseconds(100));
    } else {
      fprintf(stderr, "Unknown command: '%s'\n", command.c_str());
      return EX_USAGE;
    }
    if (!job.is_null())
      object_manager_->SetManagerAddedCallback(job);
    timeout_task_.Reset(
        base::Bind(&Daemon::OnJobTimeout, weak_factory_.GetWeakPtr()));
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE, timeout_task_.callback(), base::TimeDelta::FromSeconds(10));

    return EX_OK;
  }

  void OnJobComplete() {
    timeout_task_.Cancel();
    Quit();
  }

  void OnJobTimeout() {
    fprintf(stderr, "Timed out before completing request.");
    Quit();
  }

  void ReportError(Error* error) {
    fprintf(stderr, "Failed to receive a response: %s\n",
            error->GetMessage().c_str());
    exit_code_ = EX_UNAVAILABLE;
    OnJobComplete();
  }

  bool CheckArgs(const std::string& command,
                 const std::vector<std::string>& args,
                 size_t expected_arg_count) {
    if (args.size() == expected_arg_count)
      return true;
    fprintf(stderr, "Invalid number of arguments for command '%s'\n",
            command.c_str());
    return false;
  }

  void CallTestMethod(const std::string& message,
                      ManagerProxyInterface* manager_proxy) {
    ErrorPtr error;
    std::string response_message;
    if (!manager_proxy->TestMethod(message, &response_message, &error)) {
      return ReportError(error.get());
    }
    printf("Received a response: %s\n", response_message.c_str());
    OnJobComplete();
  }

  void CallCheckDeviceRegistered(ManagerProxyInterface* manager_proxy) {
    ErrorPtr error;
    std::string device_id;
    if (!manager_proxy->CheckDeviceRegistered(&device_id, &error)) {
      return ReportError(error.get());
    }

    printf("Device ID: %s\n",
           device_id.empty() ? "<unregistered>" : device_id.c_str());
    OnJobComplete();
  }

  void CallRegisterDevice(const std::string& args,
                          ManagerProxyInterface* manager_proxy) {
    std::string ticket_id;
    if (!args.empty()) {
      auto key_values = brillo::data_encoding::WebParamsDecode(args);
      for (const auto& pair : key_values) {
        if (pair.first == "ticket_id")
          ticket_id = pair.second;
      }
    }

    ErrorPtr error;
    std::string device_id;
    if (!manager_proxy->RegisterDevice(ticket_id, &device_id, &error)) {
      return ReportError(error.get());
    }

    printf("Device registered: %s\n", device_id.c_str());
    OnJobComplete();
  }

  void CallUpdateState(const std::string& prop,
                       const std::string& value,
                       ManagerProxyInterface* manager_proxy) {
    ErrorPtr error;
    auto json = base::JSONReader::ReadAndReturnValueWithError(
        value, base::JSON_PARSE_RFC);
    if (json.error_code != base::JSONReader::JSON_NO_ERROR) {
      Error::AddTo(&error, FROM_HERE, brillo::errors::json::kDomain,
                   brillo::errors::json::kParseError, json.error_message);
      return ReportError(error.get());
    }

    brillo::VariantDictionary property_set{{prop, JsonToAny(*json.value)}};
    if (!manager_proxy->UpdateState(property_set, &error)) {
      return ReportError(error.get());
    }
    OnJobComplete();
  }

  void CallGetState(ManagerProxyInterface* manager_proxy) {
    std::string json;
    ErrorPtr error;
    if (!manager_proxy->GetState(&json, &error)) {
      return ReportError(error.get());
    }
    printf("%s\n", json.c_str());
    OnJobComplete();
  }

  void CallAddCommand(const std::string& command,
                      ManagerProxyInterface* manager_proxy) {
    ErrorPtr error;
    std::string id;
    if (!manager_proxy->AddCommand(command, &id, &error)) {
      return ReportError(error.get());
    }
    OnJobComplete();
  }

  void CallGetPendingCommands() {
    printf("Pending commands:\n");
    for (auto* cmd : object_manager_->GetCommandInstances()) {
      printf("%10s - '%s' (id:%s)\n", cmd->state().c_str(), cmd->name().c_str(),
             cmd->id().c_str());
    }
    OnJobComplete();
  }

  std::unique_ptr<org::chromium::Buffet::ObjectManagerProxy> object_manager_;
  int exit_code_{EX_OK};
  base::CancelableCallback<void()> timeout_task_;

  base::WeakPtrFactory<Daemon> weak_factory_{this};
};

}  // anonymous namespace

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  base::CommandLine::StringVector args = cl->GetArgs();
  if (args.empty()) {
    usage();
    return EX_USAGE;
  }

  Daemon daemon;
  return daemon.Run();
}
