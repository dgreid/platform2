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

# Create MOUNT_PATH in the concierge namespace.
MOUNT_PATH="/run/arcvm/media/virtual_files"
nsenter --mount=/run/namespaces/mnt_concierge --no-fork \
  -- mkdir -p "${MOUNT_PATH}"

MOUNT_FLAGS="MS_NOSUID|MS_NODEV|MS_NOEXEC"

# Start virtual-file-provider with MOUNT_PATH as FUSE mount point
# in the concierge namespace.
# nsenter --mount=<namespace> Enter the specified mount namespace.
# --profile=minimalistic-mountns Use minimalistic-mountns profile.
# -e    Enter a new network namespace.
# -p -I Enter a new PID namespace and run the process as init (pid=1).
# -l    Enter a new IPC namespace.
# -c    Forbid all caps except CAP_SYS_ADMIN and CAP_SETPCAP.
# -u/-g Run as virtual-file-provider user/group.
# -b    /run/dbus is for D-Bus system bus socket.
#       /dev/fuse is for mounting FUSE file systems.
# -k    Mount tmpfs on /run.
#       Bind ${MOUNT_PATH} into this namespace.
exec nsenter --mount=/run/namespaces/mnt_concierge --no-fork \
     -- minijail0 \
        --profile=minimalistic-mountns \
        -e \
        -p -I \
        -l \
        -c 0x200100 \
        -u virtual-file-provider -g virtual-file-provider -G \
        -k "tmpfs,/run,tmpfs,${MOUNT_FLAGS}" \
        -b /run/dbus \
        -b /dev/fuse \
        -k "${MOUNT_PATH},${MOUNT_PATH},none,MS_BIND|MS_REC" \
        -- /usr/bin/virtual-file-provider "${MOUNT_PATH}"
