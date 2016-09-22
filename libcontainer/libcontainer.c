/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define _GNU_SOURCE /* For asprintf */

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "container_cgroup.h"
#include "libcontainer.h"
#include "libminijail.h"

#define FREE_AND_NULL(ptr) \
do { \
	free(ptr); \
	ptr = NULL; \
} while(0)

static int container_teardown(struct container *c);

static int strdup_and_free(char **dest, const char *src)
{
	char *copy = strdup(src);
	if (!copy)
		return -ENOMEM;
	if (*dest)
		free(*dest);
	*dest = copy;
	return 0;
}

struct container_mount {
	char *name;
	char *source;
	char *destination;
	char *type;
	char *data;
	int flags;
	int uid;
	int gid;
	int mode;
	int mount_in_ns;  /* True if mount should happen in new vfs ns */
	int create;  /* True if target should be created if it doesn't exist */
};

struct container_device {
	char type;  /* 'c' or 'b' for char or block */
	char *path;
	int fs_permissions;
	int major;
	int minor;
	int copy_minor; /* Copy the minor from existing node, ignores |minor| */
	int uid;
	int gid;
	int read_allowed;
	int write_allowed;
	int modify_allowed;
};

struct container_cpu_cgroup {
	int shares;
	int quota;
	int period;
	int rt_runtime;
	int rt_period;
};

/*
 * Structure that configures how the container is run.
 *
 * rootfs - Path to the root of the container's filesystem.
 * rootfs_mount_flags - Flags that will be passed to mount() for the rootfs.
 * premounted_runfs - Path to where the container will be run.
 * pid_file_path - Path to the file where the pid should be written.
 * program_argv - The program to run and args, e.g. "/sbin/init".
 * num_args - Number of args in program_argv.
 * uid - The uid the container will run as.
 * uid_map - Mapping of UIDs in the container, e.g. "0 100000 1024"
 * gid - The gid the container will run as.
 * gid_map - Mapping of GIDs in the container, e.g. "0 100000 1024"
 * alt_syscall_table - Syscall table to use or NULL if none.
 * mounts - Filesystems to mount in the new namespace.
 * num_mounts - Number of above.
 * devices - Device nodes to create.
 * num_devices - Number of above.
 * run_setfiles - Should run setfiles on mounts to enable selinux.
 * cpu_cgparams - CPU cgroup params.
 * cgroup_parent - Parent dir for cgroup creation
 * cgroup_owner - uid to own the created cgroups
 * share_host_netns - Enable sharing of the host network namespace.
 */
struct container_config {
	char *rootfs;
	unsigned long rootfs_mount_flags;
	char *premounted_runfs;
	char *pid_file_path;
	char **program_argv;
	size_t num_args;
	uid_t uid;
	char *uid_map;
	gid_t gid;
	char *gid_map;
	char *alt_syscall_table;
	struct container_mount *mounts;
	size_t num_mounts;
	struct container_device *devices;
	size_t num_devices;
	char *run_setfiles;
	struct container_cpu_cgroup cpu_cgparams;
	char *cgroup_parent;
	uid_t cgroup_owner;
	int share_host_netns;
};

struct container_config *container_config_create()
{
	return calloc(1, sizeof(struct container_config));
}

static void container_free_program_args(struct container_config *c)
{
	int i;

	if (!c->program_argv)
		return;
	for (i = 0; i < c->num_args; ++i) {
		FREE_AND_NULL(c->program_argv[i]);
	}
	FREE_AND_NULL(c->program_argv);
}

static void container_config_free_mount(struct container_mount *mount)
{
	FREE_AND_NULL(mount->name);
	FREE_AND_NULL(mount->source);
	FREE_AND_NULL(mount->destination);
	FREE_AND_NULL(mount->type);
	FREE_AND_NULL(mount->data);
}

static void container_config_free_device(struct container_device *device)
{
	FREE_AND_NULL(device->path);
}

void container_config_destroy(struct container_config *c)
{
	size_t i;

	if (c == NULL)
		return;
	FREE_AND_NULL(c->rootfs);
	container_free_program_args(c);
	FREE_AND_NULL(c->premounted_runfs);
	FREE_AND_NULL(c->pid_file_path);
	FREE_AND_NULL(c->uid_map);
	FREE_AND_NULL(c->gid_map);
	FREE_AND_NULL(c->alt_syscall_table);
	for (i = 0; i < c->num_mounts; ++i) {
		container_config_free_mount(&c->mounts[i]);
	}
	FREE_AND_NULL(c->mounts);
	for (i = 0; i < c->num_devices; ++i) {
		container_config_free_device(&c->devices[i]);
	}
	FREE_AND_NULL(c->devices);
	FREE_AND_NULL(c->run_setfiles);
	FREE_AND_NULL(c->cgroup_parent);
	FREE_AND_NULL(c);
}

int container_config_rootfs(struct container_config *c, const char *rootfs)
{
	return strdup_and_free(&c->rootfs, rootfs);
}

const char *container_config_get_rootfs(const struct container_config *c)
{
	return c->rootfs;
}

void container_config_rootfs_mount_flags(struct container_config *c,
					 unsigned long rootfs_mount_flags)
{
	/* Since we are going to add MS_REMOUNT anyways, add it here so we can
	 * simply check against zero later. MS_BIND is also added to avoid
	 * re-mounting the original filesystem, since the rootfs is always
	 * bind-mounted.
	 */
	c->rootfs_mount_flags = MS_REMOUNT | MS_BIND | rootfs_mount_flags;
}

unsigned long container_config_get_rootfs_mount_flags(
		const struct container_config *c)
{
	return c->rootfs_mount_flags;
}

int container_config_premounted_runfs(struct container_config *c, const char *runfs)
{
	return strdup_and_free(&c->premounted_runfs, runfs);
}

const char *container_config_get_premounted_runfs(const struct container_config *c)
{
	return c->premounted_runfs;
}

int container_config_pid_file(struct container_config *c, const char *path)
{
	return strdup_and_free(&c->pid_file_path, path);
}

const char *container_config_get_pid_file(const struct container_config *c)
{
	return c->pid_file_path;
}

int container_config_program_argv(struct container_config *c,
				  char **argv, size_t num_args)
{
	size_t i;

	container_free_program_args(c);
	c->num_args = num_args;
	c->program_argv = calloc(num_args + 1, sizeof(char *));
	if (!c->program_argv)
		return -ENOMEM;
	for (i = 0; i < num_args; ++i) {
		if (strdup_and_free(&c->program_argv[i], argv[i]))
			goto error_free_return;
	}
	c->program_argv[num_args] = NULL;
	return 0;

error_free_return:
	container_free_program_args(c);
	return -ENOMEM;
}

size_t container_config_get_num_program_args(const struct container_config *c)
{
	return c->num_args;
}

const char *container_config_get_program_arg(const struct container_config *c,
					     size_t index)
{
	if (index >= c->num_args)
		return NULL;
	return c->program_argv[index];
}

void container_config_uid(struct container_config *c, uid_t uid)
{
	c->uid = uid;
}

uid_t container_config_get_uid(const struct container_config *c)
{
	return c->uid;
}

int container_config_uid_map(struct container_config *c, const char *uid_map)
{
	return strdup_and_free(&c->uid_map, uid_map);
}

void container_config_gid(struct container_config *c, gid_t gid)
{
	c->gid = gid;
}

gid_t container_config_get_gid(const struct container_config *c)
{
	return c->gid;
}

int container_config_gid_map(struct container_config *c, const char *gid_map)
{
	return strdup_and_free(&c->gid_map, gid_map);
}

int container_config_alt_syscall_table(struct container_config *c,
				       const char *alt_syscall_table)
{
	return strdup_and_free(&c->alt_syscall_table, alt_syscall_table);
}

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
			       int create)
{
	struct container_mount *mount_ptr;
	struct container_mount *current_mount;

	if (name == NULL || source == NULL ||
	    destination == NULL || type == NULL)
		return -EINVAL;

	mount_ptr = realloc(c->mounts,
			    sizeof(c->mounts[0]) * (c->num_mounts + 1));
	if (!mount_ptr)
		return -ENOMEM;
	c->mounts = mount_ptr;
	current_mount = &c->mounts[c->num_mounts];
	memset(current_mount, 0, sizeof(struct container_mount));

	if (strdup_and_free(&current_mount->name, name))
		goto error_free_return;
	if (strdup_and_free(&current_mount->source, source))
		goto error_free_return;
	if (strdup_and_free(&current_mount->destination, destination))
		goto error_free_return;
	if (strdup_and_free(&current_mount->type, type))
		goto error_free_return;
	if (data && strdup_and_free(&current_mount->data, data))
		goto error_free_return;
	current_mount->flags = flags;
	current_mount->uid = uid;
	current_mount->gid = gid;
	current_mount->mode = mode;
	current_mount->mount_in_ns = mount_in_ns;
	current_mount->create = create;
	++c->num_mounts;
	return 0;

error_free_return:
	container_config_free_mount(current_mount);
	return -ENOMEM;
}

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
				int modify_allowed)
{
	struct container_device *dev_ptr;
	struct container_device *current_dev;

	if (path == NULL)
		return -EINVAL;
	/* If using a dynamic minor number, ensure that minor is -1. */
	if (copy_minor && (minor != -1))
		return -EINVAL;

	dev_ptr = realloc(c->devices,
			  sizeof(c->devices[0]) * (c->num_devices + 1));
	if (!dev_ptr)
		return -ENOMEM;
	c->devices = dev_ptr;
	current_dev = &c->devices[c->num_devices];
	memset(current_dev, 0, sizeof(struct container_device));

	current_dev->type = type;
	if (strdup_and_free(&current_dev->path, path))
		goto error_free_return;
	current_dev->fs_permissions = fs_permissions;
	current_dev->major = major;
	current_dev->minor = minor;
	current_dev->copy_minor = copy_minor;
	current_dev->uid = uid;
	current_dev->gid = gid;
	current_dev->read_allowed = read_allowed;
	current_dev->write_allowed = write_allowed;
	current_dev->modify_allowed = modify_allowed;
	++c->num_devices;
	return 0;

error_free_return:
	container_config_free_device(current_dev);
	return -ENOMEM;
}

int container_config_run_setfiles(struct container_config *c,
				   const char *setfiles_cmd)
{
	return strdup_and_free(&c->run_setfiles, setfiles_cmd);
}

const char *container_config_get_run_setfiles(const struct container_config *c)
{
	return c->run_setfiles;
}

int container_config_set_cpu_shares(struct container_config *c, int shares)
{
	/* CPU shares must be 2 or higher. */
	if (shares < 2)
		return -EINVAL;

	c->cpu_cgparams.shares = shares;
	return 0;
}

int container_config_set_cpu_cfs_params(struct container_config *c,
					int quota,
					int period)
{
	/*
	 * quota could be set higher than period to utilize more than one CPU.
	 * quota could also be set as -1 to indicate the cgroup does not adhere
	 * to any CPU time restrictions.
	 */
	if (quota <= 0 && quota != -1)
		return -EINVAL;
	if (period <= 0)
		return -EINVAL;

	c->cpu_cgparams.quota = quota;
	c->cpu_cgparams.period = period;
	return 0;
}

int container_config_set_cpu_rt_params(struct container_config *c,
				       int rt_runtime,
				       int rt_period)
{
	/*
	 * rt_runtime could be set as 0 to prevent the cgroup from using
	 * realtime CPU.
	 */
	if (rt_runtime < 0 || rt_runtime >= rt_period)
		return -EINVAL;

	c->cpu_cgparams.rt_runtime = rt_runtime;
	c->cpu_cgparams.rt_period = rt_period;
	return 0;
}

int container_config_get_cpu_shares(struct container_config *c)
{
	return c->cpu_cgparams.shares;
}

int container_config_get_cpu_quota(struct container_config *c)
{
	return c->cpu_cgparams.quota;
}

int container_config_get_cpu_period(struct container_config *c)
{
	return c->cpu_cgparams.period;
}

int container_config_get_cpu_rt_runtime(struct container_config *c)
{
	return c->cpu_cgparams.rt_runtime;
}

int container_config_get_cpu_rt_period(struct container_config *c)
{
	return c->cpu_cgparams.rt_period;
}

int container_config_set_cgroup_parent(struct container_config *c,
				       const char *parent,
				       uid_t cgroup_owner)
{
	c->cgroup_owner = cgroup_owner;
	return strdup_and_free(&c->cgroup_parent, parent);
}

const char *container_config_get_cgroup_parent(struct container_config *c)
{
	return c->cgroup_parent;
}

void container_config_share_host_netns(struct container_config *c)
{
	c->share_host_netns = 1;
}

int get_container_config_share_host_netns(struct container_config *c)
{
	return c->share_host_netns;
}

/*
 * Container manipulation
 */
struct container {
	struct container_cgroup *cgroup;
	struct minijail *jail;
	pid_t init_pid;
	char *runfs;
	char *rundir;
	char *runfsroot;
	char *pid_file_path;
	char **ext_mounts; /* Mounts made outside of the minijail */
	size_t num_ext_mounts;
	char *name;
};

struct container *container_new(const char *name,
				const char *rundir)
{
	struct container *c;

	c = calloc(1, sizeof(*c));
	if (!c)
		return NULL;
	c->rundir = strdup(rundir);
	c->name = strdup(name);
	if (!c->rundir || !c->name) {
		container_destroy(c);
		return NULL;
	}
	return c;
}

void container_destroy(struct container *c)
{
	if (c->cgroup)
		container_cgroup_destroy(c->cgroup);
	if (c->jail)
		minijail_destroy(c->jail);
	FREE_AND_NULL(c->name);
	FREE_AND_NULL(c->rundir);
	FREE_AND_NULL(c);
}

static int make_dir(const char *path, int uid, int gid, int mode)
{
	if (mkdir(path, mode))
		return -errno;
	if (chmod(path, mode))
		return -errno;
	if (chown(path, uid, gid))
		return -errno;
	return 0;
}

static int touch_file(const char *path, int uid, int gid, int mode)
{
	int rc;
	int fd = open(path, O_RDWR | O_CREAT, mode);
	if (fd < 0)
		return -errno;
	rc = fchown(fd, uid, gid);
	close(fd);

	if (rc)
		return -errno;
	return 0;
}

/* Make sure the mount target exists in the new rootfs. Create if needed and
 * possible.
 */
static int setup_mount_destination(const struct container_mount *mnt,
				   const char *source,
				   const char *dest)
{
	int rc;
	struct stat st_buf;

	rc = stat(dest, &st_buf);
	if (rc == 0) /* destination exists */
		return 0;

	/* Try to create the destination. Either make directory or touch a file
	 * depending on the source type.
	 */
	rc = stat(source, &st_buf);
	if (rc || S_ISDIR(st_buf.st_mode) || S_ISBLK(st_buf.st_mode))
		return make_dir(dest, mnt->uid, mnt->gid, mnt->mode);

	return touch_file(dest, mnt->uid, mnt->gid, mnt->mode);
}

/* Fork and exec the setfiles command to configure the selinux policy. */
static int run_setfiles_command(const struct container *c,
				const struct container_config *config,
				const char *dest)
{
	int rc;
	int status;
	int pid;
	char *context_path;

	if (!config->run_setfiles)
		return 0;

	/* Really gross hack to avoid setfiles on /data, this should be removed
	 * when data isn't under /home/chronos/user where we can't access it as
	 * the android user.
	 * TODO(b/28705740) - Fix permission to the data directory.
	 */
	if (strlen(dest) >= 5 && !strcmp(&dest[strlen(dest) - 5], "/data"))
		return 0;

	if (asprintf(&context_path, "%s/file_contexts",
		     c->runfsroot) < 0)
		return -errno;

	pid = fork();
	if (pid == 0) {
		const char *argv[] = {
			config->run_setfiles,
			"-r",
			c->runfsroot,
			context_path,
			dest,
			NULL,
		};
		const char *env[] = {
			NULL,
		};

		execve(argv[0], (char *const*)argv, (char *const*)env);

		/* Command failed to exec if execve returns. */
		_exit(-errno);
	}
	free(context_path);
	if (pid < 0)
		return -errno;
	do {
		rc = waitpid(pid, &status, 0);
	} while (rc == -1 && errno == EINTR);
	if (rc < 0)
		return -errno;
	return status;
}

/*
 * Unmounts anything we mounted in this mount namespace in the opposite order
 * that they were mounted.
 */
static int unmount_external_mounts(struct container *c)
{
	int ret = 0;

	while (c->num_ext_mounts) {
		c->num_ext_mounts--;
		if (!c->ext_mounts[c->num_ext_mounts])
			continue;
		if (umount(c->ext_mounts[c->num_ext_mounts]))
			ret = -errno;
		FREE_AND_NULL(c->ext_mounts[c->num_ext_mounts]);
	}
	FREE_AND_NULL(c->ext_mounts);
	return ret;
}

/*
 * Match mount_one in minijail, mount one mountpoint with
 * consideration for combination of MS_BIND/MS_RDONLY flag.
 */
static int mount_external(const char *src, const char *dest, const char *type,
			  unsigned long flags, const void *data)
{
	int remount_ro = 0;

	/*
	 * R/O bind mounts have to be remounted since 'bind' and 'ro'
	 * can't both be specified in the original bind mount.
	 * Remount R/O after the initial mount.
	 */
	if ((flags & MS_BIND) && (flags & MS_RDONLY)) {
		remount_ro = 1;
		flags &= ~MS_RDONLY;
	}

	if (mount(src, dest, type, flags, data) == -1)
		return -1;

	if (remount_ro) {
		flags |= MS_RDONLY;
		if (mount(src, dest, NULL, flags | MS_REMOUNT, data) == -1)
			return -1;
	}

	return 0;
}

static int do_container_mount(struct container *c,
			      const struct container_mount *mnt)
{
	char *source = NULL;
	char *dest = NULL;
	int rc = 0;

	if (asprintf(&dest, "%s%s", c->runfsroot, mnt->destination) < 0)
		return -errno;

	/*
	 * If it's a bind mount relative to rootfs, append source to
	 * rootfs path, otherwise source path is absolute.
	 */
	if ((mnt->flags & MS_BIND) && mnt->source[0] != '/') {
		if (asprintf(&source, "%s/%s", c->runfsroot, mnt->source) < 0)
			goto error_free_return;
	} else {
		if (asprintf(&source, "%s", mnt->source) < 0)
			goto error_free_return;
	}

	if (mnt->create) {
		rc = setup_mount_destination(mnt, source, dest);
		if (rc)
			goto error_free_return;
	}
	if (mnt->mount_in_ns) {
		/* We can mount this with minijail. */
		rc = minijail_mount_with_data(c->jail, source, mnt->destination,
					      mnt->type, mnt->flags, mnt->data);
		if (rc)
			goto error_free_return;
	} else {
		/* Mount this externally and unmount it on exit. */
		if (mount_external(source, dest, mnt->type, mnt->flags,
				   mnt->data))
			goto error_free_return;
		/* Save this to unmount when shutting down. */
		rc = strdup_and_free(&c->ext_mounts[c->num_ext_mounts], dest);
		if (rc)
			goto error_free_return;
		c->num_ext_mounts++;
	}

	goto exit;

error_free_return:
	if (!rc)
		rc = -errno;
exit:
	free(source);
	free(dest);
	return rc;
}

static int do_container_mounts(struct container *c,
			       const struct container_config *config)
{
	unsigned int i;
	int rc = 0;

	unmount_external_mounts(c);
	/*
	 * Allocate space to track anything we mount in our mount namespace.
	 * This over-allocates as it has space for all mounts.
	 */
	c->ext_mounts = calloc(config->num_mounts, sizeof(*c->ext_mounts));
	if (!c->ext_mounts)
		return -errno;

	for (i = 0; i < config->num_mounts; ++i) {
		rc = do_container_mount(c, &config->mounts[i]);
		if (rc)
			goto error_free_return;
	}

	return 0;

error_free_return:
	unmount_external_mounts(c);
	return rc;
}

static int container_create_device(const struct container *c,
				   const struct container_device *dev,
				   int minor)
{
	char *path = NULL;
	int rc = 0;
	int mode;

	switch (dev->type) {
	case  'b':
		mode = S_IFBLK;
		break;
	case 'c':
		mode = S_IFCHR;
		break;
	default:
		return -EINVAL;
	}
	mode |= dev->fs_permissions;

	if (asprintf(&path, "%s%s", c->runfsroot, dev->path) < 0)
		goto error_free_return;
	if (mknod(path, mode, makedev(dev->major, minor)) && errno != EEXIST)
		goto error_free_return;
	if (chown(path, dev->uid, dev->gid))
		goto error_free_return;
	if (chmod(path, dev->fs_permissions))
		goto error_free_return;

	goto exit;

error_free_return:
	rc = -errno;
exit:
	free(path);
	return rc;
}

static int mount_runfs(struct container *c, const struct container_config *config)
{
	static const mode_t root_dir_mode = 0660;
	const char *rootfs = config->rootfs;
	char *runfs_template = NULL;

	if (asprintf(&runfs_template, "%s/%s_XXXXXX", c->rundir, c->name) < 0)
		return -ENOMEM;

	c->runfs = mkdtemp(runfs_template);
	if (!c->runfs) {
		free(runfs_template);
		return -errno;
	}

	/* Make sure the container uid can access the rootfs. */
	if (chmod(c->runfs, 0700))
		return -errno;
	if (chown(c->runfs, config->uid, config->gid))
		return -errno;

	if (asprintf(&c->runfsroot, "%s/root", c->runfs) < 0)
		return -errno;

	if (mkdir(c->runfsroot, root_dir_mode))
		return -errno;
	if (chmod(c->runfsroot, root_dir_mode))
		return -errno;

	if (mount(rootfs, c->runfsroot, "", MS_BIND, NULL))
		return -errno;

	/* MS_BIND ignores any flags passed to it (except MS_REC). We need a
	 * second call to mount() to actually set them.
	 */
	if (config->rootfs_mount_flags &&
	    mount(rootfs, c->runfsroot, "",
		  config->rootfs_mount_flags, NULL)) {
		return -errno;
	}

	return 0;
}

static int get_userns_id(const char *map, int id)
{
	char *map_copy, *mapping, *saveptr1, *saveptr2;
	int inside, outside, length;
	int result = 0;
	errno = 0;

	if (asprintf(&map_copy, "%s", map) < 0)
		return -ENOMEM;

	mapping = strtok_r(map_copy, ",", &saveptr1);
	while (mapping) {
		inside = strtol(strtok_r(mapping, " ", &saveptr2), NULL, 10);
		outside = strtol(strtok_r(NULL, " ", &saveptr2), NULL, 10);
		length = strtol(strtok_r(NULL, "\0", &saveptr2), NULL, 10);
		if (errno) {
			goto error_free_return;
		} else if (inside < 0 || outside < 0 || length < 0) {
			errno = EINVAL;
			goto error_free_return;
		}

		if (id >= outside && id <= (outside + length)) {
			result = id - (outside - inside);
			goto exit;
		}

		mapping = strtok_r(NULL, ",", &saveptr1);
	}
	errno = EINVAL;

error_free_return:
	result = -errno;
exit:
	free(map_copy);
	return result;
}

int container_start(struct container *c, const struct container_config *config)
{
	int rc = 0;
	unsigned int i;
	int uid_userns, gid_userns;

	if (!c)
		return -EINVAL;
	if (!config)
		return -EINVAL;
	if (!config->program_argv || !config->program_argv[0])
		return -EINVAL;

	if (config->premounted_runfs) {
		c->runfs = NULL;
		c->runfsroot = strdup(config->premounted_runfs);
		if (!c->runfsroot) {
			rc = -ENOMEM;
			goto error_rmdir;
		}
	} else {
		rc = mount_runfs(c, config);
		if (rc)
			goto error_rmdir;
	}

	c->jail = minijail_new();
	if (!c->jail)
		goto error_rmdir;

	rc = do_container_mounts(c, config);
	if (rc)
		goto error_rmdir;

	c->cgroup = container_cgroup_new(c->name,
					 "/sys/fs/cgroup",
					 config->cgroup_parent,
					 config->cgroup_owner);
	if (!c->cgroup)
		goto error_rmdir;

	/* Must be root to modify device cgroup or mknod */
	if (getuid() == 0) {
		c->cgroup->ops->deny_all_devices(c->cgroup);

		for (i = 0; i < config->num_devices; i++) {
			const struct container_device *dev = &config->devices[i];
			int minor = dev->minor;

			if (dev->copy_minor) {
				struct stat st_buff;
				if (stat(dev->path, &st_buff) < 0)
					continue;
				/* Use the minor macro to extract the device number. */
				minor = minor(st_buff.st_rdev);
			}
			if (minor >= 0) {
				rc = container_create_device(c, dev, minor);
				if (rc)
					goto error_rmdir;
			}

			rc = c->cgroup->ops->add_device(c->cgroup, dev->major,
							minor, dev->read_allowed,
							dev->write_allowed,
							dev->modify_allowed, dev->type);
			if (rc)
				goto error_rmdir;
		}
	}

	/* Potentailly run setfiles on mounts configured outside of the jail */
	for (i = 0; i < config->num_mounts; i++) {
		const struct container_mount *mnt = &config->mounts[i];
		char *dest;

		if (mnt->mount_in_ns)
			continue;
		if (mnt->flags & MS_RDONLY)
			continue;
		if (asprintf(&dest, "%s%s", c->runfsroot, mnt->destination) < 0)
			goto error_rmdir;
		rc = run_setfiles_command(c, config, dest);
		free(dest);
		if (rc)
			goto error_rmdir;
	}

	/* Setup CPU cgroup params. */
	if (config->cpu_cgparams.shares) {
		rc = c->cgroup->ops->set_cpu_shares(
				c->cgroup, config->cpu_cgparams.shares);
		if (rc)
			goto error_rmdir;
	}
	if (config->cpu_cgparams.period) {
		rc = c->cgroup->ops->set_cpu_quota(
				c->cgroup, config->cpu_cgparams.quota);
		if (rc)
			goto error_rmdir;
		rc = c->cgroup->ops->set_cpu_period(
				c->cgroup, config->cpu_cgparams.period);
		if (rc)
			goto error_rmdir;
	}
	if (config->cpu_cgparams.rt_period) {
		rc = c->cgroup->ops->set_cpu_rt_runtime(
				c->cgroup, config->cpu_cgparams.rt_runtime);
		if (rc)
			goto error_rmdir;
		rc = c->cgroup->ops->set_cpu_rt_period(
				c->cgroup, config->cpu_cgparams.rt_period);
		if (rc)
			goto error_rmdir;
	}

	/* Setup and start the container with libminijail. */
	if (config->pid_file_path) {
		c->pid_file_path = strdup(config->pid_file_path);
		if (!c->pid_file_path) {
			rc = -ENOMEM;
			goto error_rmdir;
		}
	} else if (c->runfs) {
		if (asprintf(&c->pid_file_path, "%s/container.pid", c->runfs) < 0) {
			rc = -ENOMEM;
			goto error_rmdir;
		}
	}

	if (c->pid_file_path)
		minijail_write_pid_file(c->jail, c->pid_file_path);
	minijail_reset_signal_mask(c->jail);

	/* Setup container namespaces. */
	minijail_namespace_ipc(c->jail);
	minijail_namespace_vfs(c->jail);
	if (!config->share_host_netns)
		minijail_namespace_net(c->jail);
	minijail_namespace_pids(c->jail);
	minijail_namespace_user(c->jail);
	minijail_namespace_cgroups(c->jail);
	rc = minijail_uidmap(c->jail, config->uid_map);
	if (rc)
		goto error_rmdir;
	rc = minijail_gidmap(c->jail, config->gid_map);
	if (rc)
		goto error_rmdir;

	/* Set the UID/GID inside the container if not 0. */
	uid_userns = get_userns_id(config->uid_map, config->uid);
	if (uid_userns < 0)
		goto error_rmdir;
	else if (uid_userns > 0)
		minijail_change_uid(c->jail, (uid_t) uid_userns);
	gid_userns = get_userns_id(config->gid_map, config->gid);
	if (gid_userns < 0)
		goto error_rmdir;
	else if (gid_userns > 0)
		minijail_change_gid(c->jail, (gid_t) gid_userns);

	rc = minijail_enter_pivot_root(c->jail, c->runfsroot);
	if (rc)
		goto error_rmdir;

	/* Add the cgroups configured above. */
	rc = minijail_add_to_cgroup(c->jail, cgroup_cpu_tasks_path(c->cgroup));
	if (rc)
		goto error_rmdir;
	rc = minijail_add_to_cgroup(c->jail,
				    cgroup_cpuacct_tasks_path(c->cgroup));
	if (rc)
		goto error_rmdir;
	rc = minijail_add_to_cgroup(c->jail,
				    cgroup_devices_tasks_path(c->cgroup));
	if (rc)
		goto error_rmdir;
	rc = minijail_add_to_cgroup(c->jail,
				    cgroup_freezer_tasks_path(c->cgroup));
	if (rc)
		goto error_rmdir;

	if (config->alt_syscall_table)
		minijail_use_alt_syscall(c->jail, config->alt_syscall_table);

	minijail_run_as_init(c->jail);

	/* TODO(dgreid) - remove this once shared mounts are cleaned up. */
	minijail_skip_remount_private(c->jail);

	rc = minijail_run_pid_pipes_no_preload(c->jail,
					       config->program_argv[0],
					       config->program_argv,
					       &c->init_pid, NULL, NULL,
					       NULL);
	if (rc)
		goto error_rmdir;
	return 0;

error_rmdir:
	if (!rc)
		rc = -errno;
	container_teardown(c);
	return rc;
}

const char *container_root(struct container *c)
{
	return c->runfs;
}

int container_pid(struct container *c)
{
	return c->init_pid;
}

static int container_teardown(struct container *c)
{
	int ret = 0;

	unmount_external_mounts(c);
	if (c->runfsroot && c->runfs) {
		if (umount(c->runfsroot))
			ret = -errno;
		if (rmdir(c->runfsroot))
			ret = -errno;
		FREE_AND_NULL(c->runfsroot);
	}
	if (c->pid_file_path) {
		if (unlink(c->pid_file_path))
			ret = -errno;
		FREE_AND_NULL(c->pid_file_path);
	}
	if (c->runfs) {
		if (rmdir(c->runfs))
			ret = -errno;
		FREE_AND_NULL(c->runfs);
	}
	return ret;
}

int container_wait(struct container *c)
{
	int rc;

	do {
		rc = minijail_wait(c->jail);
	} while (rc == -EINTR);

	// If the process had already been reaped, still perform teardown.
	if (rc == -ECHILD || rc >= 0) {
		rc = container_teardown(c);
	}
	return rc;
}

int container_kill(struct container *c)
{
	if (kill(c->init_pid, SIGKILL) && errno != ESRCH)
		return -errno;
	return container_wait(c);
}
