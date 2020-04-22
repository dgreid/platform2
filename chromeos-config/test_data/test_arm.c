#include "lib/cros_config_struct.h"

static struct config_map all_configs[] = {
    {.platform_name = "",
     .firmware_name_match = "google,some",
     .sku_id = -1,
     .customization_id = "",
     .whitelabel_tag = "",
     .info = {.brand = "",
              .model = "some",
              .customization = "some",
              .signature_id = "some"}},

    {.platform_name = "",
     .firmware_name_match = "google,whitelabel",
     .sku_id = -1,
     .customization_id = "",
     .whitelabel_tag = "whitelabel1",
     .info = {.brand = "",
              .model = "whitelabel",
              .customization = "whitelabel1",
              .signature_id = "whitelabel"}},

    {.platform_name = "",
     .firmware_name_match = "google,whitelabel",
     .sku_id = -1,
     .customization_id = "",
     .whitelabel_tag = "whitelabel2",
     .info = {.brand = "",
              .model = "whitelabel",
              .customization = "whitelabel2",
              .signature_id = "whitelabel"}},

    {.platform_name = "",
     .firmware_name_match = "google,whitelabel",
     .sku_id = -1,
     .customization_id = "",
     .whitelabel_tag = "",
     .info = {.brand = "",
              .model = "whitelabel",
              .customization = "whitelabel",
              .signature_id = "whitelabel"}},

    {.platform_name = "Another",
     .firmware_name_match = "google,another",
     .sku_id = 8,
     .customization_id = "",
     .whitelabel_tag = "",
     .info = {.brand = "",
              .model = "another1",
              .customization = "another1",
              .signature_id = "another1"}},

    {.platform_name = "Another",
     .firmware_name_match = "google,another",
     .sku_id = 9,
     .customization_id = "",
     .whitelabel_tag = "",
     .info = {.brand = "",
              .model = "another2",
              .customization = "another2",
              .signature_id = "another2"}}
};

const struct config_map *cros_config_get_config_map(int *num_entries) {
  *num_entries = 6;
  return &all_configs[0];
}
