// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vm_util.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/stl_util.h>
#include <base/strings/safe_sprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>
#include <brillo/process/process.h>

namespace vm_tools {
namespace concierge {

const char kCrosvmBin[] = "/usr/bin/crosvm";

const char kAndroidUidMap[] = "0 655360 5000,5000 600 50,5050 660410 1994950";

const char kAndroidGidMap[] =
    "0 655360 1065,1065 20119 1,1066 656426 3934,5000 600 50,5050 660410 "
    "1994950";

namespace {

// Examples of the format of the given string can be seen at the enum
// UsbControlResponseType definition.
bool ParseUsbControlResponse(base::StringPiece s,
                             UsbControlResponse* response) {
  s = base::TrimString(s, base::kWhitespaceASCII, base::TRIM_ALL);

  if (s.starts_with("ok ")) {
    response->type = OK;
    unsigned port;
    if (!base::StringToUint(s.substr(3), &port))
      return false;
    if (port > UINT8_MAX) {
      return false;
    }
    response->port = port;
    return true;
  }

  if (s.starts_with("no_available_port")) {
    response->type = NO_AVAILABLE_PORT;
    response->reason = "No available ports in guest's host controller.";
    return true;
  }
  if (s.starts_with("no_such_device")) {
    response->type = NO_SUCH_DEVICE;
    response->reason = "No such host device.";
    return true;
  }
  if (s.starts_with("no_such_port")) {
    response->type = NO_SUCH_PORT;
    response->reason = "No such port in guest's host controller.";
    return true;
  }
  if (s.starts_with("fail_to_open_device")) {
    response->type = FAIL_TO_OPEN_DEVICE;
    response->reason = "Failed to open host device.";
    return true;
  }
  if (s.starts_with("devices")) {
    std::vector<base::StringPiece> device_parts = base::SplitStringPiece(
        s.substr(7), " \t", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    if ((device_parts.size() % 3) != 0) {
      return false;
    }
    response->type = DEVICES;
    for (size_t i = 0; i < device_parts.size(); i += 3) {
      unsigned port;
      unsigned vid;
      unsigned pid;
      if (!(base::StringToUint(device_parts[i + 0], &port) &&
            base::HexStringToUInt(device_parts[i + 1], &vid) &&
            base::HexStringToUInt(device_parts[i + 2], &pid))) {
        return false;
      }
      if (port > UINT8_MAX || vid > UINT16_MAX || pid > UINT16_MAX) {
        return false;
      }
      UsbDevice device;
      device.port = port;
      device.vid = vid;
      device.pid = pid;
      response->devices.push_back(device);
    }
    return true;
  }
  if (s.starts_with("error ")) {
    response->type = ERROR;
    response->reason = s.substr(6).as_string();
    return true;
  }

  return false;
}

bool CallUsbControl(brillo::ProcessImpl crosvm, UsbControlResponse* response) {
  crosvm.RedirectUsingPipe(STDOUT_FILENO, false /* is_input */);
  int ret = crosvm.Run();
  if (ret != 0)
    LOG(ERROR) << "Failed crosvm call returned code " << ret;

  base::ScopedFD read_fd(crosvm.GetPipe(STDOUT_FILENO));
  std::string crosvm_response;
  crosvm_response.resize(2048);

  ssize_t response_size =
      read(read_fd.get(), &crosvm_response[0], crosvm_response.size());
  if (response_size < 0) {
    response->reason = "Failed to read USB response from crosvm";
    return false;
  }
  if (response_size == 0) {
    response->reason = "Empty USB response from crosvm";
    return false;
  }
  crosvm_response.resize(response_size);

  if (!ParseUsbControlResponse(crosvm_response, response)) {
    response->reason =
        "Failed to parse USB response from crosvm: " + crosvm_response;
    return false;
  }
  return true;
}

}  // namespace

Disk::Disk(base::FilePath path, bool writable)
    : path_(std::move(path)), writable_(writable) {}

Disk::Disk(base::FilePath path, bool writable, bool sparse)
    : path_(std::move(path)), writable_(writable), sparse_(sparse) {}

Disk::Disk(Disk&&) = default;

base::StringPairs Disk::GetCrosvmArgs() const {
  std::string first;
  if (writable_)
    first = "--rwdisk";
  else
    first = "--disk";

  std::string sparse_arg;
  if (sparse_) {
    std::string boolean = sparse_.value() ? "true" : "false";
    sparse_arg = ",sparse=" + boolean;
  }

  std::string second = path_.value() + sparse_arg;
  base::StringPairs result = {{std::move(first), std::move(second)}};
  return result;
}

Disk::~Disk() = default;

std::string GetVmMemoryMiB() {
  int64_t sys_memory_mb = base::SysInfo::AmountOfPhysicalMemoryMB();
  int64_t vm_memory_mb;
  if (sys_memory_mb >= 4096) {
    vm_memory_mb = sys_memory_mb - 1024;
  } else {
    vm_memory_mb = sys_memory_mb / 4 * 3;
  }

  // Limit guest memory size to avoid running out of host process address space.
  int64_t size_max_mb = int64_t(SIZE_MAX / (1024 * 1024));
  vm_memory_mb = std::min(vm_memory_mb, size_max_mb / 4 * 3);

  return std::to_string(vm_memory_mb);
}

bool SetUpCrosvmProcess(const base::FilePath& cpu_cgroup) {
  // Note: This function is meant to be called after forking a process for
  // crosvm but before execve(). Since Concierge is multi-threaded, this
  // function should not call any functions that are not async signal safe
  // (see man signal-safety). Especially, don't call malloc/new or any functions
  // or constructors that may allocate heap memory. Calling malloc/new may
  // result in a dead-lock trying to lock a mutex that has already been locked
  // by one of the parent's threads.

  // Set up CPU cgroup. Note that FilePath::value() returns a const reference
  // to std::string without allocating a new object. c_str() doesn't do any copy
  // as long as we use C++11 or later.
  const int fd =
      HANDLE_EINTR(open(cpu_cgroup.value().c_str(), O_WRONLY | O_CLOEXEC));
  if (fd < 0) {
    // TODO(yusukes): Do logging here in an async safe way.
    return false;
  }

  char pid_str[32];
  const size_t len = base::strings::SafeSPrintf(pid_str, "%d", getpid());
  const ssize_t written = HANDLE_EINTR(write(fd, pid_str, len));
  close(fd);
  if (written != len) {
    // TODO(yusukes): Do logging here in an async safe way.
    return false;
  }

  // Set up process group ID.
  return SetPgid();
}

bool SetPgid() {
  // Note: This should only call async-signal-safe functions. Don't call
  // malloc/new. See SetUpCrosvmProcess() for more details.

  if (setpgid(0, 0) != 0) {
    // TODO(yusukes): Do logging here in an async safe way.
    return false;
  }

  return true;
}

bool WaitForChild(pid_t child, base::TimeDelta timeout) {
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGCHLD);

  const base::Time deadline = base::Time::Now() + timeout;
  while (true) {
    pid_t ret = waitpid(child, nullptr, WNOHANG);
    if (ret == child || (ret < 0 && errno == ECHILD)) {
      // Either the child exited or it doesn't exist anymore.
      return true;
    }

    // ret == 0 means that the child is still alive
    if (ret < 0) {
      PLOG(ERROR) << "Failed to wait for child process";
      return false;
    }

    base::Time now = base::Time::Now();
    if (deadline <= now) {
      // Timed out.
      return false;
    }

    const struct timespec ts = (deadline - now).ToTimeSpec();
    if (sigtimedwait(&set, nullptr, &ts) < 0 && errno == EAGAIN) {
      // Timed out.
      return false;
    }
  }
}

bool CheckProcessExists(pid_t pid) {
  if (pid == 0)
    return false;

  // Try to reap child process in case it just exited.
  waitpid(pid, NULL, WNOHANG);

  // kill() with a signal value of 0 is explicitly documented as a way to
  // check for the existence of a process.
  return kill(pid, 0) >= 0 || errno != ESRCH;
}

void RunCrosvmCommand(std::string command, std::string socket_path) {
  brillo::ProcessImpl crosvm;
  crosvm.AddArg(kCrosvmBin);
  crosvm.AddArg(std::move(command));
  crosvm.AddArg(std::move(socket_path));

  // This must be synchronous as we may do things after calling this function
  // that depend on the crosvm command being completed (like suspending the
  // device).
  crosvm.Run();
}

bool AttachUsbDevice(std::string socket_path,
                     uint8_t bus,
                     uint8_t addr,
                     uint16_t vid,
                     uint16_t pid,
                     int fd,
                     UsbControlResponse* response) {
  brillo::ProcessImpl crosvm;
  crosvm.AddArg(kCrosvmBin);
  crosvm.AddArg("usb");
  crosvm.AddArg("attach");
  crosvm.AddArg(base::StringPrintf("%d:%d:%x:%x", bus, addr, vid, pid));
  crosvm.AddArg("/proc/self/fd/" + std::to_string(fd));
  crosvm.AddArg(std::move(socket_path));
  crosvm.BindFd(fd, fd);
  fcntl(fd, F_SETFD, 0);  // Remove the CLOEXEC

  CallUsbControl(std::move(crosvm), response);

  return response->type == OK;
}

bool DetachUsbDevice(std::string socket_path,
                     uint8_t port,
                     UsbControlResponse* response) {
  brillo::ProcessImpl crosvm;
  crosvm.AddArg(kCrosvmBin);
  crosvm.AddArg("usb");
  crosvm.AddArg("detach");
  crosvm.AddArg(std::to_string(port));
  crosvm.AddArg(std::move(socket_path));

  CallUsbControl(std::move(crosvm), response);

  return response->type == OK;
}

bool ListUsbDevice(std::string socket_path, std::vector<UsbDevice>* device) {
  brillo::ProcessImpl crosvm;
  crosvm.AddArg(kCrosvmBin);
  crosvm.AddArg("usb");
  crosvm.AddArg("list");
  crosvm.AddArg(std::move(socket_path));

  UsbControlResponse response;
  CallUsbControl(std::move(crosvm), &response);

  if (response.type != DEVICES)
    return false;

  *device = std::move(response.devices);

  return true;
}

bool CrosvmDiskResize(std::string socket_path,
                      int disk_index,
                      uint64_t new_size) {
  brillo::ProcessImpl crosvm;
  crosvm.AddArg(kCrosvmBin);
  crosvm.AddArg("disk");
  crosvm.AddArg("resize");
  crosvm.AddArg(std::to_string(disk_index));
  crosvm.AddArg(std::to_string(new_size));
  crosvm.AddArg(std::move(socket_path));
  return crosvm.Run() == 0;
}

bool UpdateCpuShares(const base::FilePath& cpu_cgroup, int cpu_shares) {
  const std::string cpu_shares_str = std::to_string(cpu_shares);
  return base::WriteFile(cpu_cgroup.Append("cpu.shares"),
                         cpu_shares_str.c_str(),
                         cpu_shares_str.size()) == cpu_shares_str.size();
}

void LoadCustomParameters(const std::string& data, base::StringPairs* args) {
  std::vector<base::StringPiece> lines = base::SplitStringPiece(
      data, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  for (auto& line : lines) {
    if (line.empty() || line[0] == '#')
      continue;

    // Line contains a prefix key. Remove all args with this prefix.
    if (line[0] == '!' && line.size() > 1) {
      const base::StringPiece prefix = line.substr(1, line.size() - 1);
      base::EraseIf(*args, [&prefix](const auto& pair) {
        return base::StringPiece(pair.first).starts_with(prefix);
      });
      continue;
    }

    // Line contains a key only. Append the whole line.
    base::StringPairs pairs;
    if (!base::SplitStringIntoKeyValuePairs(line, '=', '\n', &pairs)) {
      args->emplace_back(std::move(line), "");
      continue;
    }

    // Line contains a key-value pair.
    base::TrimWhitespaceASCII(pairs[0].first, base::TRIM_ALL, &pairs[0].first);
    base::TrimWhitespaceASCII(pairs[0].second, base::TRIM_ALL,
                              &pairs[0].second);
    args->emplace_back(std::move(pairs[0].first), std::move(pairs[0].second));
  }
}

std::string RemoveParametersWithKey(const std::string& key,
                                    const std::string& default_value,
                                    base::StringPairs* args) {
  std::string target_value(default_value);
  base::StringPairs::reverse_iterator result =
      std::find_if(args->rbegin(), args->rend(),
                   [&key](const auto& pair) { return pair.first == key; });
  if (result != args->rend()) {
    target_value = result->second;
    base::EraseIf(*args,
                  [&key](const auto& pair) { return pair.first == key; });
  }
  return target_value;
}

std::string CreateSharedDataParam(const base::FilePath& data_dir,
                                  const std::string& tag,
                                  bool enable_caches,
                                  bool ascii_casefold) {
  // TODO(b/169446394): Go back to using "never" when caching is disabled
  // once we can switch /data/media to use 9p.
  return base::StringPrintf(
      "%s:%s:type=fs:cache=%s:uidmap=%s:gidmap=%s:timeout=%d:rewrite-"
      "security-xattrs=true:ascii_casefold=%s:writeback=%s",
      data_dir.value().c_str(), tag.c_str(), enable_caches ? "always" : "auto",
      kAndroidUidMap, kAndroidGidMap, enable_caches ? 3600 : 1,
      ascii_casefold ? "true" : "false", enable_caches ? "true" : "false");
}

}  // namespace concierge
}  // namespace vm_tools
