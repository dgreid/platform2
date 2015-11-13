// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Platform

#include "cryptohome/platform.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <mntent.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <sstream>
#include <utility>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/file_util.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/sys_info.h>
#include <base/threading/thread.h>
#include <base/time/time.h>
#include <chromeos/process.h>
#include <chromeos/secure_blob.h>
#include <openssl/rand.h>

extern "C" {
#include "cryptohome/crc32.h"
// Uses libvboot_host for accessing crossystem variables.
#include <vboot/crossystem.h>
}

#include "cryptohome/cryptohome_metrics.h"

using base::FilePath;
using base::SplitString;
using base::StringPrintf;
using std::string;

namespace {

class ScopedPath {
 public:
  ScopedPath(cryptohome::Platform* platform, const string& dir)
      : platform_(platform), dir_(dir) {}
  ~ScopedPath() {
    if (!dir_.empty() && !platform_->DeleteFile(dir_, true)) {
      PLOG(WARNING) << "Failed to clean up " << dir_;
    }
  }
  void release() {
    dir_.clear();
  }
 private:
  cryptohome::Platform* platform_;
  string dir_;
};

bool IsDirectory(const struct stat& file_info) {
  return !!S_ISDIR(file_info.st_mode);
}

void TimedSync() {
  base::Time start = base::Time::Now();
  sync();
  base::TimeDelta delta = base::Time::Now() - start;
  if (delta > base::TimeDelta::FromSeconds(10)) {
    LOG(WARNING) << "Long sync(): " << delta.InSeconds() << " seconds";
  }
}

}  // namespace

namespace cryptohome {

const int kDefaultMountOptions = MS_NOEXEC | MS_NOSUID | MS_NODEV;
const int kDefaultPwnameLength = 1024;
const int kDefaultUmask = S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH
                               | S_IXOTH;
const std::string kMtab = "/etc/mtab";
const std::string kProcDir = "/proc";
const std::string kPathTune2fs = "/sbin/tune2fs";

Platform::Platform()
  : mtab_path_(kMtab) {
}

Platform::~Platform() {
}

bool Platform::GetMountsBySourcePrefix(const std::string& from_prefix,
                 std::multimap<const std::string, const std::string>* mounts) {
  std::string contents;
  if (!base::ReadFileToString(FilePath(mtab_path_), &contents))
    return false;

  std::vector<std::string> lines;
  SplitString(contents, '\n', &lines);
  for (std::vector<std::string>::iterator it = lines.begin();
       it < lines.end();
       ++it) {
    if (it->substr(0, from_prefix.size()) != from_prefix)
      continue;
    // If there is no mounts pointer, we can return true right away.
    if (!mounts)
      return true;
    size_t src_end = it->find(' ');
    std::string source = it->substr(0, src_end);
    size_t dst_start = src_end + 1;
    size_t dst_end = it->find(' ', dst_start);
    std::string destination = it->substr(dst_start, dst_end - dst_start);
    mounts->insert(
      std::pair<const std::string, const std::string>(source, destination));
  }
  return mounts && mounts->size();
}

bool Platform::IsDirectoryMounted(const std::string& directory) {
  // Trivial string match from /etc/mtab to see if the cryptohome mount point is
  // listed.  This works because Chrome OS is a controlled environment and the
  // only way /home/chronos/user should be mounted is if cryptohome mounted it.
  string contents;
  if (base::ReadFileToString(FilePath(mtab_path_), &contents)) {
    if (contents.find(StringPrintf(" %s ", directory.c_str()))
        != string::npos) {
      return true;
    }
  }
  return false;
}

bool Platform::IsDirectoryMountedWith(const std::string& directory,
                                      const std::string& from) {
  // Trivial string match from /etc/mtab to see if the cryptohome mount point
  // and the user's vault path are present.  Assumes this user is mounted if it
  // finds both.  This will need to change if simultaneous login is implemented.
  string contents;
  if (base::ReadFileToString(FilePath(mtab_path_), &contents)) {
    if ((contents.find(StringPrintf(" %s ", directory.c_str()))
         != string::npos)
        && (contents.find(StringPrintf("%s ",
                                       from.c_str()).c_str())
            != string::npos)) {
      return true;
    }
  }
  return false;
}

bool Platform::Mount(const std::string& from, const std::string& to,
                     const std::string& type,
                     const std::string& mount_options) {
  if (mount(from.c_str(), to.c_str(), type.c_str(), kDefaultMountOptions,
            mount_options.c_str())) {
    return false;
  }
  return true;
}

bool Platform::Bind(const std::string& from, const std::string& to) {
  if (mount(from.c_str(), to.c_str(), NULL, kDefaultMountOptions | MS_BIND,
            NULL))
    return false;
  return true;
}

bool Platform::Unmount(const std::string& path, bool lazy, bool* was_busy) {
  if (lazy) {
    if (umount2(path.c_str(), MNT_DETACH)) {
      if (was_busy) {
        *was_busy = (errno == EBUSY);
      }
      return false;
    }
  } else {
    if (umount(path.c_str())) {
      if (was_busy) {
        *was_busy = (errno == EBUSY);
      }
      return false;
    }
  }
  if (was_busy) {
    *was_busy = false;
  }
  return true;
}

void Platform::LazyUnmountAndSync(const std::string& path, bool sync_first) {
  if (sync_first) {
    TimedSync();
  }
  if (umount2(path.c_str(), MNT_DETACH | UMOUNT_NOFOLLOW)) {
    if (errno != EBUSY) {
      PLOG(ERROR) << "Lazy unmount failed!";
    }
  }
  TimedSync();
}

void Platform::GetProcessesWithOpenFiles(
    const std::string& path,
    std::vector<ProcessInformation>* processes) {
  std::vector<pid_t> pids;
  LookForOpenFiles(path, &pids);
  for (std::vector<pid_t>::iterator it = pids.begin(); it != pids.end(); ++it) {
    pid_t pid = static_cast<pid_t>(*it);
    processes->push_back(ProcessInformation());
    GetProcessOpenFileInformation(pid, path,
                                  &processes->at(processes->size() - 1));
  }
}

std::string Platform::ReadLink(const std::string& link_path) {
  char link_buf[PATH_MAX];
  ssize_t link_length = readlink(link_path.c_str(), link_buf, sizeof(link_buf));
  if (link_length > 0) {
    return std::string(link_buf, link_length);
  }
  return std::string();
}

void Platform::GetProcessOpenFileInformation(pid_t pid,
                                             const std::string& path_in,
                                             ProcessInformation* process_info) {
  process_info->set_process_id(pid);
  FilePath pid_path(StringPrintf("/proc/%d", pid));
  FilePath cmdline_file = pid_path.Append("cmdline");
  string contents;
  std::vector<std::string> cmd_line;
  if (base::ReadFileToString(cmdline_file, &contents)) {
    SplitString(contents, '\0', &cmd_line);
  }
  process_info->set_cmd_line(&cmd_line);

  // Make sure that if we get a directory, it has a trailing separator
  FilePath file_path(path_in);
  file_path = file_path.AsEndingWithSeparator();
  std::string path = file_path.value();

  FilePath cwd_path = pid_path.Append("cwd");
  std::string link_val = ReadLink(cwd_path.value());
  if (IsPathChild(path, link_val)) {
    process_info->set_cwd(&link_val);
  } else {
    link_val.clear();
    process_info->set_cwd(&link_val);
  }

  // Open /proc/<pid>/fd
  FilePath fd_dirpath = pid_path.Append("fd");

  base::FileEnumerator fd_dir_enum(fd_dirpath, false,
                                   base::FileEnumerator::FILES);

  std::set<std::string> open_files;
  // List open file descriptors
  for (FilePath fd_path = fd_dir_enum.Next();
       !fd_path.empty();
       fd_path = fd_dir_enum.Next()) {
    link_val = ReadLink(fd_path.value());
    if (IsPathChild(path, link_val)) {
      open_files.insert(link_val);
    }
  }
  process_info->set_open_files(&open_files);
}

void Platform::LookForOpenFiles(const std::string& path_in,
                                std::vector<pid_t>* pids) {
  // Make sure that if we get a directory, it has a trailing separator
  FilePath file_path(path_in);
  file_path = file_path.AsEndingWithSeparator();
  std::string path = file_path.value();

  // Open /proc
  base::FileEnumerator proc_dir_enum(FilePath(kProcDir), false,
      base::FileEnumerator::DIRECTORIES);

  int linkbuf_length = path.length();
  std::vector<char> linkbuf(linkbuf_length);

  // List PIDs in /proc
  for (FilePath pid_path = proc_dir_enum.Next();
       !pid_path.empty();
       pid_path = proc_dir_enum.Next()) {
    pid_t pid = 0;
    // Ignore PID 1 and errors
    if (!base::StringToInt(pid_path.BaseName().value(), &pid) || pid <= 1) {
      continue;
    }

    FilePath cwd_path = pid_path.Append("cwd");
    ssize_t link_length = readlink(cwd_path.value().c_str(),
                                   linkbuf.data(),
                                   linkbuf.size());
    if (link_length > 0) {
      std::string open_file(linkbuf.data(), link_length);
      if (IsPathChild(path, open_file)) {
        pids->push_back(pid);
        continue;
      }
    }

    // Open /proc/<pid>/fd
    FilePath fd_dirpath = pid_path.Append("fd");

    base::FileEnumerator fd_dir_enum(fd_dirpath, false,
                                     base::FileEnumerator::FILES);

    // List open file descriptors
    for (FilePath fd_path = fd_dir_enum.Next();
         !fd_path.empty();
         fd_path = fd_dir_enum.Next()) {
      link_length = readlink(fd_path.value().c_str(), linkbuf.data(),
                                     linkbuf.size());
      if (link_length > 0) {
        std::string open_file(linkbuf.data(), link_length);
        if (IsPathChild(path, open_file)) {
          pids->push_back(pid);
          break;
        }
      }
    }
  }
}

bool Platform::IsPathChild(const std::string& parent,
                           const std::string& child) {
  if (parent.length() == 0 || child.length() == 0) {
    return false;
  }
  if (child.length() >= parent.length()) {
    if (child.compare(0, parent.length(), parent, 0, parent.length()) == 0) {
      return true;
    }
  } else if ((parent[parent.length() - 1] == '/') &&
             (child.length() == (parent.length() - 1))) {
    if (child.compare(0, child.length(), parent, 0, parent.length() - 1) == 0) {
      return true;
    }
  }
  return false;
}

bool Platform::GetOwnership(const string& path,
                            uid_t* user_id, gid_t* group_id) const {
  struct stat path_status;
  if (stat(path.c_str(), &path_status) != 0) {
    PLOG(ERROR) << "stat() of " << path << " failed.";
    return false;
  }
  if (user_id)
    *user_id = path_status.st_uid;
  if (group_id)
    *group_id = path_status.st_gid;
  return true;
}

bool Platform::SetOwnership(const std::string& path, uid_t user_id,
                            gid_t group_id) const {
  if (chown(path.c_str(), user_id, group_id)) {
    PLOG(ERROR) << "chown() of " << path.c_str() << " to (" << user_id
                << "," << group_id << ") failed.";
    return false;
  }
  return true;
}

bool Platform::GetPermissions(const string& path, mode_t* mode) const {
  struct stat path_status;
  if (stat(path.c_str(), &path_status) != 0) {
    PLOG(ERROR) << "stat() of " << path << " failed.";
    return false;
  }
  *mode = path_status.st_mode;
  return true;
}

bool Platform::SetPermissions(const std::string& path, mode_t mode) const {
  if (chmod(path.c_str(), mode)) {
    PLOG(ERROR) << "chmod() of " << path.c_str() << " to (" << std::oct << mode
                << ") failed.";
    return false;
  }
  return true;
}

bool Platform::SetGroupAccessible(const string& path, gid_t group_id,
                                  mode_t group_mode) const {
  uid_t user_id;
  mode_t mode;
  if (!GetOwnership(path, &user_id, NULL) ||
      !GetPermissions(path, &mode) ||
      !SetOwnership(path, user_id, group_id) ||
      !SetPermissions(path, (mode & ~S_IRWXG) | (group_mode & S_IRWXG))) {
    LOG(ERROR) << "Couldn't set up group access on directory: " << path;
    return false;
  }
  return true;
}

int Platform::SetMask(int new_mask) const {
  return umask(new_mask);
}

bool Platform::GetUserId(const std::string& user, uid_t* user_id,
                         gid_t* group_id) const {
  // Load the passwd entry
  long user_name_length = sysconf(_SC_GETPW_R_SIZE_MAX);  // NOLINT long
  if (user_name_length == -1) {
    user_name_length = kDefaultPwnameLength;
  }
  struct passwd user_info, *user_infop;
  std::vector<char> user_name_buf(user_name_length);
  if (getpwnam_r(user.c_str(), &user_info, user_name_buf.data(),
                user_name_length, &user_infop)) {
    return false;
  }
  *user_id = user_info.pw_uid;
  *group_id = user_info.pw_gid;
  return true;
}

bool Platform::GetGroupId(const std::string& group, gid_t* group_id) const {
  // Load the group entry
  long group_name_length = sysconf(_SC_GETGR_R_SIZE_MAX);  // NOLINT long
  if (group_name_length == -1) {
    group_name_length = kDefaultPwnameLength;
  }
  struct group group_info, *group_infop;
  std::vector<char> group_name_buf(group_name_length);
  if (getgrnam_r(group.c_str(), &group_info, group_name_buf.data(),
                group_name_length, &group_infop)) {
    return false;
  }
  *group_id = group_info.gr_gid;
  return true;
}

int64_t Platform::AmountOfFreeDiskSpace(const string& path) const {
  return base::SysInfo::AmountOfFreeDiskSpace(FilePath(path));
}

bool Platform::FileExists(const std::string& path) {
  return base::PathExists(FilePath(path));
}

bool Platform::DirectoryExists(const std::string& path) {
  return base::DirectoryExists(FilePath(path));
}

bool Platform::GetFileSize(const std::string& path, int64_t* size) {
  return base::GetFileSize(FilePath(path), size);
}

FILE* Platform::CreateAndOpenTemporaryFile(std::string* path) {
  FilePath created_path;
  FILE* f = base::CreateAndOpenTemporaryFile(&created_path);
  if (f)
    path->assign(created_path.value());

  return f;
}

FILE* Platform::OpenFile(const std::string& path, const char* mode) {
  return base::OpenFile(FilePath(path), mode);
}

bool Platform::CloseFile(FILE* fp) {
  return base::CloseFile(fp);
}

bool Platform::WriteOpenFile(FILE* fp, const chromeos::Blob& blob) {
  return (fwrite(static_cast<const void*>(&blob.at(0)), 1, blob.size(), fp)
            != blob.size());
}

bool Platform::WriteFile(const std::string& path,
                         const chromeos::Blob& blob) {
  return WriteArrayToFile(path,
                          reinterpret_cast<const char*>(blob.data()),
                          blob.size());
}

bool Platform::WriteStringToFile(const std::string& path,
                                 const std::string& data) {
  return WriteArrayToFile(path, data.data(), data.size());
}

bool Platform::WriteArrayToFile(const std::string& path, const char* data,
                                size_t size) {
  FilePath file_path(path);
  if (!base::DirectoryExists(file_path.DirName())) {
    if (!base::CreateDirectory(file_path.DirName())) {
      LOG(ERROR) << "Cannot create directory: " << file_path.DirName().value();
      return false;
    }
  }
  // chromeos::Blob::size_type is std::vector::size_type and is unsigned.
  if (size > static_cast<std::string::size_type>(INT_MAX)) {
    LOG(ERROR) << "Cannot write to " << path
               << ". Data is too large: " << size << " bytes.";
    return false;
  }

  int data_written = base::WriteFile(file_path, data, size);
  return data_written == static_cast<int>(size);
}

std::string Platform::GetRandomSuffix() {
  const int kBufferSize = 6;
  unsigned char buffer[kBufferSize];
  if (RAND_pseudo_bytes(buffer, kBufferSize) < 0) {
    return std::string();
  }
  std::string suffix;
  for (int i = 0; i < kBufferSize; ++i) {
    int random_value = buffer[i] % (2 * 26 + 10);
    if (random_value < 26) {
      suffix.push_back('a' + random_value);
    } else if (random_value < 2 * 26) {
      suffix.push_back('A' + random_value - 26);
    } else {
      suffix.push_back('0' + random_value - 2 * 26);
    }
  }
  return suffix;
}

bool Platform::WriteFileAtomic(const std::string& path,
                               const chromeos::Blob& blob,
                               mode_t mode) {
  const std::string data(reinterpret_cast<const char*>(blob.data()),
                         blob.size());
  return WriteStringToFileAtomic(path, data, mode);
}

bool Platform::WriteStringToFileAtomic(const std::string& path,
                                       const std::string& data,
                                       mode_t mode) {
  FilePath file_path(path);
  if (!base::DirectoryExists(file_path.DirName())) {
    if (!base::CreateDirectory(file_path.DirName())) {
      LOG(ERROR) << "Cannot create directory: " << file_path.DirName().value();
      return false;
    }
  }
  std::string random_suffix = GetRandomSuffix();
  if (random_suffix.empty()) {
    PLOG(WARNING) << "Could not compute random suffix";
    return false;
  }
  std::string temp_name_string = FilePath(path).DirName().
      Append(".org.chromium.cryptohome." + random_suffix).value();
  const char * temp_name = temp_name_string.c_str();
  int fd = HANDLE_EINTR(open(temp_name, O_CREAT|O_EXCL|O_WRONLY, mode));
  if (fd < 0) {
    PLOG(WARNING) << "Could not open " << temp_name << " for atomic write";
    unlink(temp_name);
    return false;
  }

  size_t position = 0;
  while (position < data.size()) {
    ssize_t bytes_written = HANDLE_EINTR(
        write(fd, data.data()+position, data.size()-position));
    if (bytes_written < 0) {
      PLOG(WARNING) << "Could not write " << temp_name;
      close(fd);
      unlink(temp_name);
      return false;
    }
    position += bytes_written;
  }

  int result = HANDLE_EINTR(fdatasync(fd));
  if (result < 0) {
    PLOG(WARNING) << "Could not fsync " << temp_name;
    close(fd);
    unlink(temp_name);
    return false;
  }
  result = close(fd);
  if (result < 0) {
    PLOG(WARNING) << "Could not close " << temp_name;
    unlink(temp_name);
    return false;
  }

  result = rename(temp_name, path.c_str());
  if (result < 0) {
    PLOG(WARNING) << "Could not close " << temp_name;
    unlink(temp_name);
    return false;
  }

  return true;
}

bool Platform::WriteFileAtomicDurable(const std::string& path,
                                      const chromeos::Blob& blob,
                                      mode_t mode) {
  const std::string data(reinterpret_cast<const char*>(blob.data()),
                         blob.size());
  return WriteStringToFileAtomicDurable(path, data, mode);
}

bool Platform::WriteStringToFileAtomicDurable(const std::string& path,
                                              const std::string& data,
                                              mode_t mode) {
  if (!WriteStringToFileAtomic(path, data, mode))
    return false;
  WriteChecksum(path, data.data(), data.size(), mode);
  return SyncDirectory(FilePath(path).DirName().value());
}

bool Platform::TouchFileDurable(const std::string& path) {
  chromeos::Blob empty_blob(0);
  if (!WriteFile(path, empty_blob))
    return false;
  return SyncDirectory(FilePath(path).DirName().value());
}

bool Platform::ReadFile(const std::string& path, chromeos::Blob* blob) {
  int64_t file_size;
  FilePath file_path(path);
  if (!base::PathExists(file_path)) {
    return false;
  }
  if (!base::GetFileSize(file_path, &file_size)) {
    LOG(ERROR) << "Could not get size of " << path;
    return false;
  }
  // Compare to the max of a signed integer.
  if (file_size > static_cast<int64_t>(INT_MAX)) {
    LOG(ERROR) << "File " << path << " is too large: "
               << file_size << " bytes.";
    return false;
  }
  chromeos::Blob buf(file_size);
  int data_read = base::ReadFile(file_path,
                                 reinterpret_cast<char*>(buf.data()),
                                 file_size);
  // Cast is okay because of comparison to INT_MAX above.
  if (data_read != static_cast<int>(file_size)) {
    LOG(ERROR) << "Only read " << data_read << " of " << file_size << " bytes.";
    return false;
  }
  blob->swap(buf);
  VerifyChecksum(path, blob->data(), blob->size());
  return true;
}

bool Platform::ReadFileToString(const std::string& path, std::string* string) {
  if (!base::ReadFileToString(FilePath(path), string)) {
    return false;
  }
  VerifyChecksum(path, string->data(), string->size());
  return true;
}

bool Platform::CreateDirectory(const std::string& path) {
  return base::CreateDirectory(FilePath(path));
}

bool Platform::DeleteFile(const std::string& path, bool is_recursive) {
  return base::DeleteFile(FilePath(path), is_recursive);
}

bool Platform::DeleteFileDurable(const std::string& path, bool is_recursive) {
  if (!base::DeleteFile(FilePath(path), is_recursive))
    return false;
  return SyncDirectory(FilePath(path).DirName().value());
}

bool Platform::Move(const std::string& from, const std::string& to) {
  return base::Move(FilePath(from), FilePath(to));
}

bool Platform::EnumerateDirectoryEntries(const std::string& path,
                                         bool recursive,
                                         std::vector<std::string>* ent_list) {
  auto ft = static_cast<base::FileEnumerator::FileType>(
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES |
      base::FileEnumerator::SHOW_SYM_LINKS);
  base::FileEnumerator ent_enum(FilePath(path), recursive, ft);
  for (FilePath path = ent_enum.Next(); !path.empty(); path = ent_enum.Next())
    ent_list->push_back(path.value());
  return true;
}

base::Time Platform::GetCurrentTime() const {
  return base::Time::Now();
}

bool Platform::Stat(const std::string& path, struct stat *buf) {
  return lstat(path.c_str(), buf) == 0;
}

bool Platform::Rename(const std::string& from, const std::string& to) {
  return rename(from.c_str(), to.c_str()) == 0;
}

bool Platform::Copy(const std::string& from, const std::string& to) {
  FilePath from_path(from);
  FilePath to_path(to);
  return base::CopyDirectory(from_path, to_path, true);
}

bool Platform::CopyPermissionsCallback(
    const std::string& old_base,
    const std::string& new_base,
    const std::string& file_path,
    const struct stat& file_info) {
  const FilePath old_base_path(old_base);
  const FilePath new_base_path(new_base);
  // Find the new path that corresponds with the old path given by file_info.
  FilePath old_path(file_path);
  FilePath new_path(new_base_path);
  if (old_path != old_base_path) {
    if (old_path.IsAbsolute()) {
      if (!old_base_path.AppendRelativePath(old_path, &new_path)) {
        LOG(ERROR) << "AppendRelativePath failed: parent="
                   << old_base_path.value() << ", child=" << old_path.value();
        return false;
      }
    } else {
      new_path = new_base_path.Append(old_path);
    }
  }
  if (!SetOwnership(new_path.value(),
                    file_info.st_uid,
                    file_info.st_gid)) {
    PLOG(ERROR) << "Failed to set ownership for " << new_path.value();
    return false;
  }
  const mode_t permissions_mask = 07777;
  if (!SetPermissions(new_path.value(),
                      file_info.st_mode & permissions_mask)) {
    PLOG(ERROR) << "Failed to set permissions for " << new_path.value();
    return false;
  }
  return true;
}

bool Platform::CopyWithPermissions(const std::string& from_path,
                                   const std::string& to_path) {
  if (!Copy(from_path, to_path)) {
    PLOG(ERROR) << "Failed to copy " << from_path;
    return false;
  }

  // If something goes wrong we want to blow away the half-baked path.
  ScopedPath scoped_new_path(this, to_path);

  // Unfortunately, ownership and permissions are not always retained.
  // Apply the old ownership / permissions on a per-file basis.
  FileEnumeratorCallback callback = base::Bind(
      &Platform::CopyPermissionsCallback,
      base::Unretained(this),
      from_path,
      to_path);
  if (!WalkPath(from_path, callback))
    return false;

  // The copy is done, keep the new path.
  scoped_new_path.release();
  return true;
}

bool Platform::ApplyPermissionsCallback(
    const Permissions& default_file_info,
    const Permissions& default_dir_info,
    const std::map<std::string, Permissions>& special_cases,
    const std::string& file_path,
    const struct stat& file_info) {
  Permissions expected;
  std::map<std::string, Permissions>::const_iterator it =
      special_cases.find(file_path);
  if (it != special_cases.end()) {
    expected = it->second;
  } else if (IsDirectory(file_info)) {
    expected = default_dir_info;
  } else {
    expected = default_file_info;
  }
  if (expected.user != file_info.st_uid ||
      expected.group != file_info.st_gid) {
    LOG(WARNING) << "Unexpected user/group for " << file_path;
    if (!SetOwnership(file_path,
                      expected.user,
                      expected.group)) {
      PLOG(ERROR) << "Failed to fix user/group for "
                  << file_path;
      return false;
    }
  }
  const mode_t permissions_mask = 07777;
  if ((expected.mode & permissions_mask) !=
      (file_info.st_mode & permissions_mask)) {
    LOG(WARNING) << "Unexpected permissions for " << file_path;
    if (!SetPermissions(file_path,
                        expected.mode & permissions_mask)) {
      PLOG(ERROR) << "Failed to set permissions for "
                  << file_path;
      return false;
    }
  }
  return true;
}

bool Platform::ApplyPermissionsRecursive(
    const std::string& path,
    const Permissions& default_file_info,
    const Permissions& default_dir_info,
    const std::map<std::string, Permissions>& special_cases) {
  FileEnumeratorCallback callback = base::Bind(
      &Platform::ApplyPermissionsCallback,
      base::Unretained(this),
      default_file_info,
      default_dir_info,
      special_cases);
  return WalkPath(path, callback);
}

bool Platform::StatVFS(const std::string& path, struct statvfs* vfs) {
  return statvfs(path.c_str(), vfs) == 0;
}

bool Platform::FindFilesystemDevice(const std::string &filesystem_in,
                                    std::string *device) {
  /* Clear device to indicate failure case. */
  device->clear();

  /* Removing trailing slashes. */
  std::string filesystem = filesystem_in;
  size_t offset = filesystem.find_last_not_of('/');
  if (offset != std::string::npos)
    filesystem.erase(offset+1);

  /* If we fail to open mtab, abort immediately. */
  FILE *mtab_file = setmntent(mtab_path_.c_str(), "r");
  if (!mtab_file)
    return false;

  /* Copy device of first matching filesystem location. */
  struct mntent *entry;
  while ((entry = getmntent(mtab_file)) != NULL) {
    if (filesystem.compare(entry->mnt_dir) == 0) {
      *device = entry->mnt_fsname;
      break;
    }
  }
  endmntent(mtab_file);

  return (device->length() > 0);
}

bool Platform::ReportFilesystemDetails(const std::string &filesystem,
                                       const std::string &logfile) {
  chromeos::ProcessImpl process;
  int rc;
  std::string device;
  if (!FindFilesystemDevice(filesystem, &device)) {
    LOG(ERROR) << "Failed to find device for " << filesystem;
    return false;
  }

  process.RedirectOutput(logfile);
  process.AddArg(kPathTune2fs);
  process.AddArg("-l");
  process.AddArg(device);

  rc = process.Run();
  if (rc == 0)
    return true;
  LOG(ERROR) << "Failed to run tune2fs on " << device
             << " (" << filesystem << ", exit " << rc << ")";
  return false;
}

bool Platform::FirmwareWriteProtected() {
  return VbGetSystemPropertyInt("wpsw_boot") != 0;
}

bool Platform::DataSyncFile(const std::string& path) {
  return SyncFileOrDirectory(path, false /* directory */);
}

bool Platform::SyncDirectory(const std::string& path) {
  return SyncFileOrDirectory(path, true /* directory */);
}

bool Platform::SyncFileOrDirectory(const std::string& path, bool is_directory) {
  int flags = (is_directory ? O_RDONLY|O_DIRECTORY : O_WRONLY);
  int fd = HANDLE_EINTR(open(path.c_str(), flags));
  if (fd < 0) {
    PLOG(WARNING) << "Could not open " << path << " for syncing";
    return false;
  }
  // POSIX specifies EINTR as a possible return value of fsync() but not for
  // fdatasync().  To be on the safe side, it is handled in both cases.
  int result =
      (is_directory ? HANDLE_EINTR(fsync(fd)) : HANDLE_EINTR(fdatasync(fd)));
  if (result < 0) {
    PLOG(WARNING) << "Failed to sync " << path;
    close(fd);
    return false;
  }
  // close() may not be retried on error.
  result = IGNORE_EINTR(close(fd));
  if (result < 0) {
    PLOG(WARNING) << "Failed to close after sync " << path;
    return false;
  }
  return true;
}

void Platform::Sync() {
  TimedSync();
}

std::string Platform::GetHardwareID() {
  char buffer[VB_MAX_STRING_PROPERTY];
  const char *rc = VbGetSystemPropertyString("hwid", buffer, arraysize(buffer));

  if (rc != nullptr) {
    return std::string(rc);
  }

  LOG(WARNING) << "Could not read hwid property";
  return std::string();
}

// Encapsulate these helpers to avoid include conflicts.
namespace ecryptfs {
extern "C" {
#include <ecryptfs.h>  // NOLINT(build/include_alpha)
#include <keyutils.h>
}

long ClearUserKeyring() {  // NOLINT(runtime/int)
  return keyctl(KEYCTL_CLEAR, KEY_SPEC_USER_KEYRING);
}

long AddEcryptfsAuthToken(  // NOLINT(runtime/int)
    const chromeos::SecureBlob& key, const std::string& key_sig,
    const chromeos::SecureBlob& salt) {
  DCHECK_EQ(static_cast<size_t>(ECRYPTFS_MAX_KEY_BYTES),
            key.size());
  DCHECK_EQ(static_cast<size_t>(ECRYPTFS_SIG_SIZE) * 2, key_sig.length());
  DCHECK_EQ(static_cast<size_t>(ECRYPTFS_SALT_SIZE), salt.size());

  struct ecryptfs_auth_tok auth_token;

  generate_payload(&auth_token, const_cast<char*>(key_sig.c_str()),
                   const_cast<char*>(salt.char_data()),
                   const_cast<char*>(key.char_data()));

  return ecryptfs_add_auth_tok_to_keyring(&auth_token,
                                          const_cast<char*>(key_sig.c_str()));
}
}  // namespace ecryptfs

long Platform::ClearUserKeyring() {  // NOLINT(runtime/int)
  return ecryptfs::ClearUserKeyring();
}

long Platform::AddEcryptfsAuthToken(  // NOLINT(runtime/int)
    const chromeos::SecureBlob& key, const std::string& key_sig,
    const chromeos::SecureBlob& salt) {
  return ecryptfs::AddEcryptfsAuthToken(key, key_sig, salt);
}

FileEnumerator* Platform::GetFileEnumerator(const std::string& root_path,
                                            bool recursive,
                                            int file_type) {
  return new FileEnumerator(root_path, recursive, file_type);
}

bool Platform::WalkPath(const std::string& path,
                        const FileEnumeratorCallback& callback) {
  struct stat base_entry_info;
  if (!Stat(path, &base_entry_info)) {
    PLOG(ERROR) << "Failed to stat " << path;
    return false;
  }
  if (!callback.Run(path, base_entry_info))
    return false;
  if (IsDirectory(base_entry_info)) {
    int file_types = base::FileEnumerator::FILES |
                     base::FileEnumerator::DIRECTORIES;
    scoped_ptr<FileEnumerator> file_enumerator(
        GetFileEnumerator(path, true, file_types));
    std::string entry_path;
    while (!(entry_path = file_enumerator->Next()).empty()) {
      if (!callback.Run(entry_path, file_enumerator->GetInfo().stat()))
        return false;
    }
  }
  return true;
}

std::string Platform::GetChecksum(const void* input, size_t input_size) {
  uint32_t sum = Crc32(input, input_size);
  return base::HexEncode(&sum, 4);
}

void Platform::WriteChecksum(const std::string& path,
                             const void* content,
                             const size_t content_size,
                             mode_t mode) {
  WriteStringToFileAtomic(path + ".sum", GetChecksum(content, content_size),
                          mode);
}

void Platform::VerifyChecksum(const std::string& path,
                              const void* content,
                              const size_t content_size) {
  // Exclude some system paths.
  if (base::StartsWithASCII(path, "/etc", true) ||
      base::StartsWithASCII(path, "/dev", true) ||
      base::StartsWithASCII(path, "/sys", true) ||
      base::StartsWithASCII(path, "/proc", true)) {
    return;
  }
  if (!FileExists(path + ".sum")) {
    ReportChecksum(kChecksumDoesNotExist);
    return;
  }
  std::string saved_sum;
  if (!base::ReadFileToString(FilePath(path + ".sum"), &saved_sum)) {
    LOG(ERROR) << "CHECKSUM: Failed to read checksum for " << path;
    ReportChecksum(kChecksumReadError);
    return;
  }
  if (saved_sum != GetChecksum(content, content_size)) {
    LOG(ERROR) << "CHECKSUM: Failed to verify checksum for " << path;
    ReportChecksum(kChecksumMismatch);
    return;
  }
  ReportChecksum(kChecksumOK);
}

FileEnumerator::FileInfo::FileInfo(
    const base::FileEnumerator::FileInfo& file_info) {
  Assign(file_info);
}

FileEnumerator::FileInfo::FileInfo(const std::string& name,
                                   const struct stat& stat)
    : name_(name), stat_(stat) {
}

FileEnumerator::FileInfo::FileInfo(const FileEnumerator::FileInfo& other) {
  if (other.info_.get()) {
    Assign(*other.info_);
  } else {
    info_.reset();
    name_ = other.name_;
    stat_ = other.stat_;
  }
}

FileEnumerator::FileInfo::FileInfo() {
  Assign(base::FileEnumerator::FileInfo());
}

FileEnumerator::FileInfo::~FileInfo() {}

FileEnumerator::FileInfo& FileEnumerator::FileInfo::operator=(
    const FileEnumerator::FileInfo& other) {
  if (other.info_.get()) {
    Assign(*other.info_);
  } else {
    info_.reset();
    name_ = other.name_;
    stat_ = other.stat_;
  }
  return *this;
}

bool FileEnumerator::FileInfo::IsDirectory() const {
  if (info_.get())
    return info_->IsDirectory();
  return ::IsDirectory(stat_);
}

std::string FileEnumerator::FileInfo::GetName() const {
  if (info_.get())
    return info_->GetName().value();
  return name_;
}

int64_t FileEnumerator::FileInfo::GetSize() const {
  if (info_.get())
    return info_->GetSize();
  return stat_.st_size;
}

base::Time FileEnumerator::FileInfo::GetLastModifiedTime() const {
  if (info_.get())
    return info_->GetLastModifiedTime();
  return base::Time::FromTimeT(stat_.st_mtime);
}

const struct stat& FileEnumerator::FileInfo::stat() const {
  if (info_.get())
    return info_->stat();
  return stat_;
}

void FileEnumerator::FileInfo::Assign(
  const base::FileEnumerator::FileInfo& file_info) {
  info_.reset(new base::FileEnumerator::FileInfo(file_info));
  memset(&stat_, 0, sizeof(stat_));
}

FileEnumerator::FileEnumerator(const std::string& root_path,
                               bool recursive,
                               int file_type) {
  enumerator_.reset(new base::FileEnumerator(FilePath(root_path),
                                             recursive,
                                             file_type));
}

FileEnumerator::FileEnumerator(const std::string& root_path,
                               bool recursive,
                               int file_type,
                               const std::string& pattern) {
  enumerator_.reset(new base::FileEnumerator(FilePath(root_path),
                                             recursive,
                                             file_type,
                                             pattern));
}

FileEnumerator::FileEnumerator() {}
FileEnumerator::~FileEnumerator() {}

std::string FileEnumerator::Next() {
  if (!enumerator_.get())
    return std::string();
  return enumerator_->Next().value();
}

FileEnumerator::FileInfo FileEnumerator::GetInfo() {
  return FileInfo(enumerator_->GetInfo());
}

}  // namespace cryptohome
