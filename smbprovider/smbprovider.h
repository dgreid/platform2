// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SMBPROVIDER_SMBPROVIDER_H_
#define SMBPROVIDER_SMBPROVIDER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <dbus_adaptors/org.chromium.SmbProvider.h>

#include "smbprovider/copy_progress_interface.h"
#include "smbprovider/id_map.h"
#include "smbprovider/iterator/caching_iterator.h"
#include "smbprovider/iterator/share_iterator.h"
#include "smbprovider/kerberos_artifact_synchronizer.h"
#include "smbprovider/mount_config.h"
#include "smbprovider/proto.h"
#include "smbprovider/proto_bindings/directory_entry.pb.h"
#include "smbprovider/read_dir_progress.h"
#include "smbprovider/smb_credential.h"
#include "smbprovider/smbprovider_helper.h"
#include "smbprovider/temp_file_manager.h"

using brillo::dbus_utils::AsyncEventSequencer;

namespace smbprovider {

class DirectoryEntryListProto;
class MountManager;
class PostDepthFirstIterator;
class SambaInterface;

// Helper method that reads entries using an |iterator| and outputs them to
// |out_entries|. Returns true on success and sets |error_code| on failure.
// |options| is used for logging purposes.
bool GetEntries(const ReadDirectoryOptionsProto& options,
                CachingIterator iterator,
                int32_t* error_code,
                ProtoBlob* out_entries);

// Helper method that reads shares on a host using a Share Iterator and outputs
// them to |out_entries|. Returns true on success and sets |error_code| on
// failure. |options| is used for logging purposes.
bool GetShareEntries(const GetSharesOptionsProto& options,
                     ShareIterator iterator,
                     int32_t* error_code,
                     ProtoBlob* out_entries);

// Implementation of smbprovider's DBus interface. Mostly routes stuff between
// DBus and samba_interface.
class SmbProvider : public org::chromium::SmbProviderAdaptor,
                    public org::chromium::SmbProviderInterface {
 public:
  using SetupKerberosCallback =
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>>;

  SmbProvider(std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object,
              std::unique_ptr<MountManager> mount_manager,
              std::unique_ptr<KerberosArtifactSynchronizer>
                  kerberos_artifact_synchronizer,
              const base::FilePath& daemon_store_directory);
  SmbProvider(const SmbProvider&) = delete;
  SmbProvider& operator=(const SmbProvider&) = delete;

  // org::chromium::SmbProviderInterface: (see org.chromium.SmbProvider.xml).
  void Mount(const ProtoBlob& options_blob,
             const base::ScopedFD& password_fd,
             int32_t* error_code,
             int32_t* mount_id) override;

  int32_t Unmount(const ProtoBlob& options_blob) override;

  void ReadDirectory(const ProtoBlob& options_blob,
                     int32_t* error_code,
                     ProtoBlob* out_entries) override;

  void GetMetadataEntry(const ProtoBlob& options_blob,
                        int32_t* error_code,
                        ProtoBlob* out_entry) override;

  void OpenFile(const ProtoBlob& options_blob,
                int32_t* error_code,
                int32_t* file_id) override;

  int32_t CloseFile(const ProtoBlob& options_blob) override;

  int32_t DeleteEntry(const ProtoBlob& options_blob) override;

  void ReadFile(const ProtoBlob& options_blob,
                int32_t* error_code,
                brillo::dbus_utils::FileDescriptor* temp_fd) override;

  int32_t CreateFile(const ProtoBlob& options_blob) override;

  int32_t Truncate(const ProtoBlob& options_blob) override;

  int32_t WriteFile(const ProtoBlob& options_blob,
                    const base::ScopedFD& temp_fd) override;

  int32_t CreateDirectory(const ProtoBlob& options_blob) override;

  int32_t MoveEntry(const ProtoBlob& options_blob) override;

  int32_t CopyEntry(const ProtoBlob& options_blob) override;

  void GetDeleteList(const ProtoBlob& options,
                     int32_t* error_code,
                     brillo::dbus_utils::FileDescriptor* temp_fd,
                     int32_t* bytes_written) override;

  void GetShares(const ProtoBlob& options_blob,
                 int32_t* error_code,
                 ProtoBlob* shares) override;

  void SetupKerberos(SetupKerberosCallback callback,
                     const std::string& account_identifier) override;

  ProtoBlob ParseNetBiosPacket(const std::vector<uint8_t>& packet,
                               uint16_t transaction_id) override;

  void StartCopy(const ProtoBlob& options_blob,
                 int32_t* error_code,
                 int32_t* copy_token) override;

  int32_t ContinueCopy(int32_t mount_id, int32_t copy_token) override;

  void StartReadDirectory(const ProtoBlob& options_blob,
                          int32_t* error_code,
                          ProtoBlob* out_entries,
                          int32_t* read_dir_token) override;

  void ContinueReadDirectory(int32_t mount_id,
                             int32_t read_dir_token,
                             int32_t* error_code,
                             ProtoBlob* out_entries) override;

  int32_t UpdateMountCredentials(const ProtoBlob& options_blob,
                                 const base::ScopedFD& password_fd) override;

  int32_t UpdateSharePath(const ProtoBlob& options_blob) override;

  // Register DBus object and interfaces.
  void RegisterAsync(
      const AsyncEventSequencer::CompletionAction& completion_callback);

 private:
  // Returns a pointer to the SambaInterface corresponding to |mount_id|.
  SambaInterface* GetSambaInterface(int32_t mount_id) const;

  // Uses |options| to create the full path based on the mount id and entry path
  // supplied in |options|. |full_path| will be unmodified on failure.
  template <typename Proto>
  bool GetFullPath(const Proto* options, std::string* full_path) const;

  // Uses |options| to create the source and target paths based on the mount id,
  // source path and target path supplied in |options|. |source_full_path| and
  // |target_full_path| will be unmodified on failure.
  template <typename Proto>
  bool GetFullPaths(const Proto* options,
                    std::string* source_full_path,
                    std::string* target_full_path) const;

  // Parses the raw contents of |blob| into |options| and validates that
  // the required fields are all correctly set.
  // |full_path| will contain the full path, including the mount root, based
  // on the mount id and entry path supplied in |options|.
  // On failure |error_code| will be populated and |options| and |full_path|
  // are undefined.
  template <typename Proto>
  bool ParseOptionsAndPath(const ProtoBlob& blob,
                           Proto* options,
                           std::string* full_path,
                           int32_t* error_code);

  // Variation of ParseOptionsAndPath. |source_path| and |target_path| will
  // contain full paths, including the mount root, based on the mount id and
  // paths supplied in |options|.
  template <typename Proto>
  bool ParseOptionsAndPaths(const ProtoBlob& blob,
                            Proto* options,
                            std::string* source_path,
                            std::string* target_path,
                            int32_t* error_code);

  // Tests whether |mount_root| can be read by attempting to open the root
  // directory.
  bool CanReadMountRoot(int32_t mount_id,
                        const std::string& mount_root,
                        int32_t* error_code);

  // Helper method to get the type of an entry. Returns boolean indicating
  // success. Sets is_directory to true for directory, and false for file.
  // Fails when called on non-file, non-directory.
  // On failure, returns false and sets |error_code|.
  bool GetEntryType(int32_t mount_id,
                    const std::string& full_path,
                    int32_t* error_code,
                    bool* is_directory);

  // Helper method to close the directory with id |dir_id|. Logs an error if the
  // directory fails to close.
  void CloseDirectory(int32_t mount_id, int32_t dir_id);

  // Removes |mount_id| from the |mount_manager_| object and sets |error_code|
  // on failure.
  bool RemoveMount(int32_t mount_id, int32_t* error_code);

  // Adds mount |mount_root| to mount_manager_ and sets |error_code| on failure.
  // A Credential that has fields such as |workgroup|, |username| and
  // |password_fd| will be used if provided. Sets |mount_id| when mount is
  // successful.
  void AddMount(const std::string& mount_root,
                const MountConfig& mount_config,
                const std::string& workgroup,
                const std::string& username,
                const base::ScopedFD& password_fd,
                const base::FilePath& password_file,
                int32_t* mount_id);

  // Removes |mount_id| from mount_manager_ if |mount_id| is mounted.
  void RemoveMountIfMounted(int32_t mount_id);

  // Helper method to read a file with valid |options| and output the results
  // into |content_buffer_|. This sets |error_code| on failure.
  bool ReadFileIntoBuffer(const ReadFileOptionsProto& options,
                          int32_t* error_code);

  // Helper method to write data from a |buffer| into a temporary file and
  // outputs the resulting file descriptor into |temp_fd|. |options| is used for
  // logging purposes. This sets |error_code| on failure.
  template <typename Proto>
  bool WriteTempFile(const Proto& options,
                     const std::vector<uint8_t>& buffer,
                     int32_t* error_code,
                     brillo::dbus_utils::FileDescriptor* temp_fd);

  // Writes |delete_list| to a temporary file and outputs the resulting file
  // descriptor into |temp_fd|. Sets |bytes_written| to the number of bytes
  // if writing suceeds, -1 on failure. |options| is used for logging. Sets
  // |error_code| on failure.
  bool WriteDeleteListToTempFile(const GetDeleteListOptionsProto& options,
                                 const DeleteListProto& delete_list,
                                 int32_t* error_code,
                                 brillo::dbus_utils::FileDescriptor* temp_fd,
                                 int32_t* bytes_written);

  // Helper method to write data from |content_buffer_| into a file specified by
  // |file_id|. |options| is used for logging. Returns true on success, and sets
  // |error_code| on failure.
  bool WriteFileFromBuffer(const WriteFileOptionsProto& options,
                           int32_t file_id,
                           int32_t* error_code);

  // Moves an entry at |source_path| to |target_path|. |options| is used for
  // logging. Returns true on success. Sets |error| and returns false on
  // failure.
  bool MoveEntry(const MoveEntryOptionsProto& options,
                 const std::string& source_path,
                 const std::string& target_path,
                 int32_t* error);

  // Calls delete on the contents of |dir_path| via postorder traversal.
  // RecursiveDelete exits and returns error if an entry cannot be deleted
  // or there was a Samba error iterating over entries.
  int32_t RecursiveDelete(int32_t mount_id, const std::string& dir_path);

  // Calls delete on a DirectoryEntry. Returns the result from either DeleteFile
  // or DeleteDirectory.
  int32_t DeleteDirectoryEntry(int32_t mount_id, const DirectoryEntry& entry);

  // Calls Unlink.
  int32_t DeleteFile(int32_t mount_id, const std::string& file_path);

  // Calls RemoveDirectory.
  int32_t DeleteDirectory(int32_t mount_id, const std::string& dir_path);

  // Removes |full_path| from the cache in the mount with |mount_id|. If the
  // entry was not in the cache, this is a no-op.
  void InvalidateCacheEntry(int32_t mount_id, const std::string& full_path);

  // Helper method to construct a PostDepthFirstIterator for a given
  // |full_path|.
  PostDepthFirstIterator GetPostOrderIterator(int32_t mount_id,
                                              const std::string& full_path);

  // Opens a file located at |full_path| with permissions based on the protobuf.
  // |file_id| is the file handle for the opened file, and error will be set on
  // failure. |options| is used for permissions and logging.
  // GetOpenFilePermissions must be overloaded to new protobufs using this
  // method. Returns true on success.
  template <typename Proto>
  bool OpenFile(const Proto& options,
                const std::string& full_path,
                int32_t* error,
                int32_t* file_id);

  // Closes a file with handle |file_id|. |options| is used for logging
  // purposes. |error| is set on failure. Returns true on success.
  template <typename Proto>
  bool CloseFile(const Proto& options, int32_t file_id, int32_t* error);

  // Truncates a file with handle |file_id| to the desired |length| and closes
  // the file whether or not the truncate was successful. |options| is used for
  // logging purposes. |error| is set on failure. Returns true on success.
  template <typename Proto>
  bool TruncateAndCloseFile(const Proto& options,
                            int32_t file_id,
                            int64_t length,
                            int32_t* error);

  // Helper method to seek given a proto |options|.
  // On failure |error_code| will be populated.
  template <typename Proto>
  bool Seek(const Proto& options, int32_t* error_code);

  // Creates the parent directories based on |options| if necessary. This call
  // will return true if no directories need to be created. Returns true on
  // success and sets |error_code| on failure.
  bool CreateParentsIfNecessary(const CreateDirectoryOptionsProto& options,
                                int32_t* error_code);

  // Helper method to create nested directories in |paths|. |paths| must be a
  // successive hierarchy of directories starting from the top-most parent. This
  // will succeed even if all the directories in |paths| already exists.
  // |options| is used for logging purposes. Returns true on success and sets
  // |error_code| on failure.
  bool CreateNestedDirectories(const CreateDirectoryOptionsProto& options,
                               const std::vector<std::string>& paths,
                               int32_t* error_code);

  // Helper method to create a single directory at |full_path|. |options| is
  // used for logging purposes. If |ignore_existing| is true, this will ignore
  // EEXIST errors. Returns true on success and sets |error_code| on failure.
  template <typename Proto>
  bool CreateSingleDirectory(const Proto& options,
                             const std::string& full_path,
                             bool ignore_existing,
                             int32_t* error_code);

  // Generates a vector of |parent_paths| from a directory path in |options|.
  // The path must be prefixed with "/". |parent_paths| will include the mount
  // root and will exclude the path in |options|. Passing in "/1/2/3" will
  // generate ["smb://{mount}/1", "smb://{mount}/1/2"]. If the path has no
  // parents aside from "/", no paths will be generated. Returns true on success
  // and sets |error_code| on failure.
  bool GenerateParentPaths(const CreateDirectoryOptionsProto& options,
                           int32_t* error_code,
                           std::vector<std::string>* parent_paths);

  // Creates a file at |full_path|. Sets |file_id| to fd of newly created file.
  // Returns false and sets |error| on failure. |options| is used for
  template <typename Proto>
  bool CreateFile(const Proto& options,
                  const std::string& full_path,
                  int32_t* file_id,
                  int32_t* error);

  // Copies the entry at |source_path| to |target_path|. Returns true on
  // success. Returns false and sets |error_code| on failure.
  bool CopyEntry(const CopyEntryOptionsProto& options,
                 const std::string& source_path,
                 const std::string& target_path,
                 int32_t* error_code);

  // Copies the file at |source_path| to a created file at |target_path|.
  // Returns true on success. Returns false and sets |error_code| on failure.
  bool CopyFile(const CopyEntryOptionsProto& options,
                const std::string& source_path,
                const std::string& target_path,
                int32_t* error_code);

  // Helper method that fills |content_buffer_| by reading the file with handle
  // |file_id|. Returns false and sets |error_code| on failure. |options| is
  // used for logging.
  bool ReadToContentBuffer(const ReadFileOptionsProto& options,
                           int32_t file_id,
                           int32_t* error_code);

  // Reads the entries in a directory using the specified type of |Iterator| and
  // outputs the entries in |out_entries|. |options_blob| is parsed into a
  // |Proto| object and is used as input for the iterator. |error_code| is set
  // on failure.
  void ReadDirectoryEntries(const ProtoBlob& options_blob,
                            int32_t* error_code,
                            ProtoBlob* out_entries);

  // Reads the shares on a host and outputs the shares in |out_entries|.
  // |options_blob| is used as input for the ShareIterator. |error_code| is set
  // on failure.
  void ReadShareEntries(const ProtoBlob& options_blob,
                        int32_t* error_code,
                        ProtoBlob* out_entries);

  // Populates |delete_list| with an ordered list of relative paths of entries
  // that must be deleted in order to recursively delete |full_path|.
  int32_t GenerateDeleteList(const GetDeleteListOptionsProto& options,
                             const std::string& full_path,
                             bool is_directory,
                             DeleteListProto* delete_list);

  bool GetCachedEntry(int32_t mount_id,
                      const std::string full_path,
                      ProtoBlob* out_entry);

  // Generates an empty file with a file descriptor for transport over D-Bus.
  // This is needed since FileDescriptor causes a crash while being transported
  // if it is not valid.
  brillo::dbus_utils::FileDescriptor GenerateEmptyFile();

  // Returns the relative path to |entry_path| by removing the server path
  // associated with |mount_id|.
  std::string GetRelativePath(int32_t mount_id, const std::string& entry_path);

  // Callback handler for SetupKerberos.
  void HandleSetupKerberosResponse(SetupKerberosCallback callback, bool result);

  // Creates a HostnamesProto from a list of |hostnames|.
  HostnamesProto BuildHostnamesProto(
      const std::vector<std::string>& hostnames) const;

  // Starts a copy of |source_path| to |target_path|.
  // Returns ERROR_COPY_PENDING and sets |copy_token| if the copy must be
  // continued, ERROR_OK if the copy was completed successfully, and any other
  // ErrorType if an error is encountered.
  ErrorType StartCopy(const CopyEntryOptionsProto& options,
                      const std::string& source_path,
                      const std::string& target_path,
                      int32_t* copy_token);

  // Starts a recursive copy of the directory at |source_path| to |target_path|.
  // Returns ERROR_COPY_PENDING and sets |copy_token| if the copy must be
  // continued, ERROR_OK if the copy was completed successfully, and any other
  // ErrorType if an error is encountered.
  ErrorType StartDirectoryCopy(const CopyEntryOptionsProto& options,
                               const std::string& source_path,
                               const std::string& target_path,
                               int32_t* copy_token);

  // Starts a copy of the file at |source_path| to |target_path|.
  // Returns ERROR_COPY_PENDING and sets |copy_token| if the copy must be
  // continued, ERROR_OK if the copy was completed successfully, and any other
  // ErrorType if an error is encountered.
  ErrorType StartFileCopy(const CopyEntryOptionsProto& options,
                          const std::string& source_path,
                          const std::string& target_path,
                          int32_t* copy_token);

  template <typename CopyProgressType>
  ErrorType StartCopyProgress(const CopyEntryOptionsProto& options,
                              const std::string& source_path,
                              const std::string& target_path,
                              int32_t* copy_token);

  // Continues the copy mapped to |copy_token|. Returns ERROR_COPY_PENDING
  // if the copy must be continued, ERROR_OK if the copy was completed
  // successfully, and any other ErrorType if an error is encountered.
  ErrorType ContinueCopy(int32_t copy_token);

  // Starts a read directory of the directory at |directory_path|.
  // Returns ERROR_OPERATION_PENDING and sets |read_dir_token| if the read dir
  // must be continued, ERROR_OK if the read directory was completed
  // successfully, and any other ErrorType if an error is encountered.
  ErrorType StartReadDirectory(const ReadDirectoryOptionsProto& options,
                               const std::string& directory_path,
                               DirectoryEntryListProto* entries,
                               int32_t* read_dir_token);

  // Continues the read directory mapped to |read_dir_token|. Returns
  // ERROR_OPERATION_PENDING if the read directory must be continued, ERROR_OK
  // if the read directory was completed successfully, and any other ErrorType
  // if an error is encountered.
  ErrorType ContinueReadDirectory(int32_t read_dir_token,
                                  DirectoryEntryListProto* entries);

  // Return the daemon store directory for the user profile |username_hash|.
  base::FilePath GetDaemonStoreDirectory(
      const std::string& username_hash) const;

  // Returns true if |mount_id_| corresponds to a valid mount. Returns false and
  // sets |error_code| to ERROR_NOT_FOUND otherwise.
  template <typename Proto>
  bool IsMounted(const Proto& options, int32_t* error_code) const;

  std::vector<uint8_t> content_buffer_;
  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
  std::unique_ptr<MountManager> mount_manager_;
  std::unique_ptr<KerberosArtifactSynchronizer> kerberos_artifact_synchronizer_;
  TempFileManager temp_file_manager_;
  // TODO(baileyberro): Add metrics for |copy_tracker_| and |read_dir_tracker_|
  // https://crbug.com/887606.
  // Keeps track of in-progress copy operations. Maps a copy token to a
  // CopyProgress.
  IdMap<std::unique_ptr<CopyProgressInterface>> copy_tracker_;
  // Keeps track of in-progress copy operations. Maps a read dir token to a
  // ReadDirProgress.
  IdMap<std::unique_ptr<ReadDirProgress>> read_dir_tracker_;
  const base::FilePath daemon_store_directory_;
};

}  // namespace smbprovider

#endif  // SMBPROVIDER_SMBPROVIDER_H_
