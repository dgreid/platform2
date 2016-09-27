// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/container_config_parser.h"

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/values.h>

#include <libcontainer/libcontainer.h>

namespace login_manager {

namespace {

// Parses |mountinfo_data| (the contents of /proc/self/mountinfo) to determine
// whether |rootfs_path| was originally mounted as read-only.
bool IsOriginalRootfsReadOnly(const std::string& mountinfo_data,
                              const base::FilePath& rootfs_path) {
  constexpr size_t kMountinfoMountPointIndex = 4;
  constexpr size_t kMountinfoMountOptionsIndex = 5;
  const size_t kMountinfoMinNumberOfTokens =
      std::max(kMountinfoMountPointIndex, kMountinfoMountOptionsIndex) + 1;

  std::vector<std::string> lines =
      base::SplitString(mountinfo_data, "\n", base::TRIM_WHITESPACE,
                        base::SPLIT_WANT_NONEMPTY);

  for (const auto& line : lines) {
    std::vector<std::string> tokens =
        base::SplitString(line, base::kWhitespaceASCII, base::TRIM_WHITESPACE,
                          base::SPLIT_WANT_NONEMPTY);
    // Some fields in /proc/self/mountinfo are optional. We only need the line
    // to have |kMountinfoMinNumberOfTokens|.
    if (tokens.size() < kMountinfoMinNumberOfTokens)
      continue;
    if (tokens[kMountinfoMountPointIndex] != rootfs_path.value())
      continue;

    std::vector<std::string> options =
        base::SplitString(tokens[kMountinfoMountOptionsIndex], ",",
                          base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

    return std::find(options.begin(), options.end(), "ro") != options.end();
  }

  LOG(WARNING) << "Did not find mount information for " << rootfs_path.value()
               << ". Assuming mounted read-only.";

  return true;
}

// Sets the rootfs of |config| to point to where the rootfs of the container
// is mounted.
bool ParseRootFileSystemConfig(const base::DictionaryValue& config_root_dict,
                               const base::FilePath& named_path,
                               const std::string& mountinfo_data,
                               ContainerConfigPtr* config_out) {
  // |rootfs_dict| stays owned by |config_root_dict|
  const base::DictionaryValue* rootfs_dict = nullptr;
  if (!config_root_dict.GetDictionary("root", &rootfs_dict)) {
    LOG(ERROR) << "Fail to parse rootfs dictionary from config";
    return false;
  }
  std::string rootfs_path;
  if (!rootfs_dict->GetString("path", &rootfs_path)) {
    LOG(ERROR) << "Fail to get rootfs path from config";
    return false;
  }
  container_config_rootfs(config_out->get(),
                          named_path.Append(rootfs_path).value().c_str());
  // Explicitly set the mount flags of the rootfs.
  //
  // In Chrome OS, the rootfs is mounted nosuid, nodev, noexec. We need the
  // filesystem to be mounted without those three flags within the container for
  // it to work correctly, so explicitly remount with none of those flags. We
  // need to preserve the ro/rw state of the original mount, though, since the
  // internal namespace will reflect whatever flag was passed here instead of
  // respecting the original filesystem's ro/rw state.
  uint32_t flags = 0;
  if (IsOriginalRootfsReadOnly(mountinfo_data,
                               named_path.Append(rootfs_path))) {
    flags |= MS_RDONLY;
  }
  container_config_rootfs_mount_flags(config_out->get(), flags);
  return true;
}

// Fills |config|, |uid|, and |gid| with information about the main process to
// run in the container and the user it should be run as.  |uid| and |gid|
// will be filled with IDs from the initial user namespace, not the IDs within
// the container.
bool ParseProcessConfig(const base::DictionaryValue& config_root_dict,
                        uid_t* uid_out,
                        gid_t* gid_out,
                        ContainerConfigPtr* config_out) {
  // |process_dict| stays owned by |config_root_dict|
  const base::DictionaryValue* process_dict = nullptr;
  if (!config_root_dict.GetDictionary("process", &process_dict)) {
    LOG(ERROR) << "Fail to get main process from config";
    return false;
  }
  // |user_dict| stays owned by |process_dict|
  const base::DictionaryValue* user_dict = nullptr;
  if (!process_dict->GetDictionary("user", &user_dict)) {
    LOG(ERROR) << "Failed to get user info from config";
    return false;
  }
  int uid_val;
  if (!user_dict->GetInteger("uid", &uid_val)) {
    LOG(ERROR) << "Failed to get uid info from config";
    return false;
  }
  container_config_uid(config_out->get(), uid_val);
  *uid_out = uid_val;
  int gid_val;
  if (!user_dict->GetInteger("gid", &gid_val)) {
    LOG(ERROR) << "Failed to get gid info from config";
    return false;
  }
  container_config_gid(config_out->get(), gid_val);
  *gid_out = gid_val;
  // |args_dict| stays owned by |process_dict|
  const base::ListValue* args_list = nullptr;
  if (!process_dict->GetList("args", &args_list)) {
    LOG(ERROR) << "Fail to get main process args from config";
    return false;
  }
  size_t num_args = args_list->GetSize();
  std::vector<std::string> argv_str(num_args);
  for (size_t i = 0; i < num_args; ++i) {
    if (!args_list->GetString(i, &argv_str[i])) {
      LOG(ERROR) << "Fail to get process args from config";
      return false;
    }
  }
  std::vector<char*> argv(num_args);
  for (size_t i = 0; i < num_args; ++i) {
    argv[i] = const_cast<char*>(argv_str[i].c_str());
  }
  container_config_program_argv(config_out->get(), &argv[0], num_args);
  return true;
}

// Parses the mount options for a given mount.
bool ParseMountOptions(const base::ListValue& options,
                       std::string* option_string_out,
                       int* flags_out,
                       bool* mount_in_ns_out,
                       bool* create_mount_point_out,
                       bool* is_root_relative_out) {
  *flags_out = 0;
  *mount_in_ns_out = true;
  *create_mount_point_out = true;
  *option_string_out = "";
  *is_root_relative_out = false;
  for (size_t j = 0; j < options.GetSize(); ++j) {
    std::string this_opt;
    if (!options.GetString(j, &this_opt)) {
      LOG(ERROR) << "Fail to get option " << j << "from mount options";
      return false;
    }
    if (this_opt == "nodev") {
      *flags_out |= MS_NODEV;
    } else if (this_opt == "noexec") {
      *flags_out |= MS_NOEXEC;
    } else if (this_opt == "nosuid") {
      *flags_out |= MS_NOSUID;
    } else if (this_opt == "bind") {
      *flags_out |= MS_BIND;
    } else if (this_opt == "ro") {
      *flags_out |= MS_RDONLY;
    } else if (this_opt == "private") {
      *flags_out |= MS_PRIVATE;
    } else if (this_opt == "recursive") {
      *flags_out |= MS_REC;
    } else if (this_opt == "slave") {
      *flags_out |= MS_SLAVE;
    } else if (this_opt == "root_relative") {
      *is_root_relative_out = true;
    } else if (this_opt == "remount") {
      *flags_out |= MS_REMOUNT;
    } else if (this_opt == "mount_outside") {
      // This is a cros-specific option
      *mount_in_ns_out = 0;
    } else if (this_opt == "nocreate") {
      // This is a cros-specific option
      *create_mount_point_out = 0;
    } else {
      // Unknown options get appended to the string passed to mount data.
      if (!option_string_out->empty())
        *option_string_out += ",";
      *option_string_out += this_opt;
    }
  }
  return true;
}

// Parses the info about a mount named |mount_name| that is specified in the
// runtime mount dictionary and adds the mount to the given container
// configuration in |config_out|
bool ParseRuntimeMount(const base::DictionaryValue& runtime_mounts_dict,
                       const base::FilePath& named_path,
                       const std::string& mount_name,
                       const base::FilePath& destination_path,
                       uid_t uid,
                       gid_t gid,
                       ContainerConfigPtr* config_out) {
  // |mount_dict| is owned by |rutime_mounts_dict|
  const base::DictionaryValue* mount_dict = nullptr;
  if (!runtime_mounts_dict.GetDictionary(mount_name, &mount_dict)) {
    LOG(ERROR) << "Fail to get mount " << mount_name << " from runtime";
    return false;
  }

  std::string type;
  if (!mount_dict->GetString("type", &type)) {
    LOG(ERROR) << "Fail to get mount type from runtime";
    return false;
  }

  // |options| are owned by |mount_dict|
  const base::ListValue* options = nullptr;
  if (!mount_dict->GetList("options", &options)) {
    LOG(ERROR) << "Fail to get mount options from runtime";
    return false;
  }
  std::string option_string;
  int flags;
  bool mount_in_ns;
  bool create_mount_point;
  bool root_relative;
  if (!ParseMountOptions(*options, &option_string, &flags, &mount_in_ns,
                         &create_mount_point, &root_relative)) {
    LOG(ERROR) << "Failed to parse mount options for " << mount_name;
    return false;
  }

  std::string source;
  if (!mount_dict->GetString("source", &source)) {
    LOG(ERROR) << "Fail to get mount source from runtime";
    return false;
  }
  base::FilePath source_path(source);
  if ((flags & MS_BIND) && !root_relative) {
    // If it's not an absolute path, append it to the container config dir
    if (!source_path.IsAbsolute()) {
      source_path = named_path.Append(source_path);
    }
  }

  if (container_config_add_mount(config_out->get(),
                                 mount_name.c_str(),
                                 source_path.value().c_str(),
                                 destination_path.value().c_str(),
                                 type.c_str(),
                                 option_string.length() ? option_string.c_str()
                                     : NULL,
                                 flags,
                                 uid,
                                 gid,
                                 0,
                                 mount_in_ns,
                                 create_mount_point)) {
    LOG(ERROR) << "Failed to add mount " << mount_name << " to config";
    return false;
  }

  return true;
}

// Mount information is distributed between the config and the runtime files.
// Parse info from each of the structs to build the mount config and add it to
// the container configuration.
bool ParseMounts(const base::DictionaryValue& config_root_dict,
                 const base::DictionaryValue& runtime_root_dict,
                 const base::FilePath& named_path,
                 uid_t uid,
                 gid_t gid,
                 ContainerConfigPtr* config_out) {
  // |config_mounts_list| stays owned by |config_root_dict|
  const base::ListValue* config_mounts_list = nullptr;
  if (!config_root_dict.GetList("mounts", &config_mounts_list)) {
    LOG(ERROR) << "Fail to get mounts from config dictionary";
    return false;
  }
  // |runtime_mounts_dict| stays owned by |runtime_root_dict|
  const base::DictionaryValue* runtime_mounts_dict = nullptr;
  if (!runtime_root_dict.GetDictionary("mounts", &runtime_mounts_dict)) {
    LOG(ERROR) << "Fail to get mounts dictionary from runtime";
    return false;
  }

  for (size_t i = 0; i < config_mounts_list->GetSize(); i++) {
    const base::DictionaryValue* config_mounts_item;
    if (!config_mounts_list->GetDictionary(i, &config_mounts_item)) {
      LOG(ERROR) << "Fail to get mount list " << i << " from config";
      return false;
    }
    std::string mount_name;
    if (!config_mounts_item->GetString("name", &mount_name)) {
      LOG(ERROR) << "Fail to get mount name " << i << " from config";
      return false;
    }
    std::string destination;
    if (!config_mounts_item->GetString("path", &destination)) {
      LOG(ERROR) << "Fail to get mount path " << i << " from config";
      return false;
    }
    base::FilePath destination_path(destination);

    if (!ParseRuntimeMount(*runtime_mounts_dict, named_path, mount_name,
                           destination_path, uid, gid, config_out)) {
      LOG(ERROR) << "Failed to parse runtime mount info for mount " << i;
      return false;
    }
  }
  return true;
}

// Parse the list of device nodes that the container needs to run.  |config|
// will have all the devices listed in |linux_dict| added to a list that creates
// and sets permissions for them when the container starts.
bool ParseDeviceList(const base::DictionaryValue& linux_dict,
                     ContainerConfigPtr* config_out) {
  // |device_list| is owned by |linux_dict|
  const base::ListValue* device_list = nullptr;
  if (!linux_dict.GetList("devices", &device_list)) {
    LOG(ERROR) << "Fail to get device list";
    return false;
  }
  size_t num_devices = device_list->GetSize();
  for (size_t i = 0; i < num_devices; ++i) {
    const base::DictionaryValue* dev;
    if (!device_list->GetDictionary(i, &dev)) {
      LOG(ERROR) << "Fail to get device " << i;
      return false;
    }
    std::string path;
    if (!dev->GetString("path", &path)) {
      LOG(ERROR) << "Fail to get path for " << path;
      return false;
    }
    int type;
    if (!dev->GetInteger("type", &type)) {
      LOG(ERROR) << "Fail to get type for " << path;
      return false;
    }
    // Only 'c' and 'b' device types supported.
    if (type != 'b' && type != 'c') {
      LOG(ERROR) << "Invalid device type for " << path;
      return false;
    }
    int major;
    if (!dev->GetInteger("major", &major)) {
      LOG(ERROR) << "Fail to get major id of " << path;
      return false;
    }
    int minor;
    if (!dev->GetInteger("minor", &minor)) {
      LOG(ERROR) << "Fail to get minor id of " << path;
      return false;
    }
    // If minor is negative, mirror the current device.
    // This is a cros-specific extension.
    int copy_minor = 0;
    if (path != "nodev" && minor < 0)
      copy_minor = 1;
    std::string permissions;
    if (!dev->GetString("permissions", &permissions)) {
      LOG(ERROR) << "Fail to get device permissions of " << path;
      return false;
    }
    int read_allowed = (permissions.find('r') != std::string::npos);
    int write_allowed = (permissions.find('w') != std::string::npos);
    int modify_allowed = (permissions.find('m') != std::string::npos);
    int fs_permissions;
    if (!dev->GetInteger("fileMode", &fs_permissions)) {
      LOG(ERROR) << "Fail to get permissions of " << path;
      return false;
    }
    int dev_uid;
    if (!dev->GetInteger("uid", &dev_uid)) {
      LOG(ERROR) << "Fail to get uid of " << path;
      return false;
    }
    int dev_gid;
    if (!dev->GetInteger("gid", &dev_gid)) {
      LOG(ERROR) << "Fail to get gid of " << path;
      return false;
    }

    container_config_add_device(config_out->get(),
                                type,
                                path.c_str(),
                                fs_permissions,
                                major,
                                minor,
                                copy_minor,
                                dev_uid,
                                dev_gid,
                                read_allowed,
                                write_allowed,
                                modify_allowed);
  }

  return true;
}

// Parse the CPU cgroup settings for the container.
// CPU cgroup params are optional.
bool ParseCpuDict(const base::DictionaryValue& linux_dict,
                  ContainerConfigPtr* config_out) {
  // |cpu_dict| is owned by |linux_dict|.
  const base::DictionaryValue* cpu_dict = nullptr;
  if (!linux_dict.GetDictionary("cpu", &cpu_dict))
    return false;

  int shares;
  if (cpu_dict->GetInteger("shares", &shares))
    container_config_set_cpu_shares(config_out->get(), shares);

  int quota, period;
  if (cpu_dict->GetInteger("quota", &quota) &&
      cpu_dict->GetInteger("period", &period)) {
    container_config_set_cpu_cfs_params(config_out->get(), quota, period);
  }

  int rt_runtime, rt_period;
  if (cpu_dict->GetInteger("realtimeRuntime", &rt_runtime) &&
      cpu_dict->GetInteger("realtimePeriod", &rt_period)) {
    container_config_set_cpu_rt_params(config_out->get(),
                                       rt_runtime,
                                       rt_period);
  }

  return true;
}

// Parses the linux node which has information about setting up a user
// namespace, alt-syscall table and the list of devices for the container.
bool ParseLinuxConfigDict(const base::DictionaryValue& runtime_root_dict,
                          ContainerConfigPtr* config_out) {
  // |linux_dict| is owned by |runtime_root_dict|
  const base::DictionaryValue* linux_dict = nullptr;
  if (!runtime_root_dict.GetDictionary("linux", &linux_dict)) {
    LOG(ERROR) << "Fail to get linux dictionary from the runtime dictionary";
    return false;
  }

  // User mappings for configuring a user namespace.
  std::string uid_map;
  if (!linux_dict->GetString("uidMappings", &uid_map)) {
    LOG(ERROR) << "Fail to get uid map from the runtime dictionary";
    return false;
  }
  container_config_uid_map(config_out->get(), uid_map.c_str());

  // Group mappings
  std::string gid_map;
  if (!linux_dict->GetString("gidMappings", &gid_map)) {
    LOG(ERROR) << "Fail to get gid map from the runtime dictionary";
    return false;
  }
  container_config_gid_map(config_out->get(), gid_map.c_str());

  // alt-syscall table is a cros-specific entry.
  std::string syscall_table;
  if (!linux_dict->GetString("altSysCallTable", &syscall_table)) {
    LOG(ERROR) << "No altSysCallTable specified";
    return false;
  }
  container_config_alt_syscall_table(config_out->get(), syscall_table.c_str());

  if (!ParseDeviceList(*linux_dict, config_out))
    return false;

  // CPU cgroup params are optional.
  ParseCpuDict(*linux_dict, config_out);

  return true;
}

// Parses the configuration file for the container.  The config file specifies
// basic filesystem info and details about the process to be run.  More specific
// information is gathered from the runtime config file.  In the runtime file
// most of the details come from the "linux" node.  They specify namespace,
// cgroup, and syscall configurations that are critical to keeping the process
// sandboxed.
bool ParseConfigDicts(const base::DictionaryValue& config_root_dict,
                      const base::DictionaryValue& runtime_root_dict,
                      const base::FilePath& named_path,
                      const std::string& mountinfo_data,
                      ContainerConfigPtr* config_out) {
  // Root fs info
  if (!ParseRootFileSystemConfig(config_root_dict, named_path,
                                 mountinfo_data, config_out)) {
    return false;
  }

  // Process info
  uid_t uid;
  gid_t gid;
  if (!ParseProcessConfig(config_root_dict, &uid, &gid, config_out))
    return false;

  // Get a list of mount points and mounts from the config dictionary.
  // The details are filled in while parsing the runtime dictionary.
  if (!ParseMounts(config_root_dict, runtime_root_dict, named_path,
                   uid, gid, config_out)) {
    LOG(ERROR) << "Failed to parse mounts of " << named_path.value();
    return false;
  }

  // Parse linux node.
  if (!ParseLinuxConfigDict(runtime_root_dict, config_out)) {
    LOG(ERROR) << "Failed to parse the linux node of " << named_path.value();
    return false;
  }

  return true;
}

}  // anonymous namespace

bool ParseContainerConfig(const std::string& config_json_data,
                          const std::string& runtime_json_data,
                          const std::string& mountinfo_data,
                          const std::string& container_name,
                          const std::string& parent_cgroup_name,
                          const base::FilePath& named_container_path,
                          ContainerConfigPtr* config_out) {
  // Basic config info comes from config.json
  std::unique_ptr<const base::Value> config_root_val =
      base::JSONReader::Read(config_json_data);
  if (!config_root_val) {
    LOG(ERROR) << "Fail to parse config for " << container_name;
    return false;
  }
  const base::DictionaryValue* config_dict = nullptr;
  if (!config_root_val->GetAsDictionary(&config_dict)) {
    LOG(ERROR) << "Fail to parse root dictionary from config for "
               << container_name;
    return false;
  }

  // Use runtime.json to complete the config struct.
  std::unique_ptr<const base::Value> runtime_root_val =
      base::JSONReader::Read(runtime_json_data);
  if (!runtime_root_val) {
    LOG(ERROR) << "Fail to parse runtime for " << container_name;
    return false;
  }
  const base::DictionaryValue* runtime_dict = nullptr;
  if (!runtime_root_val->GetAsDictionary(&runtime_dict)) {
    LOG(ERROR) << "Fail to parse root dictionary from runtime for "
               << container_name;
    return false;
  }
  if (!ParseConfigDicts(*config_dict, *runtime_dict, named_container_path,
                        mountinfo_data, config_out)) {
    return false;
  }

  // Set the cgroup configuration
  if (container_config_set_cgroup_parent(
          config_out->get(), parent_cgroup_name.c_str(),
          container_config_get_uid(config_out->get()))) {
    LOG(ERROR) << "Failed to configure cgroup structure of " << container_name;
    return false;
  }

  // Hack for android containers that need selinux commands run.
  if (container_name.find("android") != std::string::npos) {
    if (container_config_run_setfiles(config_out->get(), "/sbin/setfiles")) {
      LOG(ERROR) << "Fail to set setfiles for "
                 << container_name;
      return false;
    }
  }

  return true;
}

}  // namespace login_manager
