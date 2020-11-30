// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <brillo/process/process.h>
#include <vboot/crossystem.h>

#include "installer/inst_util.h"
#include "installer/slow_boot_notify.h"

using std::string;
using std::vector;

void ExtractFspm(const string& partition, const base::FilePath& fspm_path) {
  if (partition != "A" && partition != "B") {
    printf("%s - unsupported partition %s\n", __func__, partition.c_str());
    return;
  }

  base::FilePath fw_bin_path;
  if (!CreateTemporaryFile(&fw_bin_path))
    return;

  vector<string> cmd = {"/usr/sbin/flashrom",
                        "-p",
                        "host",
                        "-r",
                        "-i",
                        "FW_MAIN_" + partition + ":" + fw_bin_path.value()};
  int result;
  if ((result = RunCommand(cmd))) {
    printf("%s: Error reading FW_MAIN_%s %d\n", __func__, partition.c_str(),
           result);
    base::DeleteFile(fw_bin_path);
    return;
  }

  cmd = {"/usr/bin/cbfstool", fw_bin_path.value(),
         "extract",           "-n",
         "fspm.bin",          "-f",
         fspm_path.value()};
  if ((result = RunCommand(cmd)))
    printf("%s: Error extracting FSPM from FW_MAIN_%s - %d\n", __func__,
           partition.c_str(), result);

  base::DeleteFile(fw_bin_path);
}

void SlowBootNotifyPreFwUpdate(const base::FilePath& fspm_main) {
  char partition[VB_MAX_STRING_PROPERTY];

  if (!VbGetSystemPropertyString("mainfw_act", partition, sizeof(partition)))
    return;

  ExtractFspm(partition, fspm_main);
}

void SlowBootNotifyPostFwUpdate(const base::FilePath& fspm_next) {
  // After firmware update, get the ID of the new partition/region. If there is
  // no firmware update, region returned by fw_try_next is the same as
  // mainfw_act.
  const char* partition = VbGetSystemPropertyString("fw_try_next", NULL, 0);
  ExtractFspm(partition, fspm_next);
}

bool SlowBootNotifyRequired(const base::FilePath& fspm_main,
                            const base::FilePath& fspm_next) {
  // Enable slow boot notification only if FSPMs are different. Reduce
  // notification noise if one/both of the FSPMs don't exist (due to unforeseen
  // errors).
  if (base::PathExists(fspm_main) && base::PathExists(fspm_next) &&
      !ContentsEqual(fspm_main, fspm_next)) {
    printf("%s: Slow boot notification enabled\n", __func__);
    return true;
  }

  printf("%s: Slow boot notification disabled\n", __func__);
  return false;
}
