#!/bin/bash
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -u -e

# Temporary script to get cros_config values for an alternative identity.
# Used for factory process until b/152291015 is resolved.

CONFIGFS_IMAGE="/usr/share/chromeos-config/configfs.img"
SQUASHFS_BASE="/run/chromeos-config/private"

SMBIOS_NAME=""
DT_COMPATIBLE_LIST=()
SKU_ID=""
WHITELABEL_TAG=""

print_usage () {
  cat <<EOF >&2
Usage: $0 [OPTIONS...] PATH PROPERTY

Optional arguments:
  --configfs-image FILE   Path to configfs image.
  --smbios-name NAME      Override the SMBIOS name from firmware.
  --dt-compatible STRING  Add STRING to the device-tree compatible list.
  --sku-id SKU            Override the SKU id from firmware.
  --whitelabel-tag VALUE  Override the whitelabel tag from VPD.
  --help                  Show this help message and exit.

Positional arguments:
  PATH                    The path to get from config.
  PROPERTY                The property to get from config.
EOF
}

if [[ "${#@}" -eq 0 ]]; then
  print_usage
  exit 1
fi

while [[ "${1:0:1}" != "/" ]]; do
  case "$1" in
    --configfs-image )
      CONFIGFS_IMAGE="$2"
      shift
      ;;
    --smbios-name )
      SMBIOS_NAME="$2"
      shift
      ;;
    --dt-compatible )
      DT_COMPATIBLE_LIST+=("$2")
      shift
      ;;
    --sku-id )
      SKU_ID="$2"
      shift
      ;;
    --whitelabel-tag )
      WHITELABEL_TAG="$2"
      shift
      ;;
    --help )
      print_usage
      exit 0
      ;;
    * )
      print_usage
      echo >&2
      echo "Unknown argument: $1" >&2
      exit 1
      ;;
  esac
  shift
done

if [[ "${#@}" -ne 2 ]]; then
  print_usage
  exit 1
fi

PATH_NAME="$1"
PROPERTY_NAME="$2"

array_contains () {
  for item in "${$2[@]}"; do
    if [[ "$1" == "${item}" ]]; then
      return 0
    fi
  done
  return 1
}

# Load default values from firmware.
if [[ -f /sys/class/dmi/id/product_name && -z "${SMBIOS_NAME}" ]]; then
  read -r SMBIOS_NAME </sys/class/dmi/id/product_name
fi

if [[ -f /proc/device-tree/compatible && "${#DT_COMPATIBLE_LIST}" -eq 0 ]]; then
  # readarray -d '' splits on null chars
  readarray -d '' DT_COMPATIBLE_LIST </proc/device-tree/compatible
fi

if [[ -f /sys/class/dmi/id/product_sku && -z "${SKU_ID}" ]]; then
  # Trim off "sku" in front of the ID
  SKU_ID="$(cut -b4- </sys/class/dmi/id/product_sku)"
fi

if [[ -f /sys/firmware/vpd/ro/whitelabel_tag && -z "${WHITELABEL_TAG}" ]]; then
  read -r WHITELABEL_TAG </sys/firmware/vpd/ro/whitelabel_tag
fi

on_exit_unmount () {
  umount "${SQUASHFS_BASE}"
  rmdir "${SQUASHFS_BASE}"
}

if [[ "${CONFIGFS_IMAGE}" != /usr/share/chromeos-config/configfs.img || \
  ! -d "${SQUASHFS_BASE}" ]]; then
  SQUASHFS_BASE="$(mktemp -d)"
  mount -oro "${CONFIGFS_IMAGE}" "${SQUASHFS_BASE}"
  trap on_exit_unmount EXIT
fi

# file_mismatch filename contents
# returns 0 if file exists and the contents don't match, 1 otherwise
file_mismatch () {
  if [[ -f "$1" && "${2,,}" != "$(tr '[:upper:]' '[:lower:]' <"$1")" ]]; then
    return 0
  fi
  return 1
}

for base in "${SQUASHFS_BASE}"/v1/chromeos/configs/*; do
  if file_mismatch "${base}/identity/smbios-name-match" "${SMBIOS_NAME}"; then
    continue
  fi

  if [[ -f "${base}/identity/device-tree-compatible-match" ]] && \
    ! array_contains "$(cat "${base}/identity/device-tree-compatible-match")" \
    DT_COMPATIBLE_LIST; then
    continue
  fi

  if file_mismatch "${base}/identity/sku-id" "${SKU_ID}"; then
    continue
  fi

  if file_mismatch "${base}/identity/whitelabel-tag" "${WHITELABEL_TAG}"; then
    continue
  fi

  # Identity matched!
  cat "${base}${PATH_NAME}/${PROPERTY_NAME}"
  exit 0
done

echo "No identity matched!" >&2
exit 1
