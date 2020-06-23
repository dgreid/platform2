/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "runtime_probe/functions/usb_camera.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/json/json_writer.h>
#include <base/strings/stringprintf.h>
#include <base/values.h>

namespace runtime_probe {

namespace {
constexpr char kDevVideoPath[] = "/dev/video*";

struct FieldType {
  std::string key_name;
  std::string file_name;
};
const std::vector<FieldType> kRequiredFields{{"usb_vendor_id", "idVendor"},
                                             {"usb_product_id", "idProduct"}};
const std::vector<FieldType> kOptionalFields{
    {"usb_manufacturer", "manufacturer"},
    {"usb_product", "product"},
    {"usb_bcd_device", "bcdDevice"}};

bool IsCaptureDevice(const base::FilePath& path) {
  int32_t fd = open(path.value().c_str(), O_RDONLY);
  if (fd == -1) {
    LOG(ERROR) << "Failed to open " << path;
    return false;
  }

  v4l2_capability cap;
  if (ioctl(fd, VIDIOC_QUERYCAP, &cap, 1) == -1) {
    LOG(ERROR) << "Failed to execute ioctl to query the V4L2 capability";
    return false;
  }
  if (close(fd) == -1) {
    LOG(ERROR) << "Failed to close " << path;
  }

  uint32_t mask = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps
                                                            : cap.capabilities;
  return (mask & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE)) &&
         !(mask & (V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_VIDEO_OUTPUT_MPLANE)) &&
         !(mask & (V4L2_CAP_VIDEO_M2M | V4L2_CAP_VIDEO_M2M_MPLANE));
}

bool ReadSysfs(const base::FilePath& path,
               const FieldType& field,
               std::string* content) {
  base::FilePath field_path(base::StringPrintf(
      "/sys/class/video4linux/%s/device/../%s", path.BaseName().value().c_str(),
      field.file_name.c_str()));
  base::FilePath normalized_path;
  if (!base::NormalizeFilePath(field_path, &normalized_path)) {
    return false;
  }
  if (!base::ReadFileToString(normalized_path, content)) {
    LOG(ERROR) << "Failed to read the file " << normalized_path;
    return false;
  }
  base::TrimString(*content, " \n", content);
  return true;
}

bool ReadUsbSysfs(const base::FilePath& path, base::Value* res) {
  std::string content;
  for (const auto& field : kRequiredFields) {
    if (!ReadSysfs(path, field, &content)) {
      LOG(ERROR) << "Failed to read the required field " << field.key_name;
      return false;
    }
    res->SetStringKey(field.key_name, content);
  }
  for (const auto& field : kOptionalFields) {
    std::string content;
    if (ReadSysfs(path, field, &content)) {
      res->SetStringKey(field.key_name, content);
    }
  }
  return true;
}

bool ExploreAsUsbCamera(const base::FilePath& path, base::Value* res) {
  return IsCaptureDevice(path) && ReadUsbSysfs(path, res);
}

}  // namespace

std::unique_ptr<ProbeFunction> UsbCameraFunction::FromValue(
    const base::Value& dict_value) {
  if (dict_value.DictSize() != 0) {
    LOG(ERROR) << function_name << " does not take any arguments.";
    return nullptr;
  }
  return std::make_unique<UsbCameraFunction>();
}

UsbCameraFunction::DataType UsbCameraFunction::Eval() const {
  auto json_output = InvokeHelperToJSON();
  if (!json_output) {
    LOG(ERROR) << "Failed to invoke helper to retrieve usb camera results.";
    return {};
  }
  if (!json_output->is_list()) {
    LOG(ERROR) << "Failed to parse json output as list.";
    return {};
  }

  // TODO(b/161770131): replace with TakeList() after libchrome uprev.
  return DataType(std::move(json_output->GetList()));
}

int UsbCameraFunction::EvalInHelper(std::string* output) const {
  base::Value result(base::Value::Type::LIST);

  base::FilePath glob_path = base::FilePath(kDevVideoPath);
  const auto glob_root = glob_path.DirName();
  const auto glob_pattern = glob_path.BaseName();
  base::FileEnumerator path_it(glob_root, false,
                               base::FileEnumerator::FileType::FILES,
                               glob_pattern.value());
  for (auto video_path = path_it.Next(); !video_path.empty();
       video_path = path_it.Next()) {
    base::Value res(base::Value::Type::DICTIONARY);
    res.SetStringKey("path", video_path.value());
    if (ExploreAsUsbCamera(video_path, &res)) {
      res.SetStringKey("bus_type", "usb");
      result.GetList().push_back(std::move(res));
    }
  }

  if (!base::JSONWriter::Write(result, output)) {
    LOG(ERROR) << "Failed to serialize usb camera result to json string";
    return -1;
  }
  return 0;
}

}  // namespace runtime_probe
