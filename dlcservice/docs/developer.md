# Chrome OS DLC Developer Guide

## Introduction

This guide describes how to use Chrome OS DLC (Downloadable Content).
DLC allows Chrome OS developers to ship a feature (e.g. a Chrome OS package) to
stateful partition as a separate module and provides a way to download it at
runtime on device. If you‘re looking for detailed information about how to do
this, you’re in the right place.

[TOC]

### Chrome OS DLC module vs Chrome OS package

The most similar concept to Chrome OS DLC is the Chrome OS package. But here
are the differences:

*   **Development** Similar to creating a package, creating a DLC module
    requires creating an ebuild file ([Create a DLC module]). To modify a DLC
    module, it requires upreving the DLC ebuild (after DLC content is updated,
    also similar to modifying a Chrome OS package).
*   **Location** Packages are usually located in the root filesystem partition.
    DLC modules are downloaded at runtime and thus are located in the stateful
    partition.
*   **Install/Update** Packages can not be updated or installed independently
    and can only be updated via Autoupdate (as part of the root filesystem
    partition). DLC modules can be installed independently. Installation of a
    DLC module on device is handled by a daemon service ([dlcservice]) which
    takes D-Bus method calls (Install, Uninstall, etc.).
*   **Update payload/artifacts** All the package updates are embedded in a
    monolithic payload (a.k.a root filesystem partition) and are downloadable
    from Omaha. Each DLC module has its own update (or install) payload and
    is downloadable from Omaha. Chrome OS infrastructure automatically handles
    packaging and serving of DLC payloads.

### Organization and Content

The workflow of a DLC developer involves following few tasks:

* [Create a DLC module]
* [Write platform code to request DLC module]
* [Install a DLC module for dev/test]
* [Write tests for a DLC module]

## Create a DLC module

Introducing a DLC module into Chrome OS involves adding an ebuild. The ebuild
file should inherit [dlc.eclass]. Within the ebuild file the following
variables should be set:

*   `DLC_NAME` - Name of the DLC. It is for description/info purpose only and
    can be empty (but not encouraged).
*   `DLC_VERSION` - Version of the DLC module. It is for description/info
    purpose only and can be empty. By default you can set it to `${PV}`.
*   `DLC_PREALLOC_BLOCKS` - The storage space (in the unit of number of blocks,
    block size is 4 KB) the system reserves for a copy of the DLC module
    image. Note that on device we reserve 2 copies of the image so the actual
    space consumption doubles. It is necessary to set this value more than the
    minimum required at the launch time to accommodate future size growth
    (recommendation is 130% of the DLC module size).
*    `DLC_ID` - Unique ID of the DLC module (among all DLC modules). Format of
     an ID has a few restrictions:
	 *    It should not be empty.
	 *    It should only contain alphanumeric characters (a-zA-Z0-9) and `-`
          (dash).
	 *    The first letter cannot be dash.
	 *    No underscore.
	 *    It has a maximum length of 40 characters.
	 (check out [imageloader_impl.cc] for the actual implementation).
*    `DLC_PACKAGE` - Its format restrictions are the same as `DLC_ID`. Note:
     This variable is here to help enable multiple packages support for DLC in
     the future (allow downloading selectively a set of packages within one
     DLC). When multiple packages are supported, each package should have a
     unique name among all packages in a DLC module.
*    `DLC_ARTIFACT_DIR` - The path to the directory which contains all the
     content to be packed into the DLC image. See the ebuild example below for
     more details about it.

Within the build file, the implementation should include at least the
`src_install` function. Within `src_install`, all the DLC content should be
copied to `${DLC_ARTIFACT_DIR}`. Do **NOT** use ebuild functions (`doins`,
`dobin`, etc.) to install anything unless you know what you are doing.

Here is an example of a DLC ebuild (ebuild name:
`chromeos-base/demo-dlc-1.0.0.ebuild`):

```
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Distributed under the terms of the GNU General Public License v2

EAPI=6
inherit dlc
DESCRIPTION="A demo DLC"
HOMEPAGE=""
SRC_URI=""

LICENSE="BSD-Google"
SLOT="0"
KEYWORDS="*"
IUSE="dlc"
REQUIRED_USE="dlc"

DLC_NAME="A demo DLC"
DLC_VERSION="${PV}"
DLC_PREALLOC_BLOCKS="1024"
DLC_ID="demo-dlc"
DLC_PACKAGE="demo-dlc-package"
DLC_ARTIFACT_DIR="${T}/artifacts"

src_install() {
  # Create ${DLC_ARTIFACT_DIR} if it does not exist.
  mkdir -p ${DLC_ARTIFACT_DIR}

  # Copy DLC content to ${DLC_ARTIFACT_DIR} if it is not already there.
  echo "Hello World!" >> ${DLC_ARTIFACT_DIR}/one_file_dlc.txt

  # Install DLC to the build directory.
  dlc_src_install
}
```

## Write platform code to request DLC module

A DLC module is downloaded (from the Internet) and installed at runtime by
dlcservice. Any feature should not rely on the existence of a DLC module and
thus has to request (install) the DLC module from dlcservice before using the
DLC. Once a DLC module is installed, dlcservice keeps it available and mounted
across device reboot and update.

Chrome (and other system daemons that can access D-Bus) calls the dlcservice API
to install/uninstall a DLC module. For calling the dlcservice API inside Chrome,
use [system_api] to send API calls. For calling dlcservice API outside of
Chrome, use generated D-Bus bindings.

On a locally built test build|image, calling dlcservice API does not download
the DLC (no DLC is being served). You need to
[Install a DLC module for dev/test] before calling dlcservice API.

## Install a DLC module for dev/test

Installing a Chrome OS DLC module on a device is similar to installing a Chrome
OS package:

*   Build the DLC module: `emerge-${BOARD} chromeos-base/demo-dlc`
*   Copy the DLC module to device: `cros deploy ${IP} chromeos-base/demo-dlc`

## Write tests for a DLC module

Writing unit tests for a DLC module is equivalent to writing unit tests for a
Chrome OS package.
You can write integration tests for a DLC module using tast framework ([tast]).
We use Tast Test Dependencies ([tast-deps]) to run DLC integration tests on
devices with a DLC dependency satisfied. Specifically you need to introduce a
tast software dependency for your DLC ebuild (created in [Create a DLC module])
and let your test depend on the software dependency.

## Frequently Asked Questions

### How do I set up the DLC download server (upload artifacts, manage releases, etc.)?

All you need is to add a DLC module and our infrastructure automatically build
and release the DLC modules.

### Can I release my DLC at my own schedule?

No. Since the release is automatic, no one can release a DLC out of band and
each version of a DLC image is tied to the version of the OS image.

### How do I update my DLC?

Modifying a Chrome OS DLC is the same as modifying a Chrome OS package. On
user’s device, a DLC is updated at the same time when a device is updated.

[dlcservice]: https://chromium.googlesource.com/chromiumos/platform2/+/refs/heads/master/dlcservice
[Create a DLC module]: #Create-a-DLC-module
[Write platform code to request DLC module]: #Write-platform-code-to-request-DLC-module
[Install a DLC module for dev/test]: #Install-a-DLC-module-for-dev/test
[Write tests for a DLC module]: #Write-tests-for-a-DLC-module
[dlc.eclass]: https://chromium.googlesource.com/chromiumos/overlays/chromiumos-overlay/+/master/eclass/dlc.eclass
[system_api]: https://chromium.googlesource.com/chromiumos/platform2/+/refs/heads/master/system_api
[imageloader_impl.cc]: https://chromium.googlesource.com/chromiumos/platform2/+/refs/heads/master/imageloader/imageloader_impl.cc
[tast]: go/tast
[tast-deps]: go/tast-deps
