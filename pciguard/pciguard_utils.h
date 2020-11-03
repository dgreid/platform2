// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PCIGUARD_PCIGUARD_UTILS_H_
#define PCIGUARD_PCIGUARD_UTILS_H_

#include <base/files/file_util.h>

namespace pciguard {

int OnInit(void);
int DeauthorizeAllDevices(void);
int AuthorizeThunderboltDev(base::FilePath devpath);
int AuthorizeAllDevices(void);
int DenyNewDevices(void);

}  // namespace pciguard

#endif  // PCIGUARD_PCIGUARD_UTILS_H_
