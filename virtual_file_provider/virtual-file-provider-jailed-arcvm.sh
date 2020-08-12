#!/bin/sh
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Run virtual_file_provider with minijail0 for ARCVM.
# The mount path is set to /run/arcvm/media/virtual_files in the
# concierge namespace so that files created can be shared with
# ARCVM. The concierge namespace is created in the pre-start script of
# vm_concierge.conf.

set -e

# Create MOUNT_PATH in concierge namespace.
MOUNT_PATH="/run/arcvm/media/virtual_files"
nsenter --mount=/run/namespaces/mnt_concierge --no-fork \
  -- mkdir -p "${MOUNT_PATH}"

MOUNT_FLAGS="MS_NOSUID|MS_NODEV|MS_NOEXEC"

# Start constructing minijail0 args...
args=""

# Use minimalistic-mountns profile.
args="${args} --profile=minimalistic-mountns"

# Enter a new network namespace.
args="${args} -e"

# Enter a new PID namespace and run the process as init (pid=1).
args="${args} -p -I"

# Enter a new IPC namespace.
args="${args} -l"

# Forbid all caps except CAP_SYS_ADMIN and CAP_SETPCAP.
args="${args} -c 0x200100"

# Run as virtual-file-provider user/group.
args="${args} -u virtual-file-provider -g virtual-file-provider -G"

# Mount tmpfs on /run.
args="${args} -k tmpfs,/run,tmpfs,${MOUNT_FLAGS}"

# For D-Bus system bus socket.
args="${args} -b /run/dbus"

# Bind /dev/fuse to mount FUSE file systems.
args="${args} -b /dev/fuse"

# Bind ${MOUNT_PATH} into this namespace.
args="${args} -k ${MOUNT_PATH},${MOUNT_PATH},none,MS_BIND|MS_REC"

# Finally, specify command line arguments.
args="${args} -- /usr/bin/virtual-file-provider ${MOUNT_PATH}"

# Start virtual-file-provider in concierge namespace.
exec nsenter --mount=/run/namespaces/mnt_concierge --no-fork \
  -- minijail0 ${args}
