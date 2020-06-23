// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/function_templates/network.h"

#include <utility>

#include <base/files/file_util.h>
#include <base/json/json_writer.h>
#include <base/values.h>
#include <brillo/dbus/dbus_connection.h>
#include <chromeos/dbus/service_constants.h>
#include <shill/dbus-proxies.h>

#include "runtime_probe/utils/file_utils.h"
#include "runtime_probe/utils/value_utils.h"

namespace runtime_probe {
namespace {
constexpr auto kNetworkDirPath("/sys/class/net/");

constexpr auto kBusTypePci("pci");
constexpr auto kBusTypeSdio("sdio");
constexpr auto kBusTypeUsb("usb");

using FieldType = std::pair<std::string, std::string>;

const std::vector<FieldType> kPciFields = {{"vendor_id", "vendor"},
                                           {"device_id", "device"}};
const std::vector<FieldType> kPciOptionalFields = {{"revision", "revision"}};
const std::vector<FieldType> kSdioFields = {{"vendor_id", "vendor"}};
const std::vector<FieldType> kSdioOptionalFields = {
    {"manufacturer", "manufacturer"},
    {"product", "product"},
    {"bcd_device", "bcdDevice"}};
const std::vector<FieldType> kUsbFields = {{"vendor_id", "idVendor"},
                                           {"product_id", "idProduct"}};
const std::vector<FieldType> kUsbOptionalFields = {{"bcd_device", "bcdDevice"}};

}  // namespace

std::vector<brillo::VariantDictionary> NetworkFunction::GetDevicesProps(
    base::Optional<std::string> type) const {
  std::vector<brillo::VariantDictionary> devices_props{};

  brillo::DBusConnection dbus_connection;
  const auto bus = dbus_connection.Connect();
  if (bus == nullptr) {
    LOG(ERROR) << "Failed to connect to system D-Bus service.";
    return {};
  }

  auto shill_proxy =
      std::make_unique<org::chromium::flimflam::ManagerProxy>(bus);
  brillo::VariantDictionary props;
  if (!shill_proxy->GetProperties(&props, nullptr)) {
    LOG(ERROR) << "Unable to get manager properties.";
    return {};
  }
  const auto it = props.find(shill::kDevicesProperty);
  if (it == props.end()) {
    LOG(ERROR) << "Manager properties is missing devices.";
    return {};
  }

  for (const auto& path : it->second.TryGet<std::vector<dbus::ObjectPath>>()) {
    auto device =
        std::make_unique<org::chromium::flimflam::DeviceProxy>(bus, path);
    brillo::VariantDictionary device_props;
    if (!device->GetProperties(&device_props, nullptr)) {
      DLOG(INFO) << "Unable to get device properties of " << path.value()
                 << ". Skipped.";
      continue;
    }
    auto device_type = device_props[shill::kTypeProperty].TryGet<std::string>();
    if (!type || device_type == type) {
      devices_props.push_back(std::move(device_props));
    }
  }

  return devices_props;
}

NetworkFunction::DataType NetworkFunction::Eval() const {
  auto json_output = InvokeHelperToJSON();
  if (!json_output) {
    LOG(ERROR)
        << "Failed to invoke helper to retrieve cached network information.";
    return {};
  }

  if (!json_output->is_list()) {
    LOG(ERROR) << "Failed to parse output from " << GetFunctionName()
               << "::EvalInHelper.";
    return {};
  }

  // TODO(b/161770131): replace with TakeList() after libchrome uprev.
  return DataType(std::move(json_output->GetList()));
}

int NetworkFunction::EvalInHelper(std::string* output) const {
  const auto devices_props = GetDevicesProps(GetNetworkType());
  base::Value result(base::Value::Type::LIST);

  for (const auto& device_props : devices_props) {
    base::FilePath node_path(
        kNetworkDirPath +
        device_props.at(shill::kInterfaceProperty).TryGet<std::string>());
    std::string device_type =
        device_props.at(shill::kTypeProperty).TryGet<std::string>();

    DLOG(INFO) << "Processing the node \"" << node_path.value() << "\".";

    // Get type specific fields and their values.
    auto node_res = EvalInHelperByPath(node_path);
    if (!node_res)
      continue;

    // Report the absolute path we probe the reported info from.
    DLOG_IF(INFO, node_res->FindStringKey("path"))
        << "Attribute \"path\" already existed. Overrided.";
    node_res->SetStringKey("path", node_path.value());

    DLOG_IF(INFO, node_res->FindStringKey("type"))
        << "Attribute \"type\" already existed. Overrided.";
    // Align with the category name.
    if (device_type == shill::kTypeWifi) {
      node_res->SetStringKey("type", kTypeWireless);
    } else {
      node_res->SetStringKey("type", device_type);
    }

    result.GetList().push_back(std::move(*node_res));
  }
  if (!base::JSONWriter::Write(result, output)) {
    LOG(ERROR) << "Failed to serialize network probed result to json string.";
    return -1;
  }

  return 0;
}

base::Optional<base::Value> NetworkFunction::EvalInHelperByPath(
    const base::FilePath& node_path) const {
  const auto dev_path = node_path.Append("device");
  const auto dev_subsystem_path = dev_path.Append("subsystem");
  base::FilePath dev_subsystem_link_path;
  if (!base::ReadSymbolicLink(dev_subsystem_path, &dev_subsystem_link_path)) {
    LOG(ERROR) << "Cannot get real path of " << dev_subsystem_path.value();
    return base::nullopt;
  }

  auto bus_type_idx = dev_subsystem_link_path.value().find_last_of('/') + 1;
  const std::string bus_type =
      dev_subsystem_link_path.value().substr(bus_type_idx);

  const std::vector<FieldType>*fields, *optional_fields;
  base::FilePath field_path;
  if (bus_type == kBusTypePci) {
    field_path = dev_path;
    fields = &kPciFields;
    optional_fields = &kPciOptionalFields;
  } else if (bus_type == kBusTypeSdio) {
    field_path = dev_path;
    fields = &kSdioFields;
    optional_fields = &kSdioOptionalFields;
  } else if (bus_type == kBusTypeUsb) {
    field_path = base::MakeAbsoluteFilePath(dev_path.Append(".."));
    fields = &kUsbFields;
    optional_fields = &kUsbOptionalFields;
  } else {
    LOG(ERROR) << "Unknown bus_type " << bus_type;
    return base::nullopt;
  }

  auto res = MapFilesToDict(field_path, *fields, *optional_fields);
  if (!res) {
    LOG(ERROR) << "Cannot find " << bus_type << "-specific fields on network \""
               << dev_path.value() << "\"";
    return base::nullopt;
  }
  PrependToDVKey(&*res, std::string(bus_type) + "_");
  res->SetStringKey("bus_type", bus_type);

  return res;
}

}  // namespace runtime_probe
