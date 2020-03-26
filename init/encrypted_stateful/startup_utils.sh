# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Create, possibly migrate from, the unencrypted stateful partition, and bind
# mount the /var and /home/chronos mounts from the encrypted filesystem
# /mnt/stateful_partition/encrypted, all managed by the "mount-encrypted"
# helper. Takes the same arguments as mount-encrypted. Since /var is managed by
# mount-encrypted, it should not be created in the unencrypted stateful
# partition. Its mount point in the root filesystem exists already from the
# rootfs image.  Since /home is still mounted from the unencrypted stateful
# partition, having /home/chronos already doesn't matter. It will be created by
# mount-encrypted if it is missing. These mounts inherit nodev,noexec,nosuid
# from the encrypted filesystem /mnt/stateful_partition/encrypted.
mount_var_and_home_chronos() {
  mount-encrypted "$@" >/run/mount_encrypted/mount-encrypted.log 2>&1
}

# Give mount-encrypted umount 10 times to retry, otherwise
# it will fail with 'device is busy' because lazy umount does not finish
# clearing all reference points yet. Check crosbug.com/p/21345.
umount_var_and_home_chronos() {
  # Check if the encrypted stateful partition is mounted.
  if ! mountpoint -q "/mnt/stateful_partition/encrypted"; then
    return 0
  fi

  local rc=0
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    mount-encrypted umount
    rc="$?"
    if [ "${rc}" -eq "0" ]; then
      break
    fi
    sleep 0.1
  done
  return "${rc}"
}
