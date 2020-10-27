// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_CROS_EC_UTIL_H_
#define TYPECD_CROS_EC_UTIL_H_

#include "typecd/ec_util.h"

namespace typecd {

// ECUtil implementation which communicates with the Chrome EC via debugfs.
// TODO(b/171725237): Implement this.
class CrosECUtil : public ECUtil {
 public:
  CrosEcUtil();

  bool ModeEntrySupported() override { return true; };
  bool EnterMode(int port, TypeCMode mode) override { return true; };
  bool ExitMode(int port) override { return true; };
};

}  // namespace typecd

#endif  // TYPECD_CROS_EC_UTIL_H_
