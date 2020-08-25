// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hammerd/fmap_utils.h"
#include "hammerd/update_fw.h"
#include "hammerd/vb21_struct.h"

namespace hammerd {

class Environment {
 public:
  Environment() { logging::SetMinLogLevel(logging::LOG_FATAL); }
};

namespace {

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  FirmwareUpdater fw_updater_(nullptr);  // no endpoint required to load EC
  FuzzedDataProvider data_provider(data, size);
  const char* ec_ro_name = "EC_RO";
  const char* ro_frid_name = "RO_FRID";
  const char* ec_rw_name = "EC_RW";
  const char* ec_fwid_name = "EC_FWID";
  const char* rw_rbver_name = "RW_RBVER";
  const char* key_ro_name = "KEY_RO";

  // Build a fake EC image.
  // - Fake header: 5 bytes
  // - fake fmap: sizeof(fmap) bytes
  // - 6 fake fmap areas
  //  - EC_RO
  //  - RO_FRID
  //  - EC_RW
  //  - RW_FWID
  //  - RW_RBVER
  //  - KEY_RO
  // - RO version string: 32 bytes
  // - RW version string: 32 bytes
  // - RW rollback version: 4 bytes
  // - RO key: sizeof(vb21_packed_key) bytes
  std::string ec_image("12345");
  fmap fake_map;
  fake_map.nareas = data_provider.ConsumeIntegralInRange<uint16_t>(4, 6);
  fake_map.size = 5 + sizeof(fmap) + (sizeof(fmap_area) * fake_map.nareas) +
                  32 + 32 + 4 + sizeof(vb21_packed_key);
  memcpy(fake_map.signature, FMAP_SIGNATURE, sizeof(FMAP_SIGNATURE));
  ec_image.append(reinterpret_cast<char*>(&fake_map), sizeof(fake_map));

  // Setup areas
  fmap_area fake_area;
  snprintf(reinterpret_cast<char*>(fake_area.name), sizeof(fake_area.name),
           "%s", ec_ro_name);
  fake_area.offset = data_provider.ConsumeIntegral<uint32_t>();
  fake_area.size = data_provider.ConsumeIntegral<uint32_t>();
  ec_image.append(reinterpret_cast<char*>(&fake_area), sizeof(fake_area));

  snprintf(reinterpret_cast<char*>(fake_area.name), sizeof(fake_area.name),
           "%s", ro_frid_name);
  fake_area.size = sizeof(SectionInfo::version);
  fake_area.offset = data_provider.ConsumeIntegral<uint32_t>();
  ec_image.append(reinterpret_cast<char*>(&fake_area), sizeof(fake_area));

  snprintf(reinterpret_cast<char*>(fake_area.name), sizeof(fake_area.name),
           "%s", ec_rw_name);
  fake_area.offset = data_provider.ConsumeIntegral<uint32_t>();
  fake_area.size = data_provider.ConsumeIntegral<uint32_t>();
  ec_image.append(reinterpret_cast<char*>(&fake_area), sizeof(fake_area));

  snprintf(reinterpret_cast<char*>(fake_area.name), sizeof(fake_area.name),
           "%s", ec_fwid_name);
  fake_area.size = sizeof(SectionInfo::version);
  fake_area.offset = data_provider.ConsumeIntegral<uint32_t>();
  ec_image.append(reinterpret_cast<char*>(&fake_area), sizeof(fake_area));

  if (fake_map.nareas > 4) {
    snprintf(reinterpret_cast<char*>(fake_area.name), sizeof(fake_area.name),
             "%s", rw_rbver_name);
    fake_area.offset = data_provider.ConsumeIntegral<uint32_t>();
    fake_area.size = data_provider.ConsumeIntegral<uint32_t>();
    ec_image.append(reinterpret_cast<char*>(&fake_area), sizeof(fake_area));
  }

  if (fake_map.nareas > 5) {
    snprintf(reinterpret_cast<char*>(fake_area.name), sizeof(fake_area.name),
             "%s", key_ro_name);
    fake_area.offset = data_provider.ConsumeIntegral<uint32_t>();
    fake_area.size = data_provider.ConsumeIntegral<uint32_t>();
    ec_image.append(reinterpret_cast<char*>(&fake_area), sizeof(fake_area));
  }

  char ro_version[32] = "UNUSED RO FAKE VERSION";
  ec_image.append(ro_version, 32);

  char rw_version[32] = "UNUSED RW FAKE VERSION";
  ec_image.append(rw_version, 32);

  int32_t rw_rollback = 35;
  ec_image.append(reinterpret_cast<char*>(&rw_rollback), sizeof(rw_rollback));

  vb21_packed_key ro_key;
  ro_key.key_version = 1;
  ec_image.append(reinterpret_cast<char*>(&ro_key), sizeof(ro_key));

  fw_updater_.LoadEcImage(ec_image);

  return 0;
}
}  // namespace
}  // namespace hammerd
