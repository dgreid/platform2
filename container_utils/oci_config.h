// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Container configuration from the config.json data as specified in
// https://github.com/opencontainers/runtime-spec/tree/v1.0.0-rc1

#ifndef CONTAINER_UTILS_CONTAINER_OCI_CONFIG_H_
#define CONTAINER_UTILS_CONTAINER_OCI_CONFIG_H_

struct OciPlatform {
  std::string os;
  std::string arch;
};

struct OciProcessUser {
  uint32_t uid;
  uint32_t gid;
  // json field name - additionalGids
  std::vector<uint32_t> additional_gids;  // Optional
};

struct OciProcess {
  bool terminal;  // Optional
  OciProcessUser user;
  std::vector<std::string> args;
  std::vector<std::string> env;  // Optional
  std::string cwd;
  // Unused: capabilities, rlimits, apparmorProfile,
  //    selinuxLabel, noNewPrivileges
};

struct OciRoot {
  std::string path;
  // json field name - readonly
  bool read_only;  // Optional
};

struct OciMount {
  std::string destination;
  // json field name - type
  std::string mount_type;
  std::string source;
  std::vector<std::string> options;  // Optional
};

struct OciLinuxNamespaceMapping {
  // json field name - hostID
  uint64_t host_id;
  // json field name - containerID
  uint64_t container_id;
  uint64_t size;
};

struct OciLinuxDevice {
  // json field name - type
  std::string dev_type;
  std::string path;
  uint32_t major;  // Optional
  uint32_t minor;  // Optional
  // json field name - fileMode
  uint32_t file_mode;  // Optional
  uint32_t uid;  // Optional
  uint32_t gid;  // Optional
};

struct OciSeccompSyscall {
  std::string name;
  std::string action;
}

struct OciSeccomp {
  // json field name - defaultAction
  std::string default_action;
  std::vector<std::string> architectures;
  std::vector<OciSeccompSyscall> syscalls;
}

struct OciLinux {
  std::vector<OciLinuxDevice> devices;  // Optional
  // json field name - cgroupsPath
  std::string cgroups_path;  // Optional
  // Unused: resources, namespace
  // json field name - uidMappings
  std::vector<OciLinuxNamespaceMapping> uid_mappings;  // Optional
  // json field name - gidMappings
  std::vector<OciLinuxNamespaceMapping> gid_mappings;  // Optional
  OciSeccomp seccomp;
  // Unused: maskedPaths, readonlyPaths, rootfsPropagation, mountLabel, sysctl
};

struct OciConfig {
  // json field name - ociVersion
  std::string oci_version;
  OciPlatform platform;
  OciRoot root;
  OciProcess process;
  std::string hostname;  // Optional
  std::vector<OciMount> mounts;  // Optional
  // json field name - linux
  OciLinux linux_config;  // Optional
  // TODO - hooks, Annotations
};

#endif  // CONTAINER_UTILS_CONTAINER_OCI_CONFIG_H_
