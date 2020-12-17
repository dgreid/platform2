// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VM_BUILDER_H_
#define VM_TOOLS_CONCIERGE_VM_BUILDER_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_split.h>
#include <base/optional.h>
#include <dbus/object_proxy.h>

#include "vm_tools/concierge/vm_util.h"

namespace vm_tools {
namespace concierge {

class VmBuilder {
 public:
  // Contains the rootfs device and path.
  struct Rootfs {
    std::string device;
    base::FilePath path;
    bool writable;
  };

  VmBuilder();
  VmBuilder(VmBuilder&&);
  VmBuilder& operator=(VmBuilder&& other);
  VmBuilder(const VmBuilder&) = delete;
  VmBuilder& operator=(const VmBuilder&) = delete;
  ~VmBuilder();

  VmBuilder& SetKernel(base::FilePath kernel);
  VmBuilder& SetInitrd(base::FilePath initrd);
  VmBuilder& SetRootfs(const struct Rootfs& rootfs);
  VmBuilder& SetCpus(int32_t cpus);
  VmBuilder& SetVsockCid(uint32_t vsock_cid);
  VmBuilder& AppendDisks(std::vector<Disk> disks);
  VmBuilder& SetMemory(const std::string& memory_in_mb);
  VmBuilder& SetBalloonBias(const std::string& balloon_bias_mib);

  VmBuilder& SetSyslogTag(const std::string& syslog_tag);
  VmBuilder& SetSocketPath(const std::string& socket_path);
  VmBuilder& AppendTapFd(base::ScopedFD tap_fd);
  VmBuilder& AppendKernelParam(const std::string& param);
  VmBuilder& AppendAudioDevice(const std::string& device);
  VmBuilder& AppendSerialDevice(const std::string& device);
  VmBuilder& AppendWaylandSocket(const std::string& socket);
  VmBuilder& AppendSharedDir(const std::string& shared_dir);
  VmBuilder& AppendCustomParam(const std::string& key,
                               const std::string& value);

  VmBuilder& EnableGpu(bool enable);
  VmBuilder& EnableGpu(bool enable, const std::string& gpu_arg);
  VmBuilder& EnableWaylandDmaBuf(bool enable);
  VmBuilder& EnableSoftwareTpm(bool enable);
  VmBuilder& EnableVideoDecoder(bool enable);
  VmBuilder& EnableVideoEncoder(bool enable);
  VmBuilder& EnableBattery(bool enable);
  VmBuilder& EnableSmt(bool enable);

  // Builds the command line required to start a VM.
  base::StringPairs BuildVmArgs() const;

 private:
  base::FilePath kernel_;
  base::FilePath initrd_;
  base::Optional<Rootfs> rootfs_;
  int32_t cpus_ = 0;
  base::Optional<uint32_t> vsock_cid_;
  std::string memory_in_mib_;
  std::string balloon_bias_mib_;

  std::string syslog_tag_;
  std::string vm_socket_path_;

  bool enable_gpu_ = false;
  bool enable_wayland_dma_buf_ = false;
  bool enable_software_tpm_ = false;
  bool enable_video_decoder_ = false;
  bool enable_video_encoder_ = false;
  bool enable_battery_ = false;
  base::Optional<bool> enable_smt_ = false;

  std::vector<Disk> disks_;
  std::vector<std::string> kernel_params_;
  std::vector<base::ScopedFD> tap_fds_;
  std::vector<std::string> audio_devices_;
  std::vector<std::string> serial_devices_;
  std::vector<std::string> wayland_sockets_;
  std::vector<std::string> shared_dirs_;

  base::StringPairs custom_params_;

  std::string gpu_arg_;
};

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_VM_BUILDER_H_
