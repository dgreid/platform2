// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/generic_battery.h"

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_writer.h>
#include <base/strings/string_number_conversions.h>
#include <base/values.h>
#include <pcrecpp.h>

#include "runtime_probe/utils/file_utils.h"

namespace runtime_probe {

GenericBattery::DataType GenericBattery::Eval() const {
  auto json_output = InvokeHelperToJSON();
  if (!json_output) {
    LOG(ERROR) << "Failed to invoke helper to retrieve battery sysfs results.";
    return {};
  }
  if (!json_output->is_list()) {
    LOG(ERROR) << "Failed to parse json output as list.";
    return {};
  }

  // TODO(b/161770131): replace with TakeList() after libchrome uprev.
  return DataType(std::move(json_output->GetList()));
}
int GenericBattery::EvalInHelper(std::string* output) const {
  constexpr char kSysfsBatteryPath[] = "/sys/class/power_supply/BAT*";
  constexpr char kSysfsExpectedType[] = "Battery";
  const std::vector<std::string> keys{"manufacturer", "model_name",
                                      "technology", "type"};
  const std::vector<std::string> optional_keys{
      "capacity",           "capacity_level",
      "charge_full",        "charge_full_design",
      "charge_now",         "current_now",
      "cycle_count",        "present",
      "serial_number",      "status",
      "voltage_min_design", "voltage_now"};

  base::Value result(base::Value::Type::LIST);

  const base::FilePath glob_path{kSysfsBatteryPath};
  const auto glob_root = glob_path.DirName();
  const auto glob_pattern = glob_path.BaseName();

  base::FileEnumerator battery_it(glob_root, false,
                                  base::FileEnumerator::FileType::DIRECTORIES,
                                  glob_pattern.value());
  while (true) {
    // TODO(itspeter): Extra take care if there are multiple batteries.
    auto battery_path = battery_it.Next();
    if (battery_path.empty())
      break;

    auto dict_value = MapFilesToDict(battery_path, keys, optional_keys);
    if (dict_value) {
      auto* power_supply_type = dict_value->FindStringKey("type");
      if (!power_supply_type)
        continue;
      if (*power_supply_type != kSysfsExpectedType) {
        LOG(ERROR) << "power_supply_type [" << *power_supply_type
                   << "] is not [" << kSysfsExpectedType << "] for "
                   << battery_path.value();
        continue;
      }
      dict_value->SetStringKey("path", battery_path.value());

      pcrecpp::RE re(R"(BAT(\d+)$)", pcrecpp::RE_Options());
      int32_t battery_index;
      if (!re.PartialMatch(battery_path.value(), &battery_index)) {
        VLOG(1) << "Can't extract index from " << battery_path.value();
      } else {
        // The extracted index starts from 0. Shift it to start from 1.
        dict_value->SetStringKey("index",
                                 base::NumberToString(battery_index + 1));
      }

      result.GetList().push_back(std::move(*dict_value));
    }
  }

  if (result.GetList().size() > 1) {
    LOG(ERROR) << "Multiple batteries is not supported yet.";
    return -1;
  }
  if (!base::JSONWriter::Write(result, output)) {
    LOG(ERROR)
        << "Failed to serialize generic battery probed result to json string";
    return -1;
  }
  return 0;
}

}  // namespace runtime_probe
