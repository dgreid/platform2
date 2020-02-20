// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/function_templates/network.h"

#include <utility>

#include <base/files/file_util.h>
#include <base/json/json_reader.h>
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
    if (type == base::nullopt || device_type == type) {
      devices_props.push_back(std::move(device_props));
    }
  }

  return devices_props;
}

NetworkFunction::DataType NetworkFunction::Eval() const {
  DataType result{};
  std::string json_output;
  if (!InvokeHelper(&json_output)) {
    LOG(ERROR)
        << "Failed to invoke helper to retrieve cached network information.";
    return result;
  }

  const auto network_results =
      base::ListValue::From(base::JSONReader::Read(json_output));

  if (!network_results) {
    LOG(ERROR) << "Failed to parse output from " << GetFunctionName()
               << "::EvalInHelper.";
    return result;
  }

  for (int i = 0; i < network_results->GetSize(); i++) {
    base::DictionaryValue* network_res;
    if (!network_results->GetDictionary(i, &network_res)) {
      DLOG(INFO) << "Unable to get result " << i << ". Skipped.";
      continue;
    }
    result.push_back(std::move(*network_res));
  }
  return result;
}

int NetworkFunction::EvalInHelper(std::string* output) const {
  const auto devices_props = GetDevicesProps(GetNetworkType());
  base::ListValue result{};

  for (const auto& device_props : devices_props) {
    base::FilePath node_path(
        kNetworkDirPath +
        device_props.at(shill::kInterfaceProperty).TryGet<std::string>());
    std::string device_type =
        device_props.at(shill::kTypeProperty).TryGet<std::string>();

    DLOG(INFO) << "Processing the node \"" << node_path.value() << "\".";

    // Get type specific fields and their values.
    auto node_res = EvalInHelperByPath(node_path);
    if (node_res.empty())
      continue;

    // Report the absolute path we probe the reported info from.
    DLOG_IF(INFO, node_res.HasKey("path"))
        << "Attribute \"path\" already existed. Overrided.";
    node_res.SetString("path", node_path.value());

    DLOG_IF(INFO, node_res.HasKey("type"))
        << "Attribute \"type\" already existed. Overrided.";
    // Align with the category name.
    if (device_type == shill::kTypeWifi) {
      node_res.SetString("type", kTypeWireless);
    } else {
      node_res.SetString("type", device_type);
    }

    result.Append(node_res.CreateDeepCopy());
  }
  if (!base::JSONWriter::Write(result, output)) {
    LOG(ERROR) << "Failed to serialize network probed result to json string.";
    return -1;
  }

  return 0;
}

base::DictionaryValue NetworkFunction::EvalInHelperByPath(
    const base::FilePath& node_path) const {
  base::DictionaryValue res;

  const base::FilePath dev_path = node_path.Append("device");
  const base::FilePath dev_real_path = base::MakeAbsoluteFilePath(dev_path);

  const base::FilePath dev_subsystem_path = dev_path.Append("subsystem");
  base::FilePath dev_subsystem_link_path;
  if (!base::ReadSymbolicLink(dev_subsystem_path, &dev_subsystem_link_path)) {
    LOG(ERROR) << "Cannot get real path of " << dev_subsystem_path.value()
               << ".";
    return {};
  }

  auto bus_type_idx = dev_subsystem_link_path.value().find_last_of('/') + 1;
  const std::string bus_type =
      dev_subsystem_link_path.value().substr(bus_type_idx);

  res.SetString("bus_type", bus_type);

  if (bus_type == kBusTypePci) {
    auto pci_res =
        MapFilesToDict(dev_real_path, kPciFields, kPciOptionalFields);
    PrependToDVKey(&pci_res, std::string(kBusTypePci) + "_");
    res.MergeDictionary(&pci_res);
  } else if (bus_type == kBusTypeSdio) {
    auto sdio_res =
        MapFilesToDict(dev_real_path, kSdioFields, kSdioOptionalFields);
    PrependToDVKey(&sdio_res, std::string(kBusTypeSdio) + "_");
    res.MergeDictionary(&sdio_res);
  } else if (bus_type == kBusTypeUsb) {
    const base::FilePath usb_real_path =
        base::MakeAbsoluteFilePath(dev_real_path.Append(".."));
    auto usb_res =
        MapFilesToDict(usb_real_path, kUsbFields, kUsbOptionalFields);
    PrependToDVKey(&usb_res, std::string(kBusTypeUsb) + "_");
    res.MergeDictionary(&usb_res);
  }

  return res;
}

}  // namespace runtime_probe
