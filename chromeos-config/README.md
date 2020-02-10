# Chrome OS Configuration -- Master Chrome OS Configuration tools / library

This is the homepage/documentation for chromeos-config which provides access to
the master configuration for Chrome OS.

[TOC]

## Internal Documentation

See the [design doc](http://go/cros-unified-build-design) for information about
the design. This is accessible only within Google. A public page will be
published to chromium.org once the feature is complete and launched.

## Important classes

See [CrosConfig](./libcros_config/cros_config.h) for the class to use to access
configuration strings on a target. See
[cros_config_host.py](./cros_config_host/cros_config_host.py) for access to the
config on a host or during a build.

## CLI Usage

There are two CLIs built for Chrome OS configuration access, cros_config for use
on the target, and cros_config_host for use on the host/during building. See the
--help for each tool respectively for help on usage.

## Debugging

libcros_config will emit a lot of debugging log messages if you set the
CROS_CONFIG_DEBUG environment variable to a non-empty value before calling into
the library.

## Config Schema

Chrome OS config is now YAML based. If you're still using the Flat Device Tree
(FDT) based implementation, see the v1 schema description below.

The following components make up the YAML based chromeos-config support:

### YAML Source

The YAML source is designed for human maintainability. It allows for easy config
sharing across many different devices (via anchors).

For background on YAML, see: [Learn YAML in 10
minutes](https://learnxinyminutes.com/docs/yaml/)

The source is generally located at:

```bash
ls src/overlay-${BOARD}/chromeos-base/chromeos-config-bsp/files/model.yaml
```

Beyond the normal features of YAML, there are a few custom features supported
that allow for even better re-use and expressiveness in the YAML config.

1.  Templating - Templating allows config to be shared by letting callers
    reference variables in the config, which are then evaluated on a per
    device/sku/product basis.

    The basic syntax is:

    ```yaml
    some-element: "{{some-template-variable}}"
    ```

    Valid template variables are any YAML elements that are currently in scope.
    When generating config, scope is evaluated in the following order:

    1.  sku
    2.  config (this is recursive ... any variable at any level can be
        referenced)
    3.  device
    4.  product

    This order allows shared anchors to define default variables that are then
    optionally overridden by either the device or product scope.

2.  Local Variables - These are variables that are only used for templating and
    are dropped in the final JSON output. Variables starting with '$' are
    considered local and are ignored after template evaluation. The basic syntax
    is:

    ```yaml
    config:
      $some-local-variable: "some-value"
      some-element: "{{$some-local-variable}}"
    ```

    This supports the following:

    1.  Defining local variables that are re-used in multiple places (e.g. file
        paths).
    2.  Re-using common config (e.g. 'identity') where the variable value isn't
        known until the device/product/sku variables come into scope (e.g.
        $sku-id).

3.  File Imports - File imports allow common snippets of YAML to be shared
    across multiple different implementations.  File importing works the same as
    if the YAML files were cat'd together and then evaluated.  File importing is
    recursive also, so it will support importing files that import other files.
    Import paths must be relative to the file that specifies the import.

    ```yaml
    imports:
      - "some_common_import_file.yaml"
      - "../common/some_other_common_import_file.yaml"
    ```

The following provides a simple example of a config using both core YAML
features and the custom features described above.

```yaml
common_config: &common_config
  name: "{{$device-name}}"
  brand-code: "{{$brand-code}}"
  identity:
    platform-name: "SomePlatform"
    smbios-name-match: "SomePlatform"
    sku-id: "{{$sku-id}}"
  firmware-signing:
    key-id: "{{$key-id}}"
    signature-id: "{{name}}"
chromeos:
  devices:
    - $device-name: "SomeDevice"
      products:
        - $brand-code: "YYYY"
          $key-id: "SOME-KEY-ID"
      skus:
        - $sku-id: 0
          config:
            <<: *common_config
            wallpaper: "some-wallpaper"
        - $sku-id: 1
          config: *common_config
```

When this YAML is evaluated, it will fully expand out as the following:

```yaml
chromeos:
  models:
    - name: "SomeDevice"
      brand-code: "YYYY"
      identity:
        platform-name: "SomePlatform"
        smbios-name-match: "SomePlatform"
        sku-id: 0
      firmware-signing:
        key-id: "SOME-KEY-ID"
        signature-id: "SomeDevice"
      wallpaper: "some-wallpaper"
    - name: "SomeDevice"
      brand-code: "YYYY"
      identity:
        platform-name: "SomePlatform"
        smbios-name-match: "SomePlatform"
        sku-id: 1
      firmware-signing:
        key-id: "SOME-KEY-ID"
        signature-id: "SomeDevice"
```

### YAML Merging (multiple source YAML files)

There are various cases where it makes sense to manage YAML config across
multiple different repos in separate YAML files.

E.g.

* Permissions based control via git repo access to specific config
* Extending overlays for device customization (e.g. Moblab)

This is supported through cros_config_schema tool and is invoked as part of the
chromeos-config ebuild.

Using normal portage ebuilds/config, users can install as many YAML files as
they wish to be merged together into: /usr/share/chromeos-config/yaml

E.g.

* models.yaml
* models-private.yaml (private config overlaid on the public config)

These files are then merged together based on their lexicographic name order.

Merging of YAML files applies the following characteristics:

1. Order is important.  If two files supply the same config, the last file wins.
2. Identity is important.  Config is merged based on ONE OF the following:

   1. name - If the name attribute matches, the config is merged
   2. identity - all fields of identity must match explicitly for code
      generation purposes. The fields are documented in [identity](#identity).
      All fields that are present in the second file must be present in the
      first file.
      Note that a null value (the field is not present) and an empty string
      (the field is assigned the value of "") are not considered a match
      for code generation purposes.

For a detailed example of how merging works, see the following test files:

1. [test_merge_base.yaml](https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/chromeos-config/libcros_config/test_merge_base.yaml) - First file passed to the merge routine.
2. [test_merge_overlay.yaml](https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/chromeos-config/libcros_config/test_merge_overlay.yaml) - Second file passed (winner of any conflicts).
3. [test_merge.json](https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/chromeos-config/libcros_config/test_merge.json) - Result generated from the merge.

### YAML Transform (to JSON)

In addition to the templating evaluation discussed above, the YAML is converted
to JSON before it's actually used in chromeos-config. This fully
evaluated/de-normalized form accomplishes a couple things:

1.  It provides a great diffable format so it's obvious what changes are
    actually applied if, for example, a shared config element was changed.
2.  It keeps the consumer code very simple (host and runtime). The code just
    matches the identity attributes and then uses the respective config. It
    never has to care about re-use or config sharing.

The transform algorithm works as follows:

* FOREACH device in chromeos/devices
    * FOREACH product in device/products
        * FOREACH sku in device/skus
            * sku varibles are put into scope
            * config variables are put into scope
            * device variables are put into scope
            * product variables are put into scope
            * with sku/config
                * config template variables are evaluated
                * the config contents are captured and stored in the resulting json

Based on this algorithm, some key points are:

* Only 'sku/config' actually lands in the JSON output. All other YAML structure
  supports the generation of the sku/config payloads.
* 'product' generally defines identity/branding information only. The main
  reason multiple products are supported is for the whitelabel case.

### Making changes to a YAML model file

When modifying a `model.yaml` file there are few steps that need to be taken to
 manifest the change in a board target. Since the actual work to combine and
 process the YAML files is done in the chromeos-config ebuild, it needs to be
 remerged after the input YAML has been modified.


1. Start cros_workon on the ebuild where your source model.yaml lives:

   ```bash
   (chroot) $ cros_workon start chromeos-base/chromeos-config-bsp-${BOARD}
   ```

1. Make and install your incremental changes:

   ```bash
   (chroot) $ cros_workon_make chromeos-base/chromeos-config-bsp-${BOARD} --install
   ```

1. Remerge the chromeos-config ebuild:

   Note: The config-bsp overlay path may be slightly different depending on the
   board and if it is public or private.

   ```bash
   (chroot) $ emerge-$BOARD chromeos-config
   ```

### Schema Validation

The config is evaluated against a http://json-schema.org/ schema located at:
chromeos-config/cros_config_host/cros_config_schema.yaml

NOTE: The schema is managed in YAML because it's easier to edit than JSON.

Only the transformed JSON is actually evaluated against the schema. Authors can
do whatever makes sense in the YAML (from a sharing perspective) as long as it
generates compliant JSON that passes the schema validation.

The schema documentation is auto-generated (and put into this README.md file)
via: python -m cros_config_host.generate_schema_doc -o README.md

The schema definition is below:

## CrOS Config Type Definitions (v2)

### Definitions

In the tables below,

* *Build-only* attributes get automatically stripped from the platform JSON as
  part of the build.

[](begin_definitions)

### model
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| arc | [arc](#arc) |  | False |  | False |  |
| audio | [audio](#audio) |  | False |  | False |  |
| auto-night-light | boolean |  | False |  | False | Whether the auto-night-light feature is enabled on the device, which sets the schedule for Night light automatically to sunset-to-sunrise. |
| bluetooth | [bluetooth](#bluetooth) |  | False |  | False |  |
| brand-code | string |  | False |  | False | Brand code of the model (also called RLZ code). |
| camera | [camera](#camera) |  | False |  | False |  |
| cros-healthd | [cros-healthd](#cros-healthd) |  | False |  | False | Contains properties used by cros_healthd for model-specific telemetry. Each property represents a category of information and contains boolean properties that indicate whether a device supports a particular telemetry item. See cros_healthd_probe.mojom for descriptions of each property. |
| demo-mode | [demo-mode](#demo-mode) |  | False |  | False | Properties related to the ChromeOS Demo Mode, defining the user experience when the device is used in retail. |
| detachable-base | [detachable-base](#detachable-base) |  | False |  | False | Contains the configuration for the hammerd which is used to update the detachable base firmware. |
| fingerprint | [fingerprint](#fingerprint) |  | False |  | False | Contains details about the model's fingerprint implementation. |
| firmware | [firmware](#firmware) |  | True |  | False |  |
| firmware-signing | [firmware-signing](#firmware-signing) |  | False |  | True |  |
| hardware-properties | [hardware-properties](#hardware-properties) |  | False |  | False | Contains boolean flags or enums for hardware properties of this board, for example if it's convertible, has a touchscreen, has a camera, etc. This information is used to auto-generate C code that is consumed by the EC build process in order to do run-time configuration. If a value is defined within a config file, but not for a specific model, that value will be assumed to be false for that model. If a value is an enum and is not specified for a specific model, it will default to "none". All properties must be booleans or enums. If non-boolean properties are desired, the generation code in cros_config_schema.py must be updated to support them. |
| identity | [identity](#identity) |  | False |  | False | Defines attributes that are used by cros_config to detect the identity of the platform and which corresponding config should be used. This tuple must either contain x86 properties only or ARM properties only. |
| modem | [modem](#modem) |  | False |  | False |  |
| name | string | ```^[_a-zA-Z0-9]{3,}``` | True |  | False | Unique name for the given model. |
| oem-id | string | ```[0-9]+``` | False |  | False | Some projects store SKU ID, OEM ID and Board Revision in an EEPROM and only SKU ID can be updated in the factory and RMA flow but others should be pre-flashed in the chip level. In this case, we would like to validate whether oem-id here from the updated SKU ID matches the one in the EEPROM so we can prevent this device from being updated to another OEM's devices.  |
| power | [power](#power) |  | False |  | False | WARNING -- This config contains unvalidated settings, which is not a correct usage pattern, but this will be used in the interim until a longer term solution can be put in place where the overall schema can be single sourced (for the YAML and C++ that uses it); likely though some type of code generation. SUMMARY -- Contains power_manager device settings.  This is the new mechanism used in lieu of the previous file based implementation (via powerd-prefs). Power manager will first check for a property in this config, else it will revert to the file based mechanism (via the powerd-prefs setting). This provides more flexibility in sharing power settings across different devices that share the same build overlay. Any property can be overridden from - src/platform2/power_manager/default_prefs or src/platform2/power_manager/optional_prefs For details about each setting property, see - src/platform2/power_manager/common/power_constants.h For examples on setting these properties (including multiline examples), see the power config example in libcros_config/test.yaml |
| powerd-prefs | string |  | False |  | False | Powerd config that should be used. |
| regulatory-label | string |  | False |  | False | Base name of the directory containing the regulatory label files to show on this device. |
| test-label | string |  | False |  | False | Test alias (model) label that will be applied in Autotest and reported for test results. |
| thermal | [thermal](#thermal) |  | False |  | False |  |
| touch | [touch](#touch) |  | False |  | False |  |
| ui | [ui](#ui) |  | False |  | False |  |
| wallpaper | string |  | False |  | False | Base filename of the default wallpaper to show on this device. |
| wifi | [wifi](#wifi) |  | False |  | False | Sets limits on maximum WiFi transmit power for tablet and non-tablet device configurations. Currently only used for QCA ath10k WiFi driver. |

### arc
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| build-properties | [build-properties](#build-properties) |  | False |  | False |  |
| files | array - [files](#files) |  | False |  | True |  |
| scale | integer |  | False |  | False | The screen density value in dpi that will be used for ARC apps. This value should be from the list of DPIs in android cdd. |

### build-properties
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| device | string |  | False |  | False | Device name to report in 'ro.product.device'. This is often '{product}_cheets' but it can be something else if desired. |
| first-api-level | string |  | False |  | False | The first Android API level that this model shipped with.  |
| marketing-name | string |  | False |  | False | Name of this model as it is called in the market, reported in 'ro.product.model'. This often starts with '{oem}'. |
| metrics-tag | string |  | False |  | False | Tag to use to track metrics for this model. The tag can be shared across many devices if desired, but this will result in larger granularity for metrics reporting.  Ideally the metrics system should support collation of metrics with different tags into groups, but if this is not supported, this tag can be used to achieve the same end.  This is reported in 'ro.product.metrics.tag'. |
| oem | string |  | False |  | False | Original Equipment Manufacturer for this model. This generally means the OEM name printed on the device. |
| pai-regions | string | ```(^([a-zA-Z0-9\.\-]+,)*[a-zA-Z0-9\.\-]+$)|(^\*$)``` | False |  | False | (Optional) Comma-separated allow list of region codes that can be appended to 'ro.oem.key1' for the purpose of targeting Play Auto Install applications by region. The value(s) should match the values that would be returned by `cros_region_data region_code` for the relevant region(s). If the device's region code is not in the allow list, or if there is no allow list, 'ro.oem.key1' will not include the region code. The allow list can also be a single '*' character to indicate that the region code should always be appended.  |
| product | string |  | False |  | False | Product name to report in 'ro.product.name'. This may be the device name, or it can be something else, to allow several devices to be grouped into one product. |

### files
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| destination | string |  | False |  | True | Installation path for the file on the system image. |
| source | string |  | False |  | True | Source of the file relative to the build system. |

### audio
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| main | [main](#main) |  | True |  | False |  |

### main
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| cras-config-dir | string |  | True |  | False | Subdirectory for model-specific configuration. |
| disable-profile | string |  | False |  | False | Optional --disable_profile parameter for CRAS deamon. |
| files | array - [files](#files) |  | False |  | True |  |
| ucm-suffix | string |  | False |  | False | Optional UCM suffix used to determine model specific config. |

### files
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| destination | string |  | False |  | True | Installation path for the file on the system image. |
| source | string |  | False |  | True | Source of the file relative to the build system. |

### bluetooth
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| config | [config](#config) |  | True |  | False |  |
| flags | [flags](#flags) |  | False |  | False |  |

### config
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| build-path | string |  | True |  | True | Source of the file relative to the build system. |
| system-path | string |  | True |  | False | Installation path for the file on the system image. |

### flags
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| enable-suspend-management | boolean |  | False |  | False | Enable powerd suspend management callbacks. |
| reset-on-resume | boolean |  | False |  | False | Expect bluetooth chip to have reset on resume. |
| stop-on-suspend | boolean |  | False |  | False | Stop the bluetooth adapter on suspend and start it on resume. |

### camera
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| clock | string |  | False |  | False | Specified the camera clock on the model. |
| config-path | string |  | False |  | False | Specified the camera configuration file path on the model. |
| count | integer |  | False |  | False | Specified the number of cameras on the model. |
| legacy-usb | boolean |  | False |  | False | Indicates if the device has legacy usb cameras. |

### cros-healthd
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| battery | [battery](#battery) |  | False |  | False |  |
| cached-vpd | [cached-vpd](#cached-vpd) |  | False |  | False |  |

### battery
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| has-smart-battery-info | boolean |  | False |  | False |  |

### cached-vpd
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| has-sku-number | boolean |  | False |  | False |  |

### demo-mode
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| highlights-extension-id | string |  | False |  | False | The Chrome extension ID of the highlights app used during demo mode. |
| screensaver-extension-id | string |  | False |  | False | The Chrome extension ID of the attract loop played during demo mode. |

### detachable-base
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| ec-image-name | string |  | False |  | False | The target EC binary name which is placed under /lib/firmware. |
| files | array - [files](#files) |  | False |  | True |  |
| product-id | integer |  | False |  | False | The Product ID of the detachable base. This value can be queried by command 'lsusb'. By taking this as an example: Bus 001 Device 032: ID 18d1:503c Google Inc. the product-id is 20540(=0x503c). |
| touch-image-name | string |  | False |  | False | The touchpad binary name which is placed under /lib/firmware. This is only needed if the detachable base contains touchpad. |
| usb-path | string |  | False |  | False | Searches and finds the idVendor and idProduct under sysfs /sys/bus/usb/devices/* which matches the vendor-id and product-id. By taking this as an example: '/sys/bus/usb/devices/1-1.1' The usb-path is '1-1.1'. |
| vendor-id | integer |  | False |  | False | The Vendor ID of the detachable base. This value can be queried by command 'lsusb'. By taking this as an example: Bus 001 Device 032: ID 18d1:503c Google Inc. the vendor-id is 6353(=0x18d1). |

### files
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| destination | string |  | False |  | True | Installation path for the file on the system image. |
| source | string |  | False |  | True | Source of the file relative to the build system ${FILESDIR} |
| symlink | string |  | False |  | True | Symlink file that will be installed pointing to the destination. |

### fingerprint
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| board | string |  | False |  | False | Specifies the fingerprint board in use. |
| fingerprint-sensor-type | string |  | False |  | False | Type of FP sensor. Currently describes whether FP is overlapped on the power button or not. |
| sensor-location | string |  | False |  | False | Specifies the location of the fingerprint sensor. |

### firmware
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| bcs-overlay | string |  | False |  | True | BCS overlay path used to determine BCS file path for binary firmware downloads. |
| build-targets | [build-targets](#build-targets) |  | False |  | True |  |
| ec-ro-image | string |  | False |  | True | Name of the file located in BCS under the respective bcs-overlay. |
| firmware-config | integer |  | False |  | False | The firmware config bitmap to be flashed to the CBI. This field is used in the factory. |
| image-name | string |  | False |  | False | The name of the firmware image used by the firmware updater. Typically the device name, but can differ when a device may have two or more different firmware images. |
| key-id | string |  | False |  | True | Key ID from the signer key set that is used to sign the given firmware image. |
| main-ro-image | string |  | False |  | True | Name of the file located in BCS under the respective bcs-overlay. |
| main-rw-image | string |  | False |  | True | Name of the file located in BCS under the respective bcs-overlay. |
| name | string |  | False |  | True | This is a human-recognizable name used to refer to the firmware. It will be used when generating the shellball via firmware packer. Mainly, this is only for compatibility testing with device tree (since DT allowed firmwares to be named). |
| no-firmware | boolean |  | False |  | True | If present this indicates that this model has no firmware at present. This means that it will be omitted from the firmware updater (chromeos-firmware- ebuild) and it will not be included in the signer instructions file sent to the signer. This option is often useful when a model is first added, since it may not have firmware at that point. |
| pd-ro-image | string |  | False |  | True | Name of the file located in BCS under the respective bcs-overlay. |

### build-targets
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| base | string |  | False |  | True | Build target of the base EC firmware for a detachable device, that will be considered dirty when building/testing |
| coreboot | string |  | False |  | True | Build target that will be considered dirty when building/testing locally. |
| depthcharge | string |  | False |  | True | Build target that will be considered dirty when building/testing locally. |
| ec | string |  | False |  | True | Build target that will be considered dirty when building/testing locally. |
| ec_extras | array - string |  | False |  | True | Extra EC build targets to build within chromeos-ec. |
| gsc | string |  | False |  | True | Build target that will be considered dirty when building/testing locally. |
| ish | string |  | False |  | True | Build target that will be considered dirty when building/testing locally. |
| libpayload | string |  | False |  | True | Build target that will be considered dirty when building/testing locally. |
| u-boot | string |  | False |  | True | Build target that will be considered dirty when building/testing locally. |

### firmware-signing
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| key-id | string |  | True |  | True | Key ID from the signer key set that is used to sign the given firmware image. |
| sig-id-in-customization-id | boolean |  | False |  | True | Indicates that this model cannot be decoded by the mapping table. Instead the model is stored in the VPD (Vital Product Data) region in the customization_id property. This allows us to determine the model to use in the factory during the finalization stage. Note that if the VPD is wiped then the model will be lost. This may mean that the device will revert back to a generic model, or may not work. It is not possible in general to test whether the model in the VPD is correct at run-time. We simply assume that it is. The advantage of using this property is that no hardware changes are needed to change one model into another. For example we can create 20 different whitelabel boards, all with the same hardware, just by changing the customization_id that is written into SPI flash. |
| signature-id | string |  | True |  | True | ID used to generate keys/keyblocks in the firmware signing output.  This is also the value provided to mosys platform signature for the updater4.sh script. |

### hardware-properties
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| display-type | string |  | False |  | False | Denotes the type of display this device contains. |
| has-base-accelerometer | boolean |  | False |  | False | Is there an accelerometer in the base of the device. |
| has-base-gyroscope | boolean |  | False |  | False | Is there a gyroscope in the base of the device. |
| has-base-light-sensor | boolean |  | False |  | False | Is there a light sensor in the base of the device. |
| has-base-magnetometer | boolean |  | False |  | False | Is there a magnetometer in the base of the device. |
| has-lid-accelerometer | boolean |  | False |  | False | Is there an accelerometer in the lid of the device. |
| has-lid-gyroscope | boolean |  | False |  | False | Is there a gyroscope in the lid of the device. |
| has-lid-light-sensor | boolean |  | False |  | False | Is there a light sensor in the lid of the device. |
| has-lid-magnetometer | boolean |  | False |  | False | Is there a magnetometer in the lid of the device. |
| has-touchscreen | boolean |  | False |  | False | Does the device have a touchscreen. |
| is-lid-convertible | boolean |  | False |  | False | Can the lid be rotated 360 degrees. |
| psu-type | string |  | False |  | False | Type of PSU the device has: - battery: the device has a battery intended for primary use - AC_primary: the device has a battery, but it is not intended for primary use - AC_only: the device has no battery - no_power: the device does not receive power in any direct manner (e.g., it is virtualized)  |
| stylus-category | string |  | False |  | False | Denotes the category of stylus this device contains. |

### identity
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| customization-id | string |  | False | x86 | False | 'customization_id' value set in the VPD for non-unibuild Zergs and Whitelabels. Deprecated for use in new products since 2017/07/26. |
| platform-name | string |  | False | x86 | False | Defines the name that is reported by 'mosys platform name' This is typically the reference design name with the first letter capitalized |
| sku-id | integer |  | False | x86 | False | SKU/Board strapping pins configured during board manufacturing. Minimum value: -0x1. Maximum value: 0x7fffffff. |
| smbios-name-match | string |  | False | x86 | False | [x86] Firmware name built into the firmware and reflected back out in the SMBIOS tables. |
| whitelabel-tag | string |  | False | x86 | False | 'whitelabel_tag' value set in the VPD, to add Whitelabel branding over an unbranded base model. |
| customization-id | string |  | False | ARM | False | 'customization_id' value set in the VPD for non-unibuild Zergs and Whitelabels. Deprecated for use in new products since 2017/07/26. |
| device-tree-compatible-match | string |  | False | ARM | False | [ARM] String pattern (partial) that is matched against the contents of /proc/device-tree/compatible on ARM devices. |
| platform-name | string |  | False | ARM | False | Defines the name that is reported by 'mosys platform name' This is typically the reference design name with the first letter capitalized |
| sku-id | integer |  | False | ARM | False | SKU/Board strapping pins configured during board manufacturing. Minimum value: -0x1. Maximum value: 0x7fffffff. |
| whitelabel-tag | string |  | False | ARM | False | 'whitelabel_tag' value set in the VPD, to add Whitelabel branding over an unbranded base model. |

### modem
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| firmware-variant | string |  | False |  | False | Variant of the modem firmware to be used. This value is read by modemfwd to match against the variant field of a firmware entry in a firmware manifest. In most cases, we simply use the model name as the value. |

### power
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| [ANY] | N/A | N/A | N/A | N/A | N/A | This type allows additional properties not governed by the schema. See the type description for details on these additional properties.|
| allow-ambient-eq | string | ```^[01]$``` | False |  | False | Enable (1) or disable (0) Ambient EQ. |
| touchpad-wakeup | string | ```^[01]$``` | False |  | False | Enable (1) or disable (0) wake from touchpad. |

### thermal
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| dptf-dv | string |  | False |  | False | System image path to the .dv file containing DPTF (Dynamic Platform and Thermal Framework) settings. |
| files | array - [files](#files) |  | True |  | True |  |

### files
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| destination | string |  | False |  | True | Installation path for the file on the system image. |
| source | string |  | False |  | True | Source of the file relative to the build system. |

### touch
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| files | array - [files](#files) |  | False |  | True |  |
| present | string |  | False |  | False | Whether touch is present or needs to be probed for. |
| probe-regex | string |  | False |  | False | If probe is set, the regex used to look for touch. |

### files
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| destination | string |  | False |  | True | Installation path for the file on the system image. |
| source | string |  | False |  | True | Source of the file relative to the build system ${FILESDIR} |
| symlink | string |  | False |  | True | Symlink file that will be installed pointing to the destination. |

### ui
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| power-button | [power-button](#power-button) |  | False |  | False |  |
| side-volume-button | [side-volume-button](#side-volume-button) |  | False |  | False | Defines the position of the side volume button. `region` indicates whether the button is at the side of the "screen" or "keyboard" of the device. `side` indicates which edge the button is anchored to while the device in landscape primary screen orientation. It can be "left", "right", "top", "bottom". |

### power-button
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| edge | string |  | False |  | False |  |
| position | string |  | False |  | False |  |

### side-volume-button
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| region | string |  | False |  | False |  |
| side | string |  | False |  | False |  |

### wifi
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| non-tablet-mode-power-table | [non-tablet-mode-power-table](#non-tablet-mode-power-table) |  | False |  | False | WiFi power chain. Currently only used for QCA ath10k drivers. |
| tablet-mode-power-table | [tablet-mode-power-table](#tablet-mode-power-table) |  | False |  | False | WiFi power chain. Currently only used for QCA ath10k drivers. |

### non-tablet-mode-power-table
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| limit-2g | integer |  | False |  | False | 2G band power limit (dBm) Minimum value: 0x0. Maximum value: 0xff. |
| limit-5g | integer |  | False |  | False | 5G band power limit (dBm) Minimum value: 0x0. Maximum value: 0xff. |

### tablet-mode-power-table
| Attribute | Type   | RegEx     | Required | Oneof Group | Build-only | Description |
| --------- | ------ | --------- | -------- | ----------- | ---------- | ----------- |
| limit-2g | integer |  | False |  | False | 2G band power limit (dBm) Minimum value: 0x0. Maximum value: 0xff. |
| limit-5g | integer |  | False |  | False | 5G band power limit (dBm) Minimum value: 0x0. Maximum value: 0xff. |


[](end_definitions)

## Usage Instructions

## Adding and testing new properties

Before starting, `cros_workon` the following:

```bash
(chroot) $ cros_workon --host start chromeos-config-host
(chroot) $ cros_workon --board=BOARD start chromeos-config-bsp chromeos-config
```

To introduce a new property, first add its definition to the schema:

```bash
chromeos-config/cros_config_host/cros_config_schema.yaml
```

Then update the `README.md` automatically via (unit tests will check this):

```bash
(chroot) $ python -m cros_config_host.generate_schema_doc -o README.md
```

To install the updated schema, run:

```bash
(chroot) $ FEATURES=test sudo -E emerge chromeos-config-host
```

To use the new property, update your respective YAML source file. e.g.
`overlay-${BOARD}-private/chromeos-base/chromeos-config-bsp/files/model.yaml`

To install the changes, run:

```bash
(chroot) $ emerge-${BOARD} chromeos-config-bsp chromeos-config
```

At this point the updated config is located at:

```bash
/build/${BOARD}/usr/share/chromeos-config/yaml/config.yaml
```

To query your new item run the test command in the chroot:

```bash
(chroot) $  cros_config_host -c /build/${BOARD}/usr/share/chromeos-config/yaml/config.yaml -m <MODEL> get </path/to/property> <property name>
```

For instance:

```bash
(chroot) $ cros_config_host -c /build/coral/usr/share/chromeos-config/yaml /config.yaml -m robo360 get /firmware key-id
(chroot) $ cros_config_host -c /build/coral/usr/share/chromeos-config/yaml /config.yaml list-models
```

## Device Testing

To test configuration changes on actual devices use the `platform.CrosConfig`
Tast test. This will run `cros_config` tests for unibuilds and mosys
for all devices.
See [HOWTO](https://chromium.googlesource.com/chromiumos/platform/tast-tests/+/HEAD/src/chromiumos/tast/local/bundles/cros/platform/cros_config.md).
