// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/storage_tool.h"

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <fstream>
#include <iostream>
#include <linux/limits.h>
#include <mntent.h>
#include <string>
#include <unistd.h>
#include <vector>

#include "debugd/src/helper_utils.h"
#include "debugd/src/process_with_id.h"
#include "debugd/src/process_with_output.h"

namespace debugd {

namespace {

const char kSmartctl[] = "/usr/sbin/smartctl";
const char kBadblocks[] = "/sbin/badblocks";
const char kMountFile[] = "/proc/1/mounts";
const char kSource[] = "/mnt/stateful_partition";
const char kMmc[] = "/usr/bin/mmc";
const char kNvme[] = "/usr/sbin/nvme";

}  // namespace

const std::string StorageTool::GetPartition(const std::string& dst) {
  std::string::const_reverse_iterator part = dst.rbegin();

  if (!isdigit(*part))
    return "";
  part++;

  while (part < dst.rend() && isdigit(*part))
    part++;

  return std::string(part.base(), dst.end());
}

void StorageTool::StripPartition(base::FilePath* dstPath) {
  std::string dst = dstPath->value();
  std::string part = StorageTool::GetPartition(dst);
  if (part.empty())
    return;

  size_t location = dst.rfind(part);
  size_t part_size = part.length();

  // For devices matching dm-NN, the digits are not a partition.
  if (dst.at(location - 1) == '-')
    return;

  // For devices that end with a digit, the kernel uses a 'p'
  // as a separator. E.g., mmcblk1p2 and loop0p1, but not loop0.
  if (dst.at(location - 1) == 'p') {
    location--;
    part_size++;

    base::FilePath basename = dstPath->BaseName();
    std::string bname = basename.value();
    if (bname.compare(0, 4, "loop") == 0 &&
        bname.compare(bname.size() - part.length(), part.length(), part) == 0)
      return;
  }
  dst.erase(location, part_size);
  *dstPath = base::FilePath(dst);
}

const base::FilePath StorageTool::GetDevice(const base::FilePath& filesystemIn,
                                            const base::FilePath& mountsFile) {
  base::FilePath device;
  base::ScopedFILE mountinfo(fopen(mountsFile.value().c_str(), "re"));
  if (!mountinfo) {
    PLOG(ERROR) << "Failed to open " << mountsFile.value();
    return base::FilePath();
  }

  struct mntent mount_entry;
  char buffer[PATH_MAX];

  while (getmntent_r(mountinfo.get(), &mount_entry, buffer, sizeof(buffer))) {
    const std::string mountpoint = mount_entry.mnt_dir;
    if (mountpoint == filesystemIn.value()) {
      device = base::FilePath(mount_entry.mnt_fsname);
      break;
    }
  }

  StorageTool::StripPartition(&device);
  return device;
}

// This function is called by Smartctl to check for ATA devices.
// Smartctl is only supported on ATA devices, so this function
// will return false when other devices are used.
bool StorageTool::IsSupported(const base::FilePath typeFile,
                              const base::FilePath vendFile,
                              std::string* errorMsg) {
  base::FilePath r;
  bool link = base::NormalizeFilePath(typeFile, &r);
  if (!link) {
    PLOG(ERROR) << "Failed to read device type link";
    errorMsg->assign("<Failed to read device type link>");
    return false;
  }

  size_t target = r.value().find("target");
  if (target == -1) {
    errorMsg->assign("<This feature is not supported>");
    return false;
  }

  std::string vend;

  if (!base::ReadFileToString(vendFile, &vend)) {
    PLOG(ERROR) << "Failed to open " << vendFile.value();
    errorMsg->assign("<Failed to open vendor file>");
    return false;
  }

  if (vend.empty()) {
    errorMsg->assign("<Failed to find device type>");
    return false;
  }

  if (vend.compare(0, 3, "ATA") != 0) {
    errorMsg->assign("<This feature is not supported>");
    return false;
  }

  return true;
}

std::string StorageTool::Smartctl(const std::string& option) {
  const base::FilePath device =
      GetDevice(base::FilePath(kSource), base::FilePath(kMountFile));

  if (device.empty()) {
    LOG(ERROR) << "Failed to find device for " << kSource;
    return "<Failed to find device>";
  }

  base::FilePath bname = device.BaseName();

  std::string path;
  if (!GetHelperPath("storage", &path))
    return "<path too long>";

  ProcessWithOutput process;
  // Disabling sandboxing since smartctl requires higher privileges.
  process.DisableSandbox();
  if (!process.Init())
    return "<process init failed>";

  if (bname.value().compare(0, 4, "nvme") == 0) {
    process.AddArg(kSmartctl);

    if (option == "attributes")
      process.AddArg("-A");
    if (option == "capabilities")
      process.AddArg("-c");
    if (option == "error")
      process.AddStringOption("-l", "error");
    if (option == "abort_test" || option == "health" || option == "selftest" ||
        option == "short_test")
      return "<Option not supported>";

  } else {
    const base::FilePath dir =
        base::FilePath("/sys/block/" + bname.value() + "/device/");
    const base::FilePath typeFile = dir.Append("type");
    const base::FilePath vendFile = dir.Append("vendor");
    std::string message;

    if (!IsSupported(typeFile, vendFile, &message)) {
      return message;
    }

    process.AddArg(kSmartctl);

    if (option == "abort_test")
      process.AddArg("-X");
    if (option == "attributes")
      process.AddArg("-A");
    if (option == "capabilities")
      process.AddArg("-c");
    if (option == "error")
      process.AddStringOption("-l", "error");
    if (option == "health")
      process.AddArg("-H");
    if (option == "selftest")
      process.AddStringOption("-l", "selftest");
    if (option == "short_test")
      process.AddStringOption("-t", "short");
  }

  process.AddArg(device.value());
  process.Run();
  std::string output;
  process.GetOutput(&output);
  return output;
}

std::string StorageTool::Start(const base::ScopedFD& outfd) {
  const base::FilePath device =
      GetDevice(base::FilePath(kSource), base::FilePath(kMountFile));

  if (device.empty()) {
    LOG(ERROR) << "Failed to find device for " << kSource;
    return "<Failed to find device>";
  }

  ProcessWithId* p =
      CreateProcess(false /* sandboxed */, false /* access_root_mount_ns */);
  if (!p)
    return "";

  p->AddArg(kBadblocks);
  p->AddArg("-sv");
  p->AddArg(device.value());
  p->BindFd(outfd.get(), STDOUT_FILENO);
  p->BindFd(outfd.get(), STDERR_FILENO);
  LOG(INFO) << "badblocks: running process id: " << p->id();
  p->Start();
  return p->id();
}

std::string StorageTool::Mmc(const std::string& option) {
  ProcessWithOutput process;
  process.DisableSandbox();
  if (!process.Init())
    return "<process init failed>";

  process.AddArg(kMmc);

  if (option == "extcsd_read") {
    process.AddArg("extcsd");
    process.AddArg("read");
  } else if (option == "extcsd_dump") {
    process.AddArg("extcsd");
    process.AddArg("dump");
  } else {
    return "<Option not supported>";
  }

  const base::FilePath rootdev =
      GetDevice(base::FilePath(kSource), base::FilePath(kMountFile));
  process.AddArg(rootdev.value());
  process.Run();
  std::string output;
  process.GetOutput(&output);
  return output;
}

std::string StorageTool::Nvme(const std::string& option) {
  ProcessWithOutput process;
  // Disabling sandboxing since nvme requires higher privileges.
  process.DisableSandbox();
  if (!process.Init())
    return "<process init failed>";

  process.AddArg(kNvme);

  if (option == "identify_controller") {
    process.AddArg("id-ctrl");
    process.AddArg("--vendor-specific");
  } else if (option == "short_self_test") {
    // Command for selftest
    process.AddArg("device-self-test");
    // Namespace of NVMe
    process.AddArg("-n 1");
    // type of selftest: short
    process.AddArg("-s 1");
  } else if (option == "long_self_test") {
    // command for selftest
    process.AddArg("device-self-test");
    // Namespace of NVMe
    process.AddArg("-n 1");
    // type of selftest: long
    process.AddArg("-s 2");
  } else {
    return "<Option not supported>";
  }

  const base::FilePath rootdev =
      GetDevice(base::FilePath(kSource), base::FilePath(kMountFile));
  process.AddArg(rootdev.value());
  process.Run();
  std::string output;
  process.GetOutput(&output);
  return output;
}

std::string StorageTool::NvmeLog(const uint32_t& page_id,
                                 const uint32_t& length,
                                 bool raw_binary) {
  ProcessWithOutput process;
  // Disabling sandboxing since nvme requires higher privileges.
  process.DisableSandbox();
  if (!process.Init())
    return "<process init failed>";

  process.AddArg(kNvme);
  process.AddArg("get-log");

  // Log page ID ranging from 0 to 255.
  if (page_id <= 0xff) {
    process.AddArg(base::StringPrintf("--log-id=%u", page_id));
  } else {
    return "<Page ID invalid>";
  }

  // Length of byte-data must be larger than 3.
  if (length >= 4) {
    process.AddArg(base::StringPrintf("--log-len=%u", length));
  } else {
    return "<Length of byte-data invalid. At least 4 bytes for a request>";
  }

  if (raw_binary) {
    process.AddArg("--raw-binary");
  }

  const base::FilePath rootdev =
      GetDevice(base::FilePath(kSource), base::FilePath(kMountFile));
  process.AddArg(rootdev.value());
  process.Run();
  std::string output;
  process.GetOutput(&output);
  return output;
}

}  // namespace debugd
