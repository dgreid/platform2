/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "lib/cros_config_struct.h"

static struct config_map all_configs[] = {
    {.platform_name = "",
     .firmware_name_match = "google,some",
     .sku_id = -1,
     .customization_id = "",
     .whitelabel_tag = "",
     .info = {.model = "some"}},

    {.platform_name = "",
     .firmware_name_match = "google,whitelabel",
     .sku_id = -1,
     .customization_id = "",
     .whitelabel_tag = "whitelabel1",
     .info = {.model = "whitelabel"}},

    {.platform_name = "",
     .firmware_name_match = "google,whitelabel",
     .sku_id = -1,
     .customization_id = "",
     .whitelabel_tag = "whitelabel2",
     .info = {.model = "whitelabel"}},

    {.platform_name = "",
     .firmware_name_match = "google,whitelabel",
     .sku_id = -1,
     .customization_id = "",
     .whitelabel_tag = "",
     .info = {.model = "whitelabel"}},

    {.platform_name = "Another",
     .firmware_name_match = "google,another",
     .sku_id = 8,
     .customization_id = "",
     .whitelabel_tag = "",
     .info = {.model = "another1"}},

    {.platform_name = "Another",
     .firmware_name_match = "google,another",
     .sku_id = 9,
     .customization_id = "",
     .whitelabel_tag = "",
     .info = {.model = "another2"}}};

const struct config_map* cros_config_get_config_map(int* num_entries) {
  *num_entries = 6;
  return &all_configs[0];
}
