// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_TEST_UTILS_H_
#define TYPECD_TEST_UTILS_H_

#include <base/files/file_path.h>

namespace typecd {

// Helper function to create the sysfs entries for an alt mode, for testing
// purposes.
//
// Returns:
//   True on success, False otherwise.
bool CreateFakeAltMode(const base::FilePath& mode_path,
                       uint16_t svid,
                       uint32_t vdo,
                       uint32_t vdo_index);

}  // namespace typecd

#endif  // TYPECD_TEST_UTILS_H_
