#!/bin/sh

# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Chrome OS Disk Firmware Update Script
# This script checks whether if the root device needs to be upgraded.
#

. /usr/share/misc/shflags
. /usr/share/misc/chromeos-common.sh

# Temperaray directory to put device information
DEFINE_string 'tmp_dir' '' "Use existing temporary directory."
DEFINE_string 'fw_package_dir' '' "Location of the firmware package."
DEFINE_string 'hdparm' '/sbin/hdparm' "hdparm binary to use."
DEFINE_string 'mmc' '/usr/bin/mmc' "mmc binary to use."
DEFINE_string 'status' '' "Status file to write to."
DEFINE_boolean 'test' ${FLAGS_FALSE} "For unit testing."

# list global variables
#   disk_model
#   disk_fw_rev
#   disk_fw_file
#   disk_exp_fw_rev
#   disk_fw_opt

log_msg() {
  logger -t "chromeos-disk-firmware-update[${PPID}]" "$@"
  echo "$@"
}

die() {
  log_msg "error: $*"
  exit 1
}

# Parse command line
FLAGS "$@" || exit 1
eval set -- "${FLAGS_ARGV}"

# disk_fw_select - Select the proper disk firmware to use.
#
# This code reuse old installer disk firmware upgrade code.
#
# inputs:
#     disk_rules        -- the file containing the list of rules.
#     disk_model        -- the model from hdparm -I
#     disk_fw_rev       -- the firmware version of the device.
#
# outputs:
#     disk_fw_file      -- name of the DISK firmware image file for this machine
#     disk_exp_fw_rev   -- the revision code of the firmware
#     disk_fw_opt       -- the options for this update
#
disk_fw_select() {
  local disk_rules="$1"
  local rule_model
  local rule_fw_rev
  local rule_exp_fw_rev
  local rule_fw_opt
  local rule_fw_file
  disk_fw_file=""
  disk_exp_fw_rev=""
  disk_fw_opt=""

  # Check for obvious misconfiguration problems:
  if [ -z "${disk_rules}" ]; then
    log_msg "Warning: disk_rules not specified"
    return 1
  fi
  if [ ! -r "${disk_rules}" ]; then
    log_msg "Warning: cannot read config file ${disk_rules}"
    return 1
  fi

  # Read through the config file, looking for matches:
  while read -r rule_model rule_fw_rev rule_exp_fw_rev rule_fw_opt rule_fw_file; do
    if [ -z "${rule_fw_file}" ]; then
      log_msg "${disk_rules}: incorrect number of items in file"
      continue
    fi

    # Check for match:
    if [ "${disk_model}" != "${rule_model}" ]; then
      continue
    fi
    if [ "${disk_fw_rev}" != "${rule_fw_rev}" ]; then
      continue
    fi
    disk_exp_fw_rev=${rule_exp_fw_rev}
    disk_fw_opt=${rule_fw_opt}
    disk_fw_file=${rule_fw_file}
  done < "${disk_rules}"

  # If we got here, then no DISK firmware matched.
  if [ -z "${disk_fw_file}" ]; then
    return 1
  else
    return 0
  fi
}

# disk_hdparm_info - Shime for calling hdparm
#
# Useful for testing overide.
#
# inputs:
#     device            -- the device name [sda,...]
#
# echo the output of hdparm.
#
disk_hdparm_info() {
  local device="$1"

  # use -I option to be sure the drive is accessed:
  # will fail if the drive is not up
  # sure that the firmware version is up to date if the
  # disk upgrade without reset.
  "${FLAGS_hdparm}" -I "/dev/${device}"
}

#disk_mmc_info - Retrieve disk information for MMC device
#
# inputs:
#     device            -- the device name [mmcblk0]
#
# outputs:
#     disk_model        -- model of the device
#     disk_fw_rev       -- actual firmware revision on the device
#
# returns non 0 on error
#
disk_mmc_info() {
  # Some vendor use hexa decimal character for indentification.
  disk_model=$(cat "/sys/block/$1/device/cid" | tail -c +7 | head -c 12)
  disk_fw_rev=$(cat "/sys/block/$1/device/fwrev")
  if [ -z "${disk_model}" -o -z "${disk_fw_rev}" ]; then
    return 1
  fi
  return 0
}

# disk_ata_info - Retrieve disk information for ata device
#
# inputs:
#     device            -- the device name [sda]
#
# outputs:
#     disk_model        -- model of the device
#     disk_fw_rev       -- actual firmware revision on the device
#
# returns non 0 on error
#
disk_ata_info() {
  local device="$1"
  local rc=0
  local hdparm_out="${FLAGS_tmp_dir}/${device}"

  disk_model=""
  disk_fw_rev=""
  disk_hdparm_info "${device}" > "${hdparm_out}"
  rc=$?
  if [ ${rc} -ne 0 ]; then
    return ${rc}
  fi
  if [ ! -s "${hdparm_out}" ]; then
    log_msg "hdparm did not produced any output"
    return 1
  fi
  disk_model=$(sed -nre \
      '/^\t+Model/s|\t+Model Number: +(.*)|\1|p' "${hdparm_out}" \
    | sed -re 's/ +$//' -e 's/[ -]/_/g')
  disk_fw_rev=$(sed -nre \
      '/^\t+Firmware/s|\t+Firmware Revision: +(.*)|\1|p' "${hdparm_out}" \
    | sed -re 's/ +$//' -e 's/[ -]/_/g')
  if [ -z "${disk_model}" -o -z "${disk_fw_rev}" ]; then
    return 1
  fi
  return 0
}

# disk_info - Retrieve model and firmware version from disk
#
# Call the appropriate function for the device type.
#
# inputs:
#     device            -- the device name
#
# outputs:
#     disk_model        -- model of the device
#     disk_fw_rev       -- actual firmware revision on the device
#
# returns non 0 on error
#
disk_info() {
  local device="$1"
  local device_type
  device_type=$(get_device_type "/dev/${device}")
  case ${device_type} in
    "ATA")
      disk_ata_info "$@"
      ;;
    "MMC")
      disk_mmc_info "$@"
      ;;
    *)
      log_msg "Unknown device(${device}) type: ${device_type}"
      return 1
  esac
}

# disk_hdparm_upgrade - Upgrade the firmware on the dsik
#
# Update the firmware on the disk.
# TODO(gwendal): We assume the device can be updated in one shot.
#                In a future version, we may place a
#                a deep charge and reboot the machine.
#
# inputs:
#     device            -- the device name [sda,...]
#     fw_file           -- the firmware image
#     fw_options        -- the options from the rule file.
#
# returns non 0 on error
#
disk_hdparm_upgrade() {
  local device="$1"
  local fw_file="$2"
  local fw_options="$3"

  "${FLAGS_hdparm}" --fwdownload-mode7 "${fw_file}" \
    --yes-i-know-what-i-am-doing --please-destroy-my-drive \
    "/dev/${device}"
}

# disk_mmc_upgrade - Upgrade the firmware on the eMMC storage
#
# Update the firmware on the disk.
#
# inputs:
#     device            -- the device name [sda,...]
#     fw_file           -- the firmware image
#     fw_options        -- the options from the rule file. (unused)
#
# returns non 0 on error
#
disk_mmc_upgrade() {
  local device="$1"
  local fw_file="$2"
  local fw_options="$3"
  local options=""

  if [ "${fw_options}" != "-" ]; then
     # Options for mmc in the config files are separated with commas.
     # Translate the option for the command line.
     options=$(echo "${fw_options}" | sed 's/,/ -k /g')
     options="-k ${options}"
  fi

  "${FLAGS_mmc}" ffu ${options} "${fw_file##*/}" "/dev/${device}"
}

# disk_upgrade - Upgrade the firmware on the disk
#
# Update the firmware on the disk by calling the function appropriate for
# the transport.
#
# inputs:
#     device            -- the device name [sda,...]
#     fw_file           -- the firmware image
#     fw_options        -- the options from the rule file.
#
# returns non 0 on error
#
disk_upgrade() {
  local device="$1"
  local device_type
  device_type=$(get_device_type "/dev/${device}")
  case ${device_type} in
    "ATA")
      disk_hdparm_upgrade "$@"
      ;;
    "MMC")
      disk_mmc_upgrade "$@"
      ;;
    *)
      log_msg "Unknown device(${device}) type: ${device_type}"
      return 1
  esac
}

# disk_upgrade_devices - Look for firmware upgrades
#
# major function: look for a rule match and upgrade.
# updated in one shot. In a future version, we may place a
# a deep charge and reboot the machine.
#
# input:
#    list of devices to upgrade.
# retuns 0 on sucess
#    The error code of hdparm or other functions that fails
#    120 if no rules is provided
#    121 when the disk works but the firmware was not applied.
#
disk_upgrade_devices() {
  local disk_rules="$1"
  local device
  local fw_file
  local success
  local disk_old_fw_rev=""
  local rc=0
  local tries=0

  shift # skip disk rules parameters.
  for device in "$@"; do
    sucess=""
    while true; do
      disk_info "${device}"  # sets disk_model, disk_fw_rev
      rc=$?
      if [ ${rc} -ne 0 ]; then
        log_msg "Can not get info on this device. skip."
        break
      fi
      disk_fw_select "${disk_rules}"  # sets disk_fw_file, disk_exp_fw_rev, disk_fw_opt
      rc=$?
      if [ ${rc} -ne 0 ]; then
        # Nothing to do, go to next drive if any.
        : ${success:="No need to upgrade ${device}:${disk_model}"}
        log_msg "${success}"
        rc=0
        break
      fi
      fw_file="${FLAGS_fw_package_dir}/${disk_fw_file}"
      if [ ! -f "${fw_file}" ]; then
        fw_file="${FLAGS_tmp_dir}/${disk_fw_file}"
        bzcat "${FLAGS_fw_package_dir}/${disk_fw_file}.bz2" > "${fw_file}" 2> /dev/null
        rc=$?
        if [ ${rc} -ne 0 ]; then
          log_msg "${disk_fw_file} in ${FLAGS_fw_package_dir} could not be extracted: ${rc}"
          break
        fi
      fi
      disk_old_fw_rev="${disk_fw_rev}"
      disk_upgrade "${device}" "${fw_file}" "${disk_fw_opt}"
      rc=$?
      if [ ${rc} -ne 0 ]; then
        # Will change in the future if we need to power cycle, reboot...
        log_msg "Unable to upgrade ${device} from ${disk_fw_rev} to ${disk_exp_fw_rev}"
        break
      else
        # Allow the kernel to recover
        tries=4
        rc=1
        # Verify that's the firmware upgrade stuck It may take some time.
        while [ ${tries} -ne 0 -a ${rc} -ne 0 ]; do
          : $(( tries -= 1 ))
          # Allow the error handler to block the scsi queue if it is working.
          if [ ${FLAGS_test} -eq ${FLAGS_FALSE} ]; then
            sleep 1
          fi
          disk_info "${device}"
          rc=$?
        done
        if [ ${rc} -ne 0 ]; then
          # We are in trouble. The disk was expected to come back but did not.
          # TODO(gwendal): Shall we have a preemptive message to ask to
          # powercycle?
          break
        fi
        if [ "${disk_exp_fw_rev}" = "${disk_fw_rev}" ]; then
          # We are good, go to the next drive if any.
          if [ -n "${success}" ]; then
            success="${success}
"
          fi
          success="${success}Upgraded ${device}:${disk_model} from"
          success="${success} ${disk_old_fw_rev} to ${disk_fw_rev}"
          # Continue, in case we need upgrade in several steps.
          continue
        else
          # The upgrade did not stick, we will retry later.
          rc=121
          break
        fi
      fi
    done
  done
  # Leave a trace of a successful run.
  if [ ${rc} -eq 0 -a -n "${FLAGS_status}" ]; then
    echo ${success} > "${FLAGS_status}"
  fi
  return ${rc}
}

main() {
  local disk_rules_raw="${FLAGS_fw_package_dir}"/rules
  local rc=0
  local erase_tmp_dir=${FLAGS_FALSE}

  if [ ! -d "${FLAGS_tmp_dir}" ]; then
    erase_tmp_dir=${FLAGS_TRUE}
    FLAGS_tmp_dir=$(mktemp -d)
  fi
  if [ ! -f "${disk_rules_raw}" ]; then
    log_msg "Unable to find rules file in ${FLAGS_fw_package_dir}"
    return 120
  fi
  disk_rules=${FLAGS_tmp_dir}/rules

  # remove unnecessary lines
  sed '/^#/d;/^[[:space:]]*$/d' "${disk_rules_raw}" > "${disk_rules}"

  disk_upgrade_devices "${disk_rules}" \
    $(list_fixed_ata_disks) $(list_fixed_mmc_disks)
  rc=$?

  if [ ${erase_tmp_dir} -eq ${FLAGS_TRUE} ]; then
    rm -rf "${FLAGS_tmp_dir}"
  fi
  # Append a cksum to prevent multiple calls to this script.
  if [ ${rc} -eq 0 -a -n "${FLAGS_status}" ]; then
    cksum "${disk_rules_raw}" >> "${FLAGS_status}"
  fi
  return ${rc}
}

# invoke main if not in test mode, otherwise let the test code call.
if [ ${FLAGS_test} -eq ${FLAGS_FALSE} ]; then
  main "$@"
fi

