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

class MountOptionsTest : public ::testing::Test {
 protected:
  MountOptions mount_options_;
};

TEST_F(MountOptionsTest, IsReadOnlyOptionSet) {
  // default construction
  EXPECT_TRUE(mount_options_.IsReadOnlyOptionSet());

  // options: ro
  std::vector<std::string> options = {"ro"};
  mount_options_.Initialize(options, false, "", "");
  EXPECT_TRUE(mount_options_.IsReadOnlyOptionSet());

  // options: rw
  options = {"rw"};
  mount_options_.Initialize(options, false, "", "");
  EXPECT_FALSE(mount_options_.IsReadOnlyOptionSet());
}

TEST_F(MountOptionsTest, SetReadOnlyOption) {
  std::string expected_string_default = "ro";
  std::string expected_string_initialize = "ro,nodev,noexec,nosuid";

  // default construction
  mount_options_.SetReadOnlyOption();
  EXPECT_EQ(expected_string_default, mount_options_.ToString());

  // options: ro
  std::vector<std::string> options = {"ro"};
  mount_options_.Initialize(options, false, "", "");
  mount_options_.SetReadOnlyOption();
  EXPECT_EQ(expected_string_initialize, mount_options_.ToString());

  // options: rw
  options = {"rw"};
  mount_options_.Initialize(options, false, "", "");
  mount_options_.SetReadOnlyOption();
  EXPECT_EQ(expected_string_initialize, mount_options_.ToString());
}

TEST_F(MountOptionsTest, ToString) {
  std::vector<std::string> options;
  std::string expected_string;

  // default construction
  expected_string = "ro";
  EXPECT_EQ(mount_options_.ToString(), expected_string);
  EXPECT_EQ(mount_options_.ToFuseMounterOptions(), expected_string);

  // options: ro (default)
  expected_string = "ro,nodev,noexec,nosuid";
  mount_options_.Initialize(options, false, "", "");
  EXPECT_EQ(mount_options_.ToString(), expected_string);
  EXPECT_EQ(mount_options_.ToFuseMounterOptions(), expected_string);

  // options: ro (bind ignored)
  expected_string = "ro,nodev,noexec,nosuid";
  options.push_back("bind");
  mount_options_.Initialize(options, false, "", "");
  EXPECT_EQ(mount_options_.ToString(), expected_string);
  EXPECT_EQ(mount_options_.ToFuseMounterOptions(), expected_string);

  // options: ro, nodev
  expected_string = "ro,nodev,noexec,nosuid";
  options.clear();
  options.push_back("nodev");
  mount_options_.Initialize(options, false, "", "");
  EXPECT_EQ(mount_options_.ToString(), expected_string);
  EXPECT_EQ(mount_options_.ToFuseMounterOptions(), expected_string);

  // options: nodev, rw
  expected_string = "rw,nodev,noexec,nosuid";
  options.push_back("rw");
  mount_options_.Initialize(options, false, "", "");
  EXPECT_EQ(mount_options_.ToString(), expected_string);
  EXPECT_EQ(mount_options_.ToFuseMounterOptions(), expected_string);

  // options: nodev, rw, nosuid
  expected_string = "rw,nodev,noexec,nosuid";
  options.push_back("nosuid");
  mount_options_.Initialize(options, false, "", "");
  EXPECT_EQ(mount_options_.ToString(), expected_string);
  EXPECT_EQ(mount_options_.ToFuseMounterOptions(), expected_string);

  // options: nodev, rw, nosuid, noexec
  expected_string = "rw,nodev,noexec,nosuid";
  options.push_back("noexec");
  mount_options_.Initialize(options, false, "", "");
  EXPECT_EQ(mount_options_.ToString(), expected_string);
  EXPECT_EQ(mount_options_.ToFuseMounterOptions(), expected_string);

  // options: nodev, rw, nosuid, noexec, dirsync
  expected_string = "dirsync,rw,nodev,noexec,nosuid";
  options.push_back("dirsync");
  mount_options_.Initialize(options, false, "", "");
  EXPECT_EQ(mount_options_.ToString(), expected_string);
  EXPECT_EQ(mount_options_.ToFuseMounterOptions(), expected_string);

  // options: nodev, rw, nosuid, noexec, dirsync, sync
  expected_string = "dirsync,sync,rw,nodev,noexec,nosuid";
  options.push_back("sync");
  mount_options_.Initialize(options, false, "", "");
  EXPECT_EQ(mount_options_.ToString(), expected_string);
  EXPECT_EQ(mount_options_.ToFuseMounterOptions(), expected_string);

  // options: nodev, rw, nosuid, noexec, dirsync, sync
  // default uid=1000, gid=1001
  // ignore user and group ID
  expected_string = "dirsync,sync,rw,nodev,noexec,nosuid";
  mount_options_.Initialize(options, false, "1000", "1001");
  EXPECT_EQ(mount_options_.ToString(), expected_string);
  EXPECT_EQ(mount_options_.ToFuseMounterOptions(), expected_string);

  // options: nodev, rw, nosuid, noexec, dirsync, sync
  // default uid=1000, gid=1001
  expected_string = "dirsync,sync,rw,uid=1000,gid=1001,nodev,noexec,nosuid";
  mount_options_.Initialize(options, true, "1000", "1001");
  EXPECT_EQ(mount_options_.ToString(), expected_string);
  EXPECT_EQ(mount_options_.ToFuseMounterOptions(), expected_string);

  // options: nodev, rw, nosuid, noexec, dirsync, sync, uid=2000, gid=2001
  // default uid=1000, gid=1001
  // ignore user and group ID
  options.push_back("uid=2000");
  options.push_back("gid=2001");
  expected_string = "dirsync,sync,rw,nodev,noexec,nosuid";
  mount_options_.Initialize(options, false, "1000", "1001");
  EXPECT_EQ(mount_options_.ToString(), expected_string);
  EXPECT_EQ(mount_options_.ToFuseMounterOptions(), expected_string);

  // options: nodev, rw, nosuid, noexec, dirsync, sync, uid=2000, gid=2001
  // default uid=1000, gid=1001
  expected_string = "dirsync,sync,rw,uid=2000,gid=2001,nodev,noexec,nosuid";
  mount_options_.Initialize(options, true, "1000", "1001");
  EXPECT_EQ(mount_options_.ToString(), expected_string);
  EXPECT_EQ(mount_options_.ToFuseMounterOptions(), expected_string);

  // options: "nodev,dev"
  // ignore an option string containing a comma.
  expected_string = "ro,nodev,noexec,nosuid";
  options.clear();
  options.push_back("nodev,dev");
  mount_options_.Initialize(options, false, "", "");
  EXPECT_EQ(mount_options_.ToString(), expected_string);
  EXPECT_EQ(mount_options_.ToFuseMounterOptions(), expected_string);

  // Allow more options.
  expected_string = "dirsync,foo=mississippi,bar,ro,nodev,noexec,nosuid";
  options = {"dirsync", "bang", "foo=mississippi", "bar", "baz"};
  mount_options_.AllowOption("bar");
  mount_options_.AllowOptionPrefix("foo=");
  mount_options_.Initialize(options, false, "", "");
  EXPECT_EQ(mount_options_.ToString(), expected_string);
  EXPECT_EQ(mount_options_.ToFuseMounterOptions(), expected_string);

  // Force additional options.
  expected_string =
      "dirsync,foo=mississippi,bar,ro,nodev,noexec,nosuid,sheep=baa,zoo";
  options = {"dirsync", "bang", "foo=mississippi", "bar", "baz", "sheep=moo"};
  mount_options_.EnforceOption("sheep=baa");
  mount_options_.EnforceOption("zoo");
  mount_options_.Initialize(options, false, "", "");
  EXPECT_EQ(mount_options_.ToString(), expected_string);
  EXPECT_EQ(mount_options_.ToFuseMounterOptions(), expected_string);

  // Option nosymfollow should be reflected in ToString() but not
  // ToFuseMounterOptions().
  mount_options_.EnforceOption(MountOptions::kOptionNoSymFollow);
  mount_options_.Initialize(options, false, "", "");
  EXPECT_EQ(mount_options_.ToFuseMounterOptions(), expected_string);
  expected_string += ",nosymfollow";
  EXPECT_EQ(mount_options_.ToString(), expected_string);
}

TEST_F(MountOptionsTest, ToMountFlagsAndData) {
  std::vector<std::string> options;
  MountOptions::Flags expected_flags;
  MountOptions::Flags security_flags = MS_NODEV | MS_NOEXEC | MS_NOSUID;
  std::string expected_data;
  std::pair<MountOptions::Flags, std::string> flags_and_data;

  // default construction
  expected_flags = MS_RDONLY;
  expected_data = "";
  flags_and_data = mount_options_.ToMountFlagsAndData();
  EXPECT_EQ(expected_flags, flags_and_data.first);
  EXPECT_EQ(expected_data, flags_and_data.second);

  // options: ro (default)
  mount_options_.Initialize(options, false, "", "");
  expected_flags = security_flags | MS_RDONLY;
  flags_and_data = mount_options_.ToMountFlagsAndData();
  EXPECT_EQ(expected_flags, flags_and_data.first);
  EXPECT_EQ(expected_data, flags_and_data.second);

  // options: ro (bind ignored)
  options.push_back("bind");
  expected_flags = security_flags | MS_RDONLY;
  mount_options_.Initialize(options, false, "", "");
  flags_and_data = mount_options_.ToMountFlagsAndData();
  EXPECT_EQ(expected_flags, flags_and_data.first);
  EXPECT_EQ(expected_data, flags_and_data.second);

  // options: ro, nodev
  options.clear();
  options.push_back("nodev");
  expected_flags = security_flags | MS_RDONLY | MS_NODEV;
  mount_options_.Initialize(options, false, "", "");
  flags_and_data = mount_options_.ToMountFlagsAndData();
  EXPECT_EQ(expected_flags, flags_and_data.first);
  EXPECT_EQ(expected_data, flags_and_data.second);

  // options: nodev, rw
  options.push_back("rw");
  expected_flags = security_flags | MS_NODEV;
  mount_options_.Initialize(options, false, "", "");
  flags_and_data = mount_options_.ToMountFlagsAndData();
  EXPECT_EQ(expected_flags, flags_and_data.first);
  EXPECT_EQ(expected_data, flags_and_data.second);

  // options: nodev, rw, nosuid
  options.push_back("nosuid");
  expected_flags = security_flags | MS_NODEV | MS_NOSUID;
  mount_options_.Initialize(options, false, "", "");
  flags_and_data = mount_options_.ToMountFlagsAndData();
  EXPECT_EQ(expected_flags, flags_and_data.first);
  EXPECT_EQ(expected_data, flags_and_data.second);

  // options: nodev, rw, nosuid, noexec
  options.push_back("noexec");
  expected_flags = security_flags | MS_NODEV | MS_NOSUID | MS_NOEXEC;
  mount_options_.Initialize(options, false, "", "");
  flags_and_data = mount_options_.ToMountFlagsAndData();
  EXPECT_EQ(expected_flags, flags_and_data.first);
  EXPECT_EQ(expected_data, flags_and_data.second);

  // options: nodev, rw, nosuid, noexec, sync
  options.push_back("sync");
  expected_flags =
      security_flags | MS_NODEV | MS_NOSUID | MS_NOEXEC | MS_SYNCHRONOUS;
  mount_options_.Initialize(options, false, "", "");
  flags_and_data = mount_options_.ToMountFlagsAndData();
  EXPECT_EQ(expected_flags, flags_and_data.first);
  EXPECT_EQ(expected_data, flags_and_data.second);

  // options: nodev, rw, nosuid, noexec, sync
  // default uid=1000, gid=1001
  // ignore user and group ID
  expected_flags =
      security_flags | MS_NODEV | MS_NOSUID | MS_NOEXEC | MS_SYNCHRONOUS;
  mount_options_.Initialize(options, false, "1000", "1001");
  flags_and_data = mount_options_.ToMountFlagsAndData();
  EXPECT_EQ(expected_flags, flags_and_data.first);
  EXPECT_EQ(expected_data, flags_and_data.second);

  // options: nodev, rw, nosuid, noexec, sync
  // default uid=1000, gid=1001
  expected_data = "uid=1000,gid=1001";
  mount_options_.Initialize(options, true, "1000", "1001");
  flags_and_data = mount_options_.ToMountFlagsAndData();
  EXPECT_EQ(expected_flags, flags_and_data.first);
  EXPECT_EQ(expected_data, flags_and_data.second);

  // options: nodev, rw, nosuid, noexec, sync, uid=2000, gid=2001
  // default uid=1000, gid=1001
  // ignore user and group ID
  options.push_back("uid=2000");
  options.push_back("gid=2001");
  expected_data = "";
  mount_options_.Initialize(options, false, "1000", "1001");
  flags_and_data = mount_options_.ToMountFlagsAndData();
  EXPECT_EQ(expected_flags, flags_and_data.first);
  EXPECT_EQ(expected_data, flags_and_data.second);

  // options: nodev, rw, nosuid, noexec, sync, uid=2000, gid=2001
  // default uid=1000, gid=1001
  expected_data = "uid=2000,gid=2001";
  mount_options_.Initialize(options, true, "1000", "1001");
  flags_and_data = mount_options_.ToMountFlagsAndData();
  EXPECT_EQ(expected_flags, flags_and_data.first);
  EXPECT_EQ(expected_data, flags_and_data.second);
}

TEST_F(MountOptionsTest, HasOption) {
  mount_options_.AllowOption(MountOptions::kOptionNoSymFollow);

  std::vector<std::string> options = {MountOptions::kOptionNoSymFollow};
  mount_options_.Initialize(options, false, "", "");

  EXPECT_TRUE(mount_options_.HasOption(MountOptions::kOptionNoSymFollow));
  EXPECT_FALSE(mount_options_.HasOption(MountOptions::kOptionBind));

  // Enforced option
  EXPECT_TRUE(mount_options_.HasOption(MountOptions::kOptionNoDev));
}

TEST(MountOptionsUtilTest, IsReadOnlyMount) {
  EXPECT_FALSE(IsReadOnlyMount({}));
  EXPECT_FALSE(IsReadOnlyMount({"foo", "bar"}));
  EXPECT_TRUE(IsReadOnlyMount({"ro"}));
  EXPECT_FALSE(IsReadOnlyMount({"ro", "rw"}));
  EXPECT_TRUE(IsReadOnlyMount({"foo", "ro", "bar", "rw", "ro", "baz"}));
}

TEST(MountOptionsUtilTest, GetParamValue) {
  std::string value;
  EXPECT_FALSE(GetParamValue({}, "foo", &value));
  EXPECT_TRUE(GetParamValue({"a=b", "foo=bar", "baz", "x=y"}, "foo", &value));
  EXPECT_EQ("bar", value);
  EXPECT_FALSE(GetParamValue({"foo"}, "foo", &value));
  EXPECT_TRUE(GetParamValue({"foo=bar", "foo=baz"}, "foo", &value));
  EXPECT_EQ("baz", value);
}

TEST(MountOptionsUtilTest, SetParamValue) {
  std::vector<std::string> params;
  SetParamValue(&params, "foo", "bar");
  SetParamValue(&params, "baz", "");
  EXPECT_THAT(params, ElementsAre("foo=bar", "baz="));
}

}  // namespace cros_disks
