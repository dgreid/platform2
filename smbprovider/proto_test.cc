// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <dbus/smbprovider/dbus-constants.h>
#include <gtest/gtest.h>

#include "smbprovider/constants.h"
#include "smbprovider/proto.h"
#include "smbprovider/proto_bindings/directory_entry.pb.h"
#include "smbprovider/smbprovider_helper.h"
#include "smbprovider/smbprovider_test_helper.h"

namespace smbprovider {
namespace {

template <typename Proto>
void CheckMethodName(const char* name, const Proto& proto) {
  EXPECT_EQ(0, strcmp(name, GetMethodName(proto)));
}

void CheckDirectoryEntryAndDirectoryEntryProtoAreEqual(
    const DirectoryEntry& entry, const DirectoryEntryProto& proto) {
  EXPECT_EQ(entry.is_directory, proto.is_directory());
  EXPECT_EQ(entry.name, proto.name());
  EXPECT_EQ(entry.size, proto.size());
  EXPECT_EQ(entry.last_modified_time, proto.last_modified_time());
}

}  // namespace

class SmbProviderProtoTest : public testing::Test {
 public:
  SmbProviderProtoTest() = default;
  SmbProviderProtoTest(const SmbProviderProtoTest&) = delete;
  SmbProviderProtoTest& operator=(const SmbProviderProtoTest&) = delete;

  ~SmbProviderProtoTest() override = default;
};

// Blobs should be serialized correctly.
TEST_F(SmbProviderProtoTest, SerializeProtoToBlob) {
  DirectoryEntryProto entry;
  entry.set_is_directory(true);
  entry.set_name("test");
  entry.set_size(0);
  entry.set_last_modified_time(0);

  ProtoBlob blob;

  EXPECT_EQ(ERROR_OK, SerializeProtoToBlob(entry, &blob));
}

// IsValidOptions returns true when options are valid for valid protos.
TEST_F(SmbProviderProtoTest, IsValidOptionsForValidProtos) {
  MountOptionsProto mount_proto = CreateMountOptionsProto(
      "smb://testShare", "" /* original_path */, "" /* workgroup */,
      "" /* username */, "" /* account_hash */, false /* skip_connect */,
      false /* save_password */, false /* restore_password */,
      MountConfig(true /* enable_ntlm */));
  EXPECT_TRUE(IsValidOptions(mount_proto));

  UnmountOptionsProto unmount_proto =
      CreateUnmountOptionsProto(3 /* mount_id */, false /* remove_password */);
  EXPECT_TRUE(IsValidOptions(unmount_proto));

  ReadDirectoryOptionsProto read_directory_proto =
      CreateReadDirectoryOptionsProto(3 /* mount_id */, "smb://testShare/dir1");
  EXPECT_TRUE(IsValidOptions(read_directory_proto));

  GetMetadataEntryOptionsProto get_metadata_proto =
      CreateGetMetadataOptionsProto(3 /* mount_id */,
                                    "smb://testShare/dir1/pic.png");
  EXPECT_TRUE(IsValidOptions(get_metadata_proto));

  OpenFileOptionsProto open_file_proto = CreateOpenFileOptionsProto(
      3 /* mount_id */, "smb://testShare/dir1/pic.png", false /* writeable */);
  EXPECT_TRUE(IsValidOptions(open_file_proto));

  CloseFileOptionsProto close_file_proto =
      CreateCloseFileOptionsProto(3 /* mount_id */, 4 /* file_id */);
  EXPECT_TRUE(IsValidOptions(close_file_proto));

  DeleteEntryOptionsProto delete_entry_proto = CreateDeleteEntryOptionsProto(
      3 /* mount_id */, "smb://testShare/dir1/pic.png", false /* recursive */);
  EXPECT_TRUE(IsValidOptions(delete_entry_proto));

  ReadFileOptionsProto read_file_proto = CreateReadFileOptionsProto(
      3 /* mount_id */, 4 /* file_id */, 0 /* offset */, 0 /* length */);
  EXPECT_TRUE(IsValidOptions(read_file_proto));

  CreateFileOptionsProto create_file_proto = CreateCreateFileOptionsProto(
      3 /* mount_id */, "smb://testShare/dir1/pic.png");
  EXPECT_TRUE(IsValidOptions(create_file_proto));

  TruncateOptionsProto truncate_proto = CreateTruncateOptionsProto(
      3 /* mount_id */, "smb://testShare/dir1/pic.png", 0 /* length */);
  EXPECT_TRUE(IsValidOptions(truncate_proto));

  WriteFileOptionsProto write_file_proto = CreateWriteFileOptionsProto(
      3 /* mount_id */, 4 /* file_id */, 0 /* offset */, 0 /* length */);
  EXPECT_TRUE(IsValidOptions(write_file_proto));

  CreateDirectoryOptionsProto create_dir_proto =
      CreateCreateDirectoryOptionsProto(
          3 /* mount_id */, "smb://testShare/dir1", true /* recursive */);
  EXPECT_TRUE(IsValidOptions(create_dir_proto));

  MoveEntryOptionsProto move_entry_proto = CreateMoveEntryOptionsProto(
      3 /* mount_id */, "smb://testShare/src", "smb://testShare/dst");
  EXPECT_TRUE(IsValidOptions(move_entry_proto));

  CopyEntryOptionsProto copy_entry_proto = CreateCopyEntryOptionsProto(
      3 /* mount_id */, "smb://testShare/src", "smb://testShare/dst");
  EXPECT_TRUE(IsValidOptions(copy_entry_proto));

  GetDeleteListOptionsProto get_delete_list_proto =
      CreateGetDeleteListOptionsProto(3 /* mount_id */, "smb://testShare/dir1");
  EXPECT_TRUE(IsValidOptions(get_delete_list_proto));

  GetSharesOptionsProto get_shares_proto =
      CreateGetSharesOptionsProto("smb://192.168.0.1");
  EXPECT_TRUE(IsValidOptions(get_shares_proto));

  UpdateMountCredentialsOptionsProto update_proto =
      CreateUpdateMountCredentialsOptionsProto(
          3 /* mount_id */, "" /* work_group */, "" /* user_name */);
  EXPECT_TRUE(IsValidOptions(update_proto));

  UpdateSharePathOptionsProto update_share_path_proto =
      CreateUpdateSharePathOptionsProto(1 /* mount_id */,
                                        "smb://testsahre/dir1");
  EXPECT_TRUE(IsValidOptions(update_share_path_proto));
}

// IsValidOptions returns false when options are invalid for invalid protos.
TEST_F(SmbProviderProtoTest, IsValidOptionsForInValidProtos) {
  MountOptionsProto mount_proto_blank;
  EXPECT_FALSE(IsValidOptions(mount_proto_blank));

  UnmountOptionsProto unmount_proto_blank;
  EXPECT_FALSE(IsValidOptions(unmount_proto_blank));

  ReadDirectoryOptionsProto read_directory_proto_blank;
  EXPECT_FALSE(IsValidOptions(read_directory_proto_blank));

  GetMetadataEntryOptionsProto get_metadata_proto_blank;
  EXPECT_FALSE(IsValidOptions(get_metadata_proto_blank));

  OpenFileOptionsProto open_file_proto_blank;
  EXPECT_FALSE(IsValidOptions(open_file_proto_blank));

  CloseFileOptionsProto close_file_proto_blank;
  EXPECT_FALSE(IsValidOptions(close_file_proto_blank));

  DeleteEntryOptionsProto delete_entry_proto_blank;
  EXPECT_FALSE(IsValidOptions(delete_entry_proto_blank));

  ReadFileOptionsProto read_file_proto_blank;
  EXPECT_FALSE(IsValidOptions(read_file_proto_blank));

  CreateFileOptionsProto create_file_proto_blank;
  EXPECT_FALSE(IsValidOptions(create_file_proto_blank));

  TruncateOptionsProto truncate_proto_blank;
  EXPECT_FALSE(IsValidOptions(truncate_proto_blank));

  WriteFileOptionsProto write_file_proto_blank;
  EXPECT_FALSE(IsValidOptions(write_file_proto_blank));

  CreateDirectoryOptionsProto create_dir_proto_blank;
  EXPECT_FALSE(IsValidOptions(create_dir_proto_blank));

  MoveEntryOptionsProto move_entry_proto_blank;
  EXPECT_FALSE(IsValidOptions(move_entry_proto_blank));

  CopyEntryOptionsProto copy_entry_proto_blank;
  EXPECT_FALSE(IsValidOptions(copy_entry_proto_blank));

  GetDeleteListOptionsProto get_delete_list_proto_blank;
  EXPECT_FALSE(IsValidOptions(get_delete_list_proto_blank));

  UpdateMountCredentialsOptionsProto update_proto_blank;
  EXPECT_FALSE(IsValidOptions(update_proto_blank));

  UpdateSharePathOptionsProto update_share_path_proto_blank;
  EXPECT_FALSE(IsValidOptions(update_share_path_proto_blank));
}

// IsValidOptions checks offset and length ranges for ReadFileOptionsProto.
TEST_F(SmbProviderProtoTest, IsValidOptionsChecksReadFileRanges) {
  ReadFileOptionsProto read_file_proto = CreateReadFileOptionsProto(
      3 /* mount_id */, 4 /* file_id */, 0 /* offset */, 0 /* length */);
  EXPECT_TRUE(IsValidOptions(read_file_proto));

  ReadFileOptionsProto read_file_proto_bad_offset = CreateReadFileOptionsProto(
      3 /* mount_id */, 4 /* file_id */, -5 /* offset */, 0 /* length */);
  EXPECT_FALSE(IsValidOptions(read_file_proto_bad_offset));

  ReadFileOptionsProto read_file_proto_bad_length = CreateReadFileOptionsProto(
      3 /* mount_id */, 4 /* file_id */, 0 /* offset */, -10 /* length */);
  EXPECT_FALSE(IsValidOptions(read_file_proto_bad_length));
}

// IsValidOptions checks length for TruncateOptionsProto.
TEST_F(SmbProviderProtoTest, IsValidOptionsChecksTruncateLength) {
  TruncateOptionsProto truncate_proto = CreateTruncateOptionsProto(
      3 /* mount_id */, "smb://testShare/dir1/pic.png", 0 /* length */);
  EXPECT_TRUE(IsValidOptions(truncate_proto));

  TruncateOptionsProto truncate_proto_bad_length = CreateTruncateOptionsProto(
      3 /* mount_id */, "smb://testShare/dir1/pic.png", -10 /* length */);
  EXPECT_FALSE(IsValidOptions(truncate_proto_bad_length));
}

// IsValidOptions checks offset and length ranges for WriteFileOptionsProto.
TEST_F(SmbProviderProtoTest, IsValidOptionsChecksWriteFileRanges) {
  WriteFileOptionsProto write_file_proto = CreateWriteFileOptionsProto(
      3 /* mount_id */, 4 /* file_id */, 0 /* offset */, 0 /* length */);
  EXPECT_TRUE(IsValidOptions(write_file_proto));

  WriteFileOptionsProto write_file_proto_bad_offset =
      CreateWriteFileOptionsProto(3 /* mount_id */, 4 /* file_id */,
                                  -1 /* offset */, 0 /* length */);
  EXPECT_FALSE(IsValidOptions(write_file_proto_bad_offset));

  WriteFileOptionsProto write_file_proto_bad_length =
      CreateWriteFileOptionsProto(3 /* mount_id */, 4 /* file_id */,
                                  0 /* offset */, -1 /* length */);
  EXPECT_FALSE(IsValidOptions(write_file_proto_bad_length));
}

// GetEntryPath gets the correct part of corresponding protos.
TEST_F(SmbProviderProtoTest, GetEntryPath) {
  const std::string expected_dir_path = "smb://testShare/dir1";
  const std::string expected_file_path = "smb://testShare/dir1/pic.png";

  ReadDirectoryOptionsProto read_directory_proto =
      CreateReadDirectoryOptionsProto(3 /* mount_id */, expected_dir_path);
  EXPECT_EQ(expected_dir_path, GetEntryPath(read_directory_proto));

  GetMetadataEntryOptionsProto get_metadata_proto =
      CreateGetMetadataOptionsProto(3 /* mount_id */, expected_file_path);
  EXPECT_EQ(expected_file_path, GetEntryPath(get_metadata_proto));

  OpenFileOptionsProto open_file_proto = CreateOpenFileOptionsProto(
      3 /* mount_id */, expected_file_path, false /* writeable */);
  EXPECT_EQ(expected_file_path, GetEntryPath(open_file_proto));

  DeleteEntryOptionsProto delete_entry_proto = CreateDeleteEntryOptionsProto(
      3 /* mount_id */, expected_file_path, false /* recursive */);
  EXPECT_EQ(expected_file_path, GetEntryPath(delete_entry_proto));

  CreateFileOptionsProto create_file_proto =
      CreateCreateFileOptionsProto(3 /* mount_id */, expected_file_path);
  EXPECT_EQ(expected_file_path, GetEntryPath(create_file_proto));

  GetDeleteListOptionsProto get_delete_list_proto =
      CreateGetDeleteListOptionsProto(3 /* mount_id */, expected_dir_path);
  EXPECT_EQ(expected_dir_path, GetEntryPath(get_delete_list_proto));
}

// GetSourcePath and GetDestinationPath get the correct parts of a MoveEntry
// proto.
TEST_F(SmbProviderProtoTest, GetSourcePathAndGetDestinationPath) {
  const std::string expected_source_path = "smb://testShare/src";
  const std::string expected_dest_path = "smb://testShare/dest";

  MoveEntryOptionsProto move_entry_proto = CreateMoveEntryOptionsProto(
      3 /* mount_id */, expected_source_path, expected_dest_path);

  EXPECT_EQ(expected_source_path, GetSourcePath(move_entry_proto));
  EXPECT_EQ(expected_dest_path, GetDestinationPath(move_entry_proto));

  CopyEntryOptionsProto copy_entry_proto = CreateCopyEntryOptionsProto(
      3 /* mount_id */, expected_source_path, expected_dest_path);

  EXPECT_EQ(expected_source_path, GetSourcePath(copy_entry_proto));
  EXPECT_EQ(expected_dest_path, GetDestinationPath(copy_entry_proto));
}

// GetMethodName returns the correct name for each proto.
TEST_F(SmbProviderProtoTest, GetMethodName) {
  CheckMethodName(kMountMethod, MountOptionsProto());
  CheckMethodName(kUnmountMethod, UnmountOptionsProto());
  CheckMethodName(kGetMetadataEntryMethod, GetMetadataEntryOptionsProto());
  CheckMethodName(kReadDirectoryMethod, ReadDirectoryOptionsProto());
  CheckMethodName(kOpenFileMethod, OpenFileOptionsProto());
  CheckMethodName(kCloseFileMethod, CloseFileOptionsProto());
  CheckMethodName(kDeleteEntryMethod, DeleteEntryOptionsProto());
  CheckMethodName(kReadFileMethod, ReadFileOptionsProto());
  CheckMethodName(kCreateFileMethod, CreateFileOptionsProto());
  CheckMethodName(kWriteFileMethod, WriteFileOptionsProto());
  CheckMethodName(kMoveEntryMethod, MoveEntryOptionsProto());
  CheckMethodName(kCopyEntryMethod, CopyEntryOptionsProto());
  CheckMethodName(kGetDeleteListMethod, GetDeleteListOptionsProto());
  CheckMethodName(kGetSharesMethod, GetSharesOptionsProto());
  CheckMethodName(kUpdateMountCredentialsMethod,
                  UpdateMountCredentialsOptionsProto());
}

// DirectoryEntryCtor initializes a DirectoryEntry correctly.
TEST_F(SmbProviderProtoTest, DirectoryEntry) {
  const bool is_dir = false;
  const std::string name = "testentry.jpg";
  const std::string full_path = "smb://testUrl/testentry.jpg";
  int64_t size = 23;
  int64_t last_modified_time = 456;
  DirectoryEntry entry(is_dir, name, full_path, size, last_modified_time);

  EXPECT_EQ(is_dir, entry.is_directory);
  EXPECT_EQ(name, entry.name);
  EXPECT_EQ(full_path, entry.full_path);
  EXPECT_EQ(size, entry.size);
  EXPECT_EQ(last_modified_time, entry.last_modified_time);
}

// ConvertToProto correctly converts a DirectoryEntry to a DirectoryEntryProto.
TEST_F(SmbProviderProtoTest, ConvertToProto) {
  DirectoryEntry entry(false /* is_directory */, "testentry.jpg",
                       "smb://testUrl/testentry.jpg", 23 /* size */,
                       456 /* last_modified_time */);

  DirectoryEntryProto proto;
  ConvertToProto(entry, &proto);

  CheckDirectoryEntryAndDirectoryEntryProtoAreEqual(entry, proto);
}

// AddDirectoryEntry adds a DirectoryEntry to a DirectoryEntryListProto as a
// DirectoryEntryProto.
TEST_F(SmbProviderProtoTest, AddDirectoryEntry) {
  DirectoryEntry entry(false /* is_directory */, "testentry.jpg",
                       "smb://testUrl/testentry.jpg", 23 /* size */,
                       456 /* last_modified_time */);

  DirectoryEntryListProto entries_proto;

  EXPECT_EQ(0, entries_proto.entries_size());
  AddDirectoryEntry(entry, &entries_proto);
  EXPECT_EQ(1, entries_proto.entries_size());

  DirectoryEntryProto entry_proto = entries_proto.entries(0);
  CheckDirectoryEntryAndDirectoryEntryProtoAreEqual(entry, entry_proto);
}

// SerializeDirEntryVectorToProto correctly serializes a vector of dirents.
TEST_F(SmbProviderProtoTest, SerializeDirEntryVectorToProto) {
  std::vector<DirectoryEntry> entries;
  DirectoryEntry entry1(false /* is_directory */, "testentry.jpg",
                        "smb://testUrl/testentry.jpg", 23 /* size */,
                        456 /* last_modified_time */);
  DirectoryEntry entry2(true /* is_directory */, "stuff",
                        "smb://testUrl/testentry.jpg", 5 /* size */,
                        789 /* last_modified_time */);
  entries.push_back(entry1);
  entries.push_back(entry2);

  DirectoryEntryListProto entries_proto;
  SerializeDirEntryVectorToProto(entries, &entries_proto);

  EXPECT_EQ(2, entries_proto.entries_size());
  CheckDirectoryEntryAndDirectoryEntryProtoAreEqual(entry1,
                                                    entries_proto.entries(0));
  CheckDirectoryEntryAndDirectoryEntryProtoAreEqual(entry2,
                                                    entries_proto.entries(1));
}

// AddToDeleteList correctly adds entry paths to a DeleteListProto.
TEST_F(SmbProviderProtoTest, AddToDeleteList) {
  const std::string entry_path_1 = "/dogs";
  const std::string entry_path_2 = "/dogs/1.jpg";

  DeleteListProto delete_list_proto;

  AddToDeleteList(entry_path_1, &delete_list_proto);
  AddToDeleteList(entry_path_2, &delete_list_proto);

  EXPECT_EQ(2, delete_list_proto.entries_size());
  EXPECT_EQ(entry_path_1, delete_list_proto.entries(0));
  EXPECT_EQ(entry_path_2, delete_list_proto.entries(1));
}

}  // namespace smbprovider
