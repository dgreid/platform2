// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "installer/inst_util.h"

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <linux/fs.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern "C" {
#include <vboot/vboot_host.h>
}

#include <base/files/scoped_file.h>
#include <base/strings/string_util.h>
#include <brillo/process/process.h>

using std::string;
using std::vector;

// Used by LoggingTimerStart/Finish methods.
static time_t START_TIME = 0;

namespace {

// This function returns the appropriate device name for the corresponding
// |partition| number on a NAND setup. It favors a mountable device name such
// as "/dev/ubiblockX_0" over the read-write devices such as "/dev/ubiX_0".
string MakeNandPartitionDevForMounting(int partition) {
  if (partition == 0) {
    return "/dev/mtd0";
  }
  if (partition == PART_NUM_KERN_A || partition == PART_NUM_KERN_B ||
      partition == PART_NUM_KERN_C) {
    return "/dev/mtd" + std::to_string(partition);
  }
  if (partition == PART_NUM_ROOT_A || partition == PART_NUM_ROOT_B ||
      partition == PART_NUM_ROOT_C) {
    return "/dev/ubiblock" + std::to_string(partition) + "_0";
  }
  return "/dev/ubi" + std::to_string(partition) + "_0";
}

// Callback used by nftw().
int RemoveFileOrDir(const char* fpath,
                    const struct stat* /* sb */,
                    int /* typeflag */,
                    struct FTW* /*ftwbuf */) {
  return remove(fpath);
}

}  // namespace

ScopedPathRemover::~ScopedPathRemover() {
  if (root_.empty()) {
    return;
  }
  struct stat stat_buf;
  if (stat(root_.c_str(), &stat_buf) != 0) {
    warn("Cannot stat %s", root_.c_str());
    return;
  }
  if (S_ISDIR(stat_buf.st_mode)) {
    if (nftw(root_.c_str(), RemoveFileOrDir, 20,
             FTW_DEPTH | FTW_MOUNT | FTW_PHYS) != 0) {
      warn("Cannot remove directory %s", root_.c_str());
    }
  } else {
    if (unlink(root_.c_str()) != 0) {
      warn("Cannot unlink %s", root_.c_str());
    }
  }
}

string ScopedPathRemover::release() {
  string r = root_;
  root_.clear();
  return r;
}

// Start a logging timer. There can only be one active at a time.
void LoggingTimerStart() {
  START_TIME = time(NULL);
}

// Log how long since the last call to LoggingTimerStart()
void LoggingTimerFinish() {
  time_t finish_time = time(NULL);
  printf("Finished after %.f seconds.\n", difftime(finish_time, START_TIME));
}

void SplitString(const string& str, char split, vector<string>* output) {
  output->clear();

  size_t i = 0;
  while (true) {
    size_t split_at = str.find(split, i);
    if (split_at == str.npos)
      break;
    output->push_back(str.substr(i, split_at - i));
    i = split_at + 1;
  }

  output->push_back(str.substr(i));
}

void JoinStrings(const vector<string>& strs,
                 const string& split,
                 string* output) {
  output->clear();

  bool first_line = true;

  for (vector<string>::const_iterator line = strs.begin(); line != strs.end();
       line++) {
    if (first_line)
      first_line = false;
    else
      output->append(split);

    output->append(*line);
  }
}

// This is a place holder to invoke the backing scripts. Once all scripts have
// been rewritten as library calls this command should be deleted.
// Takes a vector of args and returns error code.
int RunCommand(const vector<string>& cmdline) {
  string command = base::JoinString(cmdline, " ");
  printf("Command: %s\n", command.c_str());

  fflush(stdout);
  fflush(stderr);

  brillo::ProcessImpl process;
  process.SetSearchPath(true);

  for (const auto& arg : cmdline) {
    process.AddArg(arg);
  }

  LoggingTimerStart();
  int exit_code = process.Run();
  LoggingTimerFinish();

  if (exit_code == -1) {
    printf("Failed Command - invalid process: %s\n", command.c_str());
    return 1;
  } else if (exit_code != 0) {
    printf("Failed Command: %s - Exit Code %d\n", command.c_str(), exit_code);
  }
  return exit_code;
}

// Open a file and read it's contents into a string.
// return "" on error.
bool ReadFileToString(const string& path, string* contents) {
  string result;

  int fd = open(path.c_str(), O_RDONLY);

  if (fd == -1) {
    printf("ReadFileToString failed to open %s\n", path.c_str());
    return false;
  }

  ssize_t buff_in;
  char buff[512];

  while ((buff_in = read(fd, buff, sizeof(buff))) > 0)
    result.append(buff, buff_in);

  if (close(fd) != 0)
    return false;

  // If our last read failed, return an empty string, not a partial result.
  if (buff_in < 0)
    return false;

  *contents = result;
  return true;
}

// Open a file and write the contents of an ASCII string into it.
// return "" on error.
bool WriteStringToFile(const string& contents, const string& path) {
  int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

  if (fd == -1) {
    printf("WriteFileToString failed to open %s\n", path.c_str());
    return false;
  }

  bool success = WriteFullyToFileDescriptor(contents, fd);

  if (close(fd) != 0)
    return false;

  return success;
}

bool WriteFullyToFileDescriptor(const string& content, int fd) {
  const char* buf = content.data();
  size_t nr_written = 0;
  while (nr_written < content.length()) {
    size_t to_write = content.length() - nr_written;
    ssize_t nr_chunk = write(fd, buf + nr_written, to_write);
    if (nr_chunk < 0) {
      warn("Fail to write %d bytes", static_cast<int>(to_write));
      return false;
    }
    nr_written += nr_chunk;
  }
  return true;
}

bool CopyFile(const string& from_path, const string& to_path) {
  int fd_from = open(from_path.c_str(), O_RDONLY);

  if (fd_from == -1) {
    printf("CopyFile failed to open %s\n", from_path.c_str());
    return false;
  }

  bool success = true;

  int fd_to = open(to_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                   S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

  if (fd_to == -1) {
    printf("CopyFile failed to open %s\n", to_path.c_str());
    success = false;
  }

  ssize_t buff_in = 1;
  char buff[512];

  while (success && (buff_in > 0)) {
    buff_in = read(fd_from, buff, sizeof(buff));
    success = (buff_in >= 0);

    if (success) {
      ssize_t buff_out = write(fd_to, buff, buff_in);
      success = (buff_out == buff_in);
    }
  }

  if (close(fd_from) != 0)
    success = false;

  if (close(fd_to) != 0)
    success = false;

  return success;
}

// Look up a keyed value from a /etc/lsb-release formatted file.
// TODO(dgarrett): If we ever call this more than once, cache
// file contents to avoid reparsing.
bool LsbReleaseValue(const string& file, const string& key, string* result) {
  string preamble = key + "=";

  string file_contents;
  if (!ReadFileToString(file, &file_contents))
    return false;

  vector<string> file_lines;
  SplitString(file_contents, '\n', &file_lines);

  vector<string>::iterator line;
  for (line = file_lines.begin(); line < file_lines.end(); line++) {
    if (line->compare(0, preamble.size(), preamble) == 0) {
      *result = line->substr(preamble.size());
      return true;
    }
  }

  return false;
}

// This is an array of device names that are allowed in end in a digit, and
// which use the 'p' notation to denote partitions.
const char* numbered_devices[] = {"/dev/loop", "/dev/mmcblk", "/dev/nvme"};

string GetBlockDevFromPartitionDev(const string& partition_dev) {
  if (base::StartsWith(partition_dev, "/dev/mtd",
                       base::CompareCase::SENSITIVE) ||
      base::StartsWith(partition_dev, "/dev/ubi",
                       base::CompareCase::SENSITIVE)) {
    return "/dev/mtd0";
  }

  size_t i = partition_dev.length();

  while (i > 0 && isdigit(partition_dev[i - 1]))
    i--;

  for (const char** nd = begin(numbered_devices); nd != end(numbered_devices);
       nd++) {
    size_t nd_len = strlen(*nd);
    // numbered_devices are of the form "/dev/mmcblk12p34"
    if (partition_dev.compare(0, nd_len, *nd) == 0) {
      if ((i == nd_len) || (partition_dev[i - 1] != 'p')) {
        // If there was no partition at the end (/dev/mmcblk12) return
        // unmodified.
        return partition_dev;
      } else {
        // If it ends with a p, strip off the p.
        i--;
      }
    }
  }

  return partition_dev.substr(0, i);
}

int GetPartitionFromPartitionDev(const string& partition_dev) {
  size_t i = partition_dev.length();
  if (base::EndsWith(partition_dev, "_0", base::CompareCase::SENSITIVE)) {
    i -= 2;
  }

  while (i > 0 && isdigit(partition_dev[i - 1]))
    i--;

  for (const char** nd = begin(numbered_devices); nd != end(numbered_devices);
       nd++) {
    size_t nd_len = strlen(*nd);
    // numbered_devices are of the form "/dev/mmcblk12p34"
    // If there is no ending p, there is no partition at the end (/dev/mmcblk12)
    if ((partition_dev.compare(0, nd_len, *nd) == 0) &&
        ((i == nd_len) || (partition_dev[i - 1] != 'p'))) {
      return 0;
    }
  }

  string partition_str = partition_dev.substr(i, i + 1);

  int result = atoi(partition_str.c_str());

  if (result == 0)
    printf("Bad partition number from '%s'\n", partition_dev.c_str());

  return result;
}

string MakePartitionDev(const string& block_dev, int partition) {
  if (base::StartsWith(block_dev, "/dev/mtd", base::CompareCase::SENSITIVE) ||
      base::StartsWith(block_dev, "/dev/ubi", base::CompareCase::SENSITIVE)) {
    return MakeNandPartitionDevForMounting(partition);
  }

  for (const char** nd = begin(numbered_devices); nd != end(numbered_devices);
       nd++) {
    size_t nd_len = strlen(*nd);
    if (block_dev.compare(0, nd_len, *nd) == 0)
      return block_dev + "p" + std::to_string(partition);
  }

  return block_dev + std::to_string(partition);
}

// Convert /blah/file to /blah
string Dirname(const string& filename) {
  size_t last_slash = filename.rfind('/');

  if (last_slash == string::npos)
    return "";

  return filename.substr(0, last_slash);
}

// rm *pack from /dirname
bool RemovePackFiles(const string& dirname) {
  DIR* dp;
  struct dirent* ep;

  dp = opendir(dirname.c_str());

  if (dp == NULL)
    return false;

  while ((ep = readdir(dp))) {
    string filename = ep->d_name;

    // Skip . files
    if (filename.compare(0, 1, ".") == 0)
      continue;

    if ((filename.size() < 4) ||
        (filename.compare(filename.size() - 4, 4, "pack") != 0))
      continue;

    string full_filename = dirname + '/' + filename;

    printf("Unlinked file %s\n", full_filename.c_str());
    unlink(full_filename.c_str());
  }

  closedir(dp);

  return true;
}

bool Touch(const string& filename) {
  int fd = open(filename.c_str(), O_WRONLY | O_CREAT,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

  if (fd == -1)
    return false;

  return (close(fd) == 0);
}

// Replace the first instance of pattern in the file with value.
bool ReplaceInFile(const string& pattern,
                   const string& value,
                   const string& path) {
  string contents;

  if (!ReadFileToString(path, &contents))
    return false;

  // Modify contents
  size_t offset = contents.find(pattern);

  if (offset == string::npos) {
    printf("ReplaceInFile failed to find '%s' in %s\n", pattern.c_str(),
           path.c_str());
    return false;
  }

  contents.replace(offset, pattern.length(), value);

  if (!WriteStringToFile(contents, path))
    return false;

  return true;
}

void ReplaceAll(string* target, const string& pattern, const string& value) {
  for (size_t offset = 0;;) {
    offset = target->find(pattern, offset);
    if (offset == string::npos)
      return;
    target->replace(offset, pattern.length(), value);
    offset += value.length();
  }
}

bool MakeFileSystemRw(const string& dev_name) {
  const int offset = 0x464 + 3;  // Set 'highest' byte

  base::ScopedFD fd(open(dev_name.c_str(), O_RDWR));
  if (!fd.is_valid()) {
    printf("Failed to open %s\n", dev_name.c_str());
    return false;
  }

  const off_t magic_offset = 0x438;
  if (lseek(fd.get(), magic_offset, SEEK_SET) != magic_offset) {
    printf("Failed to seek\n");
    return false;
  }

  uint16_t fs_id;
  if (read(fd.get(), &fs_id, sizeof(fs_id)) != sizeof(fs_id)) {
    printf("Can't read the filesystem identifier\n");
    return false;
  }

  if (fs_id != 0xef53) {
    printf("Non-EXT filesystem with magic 0x%04x can't be made writable.\n",
           fs_id);
    return false;
  }

  // Write out stuff
  if (lseek(fd.get(), offset, SEEK_SET) != offset) {
    printf("Failed to seek\n");
    return false;
  }

  unsigned char buff = 0;  // rw enabled.  0xFF for disable_rw_mount

  if (write(fd.get(), &buff, 1) != 1) {
    printf("Failed to write\n");
    return false;
  }

  fd.reset();
  return true;
}

extern "C" {

// The external dumpkernelconfig.a library depends on this symbol
// existing, so I redefined it here. I deserve to suffer
// very, very painfully for this, but hey.
__attribute__((__format__(__printf__, 1, 2))) void VbExError(const char* format,
                                                             ...) {
  va_list ap;
  va_start(ap, format);
  fprintf(stderr, "ERROR: ");
  vfprintf(stderr, format, ap);
  va_end(ap);
}
}

string DumpKernelConfig(const string& kernel_dev) {
  string result;

  char* config = FindKernelConfig(kernel_dev.c_str(), USE_PREAMBLE_LOAD_ADDR);
  if (!config) {
    printf("Error retrieving kernel config from '%s'\n", kernel_dev.c_str());
    return result;
  }

  result = string(config, MAX_KERNEL_CONFIG_SIZE);
  free(config);

  return result;
}

bool FindKernelArgValueOffsets(const string& kernel_config,
                               const string& key,
                               size_t* value_offset,
                               size_t* value_length) {
  // We are really looking for key=value
  string preamble = key + "=";

  size_t i;

  // Search for arg...
  for (i = 0; i < kernel_config.size(); i++) {
    // If we hit a " while searching, skip to matching quote
    if (kernel_config[i] == '"') {
      i++;
      while (i < kernel_config.size() && kernel_config[i] != '"')
        i++;
    }

    // if we found the key
    if (kernel_config.compare(i, preamble.size(), preamble) == 0)
      break;
  }

  // Didn't find the key
  if (i >= kernel_config.size())
    return false;

  // Jump past the key
  i += preamble.size();

  *value_offset = i;

  // If it's a quoted value, look for closing quote
  if (kernel_config[i] == '"') {
    i = kernel_config.find('"', i + 1);

    // If there is no closing quote, it's an error.
    if (i == string::npos)
      return false;

    i += 1;
  }

  while (i < kernel_config.size() && kernel_config[i] != ' ')
    i++;

  *value_length = i - *value_offset;
  return true;
}

string ExtractKernelArg(const string& kernel_config, const string& key) {
  size_t value_offset;
  size_t value_length;

  if (!FindKernelArgValueOffsets(kernel_config, key, &value_offset,
                                 &value_length))
    return "";

  string result = kernel_config.substr(value_offset, value_length);

  if ((result.length() >= 2) && (result[0] == '"') &&
      (result[result.length() - 1] == '"')) {
    result = result.substr(1, result.length() - 2);
  }

  return result;
}

bool SetKernelArg(const string& key,
                  const string& value,
                  string* kernel_config) {
  size_t value_offset;
  size_t value_length;

  if (!FindKernelArgValueOffsets(*kernel_config, key, &value_offset,
                                 &value_length))
    return false;

  string adjusted_value = value;

  if (value.find(" ") != string::npos) {
    adjusted_value = "\"" + value + "\"";
  }

  kernel_config->replace(value_offset, value_length, adjusted_value);
  return true;
}

// For the purposes of ChromeOS, devices that start with
// "/dev/dm" are to be treated as read-only.
bool IsReadonly(const string& device) {
  return base::StartsWith(device, "/dev/dm", base::CompareCase::SENSITIVE) ||
         base::StartsWith(device, "/dev/ubi", base::CompareCase::SENSITIVE);
}

bool GetKernelInfo(std::string* result) {
  if (result == nullptr) {
    return false;
  }

  struct utsname buf;
  if (uname(&buf)) {
    fprintf(stderr, "ERROR: uname() failed with errno: %s", strerror(errno));
    return false;
  }

  *result = string("") + "sysname(" + buf.sysname + ") nodename(" +
            buf.nodename + ") release(" + buf.release + ") version(" +
            buf.version + ") machine(" + buf.machine + ")";
  return true;
}
