// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "installer/cgpt_manager.h"

#include <err.h>
#include <linux/major.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/strings/stringprintf.h>

#include "installer/inst_util.h"

extern "C" {
#include <vboot/vboot_host.h>
}

using std::string;

namespace {

// Create a temp file, read GPT structs from NOR flash to that file, and return
// true on success. On success, |file_name| contains the path to the temp file.
bool ReadGptFromNor(string* file_name) {
  char tmp_name[] = "/tmp/cgptmanagerXXXXXX";
  int fd = mkstemp(tmp_name);
  if (fd < 0) {
    warn("Cannot create temp file to store GPT structs read from NOR");
    return false;
  }
  // Extra parens to work around the compiler parser.
  ScopedPathRemover remover((string(tmp_name)));
  // Close fd so that flashrom can write to the file right after.
  close(fd);
  if (RunCommand({"/usr/sbin/flashrom", "-i", string("RW_GPT:") + tmp_name,
                  "-r"}) != 0) {
    return false;
  }
  // Keep the temp file.
  remover.Release();
  *file_name = tmp_name;
  return true;
}

// Write |data| to NOR flash at FMAP |region|. Return true on success.
bool WriteToNor(const string& data, const string& region) {
  char tmp_name[] = "/tmp/cgptmanagerXXXXXX";
  base::ScopedFD fd(mkstemp(tmp_name));
  if (!fd.is_valid()) {
    warn("Cannot create temp file to write to NOR flash");
    return false;
  }

  // Extra parens to work around the compiler parser.
  ScopedPathRemover remover((string(tmp_name)));
  if (!WriteFullyToFileDescriptor(data, fd.get())) {
    warnx("Cannot write data to temp file %s.\n", tmp_name);
    return false;
  }

  // Close fd so that flashrom can open it right after.
  fd.reset();

  std::vector<string> cmd{"/usr/sbin/flashrom", "-i", region + ":" + tmp_name,
                          "-w", "--fast-verify"};
  if (RunCommand(cmd) != 0) {
    warnx("Cannot write %s to %s section.\n", tmp_name, region.c_str());
    return false;
  }

  return true;
}

// Write GPT data in |file_name| file to NOR flash. This function writes the
// content in two halves, one to RW_GPT_PRIMARY, and another to RW_GPT_SECONDARY
// sections. Return negative on failure, 0 on success, a positive integer means
// that many parts failed. Due to the way GPT works, we usually could recover
// from one failure.
int WriteGptToNor(const string& file_name) {
  string gpt_data;
  if (!base::ReadFileToString(base::FilePath(file_name), &gpt_data)) {
    warnx("Cannot read from %s.\n", file_name.c_str());
    return -1;
  }

  int ret = 0;
  if (!WriteToNor(gpt_data.substr(0, gpt_data.length() / 2),
                  "RW_GPT_PRIMARY")) {
    ret++;
  }
  if (!WriteToNor(gpt_data.substr(gpt_data.length() / 2), "RW_GPT_SECONDARY")) {
    ret++;
  }

  switch (ret) {
    case 0: {
      break;
    }
    case 1: {
      warnx("Failed to write some part. It might still be okay.\n");
      break;
    }
    case 2: {
      warnx("Cannot write either part to flashrom.\n");
      break;
    }
    default: {
      errx(-1, "Unexpected number of write failures (%d)", ret);
      break;
    }
  }
  return ret;
}

// Set or clear |is_mtd| depending on if |block_dev| points to an MTD device.
bool IsMtd(const string& block_dev, bool* is_mtd) {
  struct stat stat_buf;
  if (stat(block_dev.c_str(), &stat_buf) != 0) {
    warn("Failed to stat %s", block_dev.c_str());
    return false;
  }
  *is_mtd = (major(stat_buf.st_rdev) == MTD_CHAR_MAJOR);
  return true;
}

// Return the size of MTD device |block_dev| in |ret|.
bool GetMtdSize(const string& block_dev, uint64_t* ret) {
  base::FilePath size_file = base::FilePath("/sys/class/mtd/")
                                 .Append(basename(block_dev.c_str()))
                                 .Append("size");
  string size_string;
  if (!base::ReadFileToString(size_file, &size_string)) {
    warnx("Cannot read MTD size from %s.\n", size_file.value().c_str());
    return false;
  }

  uint64_t size;
  char* end;
  size = strtoull(size_string.c_str(), &end, 10);
  if (*end != '\x0A') {
    warn("Cannot convert %s into decimal", size_string.c_str());
    return false;
  }

  *ret = size;
  return true;
}

}  // namespace

// This file implements the C++ wrapper methods over the C cgpt methods.

CgptManager::CgptManager() : device_size_(0), is_initialized_(false) {}

CgptManager::~CgptManager() {
  Finalize();
}

CgptErrorCode CgptManager::Initialize(const string& device_name) {
  device_name_ = device_name;
  bool is_mtd;
  if (!IsMtd(device_name, &is_mtd)) {
    warnx("Cannot determine if %s is an MTD device.\n", device_name.c_str());
    return kCgptNotInitialized;
  }
  if (is_mtd) {
    warnx("%s is an MTD device.\n", device_name.c_str());
    if (!GetMtdSize(device_name, &device_size_)) {
      warnx("But we do not know its size.\n");
      return kCgptNotInitialized;
    }
    if (!ReadGptFromNor(&device_name_)) {
      warnx("Failed to read GPT structs from NOR flash.\n");
      return kCgptNotInitialized;
    }
  }
  is_initialized_ = true;
  return kCgptSuccess;
}

CgptErrorCode CgptManager::Finalize() {
  if (!is_initialized_) {
    return kCgptNotInitialized;
  }

  if (device_size_) {
    if (WriteGptToNor(device_name_) != 0) {
      return kCgptUnknownError;
    }
    if (unlink(device_name_.c_str()) != 0) {
      warn("Cannot remove temp file %s", device_name_.c_str());
    }
  }

  device_size_ = 0;
  is_initialized_ = false;
  return kCgptSuccess;
}

CgptErrorCode CgptManager::ClearAll() {
  if (!is_initialized_)
    return kCgptNotInitialized;

  CgptCreateParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.c_str());
  params.drive_size = device_size_;
  params.zap = 0;

  int retval = CgptCreate(&params);
  if (retval != CGPT_OK)
    return kCgptUnknownError;

  return kCgptSuccess;
}

CgptErrorCode CgptManager::AddPartition(const string& label,
                                        const Guid& partition_type_guid,
                                        const Guid& unique_id,
                                        uint64_t beginning_offset,
                                        uint64_t num_sectors) {
  if (!is_initialized_)
    return kCgptNotInitialized;

  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.c_str());
  params.drive_size = device_size_;
  params.label = const_cast<char*>(label.c_str());

  params.type_guid = partition_type_guid;
  params.set_type = 1;

  params.begin = beginning_offset;
  params.set_begin = 1;

  params.size = num_sectors;
  params.set_size = 1;

  if (!GuidIsZero(&unique_id)) {
    params.unique_guid = unique_id;
    params.set_unique = 1;
  }

  int retval = CgptAdd(&params);
  if (retval != CGPT_OK)
    return kCgptUnknownError;

  return kCgptSuccess;
}

CgptErrorCode CgptManager::GetNumNonEmptyPartitions(
    uint8_t* num_partitions) const {
  if (!is_initialized_)
    return kCgptNotInitialized;

  if (!num_partitions)
    return kCgptInvalidArgument;

  CgptShowParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.c_str());
  params.drive_size = device_size_;
  int retval = CgptGetNumNonEmptyPartitions(&params);

  if (retval != CGPT_OK)
    return kCgptUnknownError;

  *num_partitions = params.num_partitions;
  return kCgptSuccess;
}

CgptErrorCode CgptManager::SetPmbr(uint32_t boot_partition_number,
                                   const string& boot_file_name,
                                   bool should_create_legacy_partition) {
  if (!is_initialized_)
    return kCgptNotInitialized;

  CgptBootParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.c_str());
  params.drive_size = device_size_;
  if (!boot_file_name.empty())
    params.bootfile = const_cast<char*>(boot_file_name.c_str());

  params.partition = boot_partition_number;
  params.create_pmbr = should_create_legacy_partition;

  int retval = CgptBoot(&params);
  if (retval != CGPT_OK)
    return kCgptUnknownError;

  return kCgptSuccess;
}

CgptErrorCode CgptManager::GetPmbrBootPartitionNumber(
    uint32_t* boot_partition) const {
  if (!is_initialized_)
    return kCgptNotInitialized;

  if (!boot_partition)
    return kCgptInvalidArgument;

  CgptBootParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.c_str());
  params.drive_size = device_size_;

  int retval = CgptGetBootPartitionNumber(&params);
  if (retval != CGPT_OK)
    return kCgptUnknownError;

  *boot_partition = params.partition;
  return kCgptSuccess;
}

CgptErrorCode CgptManager::SetSuccessful(uint32_t partition_number,
                                         bool is_successful) {
  if (!is_initialized_)
    return kCgptNotInitialized;

  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.c_str());
  params.drive_size = device_size_;
  params.partition = partition_number;

  params.successful = is_successful;
  params.set_successful = true;

  int retval = CgptSetAttributes(&params);
  if (retval != CGPT_OK)
    return kCgptUnknownError;

  return kCgptSuccess;
}

CgptErrorCode CgptManager::GetSuccessful(uint32_t partition_number,
                                         bool* is_successful) const {
  if (!is_initialized_)
    return kCgptNotInitialized;

  if (!is_successful)
    return kCgptInvalidArgument;

  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.c_str());
  params.drive_size = device_size_;
  params.partition = partition_number;

  int retval = CgptGetPartitionDetails(&params);
  if (retval != CGPT_OK)
    return kCgptUnknownError;

  *is_successful = params.successful;
  return kCgptSuccess;
}

CgptErrorCode CgptManager::SetNumTriesLeft(uint32_t partition_number,
                                           int numTries) {
  if (!is_initialized_)
    return kCgptNotInitialized;

  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.c_str());
  params.drive_size = device_size_;
  params.partition = partition_number;

  params.tries = numTries;
  params.set_tries = true;

  int retval = CgptSetAttributes(&params);
  if (retval != CGPT_OK)
    return kCgptUnknownError;

  return kCgptSuccess;
}

CgptErrorCode CgptManager::GetNumTriesLeft(uint32_t partition_number,
                                           int* numTries) const {
  if (!is_initialized_)
    return kCgptNotInitialized;

  if (!numTries)
    return kCgptInvalidArgument;

  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.c_str());
  params.drive_size = device_size_;
  params.partition = partition_number;

  int retval = CgptGetPartitionDetails(&params);
  if (retval != CGPT_OK)
    return kCgptUnknownError;

  *numTries = params.tries;
  return kCgptSuccess;
}

CgptErrorCode CgptManager::SetPriority(uint32_t partition_number,
                                       uint8_t priority) {
  if (!is_initialized_)
    return kCgptNotInitialized;

  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.c_str());
  params.drive_size = device_size_;
  params.partition = partition_number;

  params.priority = priority;
  params.set_priority = true;

  int retval = CgptSetAttributes(&params);
  if (retval != CGPT_OK)
    return kCgptUnknownError;

  return kCgptSuccess;
}

CgptErrorCode CgptManager::GetPriority(uint32_t partition_number,
                                       uint8_t* priority) const {
  if (!is_initialized_)
    return kCgptNotInitialized;

  if (!priority)
    return kCgptInvalidArgument;

  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.c_str());
  params.drive_size = device_size_;
  params.partition = partition_number;

  int retval = CgptGetPartitionDetails(&params);
  if (retval != CGPT_OK)
    return kCgptUnknownError;

  *priority = params.priority;
  return kCgptSuccess;
}

CgptErrorCode CgptManager::GetBeginningOffset(uint32_t partition_number,
                                              uint64_t* offset) const {
  if (!is_initialized_)
    return kCgptNotInitialized;

  if (!offset)
    return kCgptInvalidArgument;

  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.c_str());
  params.drive_size = device_size_;
  params.partition = partition_number;

  int retval = CgptGetPartitionDetails(&params);
  if (retval != CGPT_OK)
    return kCgptUnknownError;

  *offset = params.begin;
  return kCgptSuccess;
}

CgptErrorCode CgptManager::GetNumSectors(uint32_t partition_number,
                                         uint64_t* num_sectors) const {
  if (!is_initialized_)
    return kCgptNotInitialized;

  if (!num_sectors)
    return kCgptInvalidArgument;

  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.c_str());
  params.drive_size = device_size_;
  params.partition = partition_number;

  int retval = CgptGetPartitionDetails(&params);
  if (retval != CGPT_OK)
    return kCgptUnknownError;

  *num_sectors = params.size;
  return kCgptSuccess;
}

CgptErrorCode CgptManager::GetPartitionTypeId(uint32_t partition_number,
                                              Guid* type_id) const {
  if (!is_initialized_)
    return kCgptNotInitialized;

  if (!type_id)
    return kCgptInvalidArgument;

  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.c_str());
  params.drive_size = device_size_;
  params.partition = partition_number;

  int retval = CgptGetPartitionDetails(&params);
  if (retval != CGPT_OK)
    return kCgptUnknownError;

  *type_id = params.type_guid;
  return kCgptSuccess;
}

CgptErrorCode CgptManager::GetPartitionUniqueId(uint32_t partition_number,
                                                Guid* unique_id) const {
  if (!is_initialized_)
    return kCgptNotInitialized;

  if (!unique_id)
    return kCgptInvalidArgument;

  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.c_str());
  params.drive_size = device_size_;
  params.partition = partition_number;

  int retval = CgptGetPartitionDetails(&params);
  if (retval != CGPT_OK)
    return kCgptUnknownError;

  *unique_id = params.unique_guid;
  return kCgptSuccess;
}

CgptErrorCode CgptManager::GetPartitionNumberByUniqueId(
    const Guid& unique_id, uint32_t* partition_number) const {
  if (!is_initialized_)
    return kCgptNotInitialized;

  if (!partition_number)
    return kCgptInvalidArgument;

  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.c_str());
  params.drive_size = device_size_;
  params.unique_guid = unique_id;
  params.set_unique = 1;

  int retval = CgptGetPartitionDetails(&params);
  if (retval != CGPT_OK)
    return kCgptUnknownError;

  *partition_number = params.partition;
  return kCgptSuccess;
}

CgptErrorCode CgptManager::SetHighestPriority(uint32_t partition_number,
                                              uint8_t highest_priority) {
  if (!is_initialized_)
    return kCgptNotInitialized;

  CgptPrioritizeParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.c_str());
  params.drive_size = device_size_;
  params.set_partition = partition_number;
  params.max_priority = highest_priority;

  int retval = CgptPrioritize(&params);
  if (retval != CGPT_OK)
    return kCgptUnknownError;

  return kCgptSuccess;
}

CgptErrorCode CgptManager::SetHighestPriority(uint32_t partition_number) {
  // The internal implementation in CgptPrioritize automatically computes the
  // right priority number if we supply 0 for the highest_priority argument.
  return SetHighestPriority(partition_number, 0);
}

CgptErrorCode CgptManager::Validate() {
  if (!is_initialized_)
    return kCgptNotInitialized;

  uint8_t num_partitions;

  // GetNumNonEmptyPartitions does the check for GptSanityCheck.
  // so call it (ignore the num_partitions result) and just return
  // its success/failure result.
  return GetNumNonEmptyPartitions(&num_partitions);
}
