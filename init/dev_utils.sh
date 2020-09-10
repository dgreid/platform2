#!/bin/sh
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Utility functions for chromeos_startup to run in developer mode.

STATEFUL_PARTITION="/mnt/stateful_partition"

# Returns if we are running on a debug build.
dev_is_debug_build() {
  crossystem 'debug_build?1'
}

# Check whether the device is allowed to boot in dev mode.
# 1. If a debug build is already installed on the system, ignore block_devmode.
#    It is pointless in this case, as the device is already in a state where the
#    local user has full control.
# 2. According to recovery mode only boot with signed images, the block_devmode
#    could be ignored here -- otherwise factory shim will be blocked expecially
#    that RMA center can't reset this device.
#
# Usage: dev_check_block_dev_mode DEV_MODE_FILE
dev_check_block_dev_mode() {
  if ! crossystem "devsw_boot?1" "debug_build?0" "recovery_reason?0"; then
    return
  fi

  # The file indicates the system has booted in developer mode and must initiate
  # a wiping process in next (normal mode) boot.
  local dev_mode_file="$1"
  local vpd_block_devmode_file=/sys/firmware/vpd/rw/block_devmode
  local block_devmode=

  # Checks ordered by run time: First try reading VPD through sysfs.
  if [ -f "${vpd_block_devmode_file}" ] &&
     [ "$(cat "${vpd_block_devmode_file}")" = "1" ]; then
    block_devmode=1
  # Second try crossystem.
  elif crossystem "block_devmode?1"; then
    block_devmode=1
  # Third re-read VPD directly from SPI flash (slow!) but only for systems
  # that don't have VPD in sysfs and only when NVRAM indicates that it has
  # been cleared.
  elif [ ! -d /sys/firmware/vpd/rw ] &&
     crossystem "nvram_cleared?1" &&
     [ "$(vpd -i RW_VPD -g block_devmode)" = "1" ]; then
    block_devmode=1
  fi

  if [ -n "${block_devmode}" ]; then
    # Put a flag file into place that will trigger a stateful partition wipe
    # after reboot in verified mode.
    touch "${dev_mode_file}"
    chromeos-boot-alert block_devmode
  fi
}

# Updates stateful partition if pending update is available.
dev_update_stateful_partition() {
  local stateful_update_file="${STATEFUL_PARTITION}/.update_available"
  if [ ! -f "${stateful_update_file}" ]; then
    return
  fi

  # To remain compatible with the prior update_stateful tarballs, expect
  # the "var_new" unpack location, but move it into the new "var_overlay"
  # target location.
  local var_target="${STATEFUL_PARTITION}/var"
  local var_new="${var_target}_new"
  local var_old="${var_target}_old"
  local var_target="${var_target}_overlay"
  local developer_target="${STATEFUL_PARTITION}/dev_image"
  local developer_new="${developer_target}_new"
  local developer_old="${developer_target}_old"
  local stateful_update_args

  stateful_update_args="$(cat "${stateful_update_file}")"

  # Only replace the developer and var_overlay directories if new replacements
  # are available.
  if [ -d "${developer_new}" ] && [ -d "${var_new}" ]; then
    clobber-log -- "Updating from ${developer_new} && ${var_new}."
    rm -rf "${developer_old}" "${var_old}"
    mv "${var_target}" "${var_old}" || true
    mv "${developer_target}" "${developer_old}" || true
    mv "${var_new}" "${var_target}"
    mv "${developer_new}" "${developer_target}"
  else
    clobber-log -- "Stateful update did not find ${developer_new} & ${var_new}."
    clobber-log -- "Keeping old development tools."
  fi

  # Check for clobber.
  if [ "${stateful_update_args}" = "clobber" ]; then
    local preserve_dir="${STATEFUL_PARTITION}/unencrypted/preserve"

    # Find everything in stateful and delete it, except for protected paths, and
    # non-empty directories. The non-empty directories contain protected content
    # or they would already be empty from depth first traversal.

    find "${STATEFUL_PARTITION}" -depth -mindepth 1 \
        -not -path "${STATEFUL_PARTITION}/.labmachine" \
        -not -path "${developer_target}/*" \
        -not -path "${var_target}/*" \
        -not -path "${preserve_dir}/*" \
        -not -type d -print0 | xargs -0 -r rm -f

    find "${STATEFUL_PARTITION}" -depth -mindepth 1 \
        -not -path "${developer_target}/*" \
        -not -path "${var_target}/*" \
        -not -path "${preserve_dir}/*" \
        -type d -print0 | xargs -0 -r rmdir --ignore-fail-on-non-empty

    # Let's really be done before coming back.
    sync
  fi

  # Backgrounded to take off boot path.
  rm -rf "${stateful_update_file}" "${developer_old}" "${var_old}" &
}

# Gather logs.
dev_gather_logs() {
  # For dev/test images, if .gatherme presents, copy files listed in .gatherme
  # to ${STATEFUL_PARTITION}/unencrypted/prior_logs.
  local lab_preserve_logs="${STATEFUL_PARTITION}/.gatherme"
  local prior_log_dir="${STATEFUL_PARTITION}/unencrypted/prior_logs"
  local log_path

  if [ ! -f "${lab_preserve_logs}" ]; then
    return
  fi

  sed -e '/^#/ d' -e '/^$/ d' "${lab_preserve_logs}" | \
      while read -r log_path; do
    if [ -d "${log_path}" ]; then
      cp -a -r --parents "${log_path}" "${prior_log_dir}" || true
    elif [ -f "${log_path}" ]; then
      cp -a "${log_path}" "${prior_log_dir}" || true
    fi
  done
  # shellcheck disable=SC2115
  rm -rf /var/*
  rm -rf /home/chronos/*
  rm "${lab_preserve_logs}"
}

# Keep this list in sync with the var_overlay elements in the DIRLIST
# found in chromeos-install from chromeos-base/chromeos-installer.
MOUNTDIRS="
  db/pkg
  lib/portage
  cache/dlc-images
"

# Mount stateful partition for dev packages.
dev_mount_packages() {
  # Set up the logging dir that ASAN compiled programs will write to.  We want
  # any privileged account to be able to write here so unittests need not worry
  # about settings things up ahead of time.  See crbug.com/453579 for details.
  mkdir -p /var/log/asan
  chmod 1777 /var/log/asan

  # Capture a snapshot of "normal" mount state here, for auditability,
  # before we start applying devmode-specific changes.
  cat /proc/mounts > /var/log/mount_options.log

  # Create dev_image directory in base images in developer mode.
  if [ ! -d "${STATEFUL_PARTITION}/dev_image" ]; then
    mkdir -p -m 0755 "${STATEFUL_PARTITION}/dev_image"
  fi

  # Mount and then remount to enable exec/suid.
  mount_or_fail --bind "${STATEFUL_PARTITION}/dev_image" /usr/local
  mount -n -o remount,exec,suid /usr/local

  if [ ! -e /usr/share/cros/startup/disable_stateful_security_hardening ]; then
    # Add exceptions to allow symlink traversal and opening of FIFOs in the
    # dev_image subtree.
    local d
    for d in "/mnt/stateful_partition/dev_image" "/var/tmp/portage"; do
      mkdir "${d}"
      allow_symlink "${d}"
      allow_fifo "${d}"
    done
  fi

  # Set up /var elements needed by gmerge.
  # TODO(keescook) Use dev/test package installs instead of piling more
  # things here (crosbug.com/14091).
  local base
  base="${STATEFUL_PARTITION}/var_overlay"
  if [ -d "${base}" ]; then
    local dir dest
    echo "${MOUNTDIRS}" | while read -r dir ; do
      if [ -n "${dir}" ]; then
        if [ ! -d "${base}/${dir}" ]; then
          continue
        fi
        dest="/var/${dir}"
        # Previous versions of this script created a symlink instead of setting
        # up a bind mount, so get rid of the symlink if it exists.
        (rm -f "${dest}" && mkdir -p "${dest}") || :
        mount_or_fail --bind "${base}/${dir}" "${dest}"
      fi
    done
  fi
}

# Unmount stateful partition for dev packages.
dev_unmount_packages() {
  # Unmount bind mounts for /var elements needed by gmerge.
  local base="/var"
  if [ -d "${base}" ]; then
    echo "${MOUNTDIRS}" | while read -r dir ; do
      if [ -n "${dir}" ]; then
        if [ ! -d "${base}/${dir}" ]; then
          continue
        fi
        umount -n "${base}/${dir}"
      fi
    done
  fi

  # unmount /usr/local to match dev_mount_package.
  umount -n /usr/local
}

# Load more utilities on test image.
if [ -f /usr/share/cros/test_utils.sh ]; then
  . /usr/share/cros/test_utils.sh
fi
