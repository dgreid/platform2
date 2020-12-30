// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/mount_options.h"

#include <sys/mount.h>

#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace cros_disks {

using testing::ElementsAre;

TEST(MountOptionsTest, IsReadOnlyMount) {
  EXPECT_FALSE(IsReadOnlyMount({}));
  EXPECT_FALSE(IsReadOnlyMount({"foo", "bar"}));
  EXPECT_TRUE(IsReadOnlyMount({"ro"}));
  EXPECT_FALSE(IsReadOnlyMount({"ro", "rw"}));
  EXPECT_TRUE(IsReadOnlyMount({"foo", "ro", "bar", "rw", "ro", "baz"}));
}

TEST(MountOptionsTest, GetParamValue) {
  std::string value;
  EXPECT_FALSE(GetParamValue({}, "foo", &value));
  EXPECT_TRUE(GetParamValue({"a=b", "foo=bar", "baz", "x=y"}, "foo", &value));
  EXPECT_EQ("bar", value);
  EXPECT_FALSE(GetParamValue({"foo"}, "foo", &value));
  EXPECT_TRUE(GetParamValue({"foo=bar", "foo=baz"}, "foo", &value));
  EXPECT_EQ("baz", value);
}

TEST(MountOptionsTest, SetParamValue) {
  std::vector<std::string> params;
  SetParamValue(&params, "foo", "bar");
  SetParamValue(&params, "baz", "");
  EXPECT_THAT(params, ElementsAre("foo=bar", "baz="));
}

}  // namespace cros_disks
