// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/flag_helper.h>

#include "installer/chromeos_install_config.h"
#include "installer/chromeos_postinst.h"

int main(int argc, char* argv[]) {
  DEFINE_string(type, "", "Install type, one of: postinst.");

  // postinst flags.
  DEFINE_string(bios, "", "Bios type, one of: secure, legacy, efi, and uboot.");
  DEFINE_string(install_dev, "", "Install device. e.g. /");
  DEFINE_string(install_dir, "", "Install directory. e.g. /tmp/blah");

  brillo::FlagHelper::Init(argc, argv, "cros_installer");

  if (FLAGS_type == "postinst") {
    // Unknown means we will attempt to autodetect later on.
    BiosType bios_type = kBiosTypeUnknown;
    if (!FLAGS_bios.empty() && !StrToBiosType(FLAGS_bios, &bios_type)) {
      LOG(ERROR) << "Invalid bios type: " << FLAGS_bios;
      return 1;
    }
    if (FLAGS_install_dev.empty()) {
      LOG(ERROR) << "--install_dev is empty.";
      return 1;
    }
    if (FLAGS_install_dir.empty()) {
      LOG(ERROR) << "--install_dir is empty.";
      return 1;
    }

    int exit_code = 0;
    if (!RunPostInstall(FLAGS_install_dev, FLAGS_install_dir, bios_type,
                        &exit_code)) {
      return exit_code ? exit_code : 1;
    }
  } else {
    LOG(ERROR) << "Invalid --type flag is passed.";
    return 1;
  }

  return 0;
}
