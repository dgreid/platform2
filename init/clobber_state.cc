// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/clobber_state.h"

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

// Keep after <sys/mount.h> to avoid build errors.
#include <linux/fs.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bits.h>
#include <base/callback_helpers.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/process/process.h>
#include <rootdev/rootdev.h>
#include <vboot/cgpt_params.h>
#include <vboot/vboot_host.h>
#include <chromeos/secure_erase_file/secure_erase_file.h>

#include "init/crossystem.h"

namespace {

constexpr char kStatefulPath[] = "/mnt/stateful_partition";
constexpr char kPowerWashCountPath[] = "unencrypted/preserve/powerwash_count";
constexpr char kClobberLogPath[] = "/tmp/clobber-state.log";
constexpr char kBioWashPath[] = "/usr/bin/bio_wash";
constexpr char kPreservedFilesTarPath[] = "/tmp/preserve.tar";
constexpr char kStatefulClobberLogPath[] = "unencrypted/clobber.log";
// Keep file names in sync with update_engine prefs.
constexpr char kLastPingDate[] = "last-active-ping-day";
constexpr char kLastRollcallDate[] = "last-roll-call-ping-day";
constexpr char kUpdateEnginePrefsPath[] = "/var/lib/update_engine/prefs/";
constexpr char kUpdateEnginePreservePath[] =
    "unencrypted/preserve/update_engine/prefs/";

// The presence of this file indicates that crash report collection across
// clobber is disabled in developer mode.
constexpr char kDisableClobberCrashCollectionPath[] =
    "/run/disable-clobber-crash-collection";
// The presence of this file indicates that the kernel supports ext4 directory
// level encryption.
constexpr char kExt4DircryptoSupportedPath[] =
    "/sys/fs/ext4/features/encryption";

constexpr char kUbiRootDisk[] = "/dev/mtd0";
constexpr char kUbiDevicePrefix[] = "/dev/ubi";
constexpr char kUbiDeviceStatefulFormat[] = "/dev/ubi%d_0";

constexpr base::TimeDelta kMinClobberDuration = base::TimeDelta::FromMinutes(5);

// |strip_partition| attempts to remove the partition number from the result.
base::FilePath GetRootDevice(bool strip_partition) {
  char buf[PATH_MAX];
  int ret = rootdev(buf, PATH_MAX, /*use_slave=*/true, strip_partition);
  if (ret == 0) {
    return base::FilePath(buf);
  } else {
    return base::FilePath();
  }
}

void CgptFindShowFunctionNoOp(struct CgptFindParams*,
                              const char*,
                              int,
                              GptEntry*) {}

bool ReadFileToInt(const base::FilePath& path, int* value) {
  std::string str;
  if (!base::ReadFileToString(path, &str)) {
    return false;
  }
  base::TrimWhitespaceASCII(str, base::TRIM_ALL, &str);
  return base::StringToInt(str, value);
}

// Calculate the maximum number of bad blocks per 1024 blocks for UBI.
int CalculateUBIMaxBadBlocksPer1024(int partition_number) {
  // The max bad blocks per 1024 is based on total device size,
  // not the partition size.
  int mtd_size = 0;
  ReadFileToInt(base::FilePath("/sys/class/mtd/mtd0/size"), &mtd_size);

  int erase_size;
  ReadFileToInt(base::FilePath("/sys/class/mtd/mtd0/erasesize"), &erase_size);

  int block_count = mtd_size / erase_size;

  int reserved_error_blocks = 0;
  base::FilePath reserved_for_bad(base::StringPrintf(
      "/sys/class/ubi/ubi%d/reserved_for_bad", partition_number));
  ReadFileToInt(reserved_for_bad, &reserved_error_blocks);
  return reserved_error_blocks * 1024 / block_count;
}

bool GetBlockCount(const base::FilePath& device_path,
                   int64_t block_size,
                   int64_t* block_count_out) {
  if (!block_count_out)
    return false;

  brillo::ProcessImpl dumpe2fs;
  dumpe2fs.AddArg("/sbin/dumpe2fs");
  dumpe2fs.AddArg("-h");
  dumpe2fs.AddArg(device_path.value());

  base::FilePath temp_file;
  base::CreateTemporaryFile(&temp_file);
  dumpe2fs.RedirectOutput(temp_file.value());
  if (dumpe2fs.Run() == 0) {
    std::string output;
    base::ReadFileToString(temp_file, &output);
    size_t label = output.find("Block count");
    size_t value_start = output.find_first_of("0123456789", label);
    size_t value_end = output.find_first_not_of("0123456789", value_start);

    if (value_start != std::string::npos && value_end != std::string::npos) {
      int64_t block_count;
      if (base::StringToInt64(
              output.substr(value_start, value_end - value_start),
              &block_count)) {
        *block_count_out = block_count;
        return true;
      }
    }
  }

  // Fallback if using dumpe2fs failed. This interface always returns a count
  // of sectors, not blocks, so we must convert to a block count.
  // Per "include/linux/types.h", Linux always considers sectors to be
  // 512 bytes long.
  base::FilePath fp("/sys/class/block");
  fp = fp.Append(device_path.BaseName());
  fp = fp.Append("size");
  std::string sector_count_str;
  if (base::ReadFileToString(fp, &sector_count_str)) {
    base::TrimWhitespaceASCII(sector_count_str, base::TRIM_ALL,
                              &sector_count_str);
    int64_t sector_count;
    if (base::StringToInt64(sector_count_str, &sector_count)) {
      *block_count_out = sector_count * 512 / block_size;
      return true;
    }
  }
  return false;
}

void AppendFileToLog(const base::FilePath& file) {
  std::string file_contents;
  if (!base::ReadFileToString(file, &file_contents)) {
    // Even if reading file failed, some of its contents may have been read
    // successfully, so we still attempt to append them to our log file.
    PLOG(ERROR) << "Reading from temporary file failed: " << file.value();
  }

  if (!base::AppendToFile(base::FilePath(kClobberLogPath),
                          file_contents.c_str(), file_contents.size())) {
    PLOG(ERROR) << "Appending " << file.value()
                << " to clobber-state log failed";
  }
}

// Attempt to save logs from the boot when the clobber happened into the
// stateful partition.
void CollectClobberCrashReports() {
  brillo::ProcessImpl crash_reporter_early_collect;
  crash_reporter_early_collect.AddArg("/sbin/crash_reporter");
  crash_reporter_early_collect.AddArg("--early");
  crash_reporter_early_collect.AddArg("--log_to_stderr");
  crash_reporter_early_collect.AddArg("--preserve_across_clobber");
  crash_reporter_early_collect.AddArg("--boot_collect");
  if (crash_reporter_early_collect.Run() != 0)
    LOG(WARNING) << "Unable to collect logs and crashes from current run.";

  return;
}

bool CreateEncryptedRebootVault() {
  brillo::ProcessImpl create_erv;
  create_erv.AddArg("/usr/sbin/encrypted-reboot-vault");
  create_erv.AddArg("--action=create");
  if (create_erv.Run() != 0)
    return false;

  return true;
}

}  // namespace

// static
ClobberState::Arguments ClobberState::ParseArgv(int argc,
                                                char const* const argv[]) {
  Arguments args;
  if (argc <= 1)
    return args;

  // Due to historical usage, the command line parsing is a bit weird.
  // We split the first argument into multiple keywords.
  std::vector<std::string> split_args = base::SplitString(
      argv[1], " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (int i = 2; i < argc; ++i)
    split_args.push_back(argv[i]);

  for (const std::string& arg : split_args) {
    if (arg == "factory") {
      args.factory_wipe = true;
    } else if (arg == "fast") {
      args.fast_wipe = true;
    } else if (arg == "keepimg") {
      args.keepimg = true;
    } else if (arg == "safe") {
      args.safe_wipe = true;
    } else if (arg == "rollback") {
      args.rollback_wipe = true;
    } else if (base::StartsWith(
                   arg, "reason=", base::CompareCase::INSENSITIVE_ASCII)) {
      args.reason = arg;
    }
  }

  return args;
}

// static
bool ClobberState::IncrementFileCounter(const base::FilePath& path) {
  int value;
  if (!ReadFileToInt(path, &value) || value < 0 || value >= INT_MAX) {
    return base::WriteFile(path, "1\n", 2) == 2;
  }

  std::string new_value = std::to_string(value + 1);
  new_value.append("\n");
  return new_value.size() ==
         base::WriteFile(path, new_value.c_str(), new_value.size());
}

// static
int ClobberState::PreserveFiles(
    const base::FilePath& preserved_files_root,
    const std::vector<base::FilePath>& preserved_files,
    const base::FilePath& tar_file_path) {
  // Remove any stale tar files from previous clobber-state runs.
  base::DeleteFile(tar_file_path);

  // We want to preserve permissions and recreate the directory structure
  // for all of the files in |preserved_files|. In order to do so we run tar
  // --no-recursion and specify the names of each of the parent directories.
  // For example for home/.shadow/install_attributes.pb
  // we pass to tar home, home/.shadow, home/.shadow/install_attributes.pb.
  std::vector<std::string> paths_to_tar;
  for (const base::FilePath& path : preserved_files) {
    // All paths should be relative to |preserved_files_root|.
    if (path.IsAbsolute()) {
      LOG(WARNING) << "Non-relative path " << path.value()
                   << " passed to PreserveFiles, ignoring.";
      continue;
    }
    if (!base::PathExists(preserved_files_root.Append(path)))
      continue;
    base::FilePath current = path;
    while (current != base::FilePath(base::FilePath::kCurrentDirectory)) {
      // List of paths is built in an order that is reversed from what we want
      // (parent directories first), but will then be passed to tar in reverse
      // order.
      //
      // e.g. for home/.shadow/install_attributes.pb, |paths_to_tar| will have
      // home/.shadow/install_attributes.pb, then home/.shadow, then home.
      paths_to_tar.push_back(current.value());
      current = current.DirName();
    }
  }

  // We can't create an empty tar file.
  if (paths_to_tar.size() == 0) {
    LOG(INFO)
        << "PreserveFiles found no files to preserve, no tar file created.";
    return 0;
  }

  brillo::ProcessImpl tar;
  tar.AddArg("/bin/tar");
  tar.AddArg("-c");
  tar.AddStringOption("-f", tar_file_path.value());
  tar.AddStringOption("-C", preserved_files_root.value());
  tar.AddArg("--no-recursion");
  tar.AddArg("--");

  // Add paths in reverse order because we built up the list of paths backwards.
  for (auto it = paths_to_tar.rbegin(); it != paths_to_tar.rend(); ++it) {
    tar.AddArg(*it);
  }
  return tar.Run();
}

// static
bool ClobberState::GetDevicePathComponents(const base::FilePath& device,
                                           std::string* base_device_out,
                                           int* partition_out) {
  if (!partition_out || !base_device_out)
    return false;
  const std::string& path = device.value();

  // MTD devices sometimes have a trailing "_0" after the partition which
  // we should ignore.
  std::string mtd_suffix = "_0";
  size_t suffix_index = path.length();
  if (base::EndsWith(path, mtd_suffix, base::CompareCase::SENSITIVE)) {
    suffix_index = path.length() - mtd_suffix.length();
  }

  size_t last_non_numeric =
      path.find_last_not_of("0123456789", suffix_index - 1);

  // If there are no non-numeric characters, this is a malformed device.
  if (last_non_numeric == std::string::npos) {
    return false;
  }

  std::string partition_number_string =
      path.substr(last_non_numeric + 1, suffix_index - (last_non_numeric + 1));
  int partition_number;
  if (!base::StringToInt(partition_number_string, &partition_number)) {
    return false;
  }
  *partition_out = partition_number;
  *base_device_out = path.substr(0, last_non_numeric + 1);
  return true;
}

bool ClobberState::IsRotational(const base::FilePath& device_path) {
  if (!dev_.IsParent(device_path)) {
    LOG(ERROR) << "Non-device given as argument to IsRotational: "
               << device_path.value();
    return false;
  }

  // Since there doesn't seem to be a good way to get from a partition name
  // to the base device name beyond simple heuristics, just find the device
  // with the same major number but with minor 0.
  struct stat st;
  if (Stat(device_path, &st) != 0) {
    return false;
  }
  unsigned int major_device_number = major(st.st_rdev);

  base::FileEnumerator enumerator(dev_, /*recursive=*/true,
                                  base::FileEnumerator::FileType::FILES);
  for (base::FilePath base_device_path = enumerator.Next();
       !base_device_path.empty(); base_device_path = enumerator.Next()) {
    if (Stat(base_device_path, &st) == 0 && S_ISBLK(st.st_mode) &&
        major(st.st_rdev) == major_device_number && minor(st.st_rdev) == 0) {
      // |base_device_path| must be the base device for |device_path|.
      base::FilePath rotational_file = sys_.Append("block")
                                           .Append(base_device_path.BaseName())
                                           .Append("queue/rotational");

      int value;
      if (ReadFileToInt(rotational_file, &value)) {
        return value == 1;
      }
    }
  }
  return false;
}

// static
bool ClobberState::GetDevicesToWipe(
    const base::FilePath& root_disk,
    const base::FilePath& root_device,
    const ClobberState::PartitionNumbers& partitions,
    ClobberState::DeviceWipeInfo* wipe_info_out) {
  if (!wipe_info_out) {
    LOG(ERROR) << "wipe_info_out must be non-null";
    return false;
  }

  if (partitions.root_a < 0 || partitions.root_b < 0 ||
      partitions.kernel_a < 0 || partitions.kernel_b < 0 ||
      partitions.stateful < 0) {
    LOG(ERROR) << "Invalid partition numbers for GetDevicesToWipe";
    return false;
  }

  if (root_disk.empty()) {
    LOG(ERROR) << "Invalid root disk for GetDevicesToWipe";
    return false;
  }

  if (root_device.empty()) {
    LOG(ERROR) << "Invalid root device for GetDevicesToWipe";
    return false;
  }

  std::string base_device;
  int active_root_partition;
  if (!GetDevicePathComponents(root_device, &base_device,
                               &active_root_partition)) {
    LOG(ERROR) << "Extracting partition number and base device from "
                  "root_device failed: "
               << root_device.value();
    return false;
  }

  ClobberState::DeviceWipeInfo wipe_info;
  if (active_root_partition == partitions.root_a) {
    wipe_info.inactive_root_device =
        base::FilePath(base_device + std::to_string(partitions.root_b));
    wipe_info.inactive_kernel_device =
        base::FilePath(base_device + std::to_string(partitions.kernel_b));
    wipe_info.active_kernel_partition = partitions.kernel_a;
  } else if (active_root_partition == partitions.root_b) {
    wipe_info.inactive_root_device =
        base::FilePath(base_device + std::to_string(partitions.root_a));
    wipe_info.inactive_kernel_device =
        base::FilePath(base_device + std::to_string(partitions.kernel_a));
    wipe_info.active_kernel_partition = partitions.kernel_b;
  } else {
    LOG(ERROR) << "Active root device partition number ("
               << active_root_partition
               << ") does not match either root partition number: "
               << partitions.root_a << ", " << partitions.root_b;
    return false;
  }

  base::FilePath kernel_device;
  if (root_disk == base::FilePath(kUbiRootDisk)) {
    /*
     * WARNING: This code has not been sufficiently tested and almost certainly
     * does not work. If you are adding support for MTD flash, you would be
     * well served to review it and add test coverage.
     */

    // Special casing for NAND devices.
    wipe_info.is_mtd_flash = true;
    wipe_info.stateful_device = base::FilePath(
        base::StringPrintf(kUbiDeviceStatefulFormat, partitions.stateful));

    // On NAND, kernel is stored on /dev/mtdX.
    if (active_root_partition == partitions.root_a) {
      kernel_device =
          base::FilePath("/dev/mtd" + std::to_string(partitions.kernel_a));
    } else if (active_root_partition == partitions.root_b) {
      kernel_device =
          base::FilePath("/dev/mtd" + std::to_string(partitions.kernel_b));
    }

    /*
     * End of untested MTD code.
     */
  } else {
    wipe_info.stateful_device =
        base::FilePath(base_device + std::to_string(partitions.stateful));

    if (active_root_partition == partitions.root_a) {
      kernel_device =
          base::FilePath(base_device + std::to_string(partitions.kernel_a));
    } else if (active_root_partition == partitions.root_b) {
      kernel_device =
          base::FilePath(base_device + std::to_string(partitions.kernel_b));
    }
  }

  *wipe_info_out = wipe_info;
  return true;
}

// static
bool ClobberState::WipeMTDDevice(
    const base::FilePath& device_path,
    const ClobberState::PartitionNumbers& partitions) {
  /*
   * WARNING: This code has not been sufficiently tested and almost certainly
   * does not work. If you are adding support for MTD flash, you would be
   * well served to review it and add test coverage.
   */

  if (!base::StartsWith(device_path.value(), kUbiDevicePrefix,
                        base::CompareCase::SENSITIVE)) {
    LOG(ERROR) << "Cannot wipe device " << device_path.value();
    return false;
  }

  std::string base_device;
  int partition_number;
  if (!GetDevicePathComponents(device_path, &base_device, &partition_number)) {
    LOG(ERROR) << "Getting partition number from device failed: "
               << device_path.value();
    return false;
  }

  std::string partition_name;
  if (partition_number == partitions.stateful) {
    partition_name = "STATE";
  } else if (partition_number == partitions.root_a) {
    partition_name = "ROOT-A";
  } else if (partition_number == partitions.root_b) {
    partition_name = "ROOT-B";
  } else {
    partition_name = base::StringPrintf("UNKNOWN_%d", partition_number);
    LOG(ERROR) << "Do not know how to name UBI partition for "
               << device_path.value();
  }

  base::FilePath temp_file;
  base::CreateTemporaryFile(&temp_file);

  std::string physical_device =
      base::StringPrintf("/dev/ubi%d", partition_number);
  struct stat st;
  stat(physical_device.c_str(), &st);
  if (!S_ISCHR(st.st_mode)) {
    // Try to attach the volume to obtain info about it.
    brillo::ProcessImpl ubiattach;
    ubiattach.AddArg("/bin/ubiattach");
    ubiattach.AddIntOption("-m", partition_number);
    ubiattach.AddIntOption("-d", partition_number);
    ubiattach.RedirectOutput(temp_file.value());
    ubiattach.Run();
    AppendFileToLog(temp_file);
  }

  int max_bad_blocks_per_1024 =
      CalculateUBIMaxBadBlocksPer1024(partition_number);

  int volume_size;
  base::FilePath data_bytes(base::StringPrintf(
      "/sys/class/ubi/ubi%d_0/data_bytes", partition_number));
  ReadFileToInt(data_bytes, &volume_size);

  brillo::ProcessImpl ubidetach;
  ubidetach.AddArg("/bin/ubidetach");
  ubidetach.AddIntOption("-d", partition_number);
  ubidetach.RedirectOutput(temp_file.value());
  int detach_ret = ubidetach.Run();
  AppendFileToLog(temp_file);
  if (detach_ret) {
    LOG(ERROR) << "Detaching MTD volume failed with code " << detach_ret;
  }

  brillo::ProcessImpl ubiformat;
  ubiformat.AddArg("/bin/ubiformat");
  ubiformat.AddArg("-y");
  ubiformat.AddIntOption("-e", 0);
  ubiformat.AddArg(base::StringPrintf("/dev/mtd%d", partition_number));
  ubiformat.RedirectOutput(temp_file.value());
  int format_ret = ubiformat.Run();
  AppendFileToLog(temp_file);
  if (format_ret) {
    LOG(ERROR) << "Formatting MTD volume failed with code " << format_ret;
  }

  // We need to attach so that we could set max beb/1024 and create a volume.
  // After a volume is created, we don't need to specify max beb/1024 anymore.
  brillo::ProcessImpl ubiattach;
  ubiattach.AddArg("/bin/ubiattach");
  ubiattach.AddIntOption("-d", partition_number);
  ubiattach.AddIntOption("-m", partition_number);
  ubiattach.AddIntOption("--max-beb-per1024", max_bad_blocks_per_1024);
  ubiattach.RedirectOutput(temp_file.value());
  int attach_ret = ubiattach.Run();
  AppendFileToLog(temp_file);
  if (attach_ret) {
    LOG(ERROR) << "Reattaching MTD volume failed with code " << attach_ret;
  }

  brillo::ProcessImpl ubimkvol;
  ubimkvol.AddArg("/bin/ubimkvol");
  ubimkvol.AddIntOption("-s", volume_size);
  ubimkvol.AddStringOption("-N", partition_name);
  ubimkvol.AddArg(physical_device);
  ubimkvol.RedirectOutput(temp_file.value());
  int mkvol_ret = ubimkvol.Run();
  AppendFileToLog(temp_file);
  if (mkvol_ret) {
    LOG(ERROR) << "Making MTD volume failed with code " << mkvol_ret;
  }

  return detach_ret == 0 && format_ret == 0 && attach_ret == 0 &&
         mkvol_ret == 0;

  /*
   * End of untested MTD code.
   */
}

// static
bool ClobberState::WipeBlockDevice(const base::FilePath& device_path,
                                   ClobberUi* ui,
                                   bool fast) {
  const int write_block_size = 4 * 1024 * 1024;
  int64_t to_write = 0;
  if (fast) {
    to_write = write_block_size;
  } else {
    // Wipe the filesystem size if we can determine it. Full partition wipe
    // takes a long time on 16G SSD or rotating media.
    struct stat st;
    if (stat(device_path.value().c_str(), &st) == -1) {
      PLOG(ERROR) << "Unable to stat " << device_path.value();
      return false;
    }
    int64_t block_size = st.st_blksize;
    int64_t block_count;
    if (!GetBlockCount(device_path, block_size, &block_count)) {
      LOG(ERROR) << "Unable to get block count for " << device_path.value();
      return false;
    }
    to_write = block_count * block_size;
    LOG(INFO) << "Filesystem block size: " << block_size;
    LOG(INFO) << "Filesystem block count: " << block_count;
  }

  LOG(INFO) << "Wiping block device " << device_path.value()
            << (fast ? " (fast) " : "");
  LOG(INFO) << "Number of bytes to write: " << to_write;

  base::File device(open(device_path.value().c_str(), O_WRONLY | O_SYNC));
  if (!device.IsValid()) {
    PLOG(ERROR) << "Unable to open " << device_path.value();
    return false;
  }

  // Don't display progress in fast mode since it runs so quickly.
  bool display_progress = !fast;
  base::ScopedClosureRunner stop_wipe_ui;
  if (display_progress) {
    if (ui->StartWipeUi(to_write)) {
      stop_wipe_ui.ReplaceClosure(
          base::BindOnce([](ClobberUi* ui) { ui->StopWipeUi(); }, ui));
    } else {
      display_progress = false;
    }
  }

  uint64_t total_written = 0;

  // Attempt to use the BLKZEROOUT ioctl since it is significantly faster if
  // supported by the device. It is not supported on kernels before 4.4.
  // If the device does not support it, the kernel will fall back to writing
  // 0's manually.
  // We call BLKZEROOUT in chunks 5% (1/20th) of the disk size so that we can
  // update progress as we go. Round up the chunk size to a multiple of 128MiB.
  // BLKZEROOUT requires that its arguments are aligned to at least 512 bytes.
  const uint64_t zero_block_size =
      base::bits::Align(to_write / 20, 128 * 1024 * 1024);
  while (total_written < to_write) {
    uint64_t write_size = std::min(zero_block_size, to_write - total_written);
    uint64_t range[2] = {total_written, write_size};
    LOG(INFO) << "Wiping from " << total_written << " to "
              << (total_written + write_size);
    int ret = ioctl(device.GetPlatformFile(), BLKZEROOUT, &range);
    if (ret == 0) {
      total_written += write_size;
      if (display_progress) {
        ui->UpdateWipeProgress(total_written);
      }
    } else if (errno == ENOTTY) {
      LOG(INFO) << "BLKZEROOUT is not supported";
      break;
    } else {
      PLOG(ERROR) << "Wiping with BLKZEROOUT failed";
      break;
    }
  }

  if (total_written == to_write) {
    LOG(INFO) << "Successfully zeroed " << to_write << " bytes on "
              << device_path.value();
    return true;
  }
  LOG(INFO) << "Reverting to manual wipe for bytes " << total_written
            << " through " << to_write;

  const std::vector<char> buffer(write_block_size, '\0');
  while (total_written < to_write) {
    int write_size = std::min(static_cast<uint64_t>(write_block_size),
                              to_write - total_written);
    int64_t bytes_written = device.WriteAtCurrentPos(buffer.data(), write_size);
    if (bytes_written < 0) {
      PLOG(ERROR) << "Failed to write to " << device_path.value();
      LOG(ERROR) << "Wrote " << total_written << " bytes before failing";
      return false;
    }
    total_written += bytes_written;
    if (display_progress) {
      ui->UpdateWipeProgress(total_written);
    }
  }
  LOG(INFO) << "Successfully wrote " << total_written << " bytes to "
            << device_path.value();

  return true;
}

// static
int ClobberState::GetPartitionNumber(const base::FilePath& drive_name,
                                     const std::string& partition_label) {
  // TODO(C++20): Switch to aggregate initialization once we require C++20.
  CgptFindParams params = {};
  params.set_label = 1;
  params.label = partition_label.c_str();
  params.drive_name = drive_name.value().c_str();
  params.show_fn = &CgptFindShowFunctionNoOp;
  CgptFind(&params);
  if (params.hits != 1) {
    LOG(ERROR) << "Could not find partition number for partition "
               << partition_label;
    return -1;
  }
  return params.match_partnum;
}

// static
bool ClobberState::ReadPartitionMetadata(const base::FilePath& disk,
                                         int partition_number,
                                         bool* successful_out,
                                         int* priority_out) {
  if (!successful_out || !priority_out)
    return false;
  // TODO(C++20): Switch to aggregate initialization once we require C++20.
  CgptAddParams params = {};
  params.drive_name = disk.value().c_str();
  params.partition = partition_number;
  if (CgptGetPartitionDetails(&params) == CGPT_OK) {
    *successful_out = params.successful;
    *priority_out = params.priority;
    return true;
  } else {
    return false;
  }
}

// static
void ClobberState::EnsureKernelIsBootable(const base::FilePath root_disk,
                                          int kernel_partition) {
  bool successful = false;
  int priority = 0;
  if (!ReadPartitionMetadata(root_disk, kernel_partition, &successful,
                             &priority)) {
    LOG(ERROR) << "Failed to read partition metadata from partition "
               << kernel_partition << " on disk " << root_disk.value();
    // If we couldn't read, we'll err on the side of caution and try to set the
    // successful bit and priority anyways.
  }

  if (!successful) {
    // TODO(C++20): Switch to aggregate initialization once we require C++20.
    CgptAddParams params = {};
    params.partition = kernel_partition;
    params.set_successful = 1;
    params.drive_name = root_disk.value().c_str();
    params.successful = 1;
    if (CgptAdd(&params) != CGPT_OK) {
      LOG(ERROR) << "Failed to set sucessful for active kernel partition: "
                 << kernel_partition;
    }
  }

  if (priority < 1) {
    // TODO(C++20): Switch to aggregate initialization once we require C++20.
    CgptPrioritizeParams params = {};
    params.set_partition = kernel_partition;
    params.drive_name = root_disk.value().c_str();
    // When reordering kernel priorities to set the active kernel to highest,
    // use 3 as the highest value. Since there are only 3 kernel partitions,
    // this ensures that all priorities are unique.
    params.max_priority = 3;
    if (CgptPrioritize(&params) != CGPT_OK) {
      LOG(ERROR) << "Failed to prioritize active kernel partition: "
                 << kernel_partition;
    }
  }

  sync();
}

ClobberState::ClobberState(const Arguments& args,
                           std::unique_ptr<CrosSystem> cros_system,
                           std::unique_ptr<ClobberUi> ui)
    : args_(args),
      cros_system_(std::move(cros_system)),
      ui_(std::move(ui)),
      stateful_(kStatefulPath),
      dev_("/dev"),
      sys_("/sys") {}

std::vector<base::FilePath> ClobberState::GetPreservedFilesList() {
  std::vector<std::string> stateful_paths;
  // Preserve these files in safe mode. (Please request a privacy review before
  // adding files.)
  //
  // - unencrypted/preserve/update_engine/prefs/rollback-happened: Contains a
  //   boolean value indicating whether a rollback has happened since the last
  //   update check where device policy was available. Needed to avoid forced
  //   updates after rollbacks (device policy is not yet loaded at this time).
  if (args_.safe_wipe) {
    stateful_paths.push_back(kPowerWashCountPath);
    stateful_paths.push_back(
        "unencrypted/preserve/tpm_firmware_update_request");
    stateful_paths.push_back(std::string(kUpdateEnginePreservePath) +
                             "rollback-happened");
    stateful_paths.push_back(std::string(kUpdateEnginePreservePath) +
                             "rollback-version");

    stateful_paths.push_back(std::string(kUpdateEnginePreservePath) +
                             std::string(kLastPingDate));
    stateful_paths.push_back(std::string(kUpdateEnginePreservePath) +
                             std::string(kLastRollcallDate));
    // Preserve pre-installed demo mode resources for offline Demo Mode.
    std::string demo_mode_resources_dir =
        "unencrypted/cros-components/offline-demo-mode-resources/";
    stateful_paths.push_back(demo_mode_resources_dir + "image.squash");
    stateful_paths.push_back(demo_mode_resources_dir + "imageloader.json");
    stateful_paths.push_back(demo_mode_resources_dir + "imageloader.sig.1");
    stateful_paths.push_back(demo_mode_resources_dir + "imageloader.sig.2");
    stateful_paths.push_back(demo_mode_resources_dir + "manifest.fingerprint");
    stateful_paths.push_back(demo_mode_resources_dir + "manifest.json");
    stateful_paths.push_back(demo_mode_resources_dir + "table");

    // For rollback wipes, we also preserve rollback data. This is an encrypted
    // proto which contains install attributes, device policy and owner.key
    // (used to keep the enrollment), also other device-level configurations
    // e.g. shill configuration to restore network connection after rollback.
    // We also preserve the attestation DB (needed because we don't do TPM clear
    // in this case).
    if (args_.rollback_wipe) {
      stateful_paths.push_back("unencrypted/preserve/attestation.epb");
      stateful_paths.push_back("unencrypted/preserve/rollback_data");
    }
  }

  // Test images in the lab enable certain extra behaviors if the
  // .labmachine flag file is present.  Those behaviors include some
  // important recovery behaviors (cf. the recover_duts upstart job).
  // We need those behaviors to survive across power wash, otherwise,
  // the current boot could wind up as a black hole.
  int debug_build;
  if (cros_system_->GetInt(CrosSystem::kDebugBuild, &debug_build) &&
      debug_build == 1) {
    stateful_paths.push_back(".labmachine");
  }

  std::vector<base::FilePath> preserved_files;
  for (const std::string& path : stateful_paths) {
    preserved_files.push_back(base::FilePath(path));
  }

  if (args_.factory_wipe) {
    base::FileEnumerator enumerator(
        stateful_.Append("unencrypted/import_extensions/extensions"), false,
        base::FileEnumerator::FileType::FILES, "*.crx");
    for (base::FilePath name = enumerator.Next(); !name.empty();
         name = enumerator.Next()) {
      preserved_files.push_back(
          base::FilePath("unencrypted/import_extensions/extensions")
              .Append(name.BaseName()));
    }
  }

  return preserved_files;
}

int ClobberState::CreateStatefulFileSystem() {
  base::FilePath temp_file;
  base::CreateTemporaryFile(&temp_file);

  brillo::ProcessImpl mkfs;
  if (wipe_info_.is_mtd_flash) {
    mkfs.AddArg("/sbin/mkfs.ubifs");
    mkfs.AddArg("-y");
    mkfs.AddStringOption("-x", "none");
    mkfs.AddIntOption("-R", 0);
    mkfs.AddArg(wipe_info_.stateful_device.value());
  } else {
    mkfs.AddArg("/sbin/mkfs.ext4");
    // Check if encryption is supported. If yes, enable the flag during mkfs.
    if (base::PathExists(base::FilePath(kExt4DircryptoSupportedPath)))
      mkfs.AddStringOption("-O", "encrypt");
    mkfs.AddArg(wipe_info_.stateful_device.value());
    // TODO(wad) tune2fs.
  }
  mkfs.RedirectOutput(temp_file.value());
  LOG(INFO) << "Creating stateful file system";
  int ret = mkfs.Run();
  AppendFileToLog(temp_file);
  return ret;
}

int ClobberState::Run() {
  DCHECK(cros_system_);

  wipe_start_time_ = base::TimeTicks::Now();

  // Defer callback to relocate log file back to stateful partition so that it
  // will be preserved after a reboot.
  base::ScopedClosureRunner relocate_clobber_state_log(base::BindRepeating(
      [](base::FilePath stateful_path) {
        base::Move(base::FilePath(kClobberLogPath),
                   stateful_path.Append("unencrypted/clobber-state.log"));
      },
      stateful_));

  // Check if this powerwash was triggered by a session manager request.
  // StartDeviceWipe D-Bus call is restricted to "chronos" so it is probably
  // safe to assume that such requests were initiated by the user.
  bool user_triggered_powerwash =
      (args_.reason.find("session_manager_dbus_request") != std::string::npos);

  // Allow crash preservation across clobber if the device is in developer mode.
  // For testing purposes, use a tmpfs path to disable collection.
  bool preserve_dev_mode_crash_reports =
      IsInDeveloperMode() &&
      !base::PathExists(base::FilePath(kDisableClobberCrashCollectionPath));

  // Check if sensitive files should be preserved. Sensitive files should be
  // preserved if any of the following conditions are met:
  // 1. The device is in developer mode and crash report collection is allowed.
  // 2. The request doesn't originate from a user-triggered powerwash.
  bool preserve_sensitive_files =
      !user_triggered_powerwash || preserve_dev_mode_crash_reports;

  // True if we should ensure that this powerwash takes at least 5 minutes.
  // Saved here because we may switch to using a fast wipe later, but we still
  // want to enforce the delay in that case.
  bool should_force_delay = !args_.fast_wipe && !args_.factory_wipe;

  LOG(INFO) << "Beginning clobber-state run";
  LOG(INFO) << "Factory wipe: " << args_.factory_wipe;
  LOG(INFO) << "Fast wipe: " << args_.fast_wipe;
  LOG(INFO) << "Keepimg: " << args_.keepimg;
  LOG(INFO) << "Safe wipe: " << args_.safe_wipe;
  LOG(INFO) << "Rollback wipe: " << args_.rollback_wipe;
  LOG(INFO) << "Reason: " << args_.reason;

  // Most effective means of destroying user data is run at the start: Throwing
  // away the key to encrypted stateful by requesting the TPM to be cleared at
  // next boot. We shouldn't do this for rollback wipes.
  if (!args_.rollback_wipe) {
    if (!cros_system_->SetInt(CrosSystem::kClearTpmOwnerRequest, 1)) {
      LOG(ERROR) << "Requesting TPM wipe via crossystem failed";
    }
  }

  // In cases where biometric sensors are available, reset the internal entropy
  // used by those sensors for encryption, to render related data/templates etc.
  // undecipherable.
  if (!ClearBiometricSensorEntropy()) {
    LOG(ERROR) << "Clearing biometric sensor internal entropy failed";
  }

  if (args_.safe_wipe) {
    IncrementFileCounter(stateful_.Append(kPowerWashCountPath));
    base::FilePath preserve_path = stateful_.Append(kUpdateEnginePreservePath);
    base::FilePath prefs_path(kUpdateEnginePrefsPath);
    base::CopyFile(prefs_path.Append(kLastPingDate),
                   preserve_path.Append(kLastPingDate));
    base::CopyFile(prefs_path.Append(kLastRollcallDate),
                   preserve_path.Append(kLastRollcallDate));
  }

  // Clear clobber log if needed.
  if (!preserve_sensitive_files) {
    base::DeleteFile(stateful_.Append(kStatefulClobberLogPath));
  }

  std::vector<base::FilePath> preserved_files = GetPreservedFilesList();
  for (const base::FilePath& fp : preserved_files) {
    LOG(INFO) << "Preserving file: " << fp.value();
  }

  base::FilePath preserved_tar_file(kPreservedFilesTarPath);
  int ret = PreserveFiles(stateful_, preserved_files, preserved_tar_file);
  if (ret) {
    LOG(ERROR) << "Preserving files failed with code " << ret;
  }

  // As we move factory wiping from release image to factory test image,
  // clobber-state will be invoked directly under a tmpfs. GetRootDevice cannot
  // report correct output under such a situation. Therefore, the output is
  // preserved then assigned to environment variables ROOT_DEV/ROOT_DISK for
  // clobber-state. For other cases, the environment variables will be empty and
  // it falls back to using GetRootDevice.
  const char* root_disk_cstr = getenv("ROOT_DISK");
  if (root_disk_cstr != nullptr) {
    root_disk_ = base::FilePath(root_disk_cstr);
  } else {
    root_disk_ = GetRootDevice(/*strip_partition=*/true);
  }

  // Special casing for NAND devices
  if (base::StartsWith(root_disk_.value(), kUbiDevicePrefix,
                       base::CompareCase::SENSITIVE)) {
    root_disk_ = base::FilePath(kUbiRootDisk);
  }

  base::FilePath root_device;
  const char* root_device_cstr = getenv("ROOT_DEV");
  if (root_device_cstr != nullptr) {
    root_device = base::FilePath(root_device_cstr);
  } else {
    root_device = GetRootDevice(/*strip_partition=*/false);
  }

  LOG(INFO) << "Root disk: " << root_disk_.value();
  LOG(INFO) << "Root device: " << root_device.value();

  partitions_.stateful = GetPartitionNumber(root_disk_, "STATE");
  partitions_.root_a = GetPartitionNumber(root_disk_, "ROOT-A");
  partitions_.root_b = GetPartitionNumber(root_disk_, "ROOT-B");
  partitions_.kernel_a = GetPartitionNumber(root_disk_, "KERN-A");
  partitions_.kernel_b = GetPartitionNumber(root_disk_, "KERN-B");

  if (!GetDevicesToWipe(root_disk_, root_device, partitions_, &wipe_info_)) {
    LOG(ERROR) << "Getting devices to wipe failed, aborting run";
    return 1;
  }

  // Determine if stateful partition's device is backed by a rotational disk.
  bool is_rotational = false;
  if (!wipe_info_.is_mtd_flash) {
    is_rotational = IsRotational(wipe_info_.stateful_device);
  }

  LOG(INFO) << "Stateful device: " << wipe_info_.stateful_device.value();
  LOG(INFO) << "Inactive root device: "
            << wipe_info_.inactive_root_device.value();
  LOG(INFO) << "Inactive kernel device: "
            << wipe_info_.inactive_kernel_device.value();

  base::FilePath temp_file;
  base::CreateTemporaryFile(&temp_file);

  brillo::ProcessImpl log_preserve;
  log_preserve.AddArg("/sbin/clobber-log");
  log_preserve.AddArg("--preserve");
  log_preserve.AddArg("clobber-state");
  if (args_.factory_wipe)
    log_preserve.AddArg("factory");
  if (args_.fast_wipe)
    log_preserve.AddArg("fast");
  if (args_.keepimg)
    log_preserve.AddArg("keepimg");
  if (args_.safe_wipe)
    log_preserve.AddArg("safe");
  if (args_.rollback_wipe)
    log_preserve.AddArg("rollback");
  if (!args_.reason.empty())
    log_preserve.AddArg(args_.reason);
  log_preserve.RedirectOutput(temp_file.value());
  log_preserve.Run();
  AppendFileToLog(temp_file);

  AttemptSwitchToFastWipe(is_rotational);

  // Make sure the stateful partition has been unmounted.
  LOG(INFO) << "Unmounting stateful partition";
  ret = umount(stateful_.value().c_str());
  if (ret) {
    // Disambiguate failures from busy or already unmounted stateful partition
    // from other generic failures.
    if (errno == EBUSY) {
      PLOG(ERROR) << "Failed to unmount busy stateful partition";
    } else if (errno != EINVAL) {
      PLOG(ERROR) << "Unable to unmount " << stateful_.value();
    } else {
      PLOG(INFO) << "Stateful partition already unmounted";
    }
  }

  // Destroy user data: wipe the stateful partition.
  if (!WipeDevice(wipe_info_.stateful_device)) {
    LOG(ERROR) << "Unable to wipe device "
               << wipe_info_.stateful_device.value();
  }

  ret = CreateStatefulFileSystem();
  if (ret)
    LOG(ERROR) << "Unable to create stateful file system. Error code: " << ret;

  // Mount the fresh image for last minute additions.
  std::string file_system_type = wipe_info_.is_mtd_flash ? "ubifs" : "ext4";
  if (mount(wipe_info_.stateful_device.value().c_str(),
            stateful_.value().c_str(), file_system_type.c_str(), 0,
            nullptr) != 0) {
    PLOG(ERROR) << "Unable to mount stateful partition at "
                << stateful_.value();
  }

  if (base::PathExists(preserved_tar_file)) {
    brillo::ProcessImpl tar;
    tar.AddArg("/bin/tar");
    tar.AddStringOption("-C", stateful_.value());
    tar.AddArg("-x");
    tar.AddStringOption("-f", preserved_tar_file.value());
    tar.RedirectOutput(temp_file.value());
    ret = tar.Run();
    AppendFileToLog(temp_file);
    if (ret != 0) {
      LOG(WARNING) << "Restoring preserved files failed with code " << ret;
    }
    base::WriteFile(stateful_.Append("unencrypted/.powerwash_completed"), "",
                    0);
  }

  brillo::ProcessImpl log_restore;
  log_restore.AddArg("/sbin/clobber-log");
  log_restore.AddArg("--restore");
  log_restore.AddArg("clobber-state");
  log_restore.RedirectOutput(temp_file.value());
  ret = log_restore.Run();
  AppendFileToLog(temp_file);
  if (ret != 0) {
    LOG(WARNING) << "Restoring clobber.log failed with code " << ret;
  }

  // Attempt to collect crashes into the reboot vault crash directory. Do not
  // collect crashes if this is a user triggered powerwash.
  if (preserve_sensitive_files) {
    if (CreateEncryptedRebootVault())
      CollectClobberCrashReports();
  }

  if (!args_.keepimg) {
    EnsureKernelIsBootable(root_disk_, wipe_info_.active_kernel_partition);
    WipeDevice(wipe_info_.inactive_root_device);
    WipeDevice(wipe_info_.inactive_kernel_device);
  }

  // Ensure that we've run for at least 5 minutes if this run requires it.
  if (should_force_delay) {
    ForceDelay();
  }

  // Check if we're in developer mode, and if so, create developer mode marker
  // file so that we don't run clobber-state again after reboot.
  if (!MarkDeveloperMode()) {
    LOG(ERROR) << "Creating developer mode marker file failed.";
  }

  // Schedule flush of filesystem caches to disk.
  sync();

  LOG(INFO) << "clobber-state has completed";
  relocate_clobber_state_log.RunAndReset();

  // Factory wipe should stop here.
  if (args_.factory_wipe)
    return 0;

  // If everything worked, reboot.
  // This return won't actually be reached unless reboot fails.
  return Reboot();
}

bool ClobberState::IsInDeveloperMode() {
  std::string firmware_name;
  int dev_mode_flag;
  return cros_system_->GetInt(CrosSystem::kDevSwitchBoot, &dev_mode_flag) &&
         dev_mode_flag == 1 &&
         cros_system_->GetString(CrosSystem::kMainFirmwareActive,
                                 &firmware_name) &&
         firmware_name != "recovery";
}

bool ClobberState::MarkDeveloperMode() {
  if (IsInDeveloperMode())
    return base::WriteFile(stateful_.Append(".developer_mode"), "", 0) == 0;

  return true;
}

void ClobberState::AttemptSwitchToFastWipe(bool is_rotational) {
  // On a non-fast wipe, rotational drives take too long. Override to run them
  // through "fast" mode. Sensitive contents should already
  // be encrypted.
  if (!args_.fast_wipe && is_rotational) {
    LOG(INFO) << "Stateful device is on rotational disk, shredding files";
    ShredRotationalStatefulFiles();
    args_.fast_wipe = true;
    LOG(INFO) << "Switching to fast wipe";
  }

  // For drives that support secure erasure, wipe the keysets,
  // and then run the drives through "fast" mode.
  //
  // Note: currently only eMMC-based SSDs are supported.
  if (!args_.fast_wipe) {
    LOG(INFO) << "Attempting to wipe encryption keysets";
    if (WipeKeysets()) {
      LOG(INFO) << "Wiping encryption keysets succeeded";
      args_.fast_wipe = true;
      LOG(INFO) << "Switching to fast wipe";
    } else {
      LOG(INFO) << "Wiping encryption keysets failed";
    }
  }
}

void ClobberState::ShredRotationalStatefulFiles() {
  // Directly remove things that are already encrypted (which are also the
  // large things), or are static from images.
  base::DeleteFile(stateful_.Append("encrypted.block"));
  base::DeletePathRecursively(stateful_.Append("var_overlay"));
  base::DeletePathRecursively(stateful_.Append("dev_image"));

  base::FileEnumerator shadow_files(
      stateful_.Append("home/.shadow"),
      /*recursive=*/true, base::FileEnumerator::FileType::DIRECTORIES);
  for (base::FilePath path = shadow_files.Next(); !path.empty();
       path = shadow_files.Next()) {
    if (path.BaseName() == base::FilePath("vault")) {
      base::DeletePathRecursively(path);
    }
  }

  base::FilePath temp_file;
  base::CreateTemporaryFile(&temp_file);

  // Shred everything else. We care about contents not filenames, so do not
  // use "-u" since metadata updates via fdatasync dominate the shred time.
  // Note that if the count-down is interrupted, the reset file continues
  // to exist, which correctly continues to indicate a needed wipe.
  brillo::ProcessImpl shred;
  shred.AddArg("/usr/bin/shred");
  shred.AddArg("--force");
  shred.AddArg("--zero");
  base::FileEnumerator stateful_files(stateful_, /*recursive=*/true,
                                      base::FileEnumerator::FileType::FILES);
  for (base::FilePath path = stateful_files.Next(); !path.empty();
       path = stateful_files.Next()) {
    shred.AddArg(path.value());
  }
  shred.RedirectOutput(temp_file.value());
  shred.Run();
  AppendFileToLog(temp_file);

  sync();
}

bool ClobberState::WipeKeysets() {
  std::vector<std::string> key_files{
      "encrypted.key", "encrypted.needs-finalization",
      "home/.shadow/cryptohome.key", "home/.shadow/salt",
      "home/.shadow/salt.sum"};
  bool found_file = false;
  for (const std::string& str : key_files) {
    base::FilePath path = stateful_.Append(str);
    if (base::PathExists(path)) {
      found_file = true;
      if (!SecureErase(path)) {
        LOG(ERROR) << "Securely erasing file failed: " << path.value();
        return false;
      }
    }
  }

  // Delete files named 'master' in directories contained in '.shadow'.
  base::FileEnumerator directories(stateful_.Append("home/.shadow"),
                                   /*recursive=*/false,
                                   base::FileEnumerator::FileType::DIRECTORIES);
  for (base::FilePath dir = directories.Next(); !dir.empty();
       dir = directories.Next()) {
    base::FileEnumerator files(dir, /*recursive=*/false,
                               base::FileEnumerator::FileType::FILES);
    for (base::FilePath file = files.Next(); !file.empty();
         file = files.Next()) {
      if (file.RemoveExtension().BaseName() == base::FilePath("master")) {
        found_file = true;
        if (!SecureErase(file)) {
          LOG(ERROR) << "Securely erasing file failed: " << file.value();
          return false;
        }
      }
    }
  }

  // If no files were found, then we can't say whether or not secure erase
  // works. Assume it doesn't.
  if (!found_file) {
    LOG(WARNING) << "No files existed to attempt secure erase";
    return false;
  }

  return DropCaches();
}

void ClobberState::ForceDelay() {
  base::TimeDelta elapsed = base::TimeTicks::Now() - wipe_start_time_;
  LOG(INFO) << "Clobber has already run for " << elapsed.InSeconds()
            << " seconds";
  base::TimeDelta remaining = kMinClobberDuration - elapsed;
  if (remaining <= base::TimeDelta::FromSeconds(0)) {
    LOG(INFO) << "Skipping forced delay";
    return;
  }
  LOG(INFO) << "Forcing a delay of " << remaining.InSeconds() << " seconds";
  if (!ui_->ShowCountdownTimer(remaining)) {
    // If showing the timer failed, we still want to make sure that we don't
    // run for less than |kMinClobberDuration|.
    base::PlatformThread::Sleep(remaining);
  }
}

// Wrapper around secure_erase_file::SecureErase(const base::FilePath&).
bool ClobberState::SecureErase(const base::FilePath& path) {
  return secure_erase_file::SecureErase(path);
}

// Wrapper around secure_erase_file::DropCaches(). Must be called after
// a call to SecureEraseFile. Files are only securely deleted if DropCaches
// returns true.
bool ClobberState::DropCaches() {
  return secure_erase_file::DropCaches();
}

bool ClobberState::WipeDevice(const base::FilePath& device_path) {
  if (wipe_info_.is_mtd_flash) {
    return WipeMTDDevice(device_path, partitions_);
  } else {
    return WipeBlockDevice(device_path, ui_.get(), args_.fast_wipe);
  }
}

void ClobberState::SetArgsForTest(const ClobberState::Arguments& args) {
  args_ = args;
}

ClobberState::Arguments ClobberState::GetArgsForTest() {
  return args_;
}

void ClobberState::SetStatefulForTest(const base::FilePath& stateful_path) {
  stateful_ = stateful_path;
}

void ClobberState::SetDevForTest(const base::FilePath& dev_path) {
  dev_ = dev_path;
}

void ClobberState::SetSysForTest(const base::FilePath& sys_path) {
  sys_ = sys_path;
}

int ClobberState::Stat(const base::FilePath& path, struct stat* st) {
  return stat(path.value().c_str(), st);
}

bool ClobberState::ClearBiometricSensorEntropy() {
  if (base::PathExists(base::FilePath(kBioWashPath))) {
    brillo::ProcessImpl bio_wash;
    bio_wash.AddArg(kBioWashPath);
    return bio_wash.Run() == 0;
  }
  // Return true here so that we don't report spurious failures on platforms
  // without the bio_wash executable.
  return true;
}

int ClobberState::Reboot() {
  brillo::ProcessImpl proc;
  proc.AddArg("/sbin/shutdown");
  proc.AddArg("-r");
  proc.AddArg("now");
  int ret = proc.Run();
  if (ret == 0) {
    // Wait for reboot to finish (it's an async call).
    sleep(60 * 60 * 24);
  }
  // If we've reached here, reboot (probably) failed.
  LOG(ERROR) << "Requesting reboot failed with failure code " << ret;
  return ret;
}
