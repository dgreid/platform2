# Overview

Runtime Probe is essentially a command line tool that consumes the
[probe syntax](https://chromium.googlesource.com/chromiumos/platform/factory/+/master/py/probe/README.md#detail-usage-the-syntax-of-a-probe-config-file)
and outputs the
[probe result](https://chromium.googlesource.com/chromiumos/platform/factory/+/master/py/probe/README.md#output-format).

This command line tool will gradually replace fields in HWID (i.e. less fields
will be encoded into HWID) as it reflects the status of a device in a more
timely approach. **We are not aiming to be a daemon service**, instead, we pose
ourselves as a one-shot call tool, just like the `ping` command, the D-Bus
method introduced later is also on-demand and expected to exit immediately after
one call.

Currently, the reported data is purely hardware information. For example, the
model of storage, the remaining capacity of battery, the screen resolution..etc.

To serve clients that are not able to call the command line directly, we offer a
simple, restricted D-Bus interface with only one method
`org.chromium.RuntimeProbe.ProbeCategories`. This D-Bus method follows the
[Chrome OS D-Bus Best Practices](https://chromium.googlesource.com/chromiumos/docs/+/master/dbus_best_practices.md#limit-use-of-d_bus-to-start-services-on_demand),
proper `minijail0`, `secomp policy` are applied inside
`dbus/org.chromium.RuntimeProbe.conf`.

# Motivation

To better reflect hardware configuration on users’ system, we need a tool in
release image that is able to reflect the truth of the moment in the field.
Traditionally, this need has been satisfied by
[factory's probe framework](https://chromium.googlesource.com/chromiumos/platform/factory/+/master/py/probe/README.md),
because we assume rare components replacement after mass production. However, we
have seen more and more requests from partners to switch components after
devices left the factory process.

This work is also related to the evolution of HWID. Instead of going into
details on the complicated plan, we would like to use an example to let the
reader get a high level concept on how this work helps cases in HWID.

Let's look into a typical scenario here: After years' love into the Chromebook,
a user has one of the DRAM broken in slots, and would like to replace it.
However, the original part is already EOL (End Of Life), ODM has no choice but
to pick another part from the
[AVL (Approved Vendor List)](https://www.google.com/chromeos/partner/fe/#avl),
while this new DRAM is installed, the original probe result (HWID) in factory is
violated. Hence, even after the factory process, we would like to get the probe
result to reflect the truth of the moment under release image. Runtime is used
to convey the concept of dynamic.

# Contribute to Runtime Probe

## Environment

Runtime Probe has a significant different logic when working with
`cros_debug=0`, in order to test the behavior under so, you might try to setup a
device like the following:

*   CCD Unlock
*   Build locally a test image for your device and flash it to ensure your USE
    flags are aligned. Image is suggested to build with rootfs verification
    disabled in `build_image` step.

*   Flash the latest dev-signed firmware, for Googler, it might be easier to get
    it from
    [GoldenEye](https://cros-goldeneye.corp.google.com/chromeos/console/listFFBuild?type=firmware#).
    Suggest to use the following for flashing while keeping the VPD: `futility
    update --force -i $FILE`

*   Switch on/off developer mode

    Besides the
    [developer
    doc](https://chromium.googlesource.com/chromiumos/docs/+/master/developer_mode.md)
    describing how to enter/leave developer mode, following is another useful
    hack to prevent some long running check while switching mode for the first
    time.
    *   Before switching on developer mode
        * `touch /mnt/stateful_partition/.developer_mode`
        * `sync`
    *   Before switching off developer mode
        * `rm /mnt/stateful_partition/.developer_mode`
        * `sync`

*   Hack[^1] to keep developer tools (For example: python, emerge for `cros
    deploy`) while mocking `cros_debug=0`

    *   Switch off developer mode
    *   `sed -i 's/CROS_DEBUG=/CROS_DEBUG=1 #/g' /sbin/chromeos_startup`
    *   Reboot

*   Toggle the `cros_debug` to 0

    *   `cd /usr/share/vboot/bin/`
    *   `./make_dev_ssd.sh --save_config /tmp/foo --partitions 2`
    *   `sed -i 's/cros_debug//g' /tmp/foo.2`
    *   `./make_dev_ssd.sh --set_config /tmp/foo --partitions 2`

*   Toggle the `cros_debug` to 1

    *   `cd /usr/share/vboot/bin/`
    *   `./make_dev_ssd.sh --save_config /tmp/foo --partitions 2`
    *   `sed -i 's/cros_secure/cros_secure cros_debug/g' /tmp/foo.2`
    *   `./make_dev_ssd.sh --set_config /tmp/foo --partitions 2`


## Development

*   `cros_workon-$BOARD start runtime_probe`
*   `emerge-$BOARD runtime_probe`
*   `cros deploy $ACTIVE_DUT runtime_probe`
*   On the device:
    *   `runtime_probe --vebosity_level=3`
*   Inside cros_sdk: TODO(itspeter)

# Usage

The code is still underdevelopment, will update when it is ready for use.

<!-- Footnotes themselves at the bottom. -->

## Notes

[^1]: Subject to change based on the other program, after this quick hack, you
    will not able to switch to developer mode easily.
