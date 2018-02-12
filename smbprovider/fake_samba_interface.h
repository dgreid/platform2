// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SMBPROVIDER_FAKE_SAMBA_INTERFACE_H_
#define SMBPROVIDER_FAKE_SAMBA_INTERFACE_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "smbprovider/samba_interface.h"

namespace smbprovider {

// Fake implementation of SambaInterface. Uses a map to simulate a fake file
// system that can open and close directories. It can also store entries through
// |FakeEntry| which hold entry metadata.
class FakeSambaInterface : public SambaInterface {
 public:
  FakeSambaInterface();
  ~FakeSambaInterface() override;

  // SambaInterface overrides.
  int32_t OpenDirectory(const std::string& directory_path,
                        int32_t* dir_id) override;

  int32_t CloseDirectory(int32_t dir_id) override;

  int32_t GetDirectoryEntries(int32_t dir_id,
                              smbc_dirent* dirp,
                              int32_t dirp_buffer_size,
                              int32_t* bytes_read) override;

  int32_t GetEntryStatus(const std::string& entry_path,
                         struct stat* stat) override;

  int32_t OpenFile(const std::string& file_path,
                   int32_t flags,
                   int32_t* file_id) override;

  int32_t CloseFile(int32_t file_id) override;

  int32_t ReadFile(int32_t file_id,
                   uint8_t* buffer,
                   size_t buffer_size,
                   size_t* bytes_read) override;

  int32_t Seek(int32_t file_id, int64_t offset) override;

  int32_t Unlink(const std::string& file_path) override;

  int32_t RemoveDirectory(const std::string& dir_path) override;

  int32_t CreateFile(const std::string& file_path, int32_t* file_id) override;

  int32_t Truncate(int32_t file_id, size_t size) override;

  int32_t WriteFile(int32_t file_id,
                    const uint8_t* buffer,
                    size_t buffer_size) override;

  int32_t CreateDirectory(const std::string& directory_path) override;

  int32_t MoveEntry(const std::string& source_path,
                    const std::string& target_path) override;

  // Adds a directory that is able to be opened through OpenDirectory().
  // Does not support recursive creation. All parents must exist.
  void AddDirectory(const std::string& path);
  void AddDirectory(const std::string& path, bool locked);

  // Adds a directory with |locked| set to true. All parents must exist.
  // Operations on a locked directory will fail.
  void AddLockedDirectory(const std::string& path);

  // Adds a file at the specified path. All parents must exist.
  void AddFile(const std::string& path);
  void AddFile(const std::string& path, size_t size);
  void AddFile(const std::string& path, size_t size, uint64_t date);
  void AddFile(const std::string& path,
               size_t size,
               uint64_t date,
               bool locked);
  void AddFile(const std::string& path,
               uint64_t date,
               std::vector<uint8_t> file_data);

  // Adds a file at the specified path with |locked| set to true. All parents
  // must exist. Operations on a locked file will fail.
  void AddLockedFile(const std::string& path);

  void AddEntry(const std::string& path, uint32_t smbc_type);

  // Helper method to check if there are any leftover open directories or files
  // in |open_fds|.
  bool HasOpenEntries() const;

  // Helpers to check the flags set on a FakeFile entry.
  bool HasReadSet(int32_t fd) const;
  bool HasWriteSet(int32_t fd) const;

  // Helpers to check whether a given file descriptor |fd| is open.
  bool IsFileFDOpen(uint32_t fd) const;
  bool IsDirectoryFDOpen(uint32_t fd) const;

  // Checks whether an entry exists in a given |path|.
  bool EntryExists(const std::string& path) const;

  // Gets current offset of file.
  size_t GetFileOffset(int32_t fd) const;

  // Gets the current file size of a file in |path|.
  size_t GetFileSize(const std::string& path) const;

  // Checks if a files data is equal to the expected value. Returns true if
  // equal.
  bool IsFileDataEqual(const std::string& path,
                       const std::vector<uint8_t>& expected) const;

  // Helper method to set the errno CloseFile() should return.
  void SetCloseFileError(int32_t error);

  // Helper method to set the errno Truncate() should return.
  void SetTruncateError(int32_t error);

  // Helper method to manually set the current_entry for the OpenInfo
  // corresponding to |dir_id|.
  void SetCurrentEntry(int32_t dir_id, size_t index);

  // Helper method to get the path for the OpenInfo corresponding to |dir_id|.
  std::string GetCurrentEntry(int32_t dir_id);

 private:
  // Replacement struct for smbc_dirent within FakeSambaInterface.
  struct FakeEntry {
    std::string name;
    uint32_t smbc_type;
    size_t size;
    uint64_t date;
    // Indicates whether the entry should be inacessable by the user.
    bool locked = false;

    FakeEntry(const std::string& full_path,
              uint32_t smbc_type,
              size_t size,
              uint64_t date,
              bool locked);

    FakeEntry(const std::string& full_path,
              uint32_t smbc_type,
              size_t size,
              uint64_t date);

    virtual ~FakeEntry() = default;

    // Returns true for SMBC_FILE and SMBC_DIR. False for all others.
    bool IsValidEntryType() const;

    // Returns true for SMBC_FILE.
    bool IsFile() const;

    // Returns true for SMBC_DIR.
    bool IsDir() const;

    DISALLOW_COPY_AND_ASSIGN(FakeEntry);
  };

  struct FakeDirectory : FakeEntry {
    using Entries = std::vector<std::unique_ptr<FakeEntry>>;
    using EntriesIterator = Entries::iterator;

    FakeDirectory(const std::string& full_path, bool locked)
        : FakeEntry(full_path, SMBC_DIR, 0 /* size */, 0 /* date */, locked) {}
    explicit FakeDirectory(const std::string& full_path)
        : FakeDirectory(full_path, false /* locked */) {}

    // Returns entries.empty().
    bool IsEmpty() const;

    // Returns a pointer to the entry in the directory with |name|.
    FakeEntry* FindEntry(const std::string& name);

    // Removes the entry in entries with |name| from the directory.
    // This function must only be called on files and empty directories.
    // Returns index of the entry if it was found and deleted. Otherwise returns
    // -1.
    int32_t RemoveEntry(const std::string& name);

    // Checks whether the provided FakeEntry is a file or an empty directory.
    bool IsFileOrEmptyDirectory(FakeEntry* entry) const;

    EntriesIterator GetEntryIt(const std::string& name);

    // Contains pointers to entries that can be found in this directory.
    Entries entries;

    DISALLOW_COPY_AND_ASSIGN(FakeDirectory);
  };

  struct FakeFile : FakeEntry {
    FakeFile(const std::string& full_path,
             size_t size,
             uint64_t date,
             bool locked)
        : FakeEntry(full_path, SMBC_FILE, size, date, locked),
          has_data(false) {}

    FakeFile(const std::string& full_path,
             uint64_t date,
             std::vector<uint8_t> file_data)
        : FakeEntry(
              full_path, SMBC_FILE, file_data.size(), date, false /* locked */),
          has_data(true),
          data(std::move(file_data)) {}

    // Writes |buffer_size| bytes from |buffer| into the file starting from
    // |offset|.
    void WriteData(size_t offset, const uint8_t* buffer, size_t buffer_size);

    // This is used to track if the file currently has data. This may be false
    // during initialization, but can be switched to true if data is added
    // later.
    bool has_data;

    // Only populated for SMBC_FILE and is optionally provided.
    // This only contains data if has_data is true.
    // Contains the data for the file.
    std::vector<uint8_t> data;

    DISALLOW_COPY_AND_ASSIGN(FakeFile);
  };

  struct OpenInfo {
    std::string full_path;

    // When type is FakeDirectory, this keeps track of the index of the next
    // file to be read from the directory. This is set to 0 when opening.
    // When type is FakeFile, this functions as the current offset of the file.
    size_t current_index = 0;

    // Type of FakeEntry that this OpenInfo is referring to.
    uint32_t smbc_type;

    // For testing that read/write are set correctly by Open().
    bool readable = false;
    bool writeable = false;

    OpenInfo(const std::string& full_path, uint32_t smbc_type)
        : full_path(full_path), smbc_type(smbc_type) {}
    OpenInfo(const std::string& full_path,
             uint32_t smbc_type,
             bool readable,
             bool writeable)
        : full_path(full_path),
          smbc_type(smbc_type),
          readable(readable),
          writeable(writeable) {}

    OpenInfo(OpenInfo&& other)
        : full_path(std::move(other.full_path)),
          current_index(other.current_index),
          smbc_type(other.smbc_type),
          readable(other.readable),
          writeable(other.writeable) {}

    // Returns true if |dir_path| is the same as full_path.
    bool IsForDir(const std::string& dir_path);

    DISALLOW_COPY_AND_ASSIGN(OpenInfo);
  };

  using OpenEntries = std::map<uint32_t, OpenInfo>;
  using OpenEntriesIterator = OpenEntries::iterator;
  using OpenEntriesConstIterator = OpenEntries::const_iterator;

  // Adds an open directory to open_fds.
  int32_t AddOpenDirectory(const std::string& path);

  // Adds an open file to open_fds.
  int32_t AddOpenFile(const std::string& path, bool readable, bool writeable);

  // Checks whether the file/directory at the specified path is open.
  bool IsOpen(const std::string& full_path) const;

  // Checks whether a file descriptor is open.
  bool IsFDOpen(uint32_t fd) const;

  // Returns an iterator to an OpenInfo in open_fds.
  OpenEntriesIterator FindOpenFD(uint32_t fd);

  // Returns a const_iterator to an OpenInfo in open_fds.
  OpenEntriesConstIterator FindOpenFD(uint32_t fd) const;

  // Recurses through the file system, returning a pointer to a directory.
  // Pointer is owned by the class and should not be retained passed the
  // lifetime of a single public method call as it could be invalidated.
  // |full_path| is expected to be in /foo/bar format.
  FakeDirectory* GetDirectory(const std::string& full_path,
                              int32_t* error) const;
  FakeDirectory* GetDirectory(const std::string& full_path) const;

  // Recurses through the file system, returning a pointer to the entry.
  // Pointer is owned by the class and should not be retained passed the
  // lifetime of a single public method call as it could be invalidated.
  FakeEntry* GetEntry(const std::string& entry_path) const;

  // Recurses through the file system, returning a pointer to the file.
  // Pointer is owned by the class and should not be retained passed the
  // lifetime of a single public method call as it could be invalidated.
  FakeFile* GetFile(const std::string& file_path) const;

  // Checks whether the directory has more entries.
  bool HasMoreEntries(uint32_t dir_fd) const;

  // Goes through |open_fds| and rewinds |current_index| if |deleted_index| is
  // already equal to entries.size() for the directory.
  void RewindOpenInfoIndicesIfNeccessary(const std::string& dir_path,
                                         size_t deleted_index);

  // Removes |full_path| from the file system and calls
  // RewindOpenInfoIndicesIfNeccessary.
  void RemoveEntryAndResetIndicies(const std::string& full_path);

  // Checks whether a MoveEntry operation should be performed from a |src_entry|
  // to an already existing |target_entry|. Returns 0 if both |src_entry| and
  // |target_entry| are empty directories, corresponding errno otherwise.
  int32_t CheckEntriesValidForMove(FakeEntry* src_entry,
                                   FakeEntry* target_entry) const;

  // Moves the directory entry at |source_path| to |target_path|.
  // Returns an error if either of the parent directories is locked or if
  // |source_path| is a directory and is locked.
  int32_t MoveEntryFromSourceToTarget(const std::string& source_path,
                                      const std::string& target_path);

  // Helper method that gets the parent directories for |source_path| and
  // |target_path|. Returns 0 on success and error on failure.
  int32_t GetSourceAndTargetParentDirectories(
      const std::string& source_path,
      const std::string& target_path,
      FakeDirectory** source_parent,
      FakeDirectory** target_parent) const;

  // Counter for assigning file descriptor when opening.
  uint32_t next_fd = 0;

  // Root directory of the file system.
  std::unique_ptr<FakeDirectory> root;

  // Errno for CloseFile() to return. If this is set to anything other than
  // 0, CloseFile() will return the error this is set to.
  int32_t close_file_error_ = 0;

  // Errno for Truncate() to return. If this is set to anything other than
  // 0, Truncate() will return the error this is set to.
  int32_t truncate_error_ = 0;

  // Keeps track of open files and directories.
  // Key: fd of the file/directory.
  // Value: OpenInfo that corresponds with the key.
  OpenEntries open_fds;

  DISALLOW_COPY_AND_ASSIGN(FakeSambaInterface);
};

}  // namespace smbprovider

#endif  // SMBPROVIDER_FAKE_SAMBA_INTERFACE_H_
