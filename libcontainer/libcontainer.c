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

/*
 * Structure that configures how the container is run.
 *
 * rootfs - Path to the root of the container's filesystem.
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
 */
struct container_config {
	char *rootfs;
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
	c->cgroup = container_cgroup_new(name, "/sys/fs/cgroup");
	c->rundir = strdup(rundir);
	c->name = strdup(name);
	if (!c->cgroup || !c->rundir || !c->name) {
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
		rc = minijail_mount(c->jail, source, mnt->destination,
				    mnt->type, mnt->flags);
		if (rc)
			goto error_free_return;
	} else {
		/* Mount this externally and unmount it on exit. */
		if (mount(source, dest, mnt->type, mnt->flags,
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

int container_start(struct container *c, const struct container_config *config)
{
	static const mode_t root_dir_mode = 0660;
	int rc = 0;
	unsigned int i;
	const char *rootfs = config->rootfs;
	char *runfs_template = NULL;

	if (!c)
		return -EINVAL;
	if (!config)
		return -EINVAL;
	if (!config->program_argv || !config->program_argv[0])
		return -EINVAL;

	if (asprintf(&runfs_template, "%s/%s_XXXXXX", c->rundir, c->name) < 0)
		return -errno;

	c->runfs = mkdtemp(runfs_template);
	if (!c->runfs) {
		free(runfs_template);
		return -errno;
	}
	/* Make sure the container uid can access the rootfs. */
	if (chmod(c->runfs, 0700))
		goto error_rmdir;
	if (chown(c->runfs, config->uid, config->gid))
		goto error_rmdir;

	if (asprintf(&c->runfsroot, "%s/root", c->runfs) < 0)
		goto error_rmdir;

	if (mkdir(c->runfsroot, root_dir_mode))
		goto error_rmdir;
	if (chmod(c->runfsroot, root_dir_mode))
		goto error_rmdir;

	if (mount(rootfs, c->runfsroot, "", MS_BIND | MS_RDONLY, NULL))
		goto error_rmdir;

	c->jail = minijail_new();
	if (!c->jail)
		goto error_rmdir;

	rc = do_container_mounts(c, config);
	if (rc)
		goto error_rmdir;

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

	/* Potentailly run setfiles on mounts configured outside of the jail */
	for (i = 0; i < config->num_mounts; i++) {
		const struct container_mount *mnt = &config->mounts[i];
		char *dest;

		if (mnt->mount_in_ns)
			continue;
		if (asprintf(&dest, "%s%s", c->runfsroot, mnt->destination) < 0)
			goto error_rmdir;
		rc = run_setfiles_command(c, config, dest);
		free(dest);
		if (rc)
			goto error_rmdir;
	}

	/* Setup and start the container with libminijail. */
	if (asprintf(&c->pid_file_path, "%s/container.pid", c->runfs) < 0)
		goto error_rmdir;
	minijail_write_pid_file(c->jail, c->pid_file_path);
	minijail_reset_signal_mask(c->jail);

	/* Setup container namespaces. */
	minijail_namespace_ipc(c->jail);
	minijail_namespace_vfs(c->jail);
	minijail_namespace_net(c->jail);
	minijail_namespace_pids(c->jail);
	minijail_namespace_user(c->jail);
	rc = minijail_uidmap(c->jail, config->uid_map);
	if (rc)
		goto error_rmdir;
	rc = minijail_gidmap(c->jail, config->gid_map);
	if (rc)
		goto error_rmdir;

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
	if (c->runfsroot) {
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
