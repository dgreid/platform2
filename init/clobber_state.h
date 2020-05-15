// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_CLOBBER_STATE_H_
#define INIT_CLOBBER_STATE_H_

#include <sys/stat.h>

#include <memory>
#include <string>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/time/time.h>

#include "init/clobber_ui.h"
#include "init/crossystem.h"

class ClobberState {
 public:
  struct Arguments {
    // Run in the context of a factory flow, do not reboot when done.
    bool factory_wipe = false;
    // Less thorough data destruction.
    bool fast_wipe = false;
    // Don't delete the non-active set of kernel/root partitions.
    bool keepimg = false;
    // Preserve some files and VPD keys.
    bool safe_wipe = false;
    // Preserve rollback data, attestation DB, and don't clear TPM
    bool rollback_wipe = false;
    // Preserve initial reason for triggering clobber, if available.
    // Assume that the reason string is already sanitized by session
    // manager (non-alphanumeric characters replaced with '_').
    std::string reason = "";
  };

  // The index of each partition within the gpt partition table.
  struct PartitionNumbers {
    int stateful = -1;
    int root_a = -1;
    int root_b = -1;
    int kernel_a = -1;
    int kernel_b = -1;
  };

  struct DeviceWipeInfo {
    // Paths under /dev for the various devices to wipe.
    base::FilePath stateful_device;
    base::FilePath inactive_root_device;
    base::FilePath inactive_kernel_device;

    // Is the stateful device backed by an MTD flash device.
    bool is_mtd_flash = false;
    // The partition number for the currently booted kernel partition.
    int active_kernel_partition = -1;
  };

  // Extracts ClobberState's arguments from argv.
  static Arguments ParseArgv(int argc, char const* const argv[]);

  // Attempts to increment the contents of |path| by 1. If the contents cannot
  // be read, or if the contents are not an integer, writes '1' to the file.
  static bool IncrementFileCounter(const base::FilePath& path);

  // Given a list of files to preserve (relative to |preserved_files_root|),
  // creates a tar file containing those files at |tar_file_path|.
  // The directory structure of the preserved files is preserved.
  static int PreserveFiles(const base::FilePath& preserved_files_root,
                           const std::vector<base::FilePath>& preserved_files,
                           const base::FilePath& tar_file_path);

  // Splits a device path, for example /dev/mmcblk0p1, /dev/sda3,
  // /dev/ubiblock9_0 into the base device and partition numbers,
  // which would be respectively /dev/mmcblk0p, 1; /dev/sda, 3; and
  // /dev/ubiblock, 9.
  // Returns true on success.
  static bool GetDevicePathComponents(const base::FilePath& device,
                                      std::string* base_device_out,
                                      int* partition_out);

  // Determine the devices to be wiped and their properties, and populate
  // |wipe_info_out| with the results. Returns true if successful.
  static bool GetDevicesToWipe(const base::FilePath& root_disk,
                               const base::FilePath& root_device,
                               const PartitionNumbers& partitions,
                               DeviceWipeInfo* wipe_info_out);

  static bool WipeMTDDevice(const base::FilePath& device_path,
                            const PartitionNumbers& partitions);

  // Wipe |device_path|, showing a progress UI using |ui|.
  //
  // If |fast| is true, wipe |device_path| using a less-thorough but much faster
  // wipe. Not all blocks are guaranteed to be overwritten, so this should be
  // reserved for situations when there is no concern of data leakage.
  // A progress indicator will not be displayed if |fast| mode is enabled.
  static bool WipeBlockDevice(const base::FilePath& device_path,
                              ClobberUi* ui,
                              bool fast);

  // Removes the following keys from the VPD. Do not do this for a safe wipe.
  //   first_active_omaha_ping_sent
  //   recovery_count
  static void RemoveVPDKeys();

  // Reads successful and priority metadata from partition numbered
  // |partition_number| on |disk|, storing the results in |successful_out| and
  // |priority_out|, respectively. Returns true on success.
  //
  // successful is a 1 bit value indicating if a kernel partition
  // has been successfully booted, while priority is a 4 bit value
  // indicating what order the kernel partitions should be booted in, 15 being
  // the highest, 1 the lowest, and 0 meaning not bootable.
  // More information on partition metadata is available at.
  // https://www.chromium.org/chromium-os/chromiumos-design-docs/disk-format
  static bool ReadPartitionMetadata(const base::FilePath& disk,
                                    int partition_number,
                                    bool* successful_out,
                                    int* priority_out);

  // Searches |drive_name| for the partition labeled |partition_label| and
  // returns its partition number if exactly one partition was found. Returns
  // -1 on error.
  static int GetPartitionNumber(const base::FilePath& drive_name,
                                const std::string& partition_label);

  // Make sure the kernel partition numbered |kernel_partition| is still
  // bootable after being wiped. The system may be in AU state that active
  // kernel does not have "successful" bit set to 1, but the kernel has been
  // successfully booted.
  static void EnsureKernelIsBootable(const base::FilePath root_disk,
                                     int kernel_partition);

  ClobberState(const Arguments& args,
               std::unique_ptr<CrosSystem> cros_system,
               std::unique_ptr<ClobberUi> ui);

  // Run the clobber state routine.
  int Run();

  bool IsInDeveloperMode();

  bool MarkDeveloperMode();

  // Attempt to switch rotational drives and drives that support
  // secure_erase_file to a fast wipe by taking some (secure) shortcuts.
  void AttemptSwitchToFastWipe(bool is_rotational);

  // If the stateful filesystem is available and the disk is rotational, do some
  // best-effort content shredding. Since on a rotational disk the filesystem is
  // not mounted with "data=journal", writes really do overwrite the block
  // contents (unlike on an SSD).
  void ShredRotationalStatefulFiles();

  // Wipe encryption key information from the stateful partition for supported
  // devices.
  bool WipeKeysets();

  // Forces a delay, writing progress to the TTY.  This is used to prevent
  // developer mode transitions from happening too quickly.
  virtual void ForceDelay();

  // Returns vector of files to be preserved. All FilePaths are relative to
  // stateful_.
  std::vector<base::FilePath> GetPreservedFilesList();

  // Determines if the given device (under |dev_|) is backed by a rotational
  // hard drive.
  // Returns true if it can conclusively determine it's rotational,
  // otherwise false.
  bool IsRotational(const base::FilePath& device_path);

  void SetArgsForTest(const Arguments& args);
  Arguments GetArgsForTest();
  void SetStatefulForTest(const base::FilePath& stateful_path);
  void SetDevForTest(const base::FilePath& dev_path);
  void SetSysForTest(const base::FilePath& sys_path);

 protected:
  // These functions are marked protected so they can be overridden for tests.

  // Wrapper around stat(2).
  virtual int Stat(const base::FilePath& path, struct stat* st);

  // Wrapper around secure_erase_file::SecureErase(const base::FilePath&).
  virtual bool SecureErase(const base::FilePath& path);

  // Wrapper around secure_erase_file::DropCaches(). Must be called after
  // a call to SecureEraseFile. Files are only securely deleted if DropCaches
  // returns true.
  virtual bool DropCaches();

 private:
  bool ClearBiometricSensorEntropy();

  // Perform media-dependent wipe of the device based on if the device is
  // an MTD device or not.
  // |device_path| should be the path under /dev/, e.g. /dev/sda3, /dev/ubi5_0.
  bool WipeDevice(const base::FilePath& device_name);

  // Makes a new filesystem on |wipe_info_.stateful_device|.
  int CreateStatefulFileSystem();

  int Reboot();

  Arguments args_;
  std::unique_ptr<CrosSystem> cros_system_;
  std::unique_ptr<ClobberUi> ui_;
  base::FilePath stateful_;
  base::FilePath dev_;
  base::FilePath sys_;
  PartitionNumbers partitions_;
  base::FilePath root_disk_;
  DeviceWipeInfo wipe_info_;
  base::TimeTicks wipe_start_time_;
};

#endif  // INIT_CLOBBER_STATE_H_
