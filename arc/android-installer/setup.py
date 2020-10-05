# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Sets up Android Installer for Chrome OS"""

from distutils import core


NAME = 'Android Installer'
DESCRIPTION = 'Android Installer for Chrome OS'
LICENSE = 'BSD-Google'


core.setup(
    name=NAME,
    description=DESCRIPTION,
    license=LICENSE,
    package_dir={'android_installer': 'android_installer'},
    packages=['android_installer'],
    scripts=['bin/android-installer']
)
