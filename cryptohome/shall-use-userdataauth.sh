#!/bin/sh

# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This file checks the configuration file in the stateful partition to
# determine if the current Chrome OS installation is going to use the new
# interface (aka UserDataAuth) or the old interface.

# If this file return 0, we use the new interface. Otherwise, it'll return
# 1 to denote that the old interface will be used. If any other return code
# is returned, then use the old interface.

# If the configuration file in stateful partition doesn't exist or is not
# conformant to the syntax, then we'll look at the one in /etc for default.

# We ignore any line that starts with #, then we look for key value pair in
# the syntax of "key=value" (Note that there's no space). Then, we look at
# the value corresponding to the key "USER_DATA_AUTH_INTERFACE", if it's "on"
# then we'll use the new interface, if it's "off", we'll use the old one.
# Line(s) after the first line, if any, is ignored.

ETC_CONF_FILE="/etc/cryptohome_userdataauth_interface.conf"
VAR_LIB_CONF_FILE="/var/lib/cryptohome/cryptohome_userdataauth_interface.conf"

result_use_new=0
result_use_old=1

check_file() {
  local filename="$1"
  if [ -f "${filename}" ]; then
    local contents=$(sed -n -e '/^USER_DATA_AUTH_INTERFACE=/p' "${filename}" |
      cut -d'=' -f2)

    if [ "${contents}" = "on" ]; then
      exit "${result_use_new}"
    fi

    if [ "${contents}" = "off" ]; then
      exit "${result_use_old}"
    fi
  fi
}

# This defaults to on, and is flipped to off in the overlay if
# cryptohome_userdataauth_interface use flag is on.
killswitch=on

if [ "${killswitch}" = "on" ]; then
  # If kill switch is on, then always use the legacy interface.
  exit "${result_use_old}"
fi

# Load the one in /var/lib, if there's a result, it'll exit with the status
# code and not return.
check_file "${VAR_LIB_CONF_FILE}"

# If we get here, then there's no valid config in /var/lib, so let's check
# the default in /etc.
check_file "${ETC_CONF_FILE}"

# Default to the old one if both are missing/corrupt
exit "${result_use_old}"
