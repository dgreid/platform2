// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "installer/chromeos_legacy.h"

#include <stdio.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include "installer/inst_util.h"

using std::string;
using std::vector;

bool UpdateLegacyKernel(const InstallConfig& install_config) {
  const base::FilePath root_mount(install_config.root.mount());
  const base::FilePath boot_mount(install_config.boot.mount());

  const base::FilePath kernel_from = root_mount.Append("boot/vmlinuz");
  const base::FilePath kernel_to =
      boot_mount.Append("syslinux").Append("vmlinuz." + install_config.slot);

  return base::CopyFile(kernel_from, kernel_to);
}

string ExplandVerityArguments(const string& kernel_config,
                              const string& root_uuid) {
  string kernel_config_dm = ExtractKernelArg(kernel_config, "dm");

  // The verity config from the kernel contains short hand symbols for
  // partition names that we have to expand to specific UUIDs.

  // %U+1 -> XXX-YYY-ZZZ
  ReplaceAll(&kernel_config_dm, "%U+1", root_uuid);

  // PARTUUID=%U/PARTNROFF=1 -> PARTUUID=XXX-YYY-ZZZ
  ReplaceAll(&kernel_config_dm, "%U/PARTNROFF=1", root_uuid);

  return kernel_config_dm;
}

bool RunLegacyPostInstall(const InstallConfig& install_config) {
  const base::FilePath root_mount(install_config.root.mount());
  const base::FilePath root_syslinux = root_mount.Append("boot/syslinux");
  const base::FilePath boot_mount(install_config.boot.mount());
  const base::FilePath boot_syslinux = boot_mount.Append("syslinux");
  printf("Running LegacyPostInstall\n");

  if (RunCommand({"cp", "-nR", root_syslinux.value(), boot_mount.value()}) !=
      0) {
    return false;
  }

  if (!UpdateLegacyKernel(install_config))
    return false;

  string kernel_config = DumpKernelConfig(install_config.kernel.device());
  string kernel_config_root = ExtractKernelArg(kernel_config, "root");

  // Prepare the new default.cfg

  string verity_enabled =
      (IsReadonly(kernel_config_root) ? "chromeos-vhd" : "chromeos-hd");

  string default_syslinux_cfg = base::StringPrintf(
      "DEFAULT %s.%s\n", verity_enabled.c_str(), install_config.slot.c_str());

  const base::FilePath syslinux_cfg = boot_syslinux.Append("default.cfg");
  if (!base::WriteFile(syslinux_cfg, default_syslinux_cfg))
    return false;

  // Prepare the new root.A/B.cfg

  const base::FilePath old_root_cfg_file =
      root_syslinux.Append("root." + install_config.slot + ".cfg");
  const base::FilePath new_root_cfg_file =
      boot_syslinux.Append(old_root_cfg_file.BaseName());

  // Copy over the unmodified version for this release...
  if (!base::CopyFile(old_root_cfg_file, new_root_cfg_file))
    return false;

  // Insert the proper root device for non-verity boots
  if (!ReplaceInFile("HDROOT" + install_config.slot,
                     install_config.root.device(), new_root_cfg_file))
    return false;

  string kernel_config_dm =
      ExplandVerityArguments(kernel_config, install_config.root.uuid());

  if (kernel_config_dm.empty()) {
    printf("Failed to extract Verity arguments.");
    return false;
  }

  // Insert the proper verity options for verity boots
  if (!ReplaceInFile("DMTABLE" + install_config.slot, kernel_config_dm,
                     new_root_cfg_file))
    return false;

  return true;
}

// Copy a file from the root partition to the boot partition.
bool CopyBootFile(const InstallConfig& install_config,
                  const std::string& src,
                  const std::string& dst) {
  bool result = true;
  const base::FilePath root_mount(install_config.root.mount());
  const base::FilePath boot_mount(install_config.boot.mount());
  const base::FilePath src_path = root_mount.Append(src);
  const base::FilePath dst_path = boot_mount.Append(dst);

  // If the source file file exists, copy it into place, else do nothing.
  if (base::PathExists(src_path)) {
    printf("Copying '%s' to '%s'\n", src_path.value().c_str(),
           dst_path.value().c_str());
    result = base::CopyFile(src_path, dst_path);
  } else {
    printf("Not present to install: '%s'\n", src_path.value().c_str());
  }
  return result;
}

bool RunLegacyUBootPostInstall(const InstallConfig& install_config) {
  bool result = true;
  printf("Running LegacyUBootPostInstall\n");

  result &= CopyBootFile(install_config,
                         "boot/boot-" + install_config.slot + ".scr.uimg",
                         "u-boot/boot.scr.uimg");
  result &= CopyBootFile(
      install_config, "boot/uEnv." + install_config.slot + ".txt", "uEnv.txt");
  result &= CopyBootFile(install_config, "boot/MLO", "MLO");
  result &= CopyBootFile(install_config, "boot/u-boot.img", "u-boot.img");

  return result;
}

bool UpdateEfiBootloaders(const InstallConfig& install_config) {
  bool result = true;
  const base::FilePath src_dir =
      base::FilePath(install_config.root.mount()).Append("boot/efi/boot");
  const base::FilePath dest_dir =
      base::FilePath(install_config.boot.mount()).Append("efi/boot");
  base::FileEnumerator file_enum(src_dir, false, base::FileEnumerator::FILES,
                                 "*.efi");
  for (auto src = file_enum.Next(); !src.empty(); src = file_enum.Next()) {
    const base::FilePath dest = dest_dir.Append(src.BaseName());
    if (!base::CopyFile(src, dest))
      result = false;
  }
  return result;
}

bool RunEfiPostInstall(const InstallConfig& install_config) {
  printf("Running EfiPostInstall\n");

  // Update the kernel we are about to use.
  if (!UpdateLegacyKernel(install_config))
    return false;

  if (!UpdateEfiBootloaders(install_config))
    return false;

  // Of the form: PARTUUID=XXX-YYY-ZZZ
  string kernel_config = DumpKernelConfig(install_config.kernel.device());
  string root_uuid = install_config.root.uuid();
  string kernel_config_dm = ExplandVerityArguments(kernel_config, root_uuid);

  base::FilePath grub_path =
      base::FilePath(install_config.boot.mount()).Append("efi/boot/grub.cfg");

  // Read in the grub.cfg to be updated.
  string grub_src;
  if (!base::ReadFileToString(grub_path, &grub_src)) {
    printf("Unable to read grub template file %s\n", grub_path.value().c_str());
    return false;
  }

  string output;
  if (!EfiGrubUpdate(grub_src, install_config.slot, root_uuid, kernel_config_dm,
                     &output)) {
    return false;
  }

  // Write out the new grub.cfg.
  if (!base::WriteFile(grub_path, output)) {
    printf("Unable to write boot menu file %s\n", grub_path.value().c_str());
    return false;
  }

  // We finished.
  return true;
}

bool EfiGrubUpdate(const string& input,
                   const string& slot,
                   const string& root_uuid,
                   const string& verity_args,
                   string* output) {
  // Split the file contents into lines.
  vector<string> file_lines = base::SplitString(
      input, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);

  // Search pattern for lines are related to our slot.
  string kernel_pattern = "/syslinux/vmlinuz." + slot;

  for (vector<string>::iterator line = file_lines.begin();
       line < file_lines.end(); line++) {
    if (line->find(kernel_pattern) != string::npos) {
      if (ExtractKernelArg(*line, "dm").empty()) {
        // If it's an unverified boot line, just set the root partition to boot.
        if (!SetKernelArg("root", "PARTUUID=" + root_uuid, &(*line))) {
          printf("Unable to update unverified root flag in %s.\n",
                 line->c_str());
          return false;
        }
      } else {
        if (!SetKernelArg("dm", verity_args, &(*line))) {
          printf("Unable to update verified dm flag.\n");
          return false;
        }
      }
    }
  }

  // Join the lines back into file contents.
  *output = base::JoinString(file_lines, "\n");

  // Other EFI post-install actions, if any, go here.
  return true;
}
