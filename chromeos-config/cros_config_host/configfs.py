# -*- coding: utf-8 -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Library for generating ChromeOS ConfigFS private data file."""

from __future__ import print_function

import enum
import json
import os
import struct
import subprocess
import tempfile

STRUCT_VERSION = 0
# version, identity_type, entry_count, 4 bytes reserved
HEADER_FORMAT = '<LLL4x'
# flags, model match, sku match, whitelabel match
ENTRY_FORMAT = '<LLLL'


class IdentityType(enum.Enum):
  """The type of identity provided by the identity data file."""
  X86 = 0
  ARM = 1


class EntryFlags(enum.Enum):
  """The flags used at the beginning of each entry."""
  HAS_SKU_ID = 1 << 0
  HAS_WHITELABEL = 1 << 1
  IS_X86_LEGACY_CUSTOMIZATION_ID = 1 << 2
  USES_FIRMWARE_NAME = 1 << 3


def Serialize(obj):
  """Convert a string, integer, bytes, or bool to its file representation.

  Args:
    obj: The string, integer, bytes, or bool to serialize.

  Returns:
    The bytes representation of the object suitable for dumping into a file.
  """
  if isinstance(obj, bytes):
    return obj
  if isinstance(obj, bool):
    return b'true' if obj else b'false'
  return str(obj).encode('utf-8')


def WriteConfigFSFiles(config, base_path):
  """Recursive function to write ConfigFS data out to files and directories.

  Args:
    config: The configuration item (dict, list, str, int, or bool).
    base_path: The path to write out to.
  """
  if isinstance(config, dict):
    iterator = config.items()
  elif isinstance(config, list):
    iterator = enumerate(config)
  else:
    iterator = None

  if iterator is not None:
    os.makedirs(base_path, mode=0o755)
    for name, entry in iterator:
      path = os.path.join(base_path, str(name))
      WriteConfigFSFiles(entry, path)
  else:
    with open(os.open(base_path, os.O_CREAT | os.O_WRONLY, 0o644), 'wb') as f:
      f.write(Serialize(config))


def WriteIdentityJson(config, output_file):
  """Write out the data file needed to provide system identification.

  This data file is used at runtime by libcros_config.  Currently, JSON is used
  as the output format to remain compatible with the existing code in
  libcros_config.  However, this function may be updated if libcros_config
  adopts a new format, so do not rely on the output being JSON.

  Args:
    config: The configuration dictionary (containing "chromeos").
    output_file: A file-like object to write to.
  """
  minified_configs = []
  minified_config = {'chromeos': {'configs': minified_configs}}
  for device_config in config['chromeos']['configs']:
    minified_configs.append({'identity': device_config['identity']})
  json.dump(minified_config, output_file)


def WriteIdentityStruct(config, output_file):
  """Write out the data file needed to provide system identification.

  This data file is used at runtime by cros_configfs to probe the
  identity of the device.  The struct must align with the C code in
  cros_configfs.

  Args:
    config: The configuration dictionary (containing "chromeos").
    output_file: A file-like object to write to, opened in binary mode.
  """
  device_configs = config['chromeos']['configs']
  string_table = []

  # Add a string to the table if it does to exist. Return the number
  # of bytes offset the string will live from the base of the string
  # table.
  def _StringTableIndex(string):
    if string is None:
      return 0

    string = string.encode('utf-8') + b'\000'
    if string not in string_table:
      string_table.append(string)

    index = 0
    for entry in string_table:
      if entry == string:
        return index
      index += len(entry)

  # Detecting x86 vs. ARM is rather annoying given the JSON-like
  # schema.  Implemented like this:
  #   1. If there are no device configs, just say it's x86 as the
  #      value will never get used anyway.
  #   2. If the first config has a key /identity:smbios-name-match,
  #      say it's x86.
  #   3. Otherwise, say it's ARM.
  if not device_configs or 'smbios-name-match' in device_configs[0]['identity']:
    identity_type = IdentityType.X86
  else:
    identity_type = IdentityType.ARM

  # Write the header of the struct, containing version and identity
  # type (x86 vs. ARM).
  output_file.write(
      struct.pack(HEADER_FORMAT, STRUCT_VERSION, identity_type.value,
                  len(device_configs)))

  # Write each of the entry structs.
  for device_config in device_configs:
    identity_info = device_config['identity']
    flags = 0
    sku_id = 0
    if 'sku-id' in identity_info:
      flags |= EntryFlags.HAS_SKU_ID.value
      sku_id = identity_info['sku-id']

    whitelabel_match = None
    if identity_type is IdentityType.X86:
      model_match = identity_info['smbios-name-match']
      if 'customization-id' in identity_info:
        flags |= EntryFlags.IS_X86_LEGACY_CUSTOMIZATION_ID.value
        whitelabel_match = identity_info['customization-id']
    elif identity_type is IdentityType.ARM:
      if 'firmware-name' in identity_info:
        flags |= EntryFlags.USES_FIRMWARE_NAME.value
        model_match = identity_info['firmware-name']
      else:
        model_match = identity_info['device-tree-compatible-match']

    if whitelabel_match is None and 'whitelabel-tag' in identity_info:
      flags |= EntryFlags.HAS_WHITELABEL.value
      whitelabel_match = identity_info['whitelabel-tag']

    output_file.write(
        struct.pack(ENTRY_FORMAT,
                    flags,
                    _StringTableIndex(model_match),
                    sku_id,
                    _StringTableIndex(whitelabel_match)))

  for entry in string_table:
    output_file.write(entry)


def GenerateConfigFSData(config, output_fs):
  """Generate the ConfigFS private data.

  Args:
    config: The configuration dictionary.
    output_fs: The file name to write the SquashFS image at.
  """
  with tempfile.TemporaryDirectory(prefix='configfs.') as configdir:
    os.chmod(configdir, 0o755)
    WriteConfigFSFiles(config, os.path.join(configdir, 'v1'))
    # TODO(jrosenth): remove the json file once we've converted to the
    # struct format.  Both files are included now as a transitional
    # scheme.
    with open(os.path.join(configdir, 'v1/identity.json'), 'w') as f:
      WriteIdentityJson(config, f)
    with open(os.path.join(configdir, 'identity.bin'), 'wb') as f:
      WriteIdentityStruct(config, f)
    subprocess.run(['mksquashfs', configdir, output_fs, '-no-xattrs',
                    '-noappend', '-all-root'], check=True,
                   stdout=subprocess.PIPE)
