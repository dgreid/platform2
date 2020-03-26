# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Bind mount /var and /home/chronos. All function arguments are ignored.
mount_var_and_home_chronos() {
  mkdir -p /mnt/stateful_partition/var || return 1
  mount -n --bind /mnt/stateful_partition/var /var || return 1
  if ! mount -n --bind /mnt/stateful_partition/home/chronos /home/chronos; then
    umount -n /var
    return 1
  fi
}

# Unmount bind mounts for /var and /home/chronos.
umount_var_and_home_chronos() {
  umount -n /var /home/chronos
}

