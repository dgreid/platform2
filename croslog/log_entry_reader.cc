// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/log_entry_reader.h"

#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "base/bind.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/strings/string_util.h"

namespace croslog {

// 256 MB limit.
// TODO(yoshiki): adjust it to an appropriate value.
constexpr size_t kMaxFileSize = 256l * 1024 * 1024 - 1;

LogEntryReader::LogEntryReader() = default;

LogEntryReader::~LogEntryReader() {
  if (file_change_watcher_)
    file_change_watcher_->RemoveWatch(file_path_);
}

void LogEntryReader::OpenFile(const base::FilePath& file_path,
                              bool install_change_watcher) {
  CHECK(file_path_.empty());
  CHECK(buffer_ == nullptr);

  file_ = base::File(file_path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file_.IsValid()) {
    LOG(ERROR) << "Could not open " << file_path;
    return;
  }
  file_path_ = file_path;

  if (install_change_watcher) {
    // Race may happen when the file rotates just after file opens.
    // TODO(yoshiki): detect the race.
    file_change_watcher_ = FileChangeWatcher::GetInstance();
    bool ret = file_change_watcher_->AddWatch(
        file_path_, base::BindRepeating(&LogEntryReader::OnChanged,
                                        base::Unretained(this)));
    if (!ret) {
      LOG(ERROR) << "Failed to install FileChangeWatcher for " << file_path_
                 << ".";
      file_change_watcher_ = nullptr;
    }
  }

  Remap();
}

void LogEntryReader::OpenMemoryBufferForTest(const char* buffer, size_t size) {
  CHECK(file_path_.empty());

  buffer_ = (const uint8_t*)buffer;
  buffer_size_ = size;
}

void LogEntryReader::SetPositionLast() {
  pos_ = buffer_size_;

  while (pos_ >= 1 && buffer_[pos_ - 1] != '\n')
    pos_--;
}

void LogEntryReader::Remap() {
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

  base::File file_duplicated = file_.Duplicate();

  base::MemoryMappedFile::Region mmap_region;
  mmap_region.offset = 0;
  mmap_region.size = file_size;

  mmap_ = std::make_unique<base::MemoryMappedFile>();
  bool mmap_result = mmap_->Initialize(std::move(file_duplicated), mmap_region);

  if (!mmap_result) {
    buffer_ = nullptr;
    buffer_size_ = 0;
    return;
  }

  buffer_ = mmap_->data();
  buffer_size_ = file_size;
}

RawLogLineUnsafe LogEntryReader::Forward() {
  CHECK(buffer_ != nullptr);

  if (pos_ != 0 && buffer_[pos_ - 1] != '\n') {
    LOG(WARNING) << "The file looks changed unexpectedly. The lines read may "
        << "be broken.";
  }

  if (pos_ >= buffer_size_)
    return RawLogLineUnsafe();

  off_t pos_line_end = -1;
  for (off_t i = pos_; i < buffer_size_; i++) {
    if (buffer_[i] == '\n') {
      pos_line_end = i;
      break;
    }
  }

  if (pos_line_end == -1) {
    // Reach EOF without '\n'.
    return RawLogLineUnsafe();
  }

  size_t pos_line_start = pos_;
  size_t line_length = pos_line_end - pos_;
  pos_ = pos_line_end + 1;

  return RawLogLineUnsafe((const char*)buffer_ + pos_line_start, line_length);
}

RawLogLineUnsafe LogEntryReader::Backward() {
  CHECK(buffer_ != nullptr);

  if (pos_ != 0 && buffer_[pos_ - 1] != '\n') {
    LOG(WARNING) << "The file looks changed unexpectedly. The lines read may "
        << "be broken.";
  }

  if (pos_ == 0)
    return RawLogLineUnsafe();

  off_t last_start = pos_ - 1;
  while (last_start > 0 && buffer_[last_start - 1] != '\n') {
    last_start--;
  }

  size_t line_length = pos_ - last_start - 1;
  pos_ = last_start;
  return RawLogLineUnsafe((const char*)buffer_ + last_start, line_length);
}

void LogEntryReader::AddObserver(Observer* obs) {
  observers_.AddObserver(obs);
}

void LogEntryReader::RemoveObserver(Observer* obs) {
  observers_.RemoveObserver(obs);
}

void LogEntryReader::OnChanged() {
  Remap();
  for (Observer& obs : observers_)
    obs.OnFileChanged();
}

}  // namespace croslog
