/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "lib/cros_config_struct.h"

static struct config_map all_configs[] = {
    {.platform_name = "Some",
     .firmware_name_match = "Some",
     .sku_id = 0,
     .customization_id = "",
     .whitelabel_tag = "",
     .info = {.model = "some"}},

    {.platform_name = "Some",
     .firmware_name_match = "Some",
     .sku_id = 1,
     .customization_id = "",
     .whitelabel_tag = "",
     .info = {.model = "some"}},

    {.platform_name = "Some",
     .firmware_name_match = "Some",
     .sku_id = 2,
     .customization_id = "",
     .whitelabel_tag = "",
     .info = {.model = "some2"}},

    {.platform_name = "Some",
     .firmware_name_match = "Another",
     .sku_id = -1,
     .customization_id = "",
     .whitelabel_tag = "",
     .info = {.model = "another"}},

    {.platform_name = "Some",
     .firmware_name_match = "SomeCustomization",
     .sku_id = -1,
     .customization_id = "SomeCustomization",
     .whitelabel_tag = "",
     .info = {.model = "some_customization"}},

    {.platform_name = "Some",
     .firmware_name_match = "Some",
     .sku_id = 8,
     .customization_id = "",
     .whitelabel_tag = "whitelabel1",
     .info = {.model = "whitelabel"}},

    {.platform_name = "Some",
     .firmware_name_match = "Some",
     .sku_id = 9,
     .customization_id = "",
     .whitelabel_tag = "whitelabel1",
     .info = {.model = "whitelabel"}},

    {.platform_name = "Some",
     .firmware_name_match = "Some",
     .sku_id = 8,
     .customization_id = "",
     .whitelabel_tag = "whitelabel2",
     .info = {.model = "whitelabel"}},

    {.platform_name = "Some",
     .firmware_name_match = "Some",
     .sku_id = 9,
     .customization_id = "",
     .whitelabel_tag = "whitelabel2",
     .info = {.model = "whitelabel"}}};

const struct config_map* cros_config_get_config_map(int* num_entries) {
  *num_entries = 9;
  return &all_configs[0];
}
