// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "container_utils/container_config_parser.h"

#include <stddef.h>
#include <sys/mount.h>

#include <base/at_exit.h>
#include <base/values.h>
#include <gtest/gtest.h>

#include "oci_config.h"

namespace {

const char kBasicJsonData[] = R"json(
    {
        "ociVersion": "1.0.0-rc1",
        "platform": {
            "os": "linux",
            "arch": "amd64"
        },
        "root": {
            "path": "rootfs",
            "readonly": true
        },
        "process": {
            "terminal": true,
            "user": {
                "uid": 0,
                "gid": 0
            },
            "args": [
                "sh"
            ],
            "env": [
                "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
                "TERM=xterm"
            ],
            "cwd": "/",
            "capabilities": [
                "CAP_AUDIT_WRITE",
                "CAP_KILL",
                "CAP_NET_BIND_SERVICE"
            ],
            "rlimits": [
                {
                    "type": "RLIMIT_NOFILE",
                    "hard": 1024,
                    "soft": 1024
                }
            ],
            "noNewPrivileges": true
        },
        "hostname": "tester",
        "mounts": [
            {
                "destination": "/proc",
                "type": "proc",
                "source": "proc"
            },
            {
                "destination": "/dev",
                "type": "tmpfs",
                "source": "tmpfs",
                "options": [
                        "nosuid",
                        "strictatime",
                        "mode=755",
                        "size=65536k"
                ]
            },
            {
                "destination": "/dev/pts",
                "type": "devpts",
                "source": "devpts",
                "options": [
                        "nosuid",
                        "noexec",
                        "newinstance",
                        "ptmxmode=0666",
                        "mode=0620",
                        "gid=5"
                ]
            },
            {
                "destination": "/dev/shm",
                "type": "tmpfs",
                "source": "shm",
                "options": [
                        "nosuid",
                        "noexec",
                        "nodev",
                        "mode=1777",
                        "size=65536k"
                ]
            },
            {
                "destination": "/dev/mqueue",
                "type": "mqueue",
                "source": "mqueue",
                "options": [
                        "nosuid",
                        "noexec",
                        "nodev"
                ]
            },
            {
                "destination": "/sys",
                "type": "sysfs",
                "source": "sysfs",
                "options": [
                        "nosuid",
                        "noexec",
                        "nodev",
                        "ro"
                ]
            },
            {
                "destination": "/sys/fs/cgroup",
                "type": "cgroup",
                "source": "cgroup",
                "options": [
                        "nosuid",
                        "noexec",
                        "nodev",
                        "relatime",
                        "ro"
                ]
            }
        ],
        "hooks" : {
            "prestart": [
                {
                    "path": "/usr/bin/fix-mounts",
                    "args": ["fix-mounts", "arg1", "arg2"],
                    "env":  [ "key1=value1"]
                },
                {
                    "path": "/usr/bin/setup-network"
                }
            ],
            "poststart": [
                {
                    "path": "/usr/bin/notify-start",
                    "timeout": 5
                }
            ],
            "poststop": [
                {
                    "path": "/usr/sbin/cleanup.sh",
                    "args": ["cleanup.sh", "-f"]
                }
            ]
        },
        "linux": {
            "devices": [
                {
                    "path": "/dev/fuse",
                    "type": "c",
                    "major": 10,
                    "minor": 229,
                    "fileMode": 438,
                    "uid": 0,
                    "gid": 0
                },
                {
                    "path": "/dev/sda",
                    "type": "b",
                    "major": 8,
                    "minor": 0,
                    "fileMode": 432,
                    "uid": 0,
                    "gid": 0
                }
            ],
            "resources": {
                "devices": [
                    {
                        "allow": false,
                        "access": "rwm"
                    }
                ],
                "network": {
                    "classID": 1048577,
                    "priorities": [
                        {
                            "name": "eth0",
                            "priority": 500
                        },
                        {
                            "name": "eth1",
                            "priority": 1000
                        }
                    ]
                }
            },
            "namespaces": [
                {
                    "type": "pid"
                },
                {
                    "type": "network"
                },
                {
                    "type": "ipc"
                },
                {
                    "type": "uts"
                },
                {
                    "type": "mount"
                }
            ],
            "uidMappings": [
                {
                    "hostID": 1000,
                    "containerID": 0,
                    "size": 10
                }
            ],
            "gidMappings": [
                {
                    "hostID": 1000,
                    "containerID": 0,
                    "size": 10
                }
            ],
            "maskedPaths": [
                "/proc/kcore",
                "/proc/latency_stats",
                "/proc/timer_list",
                "/proc/timer_stats",
                "/proc/sched_debug"
            ],
            "readonlyPaths": [
                "/proc/asound",
                "/proc/bus",
                "/proc/fs",
                "/proc/irq",
                "/proc/sys",
                "/proc/sysrq-trigger"
            ],
            "seccomp": {
                "defaultAction": "SCP_ACT_KILL",
                "architectures": [
                    "SCP_ARCH_X86"
                ],
                "syscalls": [
                    {
                        "name": "read",
                        "action": "SCP_ACT_ALLOW"
                    }
                ]
            }
        }
    }
)json";

}  // anonymous namespace

namespace container_utils {

TEST(OciConfigParserTest, TestBasicConfig) {
  OciConfigPtr basic_config(new OciConfig());
  ASSERT_TRUE(ParseContainerConfig(kBasicJsonData,
                                   basic_config));

  EXPECT_EQ(basic_config->oci_version, "1.0.0-rc1");
  EXPECT_EQ(basic_config->platform.os, "linux");
  EXPECT_EQ(basic_config->root.path, "rootfs");
  EXPECT_EQ(basic_config->root.read_only, true);
  EXPECT_EQ(basic_config->process.terminal, true);
  EXPECT_EQ(basic_config->process.user.uid, 0);
  EXPECT_EQ(basic_config->process.user.gid, 0);
  EXPECT_EQ(basic_config->process.user.additional_gids.size(), 0);
  EXPECT_EQ(basic_config->process.args.size(), 1);
  EXPECT_EQ(basic_config->process.args[0], "sh");
  EXPECT_EQ(basic_config->process.env.size(), 2);
  EXPECT_EQ(basic_config->process.env[1], "TERM=xterm");
  EXPECT_EQ(basic_config->process.cwd, "/");
  EXPECT_EQ(basic_config->hostname, "tester");
  ASSERT_EQ(basic_config->mounts.size(), 7);
  EXPECT_EQ(basic_config->mounts[0].options.size(), 0);
  EXPECT_EQ(basic_config->mounts[1].destination, "/dev");
  EXPECT_EQ(basic_config->mounts[2].options.size(), 6);
  // Devices
  ASSERT_EQ(2, basic_config->linux_config.devices.size());
  OciLinuxDevice *dev = &basic_config->linux_config.devices[0];
  EXPECT_EQ(dev->dev_type, "c");
  EXPECT_EQ(dev->path, "/dev/fuse");
  EXPECT_EQ(dev->file_mode, 438);
  EXPECT_EQ(dev->uid, 0);
  // Namespace Maps
  ASSERT_EQ(1, basic_config->linux_config.uid_mappings.size());
  OciLinuxNamespaceMapping *id_map =
          &basic_config->linux_config.uid_mappings[0];
  EXPECT_EQ(id_map->host_id, 1000);
  EXPECT_EQ(id_map->container_id, 0);
  EXPECT_EQ(id_map->size, 10);
  // seccomp
  OciSeccomp *seccomp = &basic_config->linux_config.seccomp;
  EXPECT_EQ(seccomp->default_action, "SCP_ACT_KILL");
  EXPECT_EQ(seccomp->architectures[0], "SCP_ARCH_X86");
  EXPECT_EQ(seccomp->syscalls[0].name, "read");
  EXPECT_EQ(seccomp->syscalls[0].action, "SCP_ACT_ALLOW");
}

}  // namespace container_utils

int main(int argc, char **argv) {
  base::AtExitManager exit_manager;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
