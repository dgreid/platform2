// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/log_line_reader.h"

#include <string>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "base/bind.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/strings/string_util.h"

namespace croslog {

namespace {
const uint8_t kEmptyBuffer[] = {};
}  // anonymous namespace

// 256 MB limit.
// TODO(yoshiki): adjust it to an appropriate value.
constexpr size_t kMaxFileSize = 256l * 1024 * 1024 - 1;

LogLineReader::LogLineReader(Backend backend_mode)
    : backend_mode_(backend_mode) {}

LogLineReader::~LogLineReader() {
  if (file_change_watcher_)
    file_change_watcher_->RemoveWatch(file_path_);
}

void LogLineReader::OpenFile(const base::FilePath& file_path) {
  CHECK(backend_mode_ == Backend::FILE ||
        backend_mode_ == Backend::FILE_FOLLOW);

  // Ensure the values are not initialized.
  CHECK(file_path_.empty());
  CHECK(buffer_ == nullptr);

  file_ = base::File(file_path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file_.IsValid()) {
    LOG(ERROR) << "Could not open " << file_path;
    return;
  }
  file_path_ = file_path;
  pos_ = 0;

  if (backend_mode_ == Backend::FILE_FOLLOW) {
    // Race may happen when the file rotates just after file opens.
    // TODO(yoshiki): detect the race.
    file_change_watcher_ = FileChangeWatcher::GetInstance();
    bool ret = file_change_watcher_->AddWatch(
        file_path_,
        base::BindRepeating(&LogLineReader::OnChanged, base::Unretained(this)));
    if (!ret) {
      LOG(ERROR) << "Failed to install FileChangeWatcher for " << file_path_
                 << ".";
      file_change_watcher_ = nullptr;
    }
  }

  Remap();
}

void LogLineReader::OpenMemoryBufferForTest(const char* buffer, size_t size) {
  CHECK(backend_mode_ == Backend::MEMORY_FOR_TEST);

  buffer_ = (const uint8_t*)buffer;
  buffer_size_ = size;
}

void LogLineReader::SetPositionLast() {
  pos_ = buffer_size_;

  while (pos_ >= 1 && buffer_[pos_ - 1] != '\n')
    pos_--;
}

void LogLineReader::Remap() {
  CHECK(backend_mode_ == Backend::FILE ||
        backend_mode_ == Backend::FILE_FOLLOW);

  // Ensure the file path is initialized.
  CHECK(!file_path_.empty());

  int64_t file_size = file_.GetLength();

  if (file_size > kMaxFileSize) {
    LOG(ERROR) << "File is bigger than the supported size (" << file_size
               << " > " << kMaxFileSize << ").";
    return;
  }

  if (mmap_ && buffer_size_ == file_size)
    return;

  if (mmap_ && buffer_size_ > file_size) {
    LOG(WARNING) << "Log file gets smaller. Croslog doesn't support file "
                 << "changes except for appending lines.";
    if (pos_ > file_size) {
      // Fall back to set the postiton to last.
      pos_ = file_size;
    }
  }

  mmap_.reset();
  buffer_ = nullptr;
  buffer_size_ = 0;

  if (file_size == 0) {
    // Returning without (re)mmapping, since mmapping an empty file fails.
    buffer_ = kEmptyBuffer;
    return;
  }

  base::File file_duplicated = file_.Duplicate();

  base::MemoryMappedFile::Region mmap_region;
  mmap_region.offset = 0;
  mmap_region.size = file_size;

  auto new_mmap = std::make_unique<base::MemoryMappedFile>();
  bool mmap_result =
      new_mmap->Initialize(std::move(file_duplicated), mmap_region);

  if (!mmap_result) {
    LOG(ERROR) << "Doing mmap (" << file_path_ << ") failed.";
    // resetting position.
    pos_ = 0;
    return;
  }

  std::swap(mmap_, new_mmap);
  buffer_ = mmap_->data();
  buffer_size_ = file_size;
}

base::Optional<std::string> LogLineReader::Forward() {
  CHECK(buffer_ != nullptr);

  if (pos_ != 0 && buffer_[pos_ - 1] != '\n') {
    LOG(WARNING) << "The file looks changed unexpectedly. The lines read may "
                 << "be broken.";
  }

  if (pos_ >= buffer_size_)
    return base::nullopt;

  off_t pos_line_end = -1;
  for (off_t i = pos_; i < buffer_size_; i++) {
    if (buffer_[i] == '\n') {
      pos_line_end = i;
      break;
    }
  }

  if (pos_line_end == -1) {
    // Reach EOF without '\n'.
    return base::nullopt;
  }

  size_t pos_line_start = pos_;
  size_t line_length = pos_line_end - pos_;
  pos_ = pos_line_end + 1;

  return GetString(pos_line_start, line_length);
}

base::Optional<std::string> LogLineReader::Backward() {
  CHECK(buffer_ != nullptr);

  if (pos_ != 0 && buffer_[pos_ - 1] != '\n') {
    LOG(WARNING) << "The file looks changed unexpectedly. The lines read may "
                 << "be broken.";
  }

  if (pos_ == 0)
    return base::nullopt;

  off_t last_start = pos_ - 1;
  while (last_start > 0 && buffer_[last_start - 1] != '\n') {
    last_start--;
  }

  size_t line_length = pos_ - last_start - 1;
  pos_ = last_start;
  return GetString(last_start, line_length);
}

void LogLineReader::AddObserver(Observer* obs) {
  observers_.AddObserver(obs);
}

void LogLineReader::RemoveObserver(Observer* obs) {
  observers_.RemoveObserver(obs);
}

std::string LogLineReader::GetString(off_t offset, size_t length) const {
  CHECK(buffer_ != nullptr);
  CHECK_GE(offset, 0);
  CHECK_LE((offset + length), buffer_size_);

  return std::string(reinterpret_cast<const char*>(buffer_ + offset), length);
}

void LogLineReader::OnChanged() {
  CHECK(backend_mode_ == Backend::FILE_FOLLOW);

  Remap();
  for (Observer& obs : observers_)
    obs.OnFileChanged(this);
}

}  // namespace croslog
