// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbprovider/smbprovider.h"

#include <algorithm>
#include <map>
#include <utility>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <crypto/sha2.h>
#include <dbus/smbprovider/dbus-constants.h>

#include "smbprovider/constants.h"
#include "smbprovider/file_copy_progress.h"
#include "smbprovider/iterator/caching_iterator.h"
#include "smbprovider/iterator/directory_iterator.h"
#include "smbprovider/iterator/post_depth_first_iterator.h"
#include "smbprovider/mount_manager.h"
#include "smbprovider/netbios_packet_parser.h"
#include "smbprovider/proto.h"
#include "smbprovider/proto_bindings/directory_entry.pb.h"
#include "smbprovider/recursive_copy_progress.h"
#include "smbprovider/samba_interface.h"
#include "smbprovider/smbprovider_helper.h"

namespace smbprovider {
namespace {

constexpr mode_t kDirMode = 16877;  // Dir entry

void PopulateMountRootStat(struct stat* mount_root_stat) {
  mount_root_stat->st_size = 0;
  mount_root_stat->st_mode = kDirMode;
  mount_root_stat->st_mtime = 0;
}

base::FilePath MakePasswordFileName(const std::string& share_path,
                                    const std::string& username,
                                    const std::string& workgroup) {
  const std::string raw_name = base::StringPrintf(
      "%s@@%s@%s", share_path.c_str(), username.c_str(), workgroup.c_str());
  const std::string raw_hash = crypto::SHA256HashString(raw_name);
  CHECK_EQ(raw_hash.size(), crypto::kSHA256Length);
  return base::FilePath(base::HexEncode(raw_hash.c_str(), raw_hash.size()));
}

}  // namespace

bool GetEntries(const ReadDirectoryOptionsProto& options,
                CachingIterator iterator,
                int32_t* error_code,
                ProtoBlob* out_entries) {
  DCHECK(error_code);
  DCHECK(out_entries);

  DirectoryEntryListProto directory_entries;

  int32_t result = iterator.Init();
  while (result == 0) {
    if (iterator.IsDone()) {
      *error_code = static_cast<int32_t>(
          SerializeProtoToBlob(directory_entries, out_entries));
      return true;
    }
    AddDirectoryEntry(iterator.Get(), &directory_entries);
    result = iterator.Next();
  }

  // The while-loop is only exited if there is an error. A full successful
  // execution will return from inside the above while-loop.
  *error_code = GetErrorFromErrno(result);
  LogOperationError(GetMethodName(options), GetMountId(options),
                    static_cast<ErrorType>(*error_code));
  return false;
}

bool GetShareEntries(const GetSharesOptionsProto& options,
                     ShareIterator iterator,
                     int32_t* error_code,
                     ProtoBlob* out_entries) {
  DCHECK(error_code);
  DCHECK(out_entries);

  DirectoryEntryListProto directory_entries;

  int32_t result = iterator.Init();
  while (result == 0) {
    if (iterator.IsDone()) {
      *error_code = static_cast<int32_t>(
          SerializeProtoToBlob(directory_entries, out_entries));
      return true;
    }
    AddDirectoryEntry(iterator.Get(), &directory_entries);
    result = iterator.Next();
  }

  // The while-loop is only exited if there is an error. A full successful
  // execution will return from inside the above while-loop.
  *error_code = GetErrorFromErrno(result);
  LogOperationError(GetMethodName(options), GetMountId(options),
                    static_cast<ErrorType>(*error_code));
  return false;
}

SmbProvider::SmbProvider(
    std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object,
    std::unique_ptr<MountManager> mount_manager,
    std::unique_ptr<KerberosArtifactSynchronizer>
        kerberos_artifact_synchronizer,
    const base::FilePath& daemon_store_directory)
    : org::chromium::SmbProviderAdaptor(this),
      dbus_object_(std::move(dbus_object)),
      mount_manager_(std::move(mount_manager)),
      kerberos_artifact_synchronizer_(
          std::move(kerberos_artifact_synchronizer)),
      copy_tracker_(kInitialCopyProgressTrackerId),
      read_dir_tracker_(kInitialReadDirProgressTrackerId),
      daemon_store_directory_(daemon_store_directory) {}

void SmbProvider::RegisterAsync(
    const AsyncEventSequencer::CompletionAction& completion_callback) {
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(completion_callback);
}

base::FilePath SmbProvider::GetDaemonStoreDirectory(
    const std::string& username_hash) const {
  CHECK(!username_hash.empty());
  return daemon_store_directory_.Append(username_hash);
}

void SmbProvider::Mount(const ProtoBlob& options_blob,
                        const base::ScopedFD& password_fd,
                        int32_t* error_code,
                        int32_t* mount_id) {
  DCHECK(error_code);
  DCHECK(mount_id);
  *mount_id = -1;

  // The functions below will set the error if they fail.
  *error_code = static_cast<int32_t>(ERROR_OK);

  MountOptionsProto options;
  if (!ParseOptionsProto(options_blob, &options, error_code)) {
    return;  // Error with parsing proto.
  }
  if ((options.restore_password() || options.save_password()) &&
      options.account_hash().empty()) {
    // |account_hash| should always be set if the password is being saved or
    // restored.
    LOG(ERROR) << "Unknown profile for password save/restore";
    *error_code = static_cast<int32_t>(ERROR_DBUS_PARSE_FAILED);
    return;
  }

  std::string original_share_path = options.original_path();
  if (original_share_path.empty()) {
    original_share_path = options.path();
  }

  MountConfig mount_config = ConvertToMountConfig(options);

  const base::ScopedFD* passed_password_fd = &password_fd;

  base::ScopedFD saved_password_fd;
  base::FilePath password_filepath;
  if (options.restore_password() || options.save_password()) {
    password_filepath =
        GetDaemonStoreDirectory(options.account_hash())
            .Append(MakePasswordFileName(
                original_share_path, options.username(), options.workgroup()));
  }

  if (options.restore_password()) {
    LOG(INFO) << "Restoring saved password";
    base::File password_file(password_filepath,
                             base::File::FLAG_OPEN | base::File::FLAG_READ);
    if (!password_file.IsValid()) {
      LOG(ERROR) << "Unable to open password file for read with error: "
                 << password_file.error_details();
    } else {
      saved_password_fd.reset(password_file.TakePlatformFile());
      passed_password_fd = &saved_password_fd;
    }
  }

  // AddMount() has to be called first since the credential has to be stored
  // before calling CanReadMountRoot().
  AddMount(options.path(), mount_config, options.workgroup(),
           options.username(), *passed_password_fd, password_filepath,
           mount_id);
  if (options.skip_connect()) {
    return;
  }

  if (!CanReadMountRoot(*mount_id, options.path(), error_code)) {
    // If AddMount() was successful but the mount could not be accessed, remove
    // the mount from mount_manager_.
    RemoveMountIfMounted(*mount_id);
    return;
  }

  if (options.save_password()) {
    LOG(INFO) << "Saving password for mount [" << *mount_id << "]";
    bool success = mount_manager_->SavePasswordToFile(*mount_id);
    DCHECK(success);
  }
}

int32_t SmbProvider::Unmount(const ProtoBlob& options_blob) {
  int32_t error_code;
  UnmountOptionsProto options;
  if (!ParseOptionsProto(options_blob, &options, &error_code) ||
      !IsMounted(options, &error_code)) {
    return error_code;
  }

  if (options.remove_password()) {
    bool success = mount_manager_->ErasePasswordFile(GetMountId(options));
    DCHECK(success);
  }

  if (!RemoveMount(GetMountId(options), &error_code)) {
    return error_code;
  }

  return ERROR_OK;
}

void SmbProvider::ReadDirectory(const ProtoBlob& options_blob,
                                int32_t* error_code,
                                ProtoBlob* out_entries) {
  DCHECK(error_code);
  DCHECK(out_entries);

  ReadDirectoryEntries(options_blob, error_code, out_entries);
}

void SmbProvider::ReadDirectoryEntries(const ProtoBlob& options_blob,
                                       int32_t* error_code,
                                       ProtoBlob* out_entries) {
  DCHECK(error_code);
  DCHECK(out_entries);
  out_entries->clear();

  std::string full_path;
  ReadDirectoryOptionsProto options;
  if (ParseOptionsAndPath(options_blob, &options, &full_path, error_code)) {
    MetadataCache* cache = nullptr;
    bool got_cache =
        mount_manager_->GetMetadataCache(GetMountId(options), &cache);
    DCHECK(got_cache);
    DCHECK(cache);

    SambaInterface* samba_interface = GetSambaInterface(GetMountId(options));
    // Purge the cache of expired entries before reading next directory.
    cache->PurgeExpiredEntries();
    GetEntries(options, CachingIterator(full_path, samba_interface, cache),
               error_code, out_entries);
  }
}

void SmbProvider::ReadShareEntries(const ProtoBlob& options_blob,
                                   int32_t* error_code,
                                   ProtoBlob* out_entries) {
  DCHECK(error_code);
  DCHECK(out_entries);
  out_entries->clear();

  std::string full_path;
  GetSharesOptionsProto options;
  if (ParseOptionsAndPath(options_blob, &options, &full_path, error_code)) {
    SambaInterface* samba_interface = GetSambaInterface(GetMountId(options));

    GetShareEntries(options, ShareIterator(full_path, samba_interface),
                    error_code, out_entries);
  }
}

void SmbProvider::GetMetadataEntry(const ProtoBlob& options_blob,
                                   int32_t* error_code,
                                   ProtoBlob* out_entry) {
  DCHECK(error_code);
  DCHECK(out_entry);
  out_entry->clear();

  std::string full_path;
  GetMetadataEntryOptionsProto options;
  if (!ParseOptionsAndPath(options_blob, &options, &full_path, error_code)) {
    return;
  }

  // If we have the result cached, then return it.
  if (GetCachedEntry(GetMountId(options), full_path, out_entry)) {
    *error_code = static_cast<int32_t>(ERROR_OK);
    return;
  }

  SambaInterface* samba_interface = GetSambaInterface(GetMountId(options));

  int32_t get_status_error = 0;
  struct stat stat_info;
  // Always return a successful response on the root. If the files app detects
  // a failure here it will block any further requests to the mount.
  if (options.entry_path() == "/") {
    PopulateMountRootStat(&stat_info);
  } else {
    get_status_error =
        samba_interface->GetEntryStatus(full_path.c_str(), &stat_info);
  }

  if (get_status_error != 0) {
    LogAndSetError(options, GetErrorFromErrno(get_status_error), error_code);
    return;
  }
  *error_code = GetDirectoryEntryProtoFromStat(full_path, stat_info, out_entry);
}

void SmbProvider::OpenFile(const ProtoBlob& options_blob,
                           int32_t* error_code,
                           int32_t* file_id) {
  DCHECK(error_code);
  DCHECK(file_id);

  std::string full_path;
  OpenFileOptionsProto options;
  if (!ParseOptionsAndPath(options_blob, &options, &full_path, error_code)) {
    return;
  }

  if (!OpenFile(options, full_path, error_code, file_id)) {
    *file_id = -1;
    return;
  }

  *error_code = static_cast<int32_t>(ERROR_OK);
}

int32_t SmbProvider::CloseFile(const ProtoBlob& options_blob) {
  int32_t error_code;
  CloseFileOptionsProto options;
  if (!ParseOptionsProto(options_blob, &options, &error_code)) {
    return error_code;
  }

  if (!IsMounted(options, &error_code)) {
    return error_code;
  }

  if (!CloseFile(options, options.file_id(), &error_code)) {
    return error_code;
  }

  return static_cast<int32_t>(ERROR_OK);
}

int32_t SmbProvider::DeleteEntry(const ProtoBlob& options_blob) {
  int32_t error_code;
  DeleteEntryOptionsProto options;
  std::string full_path;
  if (!ParseOptionsAndPath(options_blob, &options, &full_path, &error_code)) {
    return error_code;
  }

  bool is_directory;
  int32_t get_type_result;
  if (!GetEntryType(GetMountId(options), full_path, &get_type_result,
                    &is_directory)) {
    LogAndSetError(options, GetErrorFromErrno(get_type_result), &error_code);
    return error_code;
  }

  int32_t result;
  if (is_directory) {
    if (options.recursive()) {
      result = RecursiveDelete(GetMountId(options), full_path);
    } else {
      result = DeleteDirectory(GetMountId(options), full_path);
    }
  } else {
    result = DeleteFile(GetMountId(options), full_path);
  }

  if (result != 0) {
    LogAndSetError(options, GetErrorFromErrno(result), &error_code);
    return error_code;
  }

  return static_cast<int32_t>(ERROR_OK);
}

void SmbProvider::ReadFile(const ProtoBlob& options_blob,
                           int32_t* error_code,
                           brillo::dbus_utils::FileDescriptor* temp_fd) {
  DCHECK(error_code);
  DCHECK(temp_fd);

  ReadFileOptionsProto options;

  // The functions below will set the error if they fail.
  *error_code = static_cast<int32_t>(ERROR_OK);
  bool success = ParseOptionsProto(options_blob, &options, error_code) &&
                 IsMounted(options, error_code) && Seek(options, error_code) &&
                 ReadFileIntoBuffer(options, error_code) &&
                 WriteTempFile(options, content_buffer_, error_code, temp_fd);

  if (!success) {
    *temp_fd = GenerateEmptyFile();
  }
}

int32_t SmbProvider::CreateFile(const ProtoBlob& options_blob) {
  int32_t error_code;
  std::string full_path;
  CreateFileOptionsProto options;
  if (!ParseOptionsAndPath(options_blob, &options, &full_path, &error_code)) {
    return error_code;
  }

  int32_t file_id;
  // CreateFile() gives us back an open file descriptor to the newly created
  // file.
  if (!CreateFile(options, full_path, &file_id, &error_code)) {
    return error_code;
  }

  // Close the file handle from CreateFile().
  if (!CloseFile(options, file_id, &error_code)) {
    // Attempt to delete the file since file will not be usable.
    SambaInterface* samba_interface = GetSambaInterface(GetMountId(options));
    int32_t unlink_result = samba_interface->Unlink(full_path);
    if (unlink_result != 0) {
      // Log the unlink error but return the original error.
      LOG(ERROR) << "Error unlinking after error closing file: "
                 << GetErrorFromErrno(unlink_result);
    }
    return error_code;
  }

  return static_cast<int32_t>(ERROR_OK);
}

int32_t SmbProvider::Truncate(const ProtoBlob& options_blob) {
  int32_t error_code;
  std::string full_path;
  TruncateOptionsProto options;
  int32_t file_id;

  const bool result =
      ParseOptionsAndPath(options_blob, &options, &full_path, &error_code) &&
      OpenFile(options, full_path, &error_code, &file_id) &&
      TruncateAndCloseFile(options, file_id, options.length(), &error_code);

  return result ? static_cast<int32_t>(ERROR_OK) : error_code;
}

int32_t SmbProvider::WriteFile(const ProtoBlob& options_blob,
                               const base::ScopedFD& temp_fd) {
  int32_t error_code;
  WriteFileOptionsProto options;

  const bool result =
      ParseOptionsProto(options_blob, &options, &error_code) &&
      ReadFromFD(options, temp_fd, &error_code, &content_buffer_) &&
      Seek(options, &error_code) &&
      WriteFileFromBuffer(options, options.file_id(), &error_code);

  return result ? static_cast<int32_t>(ERROR_OK) : error_code;
}

int32_t SmbProvider::CreateDirectory(const ProtoBlob& options_blob) {
  int32_t error_code;
  CreateDirectoryOptionsProto options;
  std::string full_path;

  const bool result =
      ParseOptionsAndPath(options_blob, &options, &full_path, &error_code) &&
      CreateParentsIfNecessary(options, &error_code) &&
      CreateSingleDirectory(options, full_path, false /* ignore_existing */,
                            &error_code);

  return result ? static_cast<int32_t>(ERROR_OK) : error_code;
}

int32_t SmbProvider::MoveEntry(const ProtoBlob& options_blob) {
  int32_t error_code;
  std::string source_path;
  std::string target_path;
  MoveEntryOptionsProto options;

  const bool success =
      ParseOptionsAndPaths(options_blob, &options, &source_path, &target_path,
                           &error_code) &&
      MoveEntry(options, source_path, target_path, &error_code);

  return success ? static_cast<int32_t>(ERROR_OK) : error_code;
}

int32_t SmbProvider::CopyEntry(const ProtoBlob& options_blob) {
  int32_t error_code;
  std::string source_path;
  std::string target_path;
  CopyEntryOptionsProto options;

  const bool success =
      ParseOptionsAndPaths(options_blob, &options, &source_path, &target_path,
                           &error_code) &&
      CopyEntry(options, source_path, target_path, &error_code);

  return success ? static_cast<int32_t>(ERROR_OK) : error_code;
}

void SmbProvider::GetShares(const ProtoBlob& options_blob,
                            int32_t* error_code,
                            ProtoBlob* shares) {
  DCHECK(error_code);
  DCHECK(shares);

  ReadShareEntries(options_blob, error_code, shares);
}

void SmbProvider::SetupKerberos(SetupKerberosCallback callback,
                                const std::string& account_identifier) {
  kerberos_artifact_synchronizer_->SetupKerberos(
      account_identifier,
      base::Bind(&SmbProvider::HandleSetupKerberosResponse,
                 base::Unretained(this), base::Passed(std::move(callback))));
}

void SmbProvider::HandleSetupKerberosResponse(SetupKerberosCallback callback,
                                              bool result) {
  callback->Return(result);
}

ProtoBlob SmbProvider::ParseNetBiosPacket(const std::vector<uint8_t>& packet,
                                          uint16_t transaction_id) {
  const std::vector<std::string> servers =
      netbios::ParsePacket(packet, transaction_id);

  const HostnamesProto hostnames_proto = BuildHostnamesProto(servers);
  std::vector<uint8_t> out_blob;
  if (SerializeProtoToBlob(hostnames_proto, &out_blob) != ERROR_OK) {
    return ProtoBlob();
  }
  return out_blob;
}

void SmbProvider::StartCopy(const ProtoBlob& options_blob,
                            int32_t* error_code,
                            int32_t* copy_token) {
  DCHECK(error_code);
  DCHECK(copy_token);

  std::string source_path;
  std::string target_path;
  CopyEntryOptionsProto options;

  if (!ParseOptionsAndPaths(options_blob, &options, &source_path, &target_path,
                            error_code)) {
    return;
  }

  ErrorType error = StartCopy(options, source_path, target_path, copy_token);
  if (error != ERROR_OK && error != ERROR_COPY_PENDING) {
    LogAndSetError(options, error, error_code);
    return;
  }

  *error_code = static_cast<int32_t>(error);
}

int32_t SmbProvider::ContinueCopy(int32_t mount_id, int32_t copy_token) {
  DCHECK_GE(mount_id, 0);
  DCHECK_GE(copy_token, 0);

  int32_t error_code;
  if (!copy_tracker_.Contains(copy_token)) {
    LogAndSetError(kContinueCopyMethod, mount_id, ERROR_COPY_FAILED,
                   &error_code);
    return error_code;
  }

  if (!mount_manager_->IsAlreadyMounted(mount_id)) {
    copy_tracker_.Remove(copy_token);
    LogAndSetError(kContinueCopyMethod, mount_id, ERROR_COPY_FAILED,
                   &error_code);
    return error_code;
  }

  ErrorType error = ContinueCopy(copy_token);
  if (error != ERROR_OK && error != ERROR_COPY_PENDING) {
    LogAndSetError(kContinueCopyMethod, mount_id, error, &error_code);
    return error_code;
  }

  return static_cast<int32_t>(error);
}

void SmbProvider::StartReadDirectory(const ProtoBlob& options_blob,
                                     int32_t* error_code,
                                     ProtoBlob* out_entries,
                                     int32_t* read_dir_token) {
  DCHECK(error_code);
  DCHECK(out_entries);
  DCHECK(read_dir_token);

  std::string directory_path;
  ReadDirectoryOptionsProto options;

  if (!ParseOptionsAndPath(options_blob, &options, &directory_path,
                           error_code)) {
    return;
  }

  DirectoryEntryListProto entries;
  ErrorType error =
      StartReadDirectory(options, directory_path, &entries, read_dir_token);

  if (error != ERROR_OK && error != ERROR_OPERATION_PENDING) {
    LogAndSetError(options, error, error_code);
    return;
  }

  ErrorType serialize_error = SerializeProtoToBlob(entries, out_entries);
  if (serialize_error != ERROR_OK) {
    LogAndSetError(options, serialize_error, error_code);
    return;
  }

  *error_code = static_cast<int32_t>(error);
}

void SmbProvider::ContinueReadDirectory(int32_t mount_id,
                                        int32_t read_dir_token,
                                        int32_t* error_code,
                                        ProtoBlob* out_entries) {
  DCHECK_GE(mount_id, 0);
  DCHECK_GE(read_dir_token, 0);
  DCHECK(error_code);
  DCHECK(out_entries);

  if (!read_dir_tracker_.Contains(read_dir_token)) {
    LogAndSetError(kContinueReadDirectoryMethod, mount_id,
                   ERROR_OPERATION_FAILED, error_code);
    return;
  }

  if (!mount_manager_->IsAlreadyMounted(mount_id)) {
    read_dir_tracker_.Remove(read_dir_token);
    LogAndSetError(kContinueReadDirectoryMethod, mount_id,
                   ERROR_OPERATION_FAILED, error_code);
    return;
  }

  DirectoryEntryListProto entries;
  ErrorType error = ContinueReadDirectory(read_dir_token, &entries);

  if (error != ERROR_OK && error != ERROR_OPERATION_PENDING) {
    LogAndSetError(kContinueReadDirectoryMethod, mount_id, error, error_code);
    return;
  }

  ErrorType serialize_error = SerializeProtoToBlob(entries, out_entries);
  if (serialize_error != ERROR_OK) {
    LogAndSetError(kContinueReadDirectoryMethod, mount_id, serialize_error,
                   error_code);
    return;
  }

  *error_code = static_cast<int32_t>(error);
}

int32_t SmbProvider::UpdateMountCredentials(const ProtoBlob& options_blob,
                                            const base::ScopedFD& password_fd) {
  int32_t error_code;
  UpdateMountCredentialsOptionsProto options;

  const bool success =
      ParseOptionsProto(options_blob, &options, &error_code) &&
      mount_manager_->UpdateMountCredential(
          options.mount_id(),
          SmbCredential(options.workgroup(), options.username(),
                        GetPassword(password_fd), {}));
  if (!success) {
    LOG(ERROR) << "Failed to update credentials of mount id: "
               << options.mount_id();
    return static_cast<int32_t>(ERROR_NOT_FOUND);
  }
  return static_cast<int32_t>(ERROR_OK);
}

int32_t SmbProvider::UpdateSharePath(const ProtoBlob& options_blob) {
  int32_t error_code;
  UpdateSharePathOptionsProto options;

  const bool success =
      ParseOptionsProto(options_blob, &options, &error_code) &&
      mount_manager_->UpdateSharePath(options.mount_id(), options.path());

  if (!success) {
    LOG(ERROR) << "Failed to update share path of mount id: "
               << options.mount_id();
    return static_cast<int32_t>(ERROR_NOT_FOUND);
  }
  return static_cast<int32_t>(ERROR_OK);
}

HostnamesProto SmbProvider::BuildHostnamesProto(
    const std::vector<std::string>& hostnames) const {
  HostnamesProto hostnames_proto;
  for (const auto& hostname : hostnames) {
    AddToHostnamesProto(hostname, &hostnames_proto);
  }
  return hostnames_proto;
}

SambaInterface* SmbProvider::GetSambaInterface(int32_t mount_id) const {
  SambaInterface* samba_interface;
  if (mount_id == kInternalMountId) {
    samba_interface = mount_manager_->GetSystemSambaInterface();
  } else {
    bool success =
        mount_manager_->GetSambaInterface(mount_id, &samba_interface);
    DCHECK(success);
  }

  DCHECK(samba_interface);
  return samba_interface;
}

template <typename Proto>
bool SmbProvider::GetFullPath(const Proto* options,
                              std::string* full_path) const {
  DCHECK(options);
  DCHECK(full_path);

  const int32_t mount_id = GetMountId(*options);
  const std::string entry_path = GetEntryPath(*options);

  bool success = mount_manager_->GetFullPath(mount_id, entry_path, full_path);
  if (!success) {
    LOG(ERROR) << GetMethodName(*options) << " requested unknown mount_id "
               << mount_id;
  }

  return success;
}

template <>
bool SmbProvider::GetFullPath(const GetSharesOptionsProto* options,
                              std::string* full_path) const {
  DCHECK(options);
  DCHECK(full_path);

  *full_path = GetEntryPath(*options);
  return true;
}

template <typename Proto>
bool SmbProvider::GetFullPaths(const Proto* options,
                               std::string* source_full_path,
                               std::string* target_full_path) const {
  DCHECK(options);
  DCHECK(source_full_path);
  DCHECK(target_full_path);

  const int32_t mount_id = GetMountId(*options);
  const std::string source_path = GetSourcePath(*options);
  const std::string target_path = GetDestinationPath(*options);

  const bool success =
      mount_manager_->GetFullPath(mount_id, source_path, source_full_path) &&
      mount_manager_->GetFullPath(mount_id, target_path, target_full_path);
  if (!success) {
    LOG(ERROR) << GetMethodName(*options) << " requested unknown mount_id "
               << mount_id;
  }

  return success;
}

template <typename Proto>
bool SmbProvider::ParseOptionsAndPath(const ProtoBlob& blob,
                                      Proto* options,
                                      std::string* full_path,
                                      int32_t* error_code) {
  if (!ParseOptionsProto(blob, options, error_code)) {
    return false;
  }

  if (!GetFullPath(options, full_path)) {
    *error_code = static_cast<int32_t>(ERROR_NOT_FOUND);
    return false;
  }

  return true;
}

template <typename Proto>
bool SmbProvider::ParseOptionsAndPaths(const ProtoBlob& blob,
                                       Proto* options,
                                       std::string* source_path,
                                       std::string* target_path,
                                       int32_t* error_code) {
  DCHECK(options);
  DCHECK(source_path);
  DCHECK(target_path);
  DCHECK(error_code);

  if (!ParseOptionsProto(blob, options, error_code)) {
    return false;
  }

  if (!GetFullPaths(options, source_path, target_path)) {
    *error_code = static_cast<int32_t>(ERROR_NOT_FOUND);
    return false;
  }

  return true;
}

bool SmbProvider::GetEntryType(int32_t mount_id,
                               const std::string& full_path,
                               int32_t* error_code,
                               bool* is_directory) {
  DCHECK(error_code);
  DCHECK(is_directory);

  SambaInterface* samba_interface = GetSambaInterface(mount_id);
  struct stat stat_info;
  *error_code = samba_interface->GetEntryStatus(full_path.c_str(), &stat_info);
  if (*error_code != 0) {
    return false;
  }

  if (IsDirectory(stat_info)) {
    *is_directory = true;
    return true;
  }
  if (IsFile(stat_info)) {
    *is_directory = false;
    return true;
  }
  *error_code = ENOENT;
  return false;
}

template <typename Proto>
bool SmbProvider::Seek(const Proto& options, int32_t* error_code) {
  DCHECK(error_code);

  SambaInterface* samba_interface = GetSambaInterface(GetMountId(options));
  int32_t result = samba_interface->Seek(options.file_id(), options.offset());
  if (result != 0) {
    LogAndSetError(options, GetErrorFromErrno(result), error_code);
    return false;
  }
  return true;
}

bool SmbProvider::CanReadMountRoot(int32_t mount_id,
                                   const std::string& mount_root,
                                   int32_t* error_code) {
  DCHECK(error_code);

  SambaInterface* samba_interface = GetSambaInterface(mount_id);
  DCHECK(samba_interface);

  int32_t dir_id = -1;
  int32_t result = samba_interface->OpenDirectory(mount_root, &dir_id);
  if (result != 0) {
    LogAndSetError(kMountMethod, -1, GetErrorFromErrno(result), error_code);
    return false;
  }

  CloseDirectory(mount_id, dir_id);
  return true;
}

void SmbProvider::CloseDirectory(int32_t mount_id, int32_t dir_id) {
  SambaInterface* samba_interface = GetSambaInterface(mount_id);
  int32_t result = samba_interface->CloseDirectory(dir_id);
  if (result != 0) {
    LOG(ERROR) << "Error closing directory " << dir_id;
  }
}

bool SmbProvider::RemoveMount(int32_t mount_id, int32_t* error_code) {
  bool removed = mount_manager_->RemoveMount(mount_id);
  if (!removed) {
    *error_code = static_cast<int32_t>(ERROR_NOT_FOUND);
  }

  return removed;
}

void SmbProvider::AddMount(const std::string& mount_root,
                           const MountConfig& mount_config,
                           const std::string& workgroup,
                           const std::string& username,
                           const base::ScopedFD& password_fd,
                           const base::FilePath& password_file,
                           int32_t* mount_id) {
  SmbCredential credential(workgroup, username, GetPassword(password_fd),
                           password_file);
  mount_manager_->AddMount(mount_root, std::move(credential), mount_config,
                           mount_id);
  DCHECK_GE(*mount_id, 0);
}

void SmbProvider::RemoveMountIfMounted(int32_t mount_id) {
  if (mount_id != -1) {
    mount_manager_->RemoveMount(mount_id);
  }
}

bool SmbProvider::ReadFileIntoBuffer(const ReadFileOptionsProto& options,
                                     int32_t* error_code) {
  DCHECK(error_code);

  // Read the data in the member |content_buffer_|.
  if (!ReadToContentBuffer(options, options.file_id(), error_code)) {
    return false;
  }
  return true;
}

template <typename Proto>
bool SmbProvider::WriteTempFile(const Proto& options,
                                const std::vector<uint8_t>& buffer,
                                int32_t* error_code,
                                brillo::dbus_utils::FileDescriptor* temp_fd) {
  base::ScopedFD scoped_fd = temp_file_manager_.CreateTempFile(buffer);
  if (!scoped_fd.is_valid()) {
    LogAndSetError(options, ERROR_IO, error_code);
    return false;
  }

  // get() is called here instead of release() since FileDescriptor duplicates
  // the FD when getting assigned, meaning the local FD still needs to be
  // closed by ScopedFD when it goes out of scope.
  *temp_fd = scoped_fd.get();
  return true;
}

bool SmbProvider::WriteFileFromBuffer(const WriteFileOptionsProto& options,
                                      int32_t file_id,
                                      int32_t* error_code) {
  DCHECK(error_code);

  SambaInterface* samba_interface = GetSambaInterface(GetMountId(options));
  int32_t result = samba_interface->WriteFile(file_id, content_buffer_.data(),
                                              content_buffer_.size());
  if (result != 0) {
    LogAndSetError(options, GetErrorFromErrno(result), error_code);
    return false;
  }
  return true;
}

int32_t SmbProvider::RecursiveDelete(int32_t mount_id,
                                     const std::string& dir_path) {
  PostDepthFirstIterator it = GetPostOrderIterator(mount_id, dir_path);
  int32_t it_result = it.Init();
  while (it_result == 0) {
    if (it.IsDone()) {
      return 0;
    }

    int32_t del_result = DeleteDirectoryEntry(mount_id, it.Get());
    if (del_result != 0) {
      return del_result;
    }

    it_result = it.Next();
  }

  // while-loop is only exited from if there's an iterator error.
  DCHECK_NE(0, it_result);
  return it_result;
}

int32_t SmbProvider::DeleteDirectoryEntry(int32_t mount_id,
                                          const DirectoryEntry& entry) {
  if (entry.is_directory) {
    return DeleteDirectory(mount_id, entry.full_path);
  }

  return DeleteFile(mount_id, entry.full_path);
}

int32_t SmbProvider::DeleteFile(int32_t mount_id,
                                const std::string& file_path) {
  // It's always safe to invalidate the cache regardless of whether the delete
  // is successful or not.
  InvalidateCacheEntry(mount_id, file_path);
  SambaInterface* samba_interface = GetSambaInterface(mount_id);
  return samba_interface->Unlink(file_path.c_str());
}

int32_t SmbProvider::DeleteDirectory(int32_t mount_id,
                                     const std::string& dir_path) {
  // It's always safe to invalidate the cache regardless of whether the delete
  // is successful or not.
  InvalidateCacheEntry(mount_id, dir_path);
  SambaInterface* samba_interface = GetSambaInterface(mount_id);
  return samba_interface->RemoveDirectory(dir_path.c_str());
}

void SmbProvider::InvalidateCacheEntry(int32_t mount_id,
                                       const std::string& full_path) {
  MetadataCache* cache = nullptr;
  bool got_cache = mount_manager_->GetMetadataCache(mount_id, &cache);
  DCHECK(got_cache);
  DCHECK(cache);

  // The result can be ignored since it may or may not be in the cache.
  cache->RemoveEntry(full_path);
}

PostDepthFirstIterator SmbProvider::GetPostOrderIterator(
    int32_t mount_id, const std::string& full_path) {
  SambaInterface* samba_interface = GetSambaInterface(mount_id);
  return PostDepthFirstIterator(full_path, samba_interface);
}

template <typename Proto>
bool SmbProvider::OpenFile(const Proto& options,
                           const std::string& full_path,
                           int32_t* error,
                           int32_t* file_id) {
  DCHECK(error);
  DCHECK(file_id);

  SambaInterface* samba_interface = GetSambaInterface(GetMountId(options));
  int32_t result = samba_interface->OpenFile(
      full_path, GetOpenFilePermissions(options), file_id);
  if (result != 0) {
    LogAndSetError(options, GetErrorFromErrno(result), error);
    return false;
  }
  return true;
}

template <typename Proto>
bool SmbProvider::CloseFile(const Proto& options,
                            int32_t file_id,
                            int32_t* error) {
  DCHECK(error);

  SambaInterface* samba_interface = GetSambaInterface(GetMountId(options));
  int32_t result = samba_interface->CloseFile(file_id);
  if (result != 0) {
    LogAndSetError(options, GetErrorFromErrno(result), error);
    return false;
  }
  return true;
}

template <typename Proto>
bool SmbProvider::TruncateAndCloseFile(const Proto& options,
                                       const int32_t file_id,
                                       int64_t length,
                                       int32_t* error) {
  DCHECK(error);

  SambaInterface* samba_interface = GetSambaInterface(GetMountId(options));
  int32_t truncate_result = samba_interface->Truncate(file_id, length);
  if (truncate_result != 0) {
    LogAndSetError(options, GetErrorFromErrno(truncate_result), error);
    // Continue to close on error.
  }

  int32_t close_error;
  if (!CloseFile(options, file_id, &close_error)) {
    if (truncate_result == 0) {
      // If Truncate was successful, set error to the close error, otherwise
      // keep the truncate error.
      *error = close_error;
    }
    return false;
  }

  // Return if the truncate was successful.
  return truncate_result == 0;
}

bool SmbProvider::MoveEntry(const MoveEntryOptionsProto& options,
                            const std::string& source_path,
                            const std::string& target_path,
                            int32_t* error) {
  DCHECK(error);

  // Invalidate the cache for the source path. This is always safe even if the
  // move fails because it will just cause a cache miss.
  int32_t mount_id = GetMountId(options);
  InvalidateCacheEntry(mount_id, source_path);

  SambaInterface* samba_interface = GetSambaInterface(mount_id);
  int32_t result = samba_interface->MoveEntry(source_path, target_path);
  if (result != 0) {
    LogAndSetError(options, GetErrorFromErrno(result), error);
    return false;
  }
  return true;
}

bool SmbProvider::GenerateParentPaths(
    const CreateDirectoryOptionsProto& options,
    int32_t* error_code,
    std::vector<std::string>* parent_paths) {
  DCHECK(error_code);
  DCHECK(parent_paths);
  base::FilePath current_path(options.directory_path());
  DCHECK(current_path.IsAbsolute());

  // Skip the leaf path and start with the lowest parent.
  current_path = current_path.DirName();

  while (current_path.value() != "/") {
    std::string full_path;
    if (!mount_manager_->GetFullPath(
            GetMountId(options), current_path.StripTrailingSeparators().value(),
            &full_path)) {
      *error_code = static_cast<int32_t>(ERROR_NOT_FOUND);
      return false;
    }

    current_path = current_path.DirName();
    parent_paths->push_back(std::move(full_path));
  }

  // Reverse the vector so the top parent will be first.
  std::reverse(parent_paths->begin(), parent_paths->end());
  return true;
}

bool SmbProvider::CreateNestedDirectories(
    const CreateDirectoryOptionsProto& options,
    const std::vector<std::string>& paths,
    int32_t* error_code) {
  DCHECK(error_code);

  for (auto const& path : paths) {
    if (!CreateSingleDirectory(options, path, true /* ignore_existing */,
                               error_code)) {
      return false;
    }
  }
  return true;
}

bool SmbProvider::CreateParentsIfNecessary(
    const CreateDirectoryOptionsProto& options, int32_t* error_code) {
  DCHECK(error_code);

  if (!options.recursive()) {
    // Return true in this case since no parents need to be created.
    return true;
  }

  std::vector<std::string> paths;
  return GenerateParentPaths(options, error_code, &paths) &&
         CreateNestedDirectories(options, paths, error_code);
}

template <typename Proto>
bool SmbProvider::CreateSingleDirectory(const Proto& options,
                                        const std::string& full_path,
                                        bool ignore_existing,
                                        int32_t* error_code) {
  DCHECK(error_code);

  SambaInterface* samba_interface = GetSambaInterface(GetMountId(options));
  const int32_t result = samba_interface->CreateDirectory(full_path);
  if (ShouldReportCreateDirError(result, ignore_existing)) {
    LogAndSetError(options, GetErrorFromErrno(result), error_code);
    return false;
  }

  return true;
}

template <typename Proto>
bool SmbProvider::CreateFile(const Proto& options,
                             const std::string& full_path,
                             int32_t* file_id,
                             int32_t* error) {
  DCHECK(file_id);
  DCHECK(error);

  SambaInterface* samba_interface = GetSambaInterface(GetMountId(options));
  int32_t result = samba_interface->CreateFile(full_path, file_id);
  if (result != 0) {
    LogAndSetError(options, GetErrorFromErrno(result), error);
    return false;
  }
  return true;
}

bool SmbProvider::CopyEntry(const CopyEntryOptionsProto& options,
                            const std::string& source_path,
                            const std::string& target_path,
                            int32_t* error_code) {
  DCHECK(error_code);
  bool is_directory;
  int32_t get_type_result;
  if (!GetEntryType(GetMountId(options), source_path, &get_type_result,
                    &is_directory)) {
    LogAndSetError(options, GetErrorFromErrno(get_type_result), error_code);
    return false;
  }

  if (is_directory) {
    return CreateSingleDirectory(options, target_path,
                                 false /* ignore_existing */, error_code);
  }

  return CopyFile(options, source_path, target_path, error_code);
}

bool SmbProvider::CopyFile(const CopyEntryOptionsProto& options,
                           const std::string& source_path,
                           const std::string& target_path,
                           int32_t* error_code) {
  DCHECK(error_code);

  SambaInterface* samba_interface = GetSambaInterface(GetMountId(options));
  int32_t result = samba_interface->CopyFile(source_path, target_path);
  if (result != 0) {
    LogAndSetError(options, GetErrorFromErrno(result), error_code);
    return false;
  }

  return true;
}

ErrorType SmbProvider::StartCopy(const CopyEntryOptionsProto& options,
                                 const std::string& source_path,
                                 const std::string& target_path,
                                 int32_t* copy_token) {
  DCHECK(copy_token);

  bool is_directory;
  int32_t get_type_result;
  if (!GetEntryType(GetMountId(options), source_path, &get_type_result,
                    &is_directory)) {
    return GetErrorFromErrno(get_type_result);
  }

  if (is_directory) {
    return StartDirectoryCopy(options, source_path, target_path, copy_token);
  }

  return StartFileCopy(options, source_path, target_path, copy_token);
}

template <typename CopyProgressType>
ErrorType SmbProvider::StartCopyProgress(const CopyEntryOptionsProto& options,
                                         const std::string& source_path,
                                         const std::string& target_path,
                                         int32_t* copy_token) {
  DCHECK(copy_token);

  SambaInterface* samba_interface = GetSambaInterface(GetMountId(options));
  auto copy_progress = std::make_unique<CopyProgressType>(samba_interface);

  int32_t copy_result;
  bool should_continue_copy =
      copy_progress->StartCopy(source_path, target_path, &copy_result);
  if (should_continue_copy) {
    // The copy needs to be continued.
    *copy_token = copy_tracker_.Insert(std::move(copy_progress));
    return ERROR_COPY_PENDING;
  }
  if (copy_result == 0) {
    // The copy is complete.
    return ERROR_OK;
  }

  return GetErrorFromErrno(copy_result);
}

ErrorType SmbProvider::StartDirectoryCopy(const CopyEntryOptionsProto& options,
                                          const std::string& source_path,
                                          const std::string& target_path,
                                          int32_t* copy_token) {
  DCHECK(copy_token);

  return StartCopyProgress<RecursiveCopyProgress>(options, source_path,
                                                  target_path, copy_token);
}

ErrorType SmbProvider::StartFileCopy(const CopyEntryOptionsProto& options,
                                     const std::string& source_path,
                                     const std::string& target_path,
                                     int32_t* copy_token) {
  DCHECK(copy_token);

  return StartCopyProgress<FileCopyProgress>(options, source_path, target_path,
                                             copy_token);
}

ErrorType SmbProvider::ContinueCopy(int32_t copy_token) {
  DCHECK_GE(copy_token, 0);
  DCHECK(copy_tracker_.Contains(copy_token));

  int32_t copy_result;
  const bool should_continue_copy =
      copy_tracker_.Find(copy_token)->second->ContinueCopy(&copy_result);
  if (should_continue_copy) {
    // The copy needs to be continued.
    return ERROR_COPY_PENDING;
  }

  // The copy no longer needs to be continued as it has either completed
  // successfully or it failed so remove it from |copy_tracker_|.
  copy_tracker_.Remove(copy_token);

  if (copy_result == 0) {
    // The copy is complete.
    return ERROR_OK;
  }

  return GetErrorFromErrno(copy_result);
}

bool SmbProvider::ReadToContentBuffer(const ReadFileOptionsProto& options,
                                      int32_t file_id,
                                      int32_t* error_code) {
  DCHECK(error_code);

  // Make sure the buffer is at least large enough for the content.
  content_buffer_.resize(options.length());

  size_t bytes_read;
  SambaInterface* samba_interface = GetSambaInterface(GetMountId(options));
  int32_t result = samba_interface->ReadFile(
      file_id, content_buffer_.data(), content_buffer_.size(), &bytes_read);
  if (result != 0) {
    LogAndSetError(options, GetErrorFromErrno(result), error_code);
    return false;
  }

  DCHECK_GE(bytes_read, 0);
  DCHECK_LE(bytes_read, content_buffer_.size());

  // Make sure buffer is only as big as bytes_read.
  content_buffer_.resize(bytes_read);
  return true;
}

void SmbProvider::GetDeleteList(const ProtoBlob& options_blob,
                                int32_t* error_code,
                                brillo::dbus_utils::FileDescriptor* temp_fd,
                                int32_t* bytes_written) {
  DCHECK(error_code);
  DCHECK(temp_fd);
  DCHECK(bytes_written);

  std::string full_path;
  GetDeleteListOptionsProto options;
  if (!ParseOptionsAndPath(options_blob, &options, &full_path, error_code)) {
    *temp_fd = GenerateEmptyFile();
    return;
  }

  bool is_directory;
  int32_t get_type_result;
  if (!GetEntryType(GetMountId(options), full_path, &get_type_result,
                    &is_directory)) {
    LogAndSetError(options, GetErrorFromErrno(get_type_result), error_code);
    *temp_fd = GenerateEmptyFile();
    return;
  }

  DeleteListProto delete_list;
  int32_t result =
      GenerateDeleteList(options, full_path, is_directory, &delete_list);
  if (result != 0) {
    LogAndSetError(options, GetErrorFromErrno(result), error_code);
    *temp_fd = GenerateEmptyFile();
    return;
  }

  WriteDeleteListToTempFile(options, delete_list, error_code, temp_fd,
                            bytes_written);
}

bool SmbProvider::WriteDeleteListToTempFile(
    const GetDeleteListOptionsProto& options,
    const DeleteListProto& delete_list,
    int32_t* error_code,
    brillo::dbus_utils::FileDescriptor* temp_fd,
    int32_t* bytes_written) {
  DCHECK(error_code);
  DCHECK(temp_fd);
  DCHECK(bytes_written);

  std::vector<uint8_t> buffer;
  *error_code =
      static_cast<int32_t>(SerializeProtoToBlob(delete_list, &buffer));
  if (*error_code != ERROR_OK) {
    *temp_fd = GenerateEmptyFile();
    return false;
  }

  bool success = WriteTempFile(options, buffer, error_code, temp_fd);
  *bytes_written = success ? buffer.size() : -1;

  return success;
}

int32_t SmbProvider::GenerateDeleteList(
    const GetDeleteListOptionsProto& options,
    const std::string& full_path,
    bool is_directory,
    DeleteListProto* delete_list) {
  DCHECK(delete_list);

  if (!is_directory) {
    // |delete_list| will only contain the relative path to the file.
    AddToDeleteList(GetRelativePath(GetMountId(options), full_path),
                    delete_list);
    return 0;
  }

  PostDepthFirstIterator it =
      GetPostOrderIterator(GetMountId(options), full_path);
  int32_t it_result = it.Init();
  while (it_result == 0) {
    if (it.IsDone()) {
      return 0;
    }

    AddToDeleteList(GetRelativePath(GetMountId(options), it.Get().full_path),
                    delete_list);
    it_result = it.Next();
  }

  // while-loop is only exited from if there's an iterator error.
  DCHECK_NE(0, it_result);
  return it_result;
}

bool SmbProvider::GetCachedEntry(int32_t mount_id,
                                 const std::string full_path,
                                 ProtoBlob* out_entry) {
  MetadataCache* cache = nullptr;
  if (!mount_manager_->GetMetadataCache(mount_id, &cache)) {
    return false;
  }

  DCHECK(cache);
  DirectoryEntry entry;
  if (!cache->FindEntry(full_path, &entry)) {
    return false;
  }

  DirectoryEntryProto proto;
  ConvertToProto(entry, &proto);
  if (SerializeProtoToBlob(proto, out_entry) != ERROR_OK) {
    return false;
  }

  return true;
}

brillo::dbus_utils::FileDescriptor SmbProvider::GenerateEmptyFile() {
  base::ScopedFD temp_fd = temp_file_manager_.CreateTempFile();
  DCHECK(temp_fd.is_valid());

  // get() is called here instead of release() since FileDescriptor duplicates
  // the FD when getting assigned, meaning the local FD still needs to be
  // closed by ScopedFD when it goes out of scope.
  return brillo::dbus_utils::FileDescriptor(temp_fd.get());
}

std::string SmbProvider::GetRelativePath(int32_t mount_id,
                                         const std::string& entry_path) {
  return mount_manager_->GetRelativePath(mount_id, entry_path);
}

template <typename Proto>
bool SmbProvider::IsMounted(const Proto& options, int32_t* error_code) const {
  const bool is_valid = mount_manager_->IsAlreadyMounted(GetMountId(options));
  if (!is_valid) {
    LogAndSetError(options, ERROR_NOT_FOUND, error_code);
  }
  return is_valid;
}

ErrorType SmbProvider::StartReadDirectory(
    const ReadDirectoryOptionsProto& options,
    const std::string& directory_path,
    DirectoryEntryListProto* entries,
    int32_t* read_dir_token) {
  DCHECK(entries);
  DCHECK(read_dir_token);

  SambaInterface* samba_interface = GetSambaInterface(GetMountId(options));
  auto read_dir_progress = std::make_unique<ReadDirProgress>(samba_interface);

  MetadataCache* cache = nullptr;
  bool got_cache =
      mount_manager_->GetMetadataCache(GetMountId(options), &cache);
  DCHECK(got_cache);
  DCHECK(cache);

  int32_t read_dir_result;
  const bool should_continue_read_dir = read_dir_progress->StartReadDir(
      directory_path, cache, &read_dir_result, entries);

  if (should_continue_read_dir) {
    DCHECK_EQ(0, read_dir_result);
    // The read directory needs to be continued.
    *read_dir_token = read_dir_tracker_.Insert(std::move(read_dir_progress));
    return ERROR_OPERATION_PENDING;
  }

  if (read_dir_result == 0) {
    // The readdir is complete.
    return ERROR_OK;
  }

  return GetErrorFromErrnoForReadDir(read_dir_result);
}

ErrorType SmbProvider::ContinueReadDirectory(int32_t read_dir_token,
                                             DirectoryEntryListProto* entries) {
  DCHECK(entries);

  DCHECK_GE(read_dir_token, 0);
  DCHECK(read_dir_tracker_.Contains(read_dir_token));

  int32_t read_dir_result;
  const bool should_continue_read_dir =
      read_dir_tracker_.Find(read_dir_token)
          ->second->ContinueReadDir(&read_dir_result, entries);

  if (should_continue_read_dir) {
    DCHECK_EQ(0, read_dir_result);
    // The read directory needs to be continued.
    return ERROR_OPERATION_PENDING;
  }

  // The read directory no longer needs to be continued as it has either
  // completed successfully or it failed so remove it for |read_dir_tracker_|.
  read_dir_tracker_.Remove(read_dir_token);

  if (read_dir_result == 0) {
    // The readdir is complete.
    return ERROR_OK;
  }

  return GetErrorFromErrno(read_dir_result);
}

// These are required to explicitly instantiate the template functions.
template ErrorType SmbProvider::StartCopyProgress<FileCopyProgress>(
    const CopyEntryOptionsProto&,
    const std::string&,
    const std::string&,
    int32_t*);
template ErrorType SmbProvider::StartCopyProgress<RecursiveCopyProgress>(
    const CopyEntryOptionsProto&,
    const std::string&,
    const std::string&,
    int32_t*);

}  // namespace smbprovider
