// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// There are several methods to use lvm2 constructs from C or C++ code:
// - liblvm2app (deprecated) is a natural interface to creating and using
//   lvm2 construct objects and performing operations on them. However, the
//   deprecation of this library renders its use a non-starter.
// - executing the command line utilities directly.
// - liblvm2cmd provides an interface to run lvm2 commands without the
//   indirection of another process execution. While this is faster, the output
//   collection mechanism for liblvm2cmd relies on overriding the logging
//   function.
// - lvmdbusd is a daemon (written in Python) with a D-Bus interface which
//   exec()s the relevant commands. However, this library is additionally
//   intended to be used in situations where D-Bus may or may not be running.
//
// To strike a balance between speed and usability, the following class uses
// liblvm2cmd for commands without output (eg. pvcreate, vgcreate ...) and
// uses a process invocation for the rest.

#include "brillo/blkdev_utils/lvm.h"

#include <base/json/json_reader.h>

namespace brillo {

namespace {

// LVM reports are structured as:
//  {
//      "report": [
//          {
//              "lv": [
//                  {"lv_name":"foo", "vg_name":"bar", ...},
//                  {...}
//              ]
//          }
//      ]
//  }
//
// Common function to fetch the underlying dictionary (assume for now
// that the reports will be reporting just a single type (lv/vg/pv) for now).
base::Optional<base::Value> UnwrapReportContents(const std::string& output,
                                                 const std::string& key) {
  auto report = base::JSONReader::Read(output);
  base::DictionaryValue* dictionary_report;
  if (!report || !report->is_dict() ||
      !report->GetAsDictionary(&dictionary_report)) {
    LOG(ERROR) << "Failed to get report as dictionary";
    return base::nullopt;
  }

  base::ListValue* report_list;
  if (!dictionary_report->GetList("report", &report_list)) {
    LOG(ERROR) << "Failed to find 'report' list";
    return base::nullopt;
  }

  if (report_list->GetSize() != 1) {
    LOG(ERROR) << "Unexpected size: " << report_list->GetSize();
    return base::nullopt;
  }

  base::DictionaryValue* report_dictionary;
  if (!report_list->GetDictionary(0, &report_dictionary)) {
    LOG(ERROR) << "Failed to find 'report' dictionary";
    return base::nullopt;
  }

  base::ListValue* key_list;
  if (!report_dictionary->GetList(key, &key_list)) {
    LOG(ERROR) << "Failed to find " << key << " list";
    return base::nullopt;
  }

  // If the list has just a single dictionary element, return it directly.
  if (key_list && key_list->GetSize() == 1) {
    base::DictionaryValue* key_dictionary;
    if (!key_list->GetDictionary(0, &key_dictionary)) {
      LOG(ERROR) << "Failed to get " << key << " dictionary";
      return base::nullopt;
    }
    return key_dictionary->Clone();
  }

  return key_list->Clone();
}

}  // namespace

LogicalVolumeManager::LogicalVolumeManager()
    : LogicalVolumeManager(std::make_shared<LvmCommandRunner>()) {}

LogicalVolumeManager::LogicalVolumeManager(
    std::shared_ptr<LvmCommandRunner> lvm)
    : lvm_(lvm) {}

bool LogicalVolumeManager::ValidatePhysicalVolume(
    const base::FilePath& device_path, std::string* volume_group_name) {
  std::string output;

  if (!lvm_->RunProcess({"/sbin/pvdisplay", "-C", "--reportformat", "json",
                         device_path.value()},
                        &output)) {
    LOG(ERROR) << "Failed to get output from pvdisplay";
    return false;
  }

  base::Optional<base::Value> report_contents =
      UnwrapReportContents(output, "pv");
  base::DictionaryValue* pv_dictionary;

  if (!report_contents || !report_contents->GetAsDictionary(&pv_dictionary)) {
    LOG(ERROR) << "Failed to get report contents";
    return false;
  }

  std::string pv_name;
  if (!pv_dictionary->GetString("pv_name", &pv_name) &&
      pv_name != device_path.value()) {
    LOG(ERROR) << "Mismatched value: expected: " << device_path
               << " actual: " << pv_name;
    return false;
  }

  if (volume_group_name) {
    if (!pv_dictionary->GetString("vg_name", volume_group_name)) {
      LOG(ERROR) << "Failed to fetch volume group name";
      return false;
    }
  }

  return true;
}

base::Optional<PhysicalVolume> LogicalVolumeManager::GetPhysicalVolume(
    const base::FilePath& device_path) {
  return ValidatePhysicalVolume(device_path, nullptr)
             ? base::make_optional(PhysicalVolume(device_path, lvm_))
             : base::nullopt;
}

base::Optional<VolumeGroup> LogicalVolumeManager::GetVolumeGroup(
    const PhysicalVolume& pv) {
  std::string vg_name;
  return ValidatePhysicalVolume(pv.GetPath(), &vg_name)
             ? base::make_optional(VolumeGroup(vg_name, lvm_))
             : base::nullopt;
}

bool LogicalVolumeManager::ValidateLogicalVolume(const VolumeGroup& vg,
                                                 const std::string& lv_name,
                                                 bool is_thinpool) {
  std::string output;
  const std::string vg_name = vg.GetName();

  std::string pool_lv_check = is_thinpool ? "pool_lv=\"\"" : "pool_lv!=\"\"";

  if (!lvm_->RunProcess({"/sbin/lvdisplay", "-S", pool_lv_check, "-C",
                         "--reportformat", "json", vg_name + "/" + lv_name},
                        &output)) {
    LOG(ERROR) << "Failed to get output from lvdisplay";
    return false;
  }

  base::Optional<base::Value> report_contents =
      UnwrapReportContents(output, "lv");
  base::DictionaryValue* lv_dictionary;

  if (!report_contents || !report_contents->GetAsDictionary(&lv_dictionary)) {
    LOG(ERROR) << "Failed to get report contents";
    return false;
  }

  std::string output_lv_name;
  if (!lv_dictionary->GetString("lv_name", &output_lv_name) &&
      output_lv_name != lv_name) {
    LOG(ERROR) << "Mismatched value: expected: " << lv_name
               << " actual: " << output_lv_name;
    return false;
  }

  return true;
}

base::Optional<Thinpool> LogicalVolumeManager::GetThinpool(
    const VolumeGroup& vg, const std::string& thinpool_name) {
  return ValidateLogicalVolume(vg, thinpool_name, true /* is_thinpool */)
             ? base::make_optional(Thinpool(thinpool_name, vg.GetName(), lvm_))
             : base::nullopt;
}

base::Optional<LogicalVolume> LogicalVolumeManager::GetLogicalVolume(
    const VolumeGroup& vg, const std::string& lv_name) {
  return ValidateLogicalVolume(vg, lv_name, false /* is_thinpool */)
             ? base::make_optional(LogicalVolume(lv_name, vg.GetName(), lvm_))
             : base::nullopt;
}

std::vector<LogicalVolume> LogicalVolumeManager::ListLogicalVolumes(
    const VolumeGroup& vg) {
  std::string output;
  std::string vg_name = vg.GetName();
  std::vector<LogicalVolume> lv_vector;

  if (!lvm_->RunProcess({"/sbin/lvdisplay", "-S", "pool_lv!=\"\"", "-C",
                         "--reportformat", "json", vg_name},
                        &output)) {
    LOG(ERROR) << "Failed to get output from lvdisplay";
    return lv_vector;
  }

  base::Optional<base::Value> report_contents =
      UnwrapReportContents(output, "lv");
  base::ListValue* lv_list;
  if (!report_contents || !report_contents->GetAsList(&lv_list)) {
    LOG(ERROR) << "Failed to get report contents";
    return lv_vector;
  }

  for (size_t i = 0; i < lv_list->GetSize(); i++) {
    base::DictionaryValue* lv_dictionary;
    if (!lv_list->GetDictionary(i, &lv_dictionary)) {
      LOG(ERROR) << "Failed to get dictionary value for physical volume";
      continue;
    }

    std::string output_lv_name;
    if (!lv_dictionary->GetString("lv_name", &output_lv_name)) {
      LOG(ERROR) << "Failed to get logical volume name";
      continue;
    }

    lv_vector.push_back(LogicalVolume(output_lv_name, vg_name, lvm_));
  }

  return lv_vector;
}

base::Optional<PhysicalVolume> LogicalVolumeManager::CreatePhysicalVolume(
    const base::FilePath& device_path) {
  return lvm_->RunCommand({"pvcreate", "-ff", "--yes", device_path.value()})
             ? base::make_optional(PhysicalVolume(device_path, lvm_))
             : base::nullopt;
}

base::Optional<VolumeGroup> LogicalVolumeManager::CreateVolumeGroup(
    const PhysicalVolume& pv, const std::string& vg_name) {
  return lvm_->RunCommand(
             {"vgcreate", "-p", "1", vg_name, pv.GetPath().value()})
             ? base::make_optional(VolumeGroup(vg_name, lvm_))
             : base::nullopt;
}

base::Optional<Thinpool> LogicalVolumeManager::CreateThinpool(
    const VolumeGroup& vg, const base::DictionaryValue& config) {
  std::vector<std::string> cmd = {"lvcreate"};
  std::string size, metadata_size, name;
  if (!config.GetString("size", &size) || !config.GetString("name", &name) ||
      !config.GetString("metadata_size", &metadata_size)) {
    LOG(ERROR) << "Invalid configuration";
    return base::nullopt;
  }

  cmd.insert(cmd.end(),
             {"--size", size + "M", "--poolmetadatasize", metadata_size + "M",
              "--thinpool", name, vg.GetName()});

  return lvm_->RunCommand(cmd)
             ? base::make_optional(Thinpool(name, vg.GetName(), lvm_))
             : base::nullopt;
}

base::Optional<LogicalVolume> LogicalVolumeManager::CreateLogicalVolume(
    const VolumeGroup& vg,
    const Thinpool& thinpool,
    const base::DictionaryValue& config) {
  std::vector<std::string> cmd = {"lvcreate", "--thin"};
  std::string size, name;
  if (!config.GetString("size", &size) || !config.GetString("name", &name)) {
    LOG(ERROR) << "Invalid configuration";
    return base::nullopt;
  }

  cmd.insert(cmd.end(), {"-V", size + "M", "-n", name, thinpool.GetName()});

  return lvm_->RunCommand(cmd)
             ? base::make_optional(LogicalVolume(name, vg.GetName(), lvm_))
             : base::nullopt;
}

}  // namespace brillo
