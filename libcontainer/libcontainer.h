/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef LIBCONTAINER_LIBCONTAINER_H_
#define LIBCONTAINER_LIBCONTAINER_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct container_config;

/* Create a container config. */
struct container_config *container_config_create();

/* Destroy a config create with container_config_create. */
void container_config_destroy(struct container_config *c);

/* rootfs - Path to the root of the container's filesystem. */
int container_config_rootfs(struct container_config *c, const char *rootfs);

/* Get the configured rootfs path. */
const char *container_config_get_rootfs(const struct container_config *c);

/* rootfs_mount_flags - Flags that will be passed to the mount() call when
 * mounting the root of the container's filesystem. */
void container_config_rootfs_mount_flags(struct container_config *c,
					 unsigned long flags);

/* Get the configured rootfs mount() flags. */
unsigned long container_config_get_rootfs_mount_flags(
		const struct container_config *c);

/* runfs - Path to where the container filesystem has been mounted. */
int container_config_premounted_runfs(struct container_config *c, const char *runfs);

/* Get the pre-mounted runfs path. */
const char *container_config_get_premounted_runfs(const struct container_config *c);

/* The pid of the program will be written here. */
int container_config_pid_file(struct container_config *c, const char *path);

/* Get the pid file path. */
const char *container_config_get_pid_file(const struct container_config *c);

/* The program to run and args, e.g. "/sbin/init", "--second-stage". */
int container_config_program_argv(struct container_config *c,
				  char **argv, size_t num_args);

/* Get the number of command line args for the program to be run. */
size_t container_config_get_num_program_args(const struct container_config *c);

/* Get the program argument at the given index. */
const char *container_config_get_program_arg(const struct container_config *c,
					     size_t index);

/* Sets/Gets the uid the container will run as. */
void container_config_uid(struct container_config *c, uid_t uid);
uid_t container_config_get_uid(const struct container_config *c);

/* Mapping of UIDs in the container, e.g. "0 100000 1024" */
int container_config_uid_map(struct container_config *c, const char *uid_map);

/* Sets/Gets the gid the container will run as. */
void container_config_gid(struct container_config *c, gid_t gid);
gid_t container_config_get_gid(const struct container_config *c);

/* Mapping of GIDs in the container, e.g. "0 100000 1024" */
int container_config_gid_map(struct container_config *c, const char *gid_map);

/* Alt-Syscall table to use or NULL if none. */
int container_config_alt_syscall_table(struct container_config *c,
				       const char *alt_syscall_table);

/*
 * Add a filesystem to mount in the new VFS namespace.
 *
 * c - The container config in which to add the mount.
 * source - Mount source, e.g. "tmpfs" or "/data".
 * destination - Mount point in the container, e.g. "/dev".
 * type - Mount type, e.g. "tmpfs", "selinuxfs", or "devpts".
 * data - Mount data for extra options, e.g. "newinstance" or "ptmxmode=0000".
 * flags - Mount flags as defined in mount(2);
 * uid - uid to chown mount point to if created.
 * gid - gid to chown mount point to if created.
 * mode - Permissions of mount point if created.
 * mount_in_ns - True if mount should happen in the process' vfs namespace.
 * create - If true, create mount destination if it doesn't exist.
 */
int container_config_add_mount(struct container_config *c,
			       const char *name,
			       const char *source,
			       const char *destination,
			       const char *type,
			       const char *data,
			       int flags,
			       int uid,
			       int gid,
			       int mode,
			       int mount_in_ns,
			       int create);

/*
 * Add a device node to create.
 *
 * c - The container config in which to add the mount.
 * type - 'c' or 'b' for char or block respectively.
 * path - Where to mknod, "/dev/zero".
 * fs_permissions - Permissions to set on the node.
 * major - Major device number.
 * minor - Minor device number.
 * copy_minor - Overwrite minor with the minor of the existing device node.  If
 *   this is true minor will be copied from an existing node.  The |minor| param
 *   should be set to -1 in this case.
 * uid - User to own the device.
 * gid - Group to own the device.
 * read_allowed - If true allow reading from the device via "devices" cgroup.
 * write_allowed - If true allow writing to the device via "devices" cgroup.
 * modify_allowed - If true allow creation of the device via "devices" cgroup.
 */
int container_config_add_device(struct container_config *c,
				char type,
				const char *path,
				int fs_permissions,
				int major,
				int minor,
				int copy_minor,
				int uid,
				int gid,
				int read_allowed,
				int write_allowed,
				int modify_allowed);

/*
 * Set to cause the given setfiles command to be run whenever a mount is made
 * in the parent mount namespace.
 */
int container_config_run_setfiles(struct container_config *c,
				  const char *setfiles_cmd);

/* Get the setfiles command that is configured to be run. */
const char *container_config_get_run_setfiles(const struct container_config *c);

/* Set the CPU shares cgroup param for container. */
int container_config_set_cpu_shares(struct container_config *c, int shares);

/* Set the CFS CPU cgroup params for container. */
int container_config_set_cpu_cfs_params(struct container_config *c,
					int quota,
					int period);

/* Set the RT CPU cgroup params for container. */
int container_config_set_cpu_rt_params(struct container_config *c,
				       int rt_runtime,
				       int rt_period);

int container_config_get_cpu_shares(struct container_config *c);
int container_config_get_cpu_quota(struct container_config *c);
int container_config_get_cpu_period(struct container_config *c);
int container_config_get_cpu_rt_runtime(struct container_config *c);
int container_config_get_cpu_rt_period(struct container_config *c);

/*
 * Configure the owner of cgroups created for the container.
 *
 * This is needed so the container's cgroup namespace rootdir is accessible
 * inside the container.
 *
 * cgroup_parent - Parent directory under which to create the cgroup.
 * cgroup_owner - The uid that should own the cgroups that are created.
 */
int container_config_set_cgroup_parent(struct container_config *c,
				       const char *parent,
				       uid_t cgroup_owner);

/* Get the parent cgroup directory from the config.  Here for UT only. */
const char *container_config_get_cgroup_parent(struct container_config *c);

/* Enable sharing of the host's network namespace with the container */
void container_config_share_host_netns(struct container_config *c);
int get_container_config_share_host_netns(struct container_config *c);

/* Container manipulation. */
struct container;

/*
 * Create a container based on the given config.
 *
 * name - Name of the directory holding the container config files.
 * rundir - Where to build the temporary rootfs.
 */
struct container *container_new(const char *name,
				const char *rundir);

/* Destroy a container created with container_new. */
void container_destroy(struct container *c);

/* Start the container. Returns 0 on success.
 * c - The container to run.
 * config - Details of how the container should be run.
 */
int container_start(struct container *c,
		    const struct container_config *config);

/* Get the path to the root of the container. */
const char *container_root(struct container *c);

/* Get the pid of the init process in the container. */
int container_pid(struct container *c);

/* Wait for the container to exit. Returns 0 on success. */
int container_wait(struct container *c);

/* Kill the container's init process, then wait for it to exit. */
int container_kill(struct container *c);

#ifdef __cplusplus
}; /* extern "C" */
#endif

#endif  /* LIBCONTAINER_LIBCONTAINER_H_ */
