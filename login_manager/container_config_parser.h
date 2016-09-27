// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_CONTAINER_CONFIG_PARSER_H_
#define LOGIN_MANAGER_CONTAINER_CONFIG_PARSER_H_

#include <memory>
#include <string>

#include <libcontainer/libcontainer.h>
#include <base/files/file_path.h>
#include <base/values.h>

namespace login_manager {

using ContainerConfigPtr = std::unique_ptr<container_config,
                                           decltype(&container_config_destroy)>;

// Parses container configuration from the config.json and runtime.json data as
// specified in https://github.com/opencontainers/runtime-spec/tree/v0.2.0
//  |config_json_data| - The text from config.json.
//  |runtime_json_data| - The text from runtime.json.
//  |mountinfo_data| - The text from /proc/self/mountinfo.
//  |container_name| - Unique name for the container.
//  |parent_cgroup_name| - Name of the parent cgroup for this container.
//  |named_container_path| - Path to the base of the container data and rootfs.
//  |config_out| - Filled with the configuration, defined in libcontainer.
bool ParseContainerConfig(const std::string& config_json_data,
                          const std::string& runtime_json_data,
                          const std::string& mountinfo_data,
                          const std::string& container_name,
                          const std::string& parent_cgroup_name,
                          const base::FilePath& named_container_path,
                          ContainerConfigPtr* config_out);

}  // namespace login_manager

#endif  // LOGIN_MANAGER_CONTAINER_CONFIG_PARSER_H_
