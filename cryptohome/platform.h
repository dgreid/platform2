// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_PLATFORM_H_
#define CRYPTOHOME_PLATFORM_H_

#include <stdint.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include <base/callback_forward.h>
#include <base/files/file_enumerator.h>
#include <base/macros.h>
#include <base/memory/scoped_ptr.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest_prod.h>

namespace base {
class Thread;
class Time;
}  // namespace base

namespace cryptohome {

// Default umask
extern const int kDefaultUmask;

class ProcessInformation;

// A class for enumerating the files in a provided path. The order of the
// results is not guaranteed.
//
// DO NOT USE FROM THE MAIN THREAD of your application unless it is a test
// program where latency does not matter. This class is blocking.
//
// See base::FileEnumerator for details.  This is merely a mockable wrapper.
class FileEnumerator {
 public:
  // Copy and assign enabled.
  class FileInfo {
   public:
    explicit FileInfo(const base::FileEnumerator::FileInfo& file_info);
    FileInfo(const std::string& name, const struct stat& stat);
    FileInfo(const FileInfo& other);
    FileInfo();
    virtual ~FileInfo();
    FileInfo& operator=(const FileInfo& other);

    bool IsDirectory() const;
    std::string GetName() const;
    int64_t GetSize() const;
    base::Time GetLastModifiedTime() const;
    const struct stat& stat() const;

   private:
    void Assign(const base::FileEnumerator::FileInfo& file_info);

    scoped_ptr<base::FileEnumerator::FileInfo> info_;
    std::string name_;
    struct stat stat_;
  };

  FileEnumerator(const std::string& root_path,
                 bool recursive,
                 int file_type);
  FileEnumerator(const std::string& root_path,
                 bool recursive,
                 int file_type,
                 const std::string& pattern);
  // Meant for testing only.
  FileEnumerator();
  virtual ~FileEnumerator();

  // Returns an empty string if there are no more results.
  virtual std::string Next();

  // Write the file info into |info|.
  virtual FileInfo GetInfo();

 private:
  scoped_ptr<base::FileEnumerator> enumerator_;
};

// Platform specific routines abstraction layer.
// Also helps us to be able to mock them in tests.
class Platform {
 public:
  struct Permissions {
    uid_t user;
    gid_t group;
    mode_t mode;
  };

  typedef base::Callback<bool(const std::string&, const struct stat&)>
      FileEnumeratorCallback;

  Platform();

  virtual ~Platform();

  // Calls the platform mount
  //
  // Parameters
  //   from - The node to mount from
  //   to - The node to mount to
  //   type - The fs type
  //   mount_options - The mount options to pass to mount()
  virtual bool Mount(const std::string& from, const std::string& to,
                     const std::string& type, const std::string& mount_options);

  // Creates a bind mount
  //
  // Parameters
  //   from - Where to mount from
  //   to - Where to mount to
  virtual bool Bind(const std::string& from, const std::string& to);

  // Calls the platform unmount
  //
  // Parameters
  //   path - The path to unmount
  //   lazy - Whether to call a lazy unmount
  //   was_busy (OUT) - Set to true on return if the mount point was busy
  virtual bool Unmount(const std::string& path, bool lazy, bool* was_busy);

  // Lazy unmounts |path| and then calls sync().  If |sync_first| is true then
  // it also calls sync() before unmount.
  //
  // Parameters
  //   path - The path to unmount
  //   sync_first - Whether to call sync() before unmount.
  virtual void LazyUnmountAndSync(const std::string& path, bool sync_first);

  // Returns true if any mounts match. Populates |mounts| if
  // any mount sources have a matching prefix (|from_prefix|).
  //
  // Parameters
  //   from_prefix - Prefix for matching mount sources
  //   mounts - matching mounted paths, may be NULL
  virtual bool GetMountsBySourcePrefix(const std::string& from_prefix,
                  std::multimap<const std::string, const std::string>* mounts);

  // Returns true if the directory is in the mtab
  //
  // Parameters
  //   directory - The directory to check
  virtual bool IsDirectoryMounted(const std::string& directory);

  // Returns true if the directory is in the mtab mounted with the specified
  // source
  //
  // Parameters
  //   directory - The directory to check
  //   from - The source node
  virtual bool IsDirectoryMountedWith(const std::string& directory,
                                      const std::string& from);

  // GetProcessesWithOpenFiles
  //
  // Parameters
  //   path - The path to check if the process has open files on
  //   pids (OUT) - The PIDs found
  virtual void GetProcessesWithOpenFiles(const std::string& path_in,
                                 std::vector<ProcessInformation>* processes);

  // Calls the platform stat() function to obtain the ownership of
  // a given path. The path may be a directory or a file.
  //
  // Parameters
  //   path - The path to look up
  //   user_id - The user ID of the path. NULL if the result is not needed.
  //   group_id - The group ID of the path. NULL if the result is not needed.
  virtual bool GetOwnership(const std::string& path, uid_t* user_id,
                            gid_t* group_id) const;

  // Calls the platform chown() function on the given path.
  //
  // The path may be a directory or a file.
  //
  // Parameters
  //   path - The path to set ownership on
  //   user_id - The user_id to assign ownership to
  //   group_id - The group_id to assign ownership to
  virtual bool SetOwnership(const std::string& directory, uid_t user_id,
                            gid_t group_id) const;

  // Calls the platform stat() function to obtain the permissions of
  // the given path. The path may be a directory or a file.
  //
  // Parameters
  //   path - The path to look up
  //   mode - The permissions of the path
  virtual bool GetPermissions(const std::string& path, mode_t* mode) const;

  // Calls the platform chmod() function on the given path.
  // The path may be a directory or a file.
  //
  // Parameters
  //   path - The path to change the permissions on
  //   mode - the mode to change the permissions to
  virtual bool SetPermissions(const std::string& path, mode_t mode) const;

  // Sets the path accessible by a group with specified permissions
  //
  // Parameters
  //   path - The path to change the ownership and permissions on
  //   group_id - The group ID to assign to the path
  //   group_mode - The group permissions to assign to the path
  virtual bool SetGroupAccessible(const std::string& path,
                                  gid_t group_id,
                                  mode_t group_mode) const;

  // Applies ownership and permissions recursively if they do not already match.
  // Logs a warning each time ownership or permissions need to be set.
  //
  // Parameters
  //   path - The base path.
  //   default_file_info - Default ownership / perms for files.
  //   default_dir_info - Default ownership / perms for directories.
  //   special_cases - A map of absolute path to ownership / perms.
  virtual bool ApplyPermissionsRecursive(
      const std::string& path,
      const Permissions& default_file_info,
      const Permissions& default_dir_info,
      const std::map<std::string, Permissions>& special_cases);

  // Sets the current umask, returning the old mask
  //
  // Parameters
  //   new_mask - The mask to set
  virtual int SetMask(int new_mask) const;

  // Returns the user and group ids for a user
  //
  // Parameters
  //   user - The username to query for
  //   user_id (OUT) - The user ID on success
  //   group_id (OUT) - The group ID on success
  virtual bool GetUserId(const std::string& user, uid_t* user_id,
                         gid_t* group_id) const;

  // Returns the group id for a group
  //
  // Parameters
  //   group - The group name to query for
  //   group_id (OUT) - The group ID on success
  virtual bool GetGroupId(const std::string& group, gid_t* group_id) const;

  // Return the available disk space in bytes on the volume containing |path|,
  // or -1 on failure.
  // Code duplicated from Chrome's base::SysInfo::AmountOfFreeDiskSpace().
  //
  // Parameters
  //   path - the pathname of any file within the mounted file system
  virtual int64_t AmountOfFreeDiskSpace(const std::string& path) const;

  // Returns true if the specified file exists.
  //
  // Parameters
  //  path - Path of the file to check
  virtual bool FileExists(const std::string& path);

  // Check if a directory exists as the given path
  virtual bool DirectoryExists(const std::string& path);

  // Provides the size of a file at |path| if it exists.
  //
  // Parameters
  //   path - Path of the file to check
  //   size - int64_t* to populate with the size
  // Returns true if the size was acquired and false otherwise.
  virtual bool GetFileSize(const std::string& path, int64_t* size);

  // Opens a file, if possible, returning a FILE*. If not, returns NULL.
  //
  // Parameters
  //   path - Path of the file to open
  //   mode - mode string of the file when opened
  virtual FILE* OpenFile(const std::string& path, const char* mode);

  // Closes a FILE* opened with OpenFile()
  //
  // Parameters
  //  fp - FILE* to close
  virtual bool CloseFile(FILE* fp);

  // Creates and opens a temporary file if possible.
  //
  // Parameters
  //  path - Pointer to where the file is created if successful.
  virtual FILE* CreateAndOpenTemporaryFile(std::string* path);

  // Reads a file completely into a blob/string.
  //
  // Parameters
  //  path              - Path of the file to read
  //  blob/string (OUT) - blob/string to populate
  virtual bool ReadFile(const std::string& path, brillo::Blob* blob);
  virtual bool ReadFileToString(const std::string& path, std::string* string);

  // Writes to the open file pointer.
  //
  // Parameters
  //   fp   - pointer to the FILE*
  //   blob - data to write
  virtual bool WriteOpenFile(FILE* fp, const brillo::Blob& blob);

  // Writes the entirety of the given data to |path| with 0640 permissions
  // (modulo umask).  If missing, parent (and parent of parent etc.) directories
  // are created with 0700 permissions (modulo umask).  Returns true on success.
  //
  // Parameters
  //  path      - Path of the file to write
  //  blob/data - blob/string/array to populate from
  // (size      - array size)
  virtual bool WriteFile(const std::string& path, const brillo::Blob& blob);
  virtual bool WriteStringToFile(const std::string& path,
                                 const std::string& data);
  virtual bool WriteArrayToFile(const std::string& path, const char* data,
                                size_t size);

  // Atomically writes the entirety of the given data to |path| with |mode|
  // permissions (modulo umask).  If missing, parent (and parent of parent etc.)
  // directories are created with 0700 permissions (modulo umask).  Returns true
  // if the file has been written successfully and it has physically hit the
  // disk.  Returns false if either writing the file has failed or if it cannot
  // be guaranteed that it has hit the disk.
  //
  // Parameters
  //   path - Path of the file to write
  //   data - Blob to populate from
  //   mode - File permission bit-pattern, eg. 0644 for rw-r--r--
  bool WriteFileAtomic(const std::string& path,
                       const brillo::Blob& blob,
                       mode_t mode);
  bool WriteStringToFileAtomic(const std::string& path,
                               const std::string& data,
                               mode_t mode);

  // Atomically and durably writes the entirety of the given data to |path| with
  // |mode| permissions (modulo umask).  If missing, parent (and parent of
  // parent etc.)  directories are created with 0700 permissions (modulo umask).
  // Returns true if the file has been written successfully and it has
  // physically hit the disk.  Returns false if either writing the file has
  // failed or if it cannot be guaranteed that it has hit the disk.
  //
  // Parameters
  //  path      - Path of the file to write
  //  blob/data - blob/string to populate from
  //  mode      - File permission bit-pattern, eg. 0644 for rw-r--r--
  virtual bool WriteFileAtomicDurable(const std::string& path,
                                      const brillo::Blob& blob,
                                      mode_t mode);
  virtual bool WriteStringToFileAtomicDurable(const std::string& path,
                                              const std::string& data,
                                              mode_t mode);

  // Creates empty file durably, i.e. ensuring that the directory entry is
  // created on-disk immediately.  Set 0640 permissions (modulo umask).
  //
  // Parameters
  //   path - Path to the file to create
  virtual bool TouchFileDurable(const std::string& path);

  // Delete file(s) at the given path
  //
  // Parameters
  //  path - string containing file path to delete
  //  recursive - whether to perform recursive deletion of the subtree
  virtual bool DeleteFile(const std::string& path, bool recursive);

  // Deletes file durably, i.e. ensuring that the directory entry is immediately
  // removed from the on-disk directory structure.
  //
  // Parameters
  //   path - Path to the file to delete
  virtual bool DeleteFileDurable(const std::string& path, bool recursive);

  // Create a directory with the given path (including parent directories, if
  // missing).  All created directories will have 0700 permissions (modulo
  // umask).
  virtual bool CreateDirectory(const std::string& path);

  // Enumerate all directory entries in a given directory
  //
  // Parameters
  //  path - root of the tree to enumerate
  //  is_recursive - true to enumerate recursively
  //  ent_list - vector of strings to add enumerate directory entry paths into
  virtual bool EnumerateDirectoryEntries(const std::string& path,
                                         bool is_recursive,
                                         std::vector<std::string>* ent_list);

  // Returns a new FileEnumerator instance.
  //
  // The caller TAKES OWNERSHIP of the returned pointer.
  //
  // Parameters
  // (see FileEnumerator())
  virtual FileEnumerator* GetFileEnumerator(const std::string& root_path,
                                            bool recursive,
                                            int file_type);

  // Look up information about a file or directory
  //
  // Parameters
  //  path - element to look up
  //  buf - buffer to store results into
  virtual bool Stat(const std::string& path, struct stat *buf);

  // Rename a file or directory
  //
  // Parameters
  //  from
  //  to
  virtual bool Rename(const std::string& from, const std::string& to);

  // Retuns the current time.
  virtual base::Time GetCurrentTime() const;

  // Copies from to to.
  virtual bool Copy(const std::string& from, const std::string& to);

  // Copies and retains permissions and ownership.
  virtual bool CopyWithPermissions(const std::string& from,
                                   const std::string& to);

  // Moves a given path on the filesystem
  //
  // Parameters
  //   from - path to move
  //   to   - destination of the move
  virtual bool Move(const std::string& from, const std::string& to);

  // Calls statvfs() on path.
  //
  // Parameters
  //   path - path to statvfs on
  //   vfs - buffer to store result in
  virtual bool StatVFS(const std::string& path, struct statvfs* vfs);

  // Find the device for a given filesystem.
  //
  // Parameters
  //   filesystem - the filesystem to examine
  //   device - output: the device name that "filesystem" in mounted on
  virtual bool FindFilesystemDevice(const std::string &filesystem,
                                    std::string *device);

  // Runs "tune2fs -l" with redirected output.
  //
  // Parameters
  //  filesystem - the filesystem to examine
  //  lgofile - the path written with output
  virtual bool ReportFilesystemDetails(const std::string &filesystem,
                                       const std::string &logfile);


  // Clears the kernel-managed user keyring
  virtual long ClearUserKeyring();  // NOLINT(runtime/int)

  // Creates an ecryptfs auth token and installs it in the kernel keyring.
  //
  // Parameters
  //   key - The key to add
  //   key_sig - The key's (ascii) signature
  //   salt - The salt
  virtual long AddEcryptfsAuthToken(const brillo::SecureBlob& key,  // NOLINT
                                    const std::string& key_sig,
                                    const brillo::SecureBlob& salt);

  // Override the location of the mtab file used. Default is kMtab.
  virtual void set_mtab_path(const std::string &mtab_path) {
    mtab_path_ = mtab_path;
  }

  // Report condition of the Firmware Write-Protect flag.
  virtual bool FirmwareWriteProtected();

  // Syncs file data to disk for the given |path| but syncs metadata only to the
  // extent that is required to acess the file's contents.  (I.e. the directory
  // entry and file size are sync'ed if changed, but not atime or mtime.)  This
  // method is expensive and synchronous, use with care.  Returns true on
  // success.
  virtual bool DataSyncFile(const std::string& path);

  // Syncs everything to disk.  This method is synchronous and very, very
  // expensive, use with even more care than SyncFile.
  virtual void Sync();

  // Gets the system HWID. This is the same ID that is used on some systems to
  // extend the TPM's PCR_1.
  virtual std::string GetHardwareID();

 private:
  // Returns the process and open file information for the specified process id
  // with files open on the given path
  //
  // Parameters
  //   pid - The process to check
  //   path_in - The file path to check for
  //   process_info (OUT) - The ProcessInformation to store the results in
  void GetProcessOpenFileInformation(pid_t pid, const std::string& path_in,
                                     ProcessInformation* process_info);

  // Returns a vector of PIDs that have files open on the given path
  //
  // Parameters
  //   path - The path to check if the process has open files on
  //   pids (OUT) - The PIDs found
  void LookForOpenFiles(const std::string& path_in, std::vector<pid_t>* pids);

  // Returns true if child is a file or folder below or equal to parent.  If
  // parent is a directory, it should end with a '/' character.
  //
  // Parameters
  //   parent - The parent directory
  //   child - The child directory/file
  bool IsPathChild(const std::string& parent, const std::string& child);

  // Returns the target of the specified link
  //
  // Parameters
  //   link_path - The link to check
  std::string ReadLink(const std::string& link_path);

  // Creates a random string suitable to append to a filename.  Returns empty
  // string in case of error.
  virtual std::string GetRandomSuffix();

  // Calls fsync() on directory.  Returns true on success.
  //
  // Parameters
  //   path - File/directory to be sync'ed
  bool SyncDirectory(const std::string& path);

  // Calls fdatasync() on file or fsync() on directory.  Returns true on
  // success.
  //
  // Parameters
  //   path - File/directory to be sync'ed
  //   is_directory - True if |path| is a directory
  bool SyncFileOrDirectory(const std::string& path, bool is_directory);

  // Calls |callback| with |path| and, if |path| is a directory, with every
  // entry recursively.  Order is not guaranteed, see base::FileEnumerator.  If
  // |path| is an absolute path, then the file names sent to |callback| will
  // also be absolute.  Returns true if all invocations of |callback| succeed.
  // If an invocation fails, the walk terminates and false is returned.
  bool WalkPath(const std::string& path,
                const FileEnumeratorCallback& callback);

  // Copies permissions from a file specified by |file_path| and |file_info| to
  // another file with the same name but a child of |new_base|, not |old_base|.
  bool CopyPermissionsCallback(const std::string& old_base,
                               const std::string& new_base,
                               const std::string& file_path,
                               const struct stat& file_info);

  // Applies ownership and permissions to a single file or directory.
  //
  // Parameters
  //   default_file_info - Default ownership / perms for files.
  //   default_dir_info - Default ownership / perms for directories.
  //   special_cases - A map of absolute path to ownership / perms.
  //   file_info - Info about the file or directory.
  bool ApplyPermissionsCallback(
      const Permissions& default_file_info,
      const Permissions& default_dir_info,
      const std::map<std::string, Permissions>& special_cases,
      const std::string& file_path,
      const struct stat& file_info);

  void PostWorkerTask(const base::Closure& task);

  // Computes a checksum and returns an ASCII representation.
  std::string GetChecksum(const void* input, size_t input_size);

  // Computes a checksum of |content| and writes it atomically to the same
  // |path| but with a .sum suffix and the given |mode|.
  void WriteChecksum(const std::string& path,
                     const void* content,
                     size_t content_size,
                     mode_t mode);

  // Looks for a .sum file for |path| and verifies the checksum if it exists.
  void VerifyChecksum(const std::string& path,
                      const void* content,
                      size_t content_size);

  std::string mtab_path_;

  friend class PlatformTest;
  FRIEND_TEST(PlatformTest, SyncDirectoryHasSaneReturnCodes);
  DISALLOW_COPY_AND_ASSIGN(Platform);
};

class ProcessInformation {
 public:
  ProcessInformation()
      : cmd_line_(),
      process_id_(-1) { }
  virtual ~ProcessInformation() { }

  std::string GetCommandLine() {
    std::string result;
    for (std::vector<std::string>::iterator cmd_itr = cmd_line_.begin();
         cmd_itr != cmd_line_.end();
         cmd_itr++) {
      if (result.length()) {
        result.append(" ");
      }
      result.append((*cmd_itr));
    }
    return result;
  }

  // Set the command line array.  This method DOES swap out the contents of
  // |value|.  The caller should expect an empty vector on return.
  void set_cmd_line(std::vector<std::string>* value) {
    cmd_line_.clear();
    cmd_line_.swap(*value);
  }

  const std::vector<std::string>& get_cmd_line() {
    return cmd_line_;
  }

  // Set the command line array.  This method DOES swap out the contents of
  // |value|.  The caller should expect an empty set on return.
  void set_open_files(std::set<std::string>* value) {
    open_files_.clear();
    open_files_.swap(*value);
  }

  const std::set<std::string>& get_open_files() {
    return open_files_;
  }

  // Set the command line array.  This method DOES swap out the contents of
  // |value|.  The caller should expect an empty string on return.
  void set_cwd(std::string* value) {
    cwd_.clear();
    cwd_.swap(*value);
  }

  const std::string& get_cwd() {
    return cwd_;
  }

  void set_process_id(int value) {
    process_id_ = value;
  }

  int get_process_id() {
    return process_id_;
  }

 private:
  std::vector<std::string> cmd_line_;
  std::set<std::string> open_files_;
  std::string cwd_;
  int process_id_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_PLATFORM_H_
