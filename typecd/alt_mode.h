// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_ALT_MODE_H_
#define TYPECD_ALT_MODE_H_

namespace typecd {

// Simple struct used to represent the altmodes supported by a partner or cable.
struct AltMode {
  AltMode(uint16_t svid, uint32_t vdo) : svid_(svid), vdo_(vdo) {}

  uint16_t svid_;
  uint32_t vdo_;
};

}  // namespace typecd

#endif  //  TYPECD_ALT_MODE_H_
