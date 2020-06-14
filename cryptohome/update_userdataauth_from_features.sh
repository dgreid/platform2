#!/bin/sh

# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

try_once() {
  local ctrl_file="/var/lib/cryptohome/cryptohome_userdataauth_interface.conf"
  local dbus_interface="org.chromium.ChromeFeaturesServiceInterface"
  local dbus_method="IsCryptohomeUserDataAuthKillswitchEnabled"
  mkdir -p "$(dirname "${ctrl_file}")"

  local status=0
  local reply
  reply="$(minijail0 -u chronos /usr/bin/dbus-send --system \
    --type=method_call --print-reply \
    --dest=org.chromium.ChromeFeaturesService \
    /org/chromium/ChromeFeaturesService \
    "${dbus_interface}.${dbus_method}" \
    2>/dev/null)" || status=$?
  if [ "${status}" -ne 0 ]; then
    # The command failed.
    logger -p WARN "Failed to contact chrome features service to" \
      "check if cryptohome UserDataAuth interface is disabled;" \
      "status=${status} reply=${reply}"
    return 1
  fi

  if [ "${reply##* }" = "true" ] ; then
    # Killswitch is on, disable cryptohome userdataauth.
    echo "USER_DATA_AUTH_INTERFACE=off" > "${ctrl_file}"
    logger -p INFO "Cryptohome UserDataAuth kill switch on"
  elif [ "${reply##* }" = "false" ] ; then
    # Killswitch is off, leave it to /etc to decide.
    rm "${ctrl_file}"
    logger -p INFO "Cryptohome UserDataAuth kill switch off"
  else
    # Response is bad.
    logger -p WARN "Bad response from chrome features service when" \
      "checking if cryptohome UserDataAuth interface is disabled;" \
      "reply=${reply}"
    return 1
  fi

  return 0
}

if [ "$1" = "--once" ]; then
  try_once
  exit "$?"
fi

# If not --once, then we'll try multiple times with exponential backoff.
for delay in 0.5 1.0 2.0 4.0 8.0 16.0; do
  if try_once; then
    # Success, we are done.
    exit 0
  fi
  sleep "${delay}"
done
