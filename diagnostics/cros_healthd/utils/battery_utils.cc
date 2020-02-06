// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/battery_utils.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/process/launch.h>
#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <base/values.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <re2/re2.h>

#include "power_manager/proto_bindings/power_supply_properties.pb.h"

namespace diagnostics {

namespace {

using ::chromeos::cros_healthd::mojom::BatteryInfo;
using ::chromeos::cros_healthd::mojom::BatteryInfoPtr;
using ::chromeos::cros_healthd::mojom::SmartBatteryInfo;

// The path used to check a device's master configuration hardware properties.
constexpr char kHardwarePropertiesPath[] = "/hardware-properties";
// The master configuration property that specifies a device's PSU type.
constexpr char kPsuTypeProperty[] = "psu-type";

// The path used to check a device's master configuration cros_healthd battery
// properties.
constexpr char kBatteryPropertiesPath[] = "/cros-healthd/battery";
// The master configuration property that indicates whether a device has Smart
// Battery info.
constexpr char kHasSmartBatteryInfoProperty[] = "has-smart-battery-info";

// The name of the Smart Battery manufacture date metric.
constexpr char kManufactureDateSmart[] = "manufacture_date_smart";
// The name of the Smart Battery temperature metric.
constexpr char kTemperatureSmart[] = "temperature_smart";

// The maximum amount of time to wait for a powerd response.
constexpr base::TimeDelta kPowerManagerDBusTimeout =
    base::TimeDelta::FromSeconds(3);

// The maximum amount of time to wait for a debugd response.
constexpr int kDebugdDBusTimeout = 10 * 1000;

// Converts a Smart Battery manufacture date from the ((year - 1980) * 512 +
// month * 32 + day) format to yyyy-mm-dd format.
std::string ConvertSmartBatteryManufactureDate(int64_t manufacture_date) {
  int remainder = manufacture_date;
  int day = remainder % 32;
  remainder /= 32;
  int month = remainder % 16;
  remainder /= 16;
  int year = remainder + 1980;
  return base::StringPrintf("%04d-%02d-%02d", year, month, day);
}

}  // namespace

BatteryFetcher::BatteryFetcher(
    org::chromium::debugdProxyInterface* debugd_proxy,
    dbus::ObjectProxy* power_manager_proxy,
    brillo::CrosConfigInterface* cros_config)
    : debugd_proxy_(debugd_proxy),
      power_manager_proxy_(power_manager_proxy),
      cros_config_(cros_config) {
  DCHECK(debugd_proxy_);
  DCHECK(power_manager_proxy_);
  DCHECK(cros_config_);
}

BatteryFetcher::~BatteryFetcher() = default;

BatteryInfoPtr BatteryFetcher::FetchBatteryInfo() {
  if (!HasBattery())
    return BatteryInfoPtr();

  BatteryInfo info;
  dbus::MethodCall method_call(power_manager::kPowerManagerInterface,
                               power_manager::kGetPowerSupplyPropertiesMethod);
  auto response = power_manager_proxy_->CallMethodAndBlock(
      &method_call, kPowerManagerDBusTimeout.InMilliseconds());
  if (!response) {
    LOG(ERROR) << "Failed to obtain response from powerd.";
    return BatteryInfoPtr();
  }

  if (!GetBatteryInfoFromPowerdResponse(response.get(), &info))
    return BatteryInfoPtr();

  if (HasSmartBatteryInfo()) {
    SmartBatteryInfo smart_info;
    GetSmartBatteryInfo(&smart_info);
    info.smart_battery_info = smart_info.Clone();
  }

  return info.Clone();
}

bool BatteryFetcher::GetBatteryInfoFromPowerdResponse(dbus::Response* response,
                                                      BatteryInfo* info) {
  DCHECK(response);
  DCHECK(info);

  power_manager::PowerSupplyProperties power_supply_proto;
  dbus::MessageReader reader(response);
  if (!reader.PopArrayOfBytesAsProto(&power_supply_proto)) {
    LOG(ERROR) << "Could not successfully read PowerSupplyProperties protobuf.";
    return false;
  }

  if (!power_supply_proto.has_battery_state() ||
      power_supply_proto.battery_state() ==
          power_manager::PowerSupplyProperties_BatteryState_NOT_PRESENT) {
    LOG(ERROR)
        << "PowerSupplyProperties protobuf indicates battery is not present.";
    return false;
  }

  info->cycle_count = power_supply_proto.battery_cycle_count();
  info->vendor = power_supply_proto.battery_vendor();
  info->voltage_now = power_supply_proto.battery_voltage();
  info->charge_full = power_supply_proto.battery_charge_full();
  info->charge_full_design = power_supply_proto.battery_charge_full_design();
  info->serial_number = power_supply_proto.battery_serial_number();
  info->voltage_min_design = power_supply_proto.battery_voltage_min_design();
  info->model_name = power_supply_proto.battery_model_name();
  info->charge_now = power_supply_proto.battery_charge();

  return true;
}

void BatteryFetcher::GetSmartBatteryInfo(SmartBatteryInfo* smart_info) {
  int64_t manufacture_date;
  auto convert_string_to_int =
      base::BindOnce([](const base::StringPiece& input, int64_t* output) {
        return base::StringToInt64(input, output);
      });
  smart_info->manufacture_date =
      GetSmartBatteryMetric<int64_t>(kManufactureDateSmart,
                                     std::move(convert_string_to_int),
                                     &manufacture_date)
          ? ConvertSmartBatteryManufactureDate(manufacture_date)
          : "0000-00-00";
  uint64_t temperature;
  auto convert_string_to_uint =
      base::BindOnce([](const base::StringPiece& input, uint64_t* output) {
        return base::StringToUint64(input, output);
      });
  smart_info->temperature =
      GetSmartBatteryMetric<uint64_t>(
          kTemperatureSmart, std::move(convert_string_to_uint), &temperature)
          ? temperature
          : 0;
}

template <typename T>
bool BatteryFetcher::GetSmartBatteryMetric(
    const std::string& metric_name,
    base::OnceCallback<bool(const base::StringPiece& input, T* output)>
        convert_string_to_num,
    T* metric_value) {
  brillo::ErrorPtr error;
  std::string debugd_result;
  if (!debugd_proxy_->CollectSmartBatteryMetric(metric_name, &debugd_result,
                                                &error, kDebugdDBusTimeout)) {
    LOG(ERROR) << "Failed retrieving " << metric_name
               << " from debugd: " << error->GetCode() << " "
               << error->GetMessage();
    return false;
  }

  std::move(convert_string_to_num).Run(debugd_result, metric_value);
  return true;
}

bool BatteryFetcher::HasBattery() {
  std::string psu_type;
  cros_config_->GetString(kHardwarePropertiesPath, kPsuTypeProperty, &psu_type);
  return psu_type != "AC_only";
}

bool BatteryFetcher::HasSmartBatteryInfo() {
  std::string has_smart_battery_info;
  cros_config_->GetString(kBatteryPropertiesPath, kHasSmartBatteryInfoProperty,
                          &has_smart_battery_info);
  return has_smart_battery_info == "true";
}

}  // namespace diagnostics
