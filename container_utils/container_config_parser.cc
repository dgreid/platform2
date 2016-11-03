// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "container_utils/container_config_parser.h"

#include <unistd.h>

#include <string>
#include <vector>

#include <base/json/json_reader.h>
#include <base/values.h>

namespace container_utils {

namespace {

// Parses basic platform configuration.
bool ParsePlatformConfig(const base::DictionaryValue& config_root_dict,
                         OciConfigPtr const& config_out) {
  // |platform_dict| stays owned by |config_root_dict|
  const base::DictionaryValue* platform_dict = nullptr;
  if (!config_root_dict.GetDictionary("platform", &platform_dict)) {
    LOG(ERROR) << "Fail to parse platform dictionary from config";
    return false;
  }

  if (!platform_dict->GetString("os", &config_out->platform.os)) {
    return false;
  }

  if (!platform_dict->GetString("arch", &config_out->platform.arch)) {
    return false;
  }

  return true;
}

// Parses root fs info.
bool ParseRootFileSystemConfig(const base::DictionaryValue& config_root_dict,
                               OciConfigPtr const& config_out) {
  // |rootfs_dict| stays owned by |config_root_dict|
  const base::DictionaryValue* rootfs_dict = nullptr;
  if (!config_root_dict.GetDictionary("root", &rootfs_dict)) {
    LOG(ERROR) << "Fail to parse rootfs dictionary from config";
    return false;
  }
  if (!rootfs_dict->GetString("path", &config_out->root.path)) {
    LOG(ERROR) << "Fail to get rootfs path from config";
    return false;
  }
  rootfs_dict->GetBoolean("readonly", &config_out->root.read_only);
  return true;
}

// Fills |config_out| with information about the main process to run in the
// container and the user it should be run as.
bool ParseProcessConfig(const base::DictionaryValue& config_root_dict,
                        OciConfigPtr const& config_out) {
  // |process_dict| stays owned by |config_root_dict|
  const base::DictionaryValue* process_dict = nullptr;
  if (!config_root_dict.GetDictionary("process", &process_dict)) {
    LOG(ERROR) << "Fail to get main process from config";
    return false;
  }
  process_dict->GetBoolean("terminal", &config_out->process.terminal);
  // |user_dict| stays owned by |process_dict|
  const base::DictionaryValue* user_dict = nullptr;
  if (!process_dict->GetDictionary("user", &user_dict)) {
    LOG(ERROR) << "Failed to get user info from config";
    return false;
  }
  int uid;
  if (!user_dict->GetInteger("uid", &uid)) {
    LOG(ERROR) << "Failed to get uid info from config";
    return false;
  }
  config_out->process.user.uid = uid;
  int gid;
  if (!user_dict->GetInteger("gid", &gid)) {
    LOG(ERROR) << "Failed to get gid info from config";
    return false;
  }
  config_out->process.user.gid = gid;
  // |args_list| stays owned by |process_dict|
  const base::ListValue* args_list = nullptr;
  if (!process_dict->GetList("args", &args_list)) {
    LOG(ERROR) << "Fail to get main process args from config";
    return false;
  }
  size_t num_args = args_list->GetSize();
  for (size_t i = 0; i < num_args; ++i) {
    std::string arg;
    if (!args_list->GetString(i, &arg)) {
      LOG(ERROR) << "Fail to get process args from config";
      return false;
    }
    config_out->process.args.push_back(arg);
  }
  // |env_list| stays owned by |process_dict|
  const base::ListValue* env_list = nullptr;
  if (process_dict->GetList("env", &env_list)) {
    size_t num_env = env_list->GetSize();
    for (size_t i = 0; i < num_env; ++i) {
      std::string env;
      if (!env_list->GetString(i, &env)) {
        LOG(ERROR) << "Fail to get process env from config";
        return false;
      }
      config_out->process.env.push_back(env);
    }
  }
  if (!process_dict->GetString("cwd", &config_out->process.cwd)) {
    LOG(ERROR) << "failed to get cwd of process";
    return false;
  }

  return true;
}

// Parses the 'mounts' field.  The necessary mounts for running the container
// are specified here.
bool ParseMounts(const base::DictionaryValue& config_root_dict,
                 OciConfigPtr const& config_out) {
  // |config_mounts_list| stays owned by |config_root_dict|
  const base::ListValue* config_mounts_list = nullptr;
  if (!config_root_dict.GetList("mounts", &config_mounts_list)) {
    LOG(ERROR) << "Fail to get mounts from config dictionary";
    return false;
  }

  for (size_t i = 0; i < config_mounts_list->GetSize(); ++i) {
    const base::DictionaryValue* mount_dict;
    if (!config_mounts_list->GetDictionary(i, &mount_dict)) {
      LOG(ERROR) << "Fail to get mount item " << i;
      return false;
    }
    OciMount mount;
    if (!mount_dict->GetString("destination", &mount.destination)) {
      LOG(ERROR) << "Fail to get mount path for mount " << i;
      return false;
    }
    if (!mount_dict->GetString("type", &mount.mount_type)) {
      LOG(ERROR) << "Fail to get mount type for mount " << i;
      return false;
    }
    if (!mount_dict->GetString("source", &mount.source)) {
      LOG(ERROR) << "Fail to get mount source for mount " << i;
      return false;
    }

    // |options| are owned by |mount_dict|
    const base::ListValue* options = nullptr;
    if (mount_dict->GetList("options", &options)) {
      for (size_t j = 0; j < options->GetSize(); ++j) {
        std::string this_opt;
        if (!options->GetString(j, &this_opt)) {
          LOG(ERROR) << "Fail to get option " << j << " from mount options";
          return false;
        }
        mount.options.push_back(this_opt);
      }
    }

    config_out->mounts.push_back(mount);
  }
  return true;
}

// Parse the list of device nodes that the container needs to run.
bool ParseDeviceList(const base::DictionaryValue& linux_dict,
                     OciConfigPtr const& config_out) {
  // |device_list| is owned by |linux_dict|
  const base::ListValue* device_list = nullptr;
  if (!linux_dict.GetList("devices", &device_list)) {
    LOG(ERROR) << "Fail to get device list";
    return false;
  }
  size_t num_devices = device_list->GetSize();
  for (size_t i = 0; i < num_devices; ++i) {
    OciLinuxDevice device;

    const base::DictionaryValue* dev;
    if (!device_list->GetDictionary(i, &dev)) {
      LOG(ERROR) << "Fail to get device " << i;
      return false;
    }
    std::string path;
    if (!dev->GetString("path", &device.path)) {
      LOG(ERROR) << "Fail to get path for dev";
      return false;
    }
    if (!dev->GetString("type", &device.dev_type)) {
      LOG(ERROR) << "Fail to get type for " << device.path;
      return false;
    }
    int major = 0;
    dev->GetInteger("major", &major);
    device.major = major;
    int minor = 0;
    dev->GetInteger("minor", &minor);
    device.minor = minor;
    int file_mode = 0;
    dev->GetInteger("fileMode", &file_mode);
    device.file_mode = file_mode;
    int dev_uid = 0;
    dev->GetInteger("uid", &dev_uid);
    device.uid = dev_uid;
    int dev_gid = 0;
    dev->GetInteger("gid", &dev_gid);
    device.gid = dev_gid;

    config_out->linux_config.devices.push_back(device);
  }

  return true;
}

// Parses the list of ID mappings and fills |mappings_out| with them.
bool ParseLinuxIdMappings(const base::ListValue* id_map_list,
                          std::vector<OciLinuxNamespaceMapping>& mappings_out) {
  for (size_t i = 0; i < id_map_list->GetSize(); ++i) {
    OciLinuxNamespaceMapping new_map;
    const base::DictionaryValue* map;
    if (!id_map_list->GetDictionary(i, &map)) {
      LOG(ERROR) << "Fail to get id map " << i;
      return false;
    }
    int host_id = 0;
    if (!map->GetInteger("hostID", &host_id)) {
      LOG(ERROR) << "Failed to read hostID from map " << i;
      return false;
    }
    new_map.host_id = host_id;
    int container_id = 0;
    if (!map->GetInteger("containerID", &container_id)) {
      LOG(ERROR) << "Failed to read containerID from map " << i;
      return false;
    }
    new_map.container_id = container_id;
    int size = 0;
    if (!map->GetInteger("size", &size)) {
      LOG(ERROR) << "Failed to read size from map " << i;
      return false;
    }
    new_map.size = size;
    mappings_out.push_back(new_map);
  }
  return true;
}

// Parses the seccomp node if it is present
bool ParseLinuxSeccompInfo(const base::DictionaryValue& seccomp_dict,
                           OciSeccomp* seccomp_out) {
  if (!seccomp_dict.GetString("defaultAction",
                              *seccom_out->default_architecture))
    return false;

  // Get the list of architectures
  const base::ListValue* architectures = nullptr;
  if (!seccom_dict.GetList("architectures", &architectures)) {
    LOG(ERROR) << "Fail to read seccomp architectures";
    return false;
  }
  for (int i = 0; i < architectures.GetSize(); ++i) {
    std::string this_arch;
    if (!architectures->GetString(i, *this_arch)) {
      LOG(ERROR) << "Fail to parse seccomp architecture list";
      return false;
    }
    seccomp_out->architectures.push_back(this_arch);
  }

  // Get the list of syscalls
  const base::ListValue* syscalls = nullptr;
  if (!seccom_dict.GetList("syscalls", &syscalls)) {
    LOG(ERROR) << "Fail to read seccomp syscalls";
    return false;
  }
  for (int i = 0; i < syscalls.GetSize(); ++i) {
    const base::DictionaryValue* syscall_dict;
    if (!syscalls->GetDictionary(i, *syscall_dict)) {
      LOG(ERROR) << "Fail to parse seccomp syscalls list";
      return false;
    }
    OciLinuxSeccompSyscall this_syscall;
    if (!syscalls->GetString("name", &this_syscall->name)) {
      LOG(ERROR) << "Fail to parse syscall name " << i;
      return false;
    }
    if (!syscalls->GetString("action", &this_syscall->action)) {
      LOG(ERROR) << "Fail to parse syscall action " << i;
      return false;
    }
    seccomp_out->syscalls.push_back(this_syscall);
  }
}

// Parses the linux node which has information about setting up a user
// namespace, and the list of devices for the container.
bool ParseLinuxConfigDict(const base::DictionaryValue& runtime_root_dict,
                          OciConfigPtr const& config_out) {
  // |linux_dict| is owned by |runtime_root_dict|
  const base::DictionaryValue* linux_dict = nullptr;
  if (!runtime_root_dict.GetDictionary("linux", &linux_dict)) {
    LOG(ERROR) << "Fail to get linux dictionary from the runtime dictionary";
    return false;
  }

  // |uid_map_list| is owned by |linux_dict|
  const base::ListValue* uid_map_list = nullptr;
  if (!linux_dict->GetList("uidMappings", &uid_map_list)) {
    LOG(ERROR) << "Fail to get uid mappings list";
    return false;
  }
  ParseLinuxIdMappings(uid_map_list, config_out->linux_config.uid_mappings);

  // |gid_map_list| is owned by |linux_dict|
  const base::ListValue* gid_map_list = nullptr;
  if (!linux_dict->GetList("gidMappings", &gid_map_list)) {
    LOG(ERROR) << "Fail to get gid mappings list";
    return false;
  }
  ParseLinuxIdMappings(gid_map_list, config_out->linux_config.gid_mappings);

  if (!ParseDeviceList(*linux_dict, config_out))
    return false;

  const base::DictionaryValue* seccomp_dict = nullptr;
  if (linux_dict->GetList("seccomp", &seccomp_dict)) {
    if (!ParseSeccompInfo(*seccomp_dict, config_out))
      return false;
  }

  return true;
}

// Parses the configuration file for the container.  The config file specifies
// basic filesystem info and details about the process to be run.  namespace,
// cgroup, and syscall configurations are also specified
bool ParseConfigDict(const base::DictionaryValue& config_root_dict,
                     OciConfigPtr const& config_out) {
  if (!config_root_dict.GetString("ociVersion", &config_out->oci_version)) {
    LOG(ERROR) << "Failed to parse ociVersion";
    return false;
  }
  if (!config_root_dict.GetString("hostname", &config_out->hostname)) {
    LOG(ERROR) << "Failed to parse hostname";
    return false;
  }

  // Platform info
  if (!ParsePlatformConfig(config_root_dict, config_out)) {
    return false;
  }

  // Root fs info
  if (!ParseRootFileSystemConfig(config_root_dict, config_out)) {
    return false;
  }

  // Process info
  if (!ParseProcessConfig(config_root_dict, config_out)) {
    return false;
  }

  // Get a list of mount points and mounts.
  if (!ParseMounts(config_root_dict, config_out)) {
    LOG(ERROR) << "Failed to parse mounts";
    return false;
  }

  // Parse linux node.
  if (!ParseLinuxConfigDict(config_root_dict, config_out)) {
    LOG(ERROR) << "Failed to parse the linux node";
    return false;
  }

  return true;
}

} // anonymous namespace

bool ParseContainerConfig(const std::string& config_json_data,
                          OciConfigPtr const& config_out) {
  std::unique_ptr<const base::Value> config_root_val =
      base::JSONReader::Read(config_json_data);
  if (!config_root_val) {
    LOG(ERROR) << "Fail to parse config.json";
    return false;
  }
  const base::DictionaryValue* config_dict = nullptr;
  if (!config_root_val->GetAsDictionary(&config_dict)) {
    LOG(ERROR) << "Fail to parse root dictionary from config.json";
    return false;
  }
  if (!ParseConfigDict(*config_dict, config_out)) {
    return false;
  }

  return true;
}

} // namespace container_utils
