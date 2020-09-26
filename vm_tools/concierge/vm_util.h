// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VM_UTIL_H_
#define VM_TOOLS_CONCIERGE_VM_UTIL_H_

#include <sys/types.h>

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/strings/string_split.h>
#include <base/time/time.h>
#include <brillo/process/process.h>
#include <vm_tools/concierge/usb_control.h>

namespace base {
class FilePath;
}  // namespace base

namespace vm_tools {
namespace concierge {

class Disk {
 public:
  Disk(base::FilePath path, bool writable);
  Disk(base::FilePath path, bool writable, bool sparse);
  Disk(Disk&&);
  virtual ~Disk();

  // Gets the command line argument that needs to be passed to crosvm
  // corresponding to this disk.
  base::StringPairs GetCrosvmArgs() const;

 private:
  // Path to the disk image on the host.
  base::FilePath path_;

  // Whether the disk should be writable by the VM.
  bool writable_;

  // Whether the disk should allow sparse file operations (discard) by the VM.
  base::Optional<bool> sparse_;

  DISALLOW_COPY_AND_ASSIGN(Disk);
};

// Path to the crosvm binary.
extern const char kCrosvmBin[];

// Uid and gid mappings for the android data directory. This is a
// comma-separated list of 3 values: <start of range inside the user namespace>
// <start of range outside the user namespace> <count>. The values are taken
// from platform2/arc/container-bundle/pi/config.json.
extern const char kAndroidUidMap[];
extern const char kAndroidGidMap[];

// Calculates the amount of memory to give the virtual machine. Currently
// configured to provide 75% of system memory. This is deliberately over
// provisioned with the expectation that we will use the balloon driver to
// reduce the actual memory footprint.
std::string GetVmMemoryMiB();

// Puts the current process in a CPU cgroup specificed by |cpu_cgroup|, and
// then calls SetPgid(). This function can be called as brillo::ProcessImpl's
// PreExecCallback.
bool SetUpCrosvmProcess(const base::FilePath& cpu_cgroup);

// Sets the pgid of the current process to its pid.  This is needed because
// crosvm assumes that only it and its children are in the same process group
// and indiscriminately sends a SIGKILL if it needs to shut them down. This
// function can be called as brillo::ProcessImpl's PreExecCallback.
bool SetPgid();

// Waits for the |pid| to exit.  Returns true if |pid| successfully exited and
// false if it did not exit in time.
bool WaitForChild(pid_t child, base::TimeDelta timeout);

// Returns true if a process with |pid| exists.
bool CheckProcessExists(pid_t pid);

// Runs a crosvm subcommand.
void RunCrosvmCommand(std::string command, std::string socket_path);

// Attaches an usb device at host |bus|:|addr|, with |vid|, |pid| and an
// opened |fd|.
bool AttachUsbDevice(std::string socket_path,
                     uint8_t bus,
                     uint8_t addr,
                     uint16_t vid,
                     uint16_t pid,
                     int fd,
                     UsbControlResponse* response);

// Detaches the usb device at guest |port|.
bool DetachUsbDevice(std::string socket_path,
                     uint8_t port,
                     UsbControlResponse* response);

// Lists all usb devices attached to guest.
bool ListUsbDevice(std::string socket_path, std::vector<UsbDevice>* devices);

// Resizes the disk identified by |disk_index| to |new_size| in bytes.
bool CrosvmDiskResize(std::string socket_path,
                      int disk_index,
                      uint64_t new_size);

// Updates |cpu_cgroup|'s cpu.shares to |cpu_shares|.
bool UpdateCpuShares(const base::FilePath& cpu_cgroup, int cpu_shares);

// Loads custom parameters from a string. The result is appended to parameter
// |args| as a vector of string pairs. Please check vm_tools/init/arcvm_dev.conf
// for the list of supported directives.
void LoadCustomParameters(const std::string& data, base::StringPairs* args);

// Removes all parameters with |key| from |args|. If it exists, the value of
// its last occurrence in |args| will be returned. Otherwise, |default_value|
// will be returned.
std::string RemoveParametersWithKey(const std::string& key,
                                    const std::string& default_value,
                                    base::StringPairs* args);

// Creates shared data parameter for crovm.
std::string CreateSharedDataParam(const base::FilePath& data_dir,
                                  const std::string& tag,
                                  bool enable_caches,
                                  bool ascii_casefold);

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_VM_UTIL_H_
