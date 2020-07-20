# Chrome OS DLC Developer Guide

## Introduction

This guide describes how to use Chrome OS DLC (Downloadable Content).
DLC allows Chrome OS developers to ship a feature (e.g. a Chrome OS package) to
stateful partition as packages and provides a way to download it at runtime
onto the device. If you‘re looking for detailed information about how to do
this, you’re in the right place.

### Chrome OS DLC vs Chrome OS package (ebuild)

Chrome OS DLC is an extension of the Chrome OS package (ebuild).

*   **Development** Similar to creating a package, creating a DLC requires
    creating an ebuild file ([Create a DLC]). Modifying a DLC requires upreving
    the ebuild like when modifying a Chrome OS package.
*   **Location** A Package is usually located in the root filesystem partition.
    A DLC is downloaded at runtime and located in the stateful partition.
*   **Install/Update** Packages can not be updated or installed independently
    and can only be updated via Autoupdate (as part of the root filesystem
    partition). A DLC can be installed independently. Installation of a
    DLC is handled by a daemon service ([dlcservice]) which takes D-Bus method
    calls (Install, Uninstall, etc.).
*   **Update payload/artifacts** All the package updates are embedded in a
    monolithic payload (a.k.a root filesystem partition) and are downloadable
    from Omaha. Each DLC has its own update (or install) payload and
    is downloadable from Omaha. Chrome OS infrastructure automatically handles
    packaging and serving of DLC payloads.

### Organization and Content

The workflow of a DLC developer involves following few tasks:

* [Enable DLC for your board]
* [Create a DLC]
* [Write platform code to request DLC]
* [Install a DLC for dev/test]
* [Write tests for a DLC]

## Enable DLC for your board

To enable DLCs for your board, you have to add the `USE flags` `dlc` and
`dlc_test` to the overlay of the board you are building for.
If DLC was not previously enabled for your board, you will have to build and
flash a new image to your DUT before deploying your first DLC, since the
`dlcservice` user permissions are not yet in devices without DLC support.

See example: [overlay-eve make.defaults]

## Create a DLC

Introducing a DLC into Chrome OS involves adding an ebuild. The ebuild
file should inherit [dlc.eclass]. Within the ebuild file the following
variables should be set:

Required:
*   ` DLC_PREALLOC_BLOCKS` - The storage space (in the unit of number of blocks,
    block size is 4 KB) the system reserves for a copy of the DLC image.
    Note that on device we reserve 2 copies of the image so the actual
    space consumption doubles. It is necessary to set this value more than the
    minimum required at the launch time to accommodate future size growth
    (recommendation is 130% of the DLC size).

Optional (Add these only if you kow exactly what you are doing otherwise do not
add them):
*    `DLC_ID` - Unique ID. Format of an ID has a few restrictions:
     *    It should not be empty.
     *    It should only contain alphanumeric characters (a-zA-Z0-9) and `-`
          (dash).
     *    The first letter cannot be dash.
     *    No underscore.
     *    It has a maximum length of 40 characters.
    (Default is `${PN}`)
    (check out [imageloader_impl.cc] for the actual implementation).
*   `DLC_PACKAGE` - Its format restrictions are the same as `DLC_ID`. Note:
    This variable is here to help enable multiple packages support for DLC in
    the future (allow downloading selectively a set of packages within one
    DLC). When multiple packages are supported, each package should have a
    unique name among all packages in a DLC.
    (Default is `package`)
*   `DLC_NAME` - Name of the DLC.
    It is for description/info purpose only.
    (Default is `${PN}`)
*   `DLC_VERSION` - Version of the DLC.
    It is for description/info purpose only.
    (Default is `${PVR}`)
*   `DLC_PRELOAD` - Preloading DLC.
    When set to true, the DLC will be preloaded during startup of dlcservice
    for test images.
    (Default is false)
*   `DLC_ENABLED` - Override being a DLC.
    When set to false, `$(dlc_add_path)` will not modify the path and everything
    will be installed into the rootfs instead of the DLC path. This allows the
    use of the same ebuild file to create a DLC under special conditions (i.e.
    Make a package a DLC for certain boards or install in rootfs for others).
    (Default is true)
*   `DLC_USED_BY` - Defines who is the user of this DLC. This is
    primarily used by DLCs that have visibility and privacy issues among users
    on a device, and setting this flag allows us to properly do a ref-count of
    the users of this DLC and show proper UI/confirmations to address these
    issues. Acceptable values are "user" and "system".
    (Default is "system")

Within the build file, the implementation should include at least the
`src_install` function. Within `src_install`, all the DLC content should be
installed using the special path prefix set by `$(dlc_add_path )`. This means,
that before installing any DLC files, you have to add the dlc prefix path to
`into, insinto` and `exeinto` using `$(dlc_add_path your_path)`.

See an example of a DLC ebuild: [dummy-dlc]

To avoid generating large tarballs with all the DLC build files, a configuration
file should be added under
src/third_party/chromiumos-overlay/chromeos/config/env/\[name of ebuild\]. This
reduces the build time and disk space used by the build.

See the example: [dummy-dlc config]

## Write platform code to request DLC

A DLC is downloaded (from Omaha server) and installed at runtime by dlcservice.
No feature should rely on the existence of a DLC and thus needs to request
(install) the DLC from dlcservice before using the DLC. Once a DLC is
installed, dlcservice keeps it available and mounted. The DLC will remain
mounted as long as the device or UI does not restart.

Chrome (and other system daemons that can access D-Bus) calls the dlcservice API
to install/uninstall a DLC. For calling the dlcservice API inside Chrome,
use [system_api] to send API calls. For calling dlcservice API outside of
Chrome, use generated D-Bus bindings.

If your daemon uses minijail, you will have to:
*   Bind mount `/run/imageloader/` by passing the parameter
    `-b /run/imageloader/` to minijail.
*   Set the parameters `-v -Kslave` to allow propagation of the mount namespace
    of the mounted DLC image to your service.
*   Depending on your current Seccomp filters, you might have to add some
    permissions to it. See [sandboxing].

On a locally built test build|image, calling dlcservice API does not download
the DLC (no DLC is being served). You need to
[Install a DLC for dev/test] before calling dlcservice API.

## Install a DLC for dev/test

Installing a Chrome OS DLC on a device is similar to installing a Chrome
OS package:

*   Build the DLC: `emerge-${BOARD} chromeos-base/demo-dlc`
*   Build DLC image and copy the DLC to device:
    `cros deploy ${IP} chromeos-base/demo-dlc`

## Write tests for a DLC

In order to test a DLC, the optional variable field `DLC_PRELOAD` should be set
to true while the integration/tast tests invoke installing the DLC.

## Frequently Asked Questions

### How do I set up the DLC download server (upload artifacts, manage releases, etc.)?

All you need is to add a DLC and our infrastructure will automatically build
and release the DLC.

### Can I release my DLC at my own schedule?

No. Since the release is automatic, no one can release a DLC out of band and
each version of a DLC image is tied to the version of the OS image.

### How do I update my DLC?

Modifying a Chrome OS DLC is the same as modifying a Chrome OS package (ebuild).
A DLC is updated at the same time the device itself is updated.

[dlcservice]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/dlcservice
[Enable DLC for your board]: #Enable-DLC-for-your-board
[Create a DLC]: #Create-a-DLC
[Write platform code to request DLC]: #Write-platform-code-to-request-DLC
[Install a DLC for dev/test]: #install-a-dlc-for-devtest
[Write tests for a DLC]: #Write-tests-for-a-DLC
[dlc.eclass]: https://chromium.googlesource.com/chromiumos/overlays/chromiumos-overlay/+/master/eclass/dlc.eclass
[sandboxing]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/sandboxing.md
[system_api]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/system_api
[imageloader_impl.cc]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/imageloader/imageloader_impl.cc
[tast]: go/tast
[tast-deps]: go/tast-deps
[dummy-dlc]: https://chromium.googlesource.com/chromiumos/overlays/chromiumos-overlay/+/HEAD/chromeos-base/dummy-dlc/dummy-dlc-1.0.0.ebuild
[dummy-dlc config]: https://chromium.googlesource.com/chromiumos/overlays/chromiumos-overlay/+/HEAD/chromeos/config/env/chromeos-base/dummy-dlc
[overlay-eve make.defaults]: https://chromium.googlesource.com/chromiumos/overlays/board-overlays/+/HEAD/overlay-eve/profiles/base/make.defaults
