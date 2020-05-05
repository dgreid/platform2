// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/partner.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace typecd {

namespace {

// Some SVIDs and VDOs we can use to populate the AltMode list.
const uint16_t kDPSvid = 0xff01;
const uint32_t kDPVdo = 0x1c46;
const uint16_t kTBTSvid = 0x8087;
const uint32_t kTBTVdo = 0x1;

MATCHER_P2(AltModeMatcher, svid, vdo, "") {
  return svid == arg->svid_ && vdo == arg->vdo_;
}

}  // namespace

class PartnerTest : public ::testing::Test {};

// Check that calls of AddAltMode() done explicitly function correctly.
TEST_F(PartnerTest, TestAltModeManualAddition) {
  Partner p;

  p.AddAltMode(0, kDPSvid, kDPVdo);
  p.AddAltMode(1, kTBTSvid, kTBTVdo);

  // Bogus index.
  EXPECT_EQ(nullptr, p.GetAltMode(2));

  // Retrieve an altmode pointer, given an index.
  AltMode* altmode = p.GetAltMode(1);
  EXPECT_THAT(altmode, AltModeMatcher(kTBTSvid, kTBTVdo));

  EXPECT_THAT(2, p.alt_modes_.size());
}

}  // namespace typecd
