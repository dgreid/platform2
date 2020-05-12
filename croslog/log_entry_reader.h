// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROSLOG_LOG_ENTRY_READER_H_
#define CROSLOG_LOG_ENTRY_READER_H_

#include <memory>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/memory_mapped_file.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"

#include "croslog/file_change_watcher.h"

namespace croslog {

// RawLogLineUnsafe isn't guaranteed to be safe to read, since this doesn't own
// the underlying memory. We have to use it with care and lifetime.
using RawLogLineUnsafe = base::StringPiece;

class LogEntryReader {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnFileChanged() = 0;
  };

  LogEntryReader();
  virtual ~LogEntryReader();

  // Open the file to read.
  void OpenFile(const base::FilePath& file_path, bool install_change_watcher);
  // Open the buffer on memory instead of a file.
  void OpenMemoryBufferForTest(const char* buffer, size_t size);
  // Read the next line from log. Returns an invalid line (.data() == nullptr)
  // on error or when the pos reaches the last.
  // CAUTION: Returned line gets invalidate when the file is remapped. We can
  // keep it only during this run loop.
  RawLogLineUnsafe Forward();
  // Read the previous line from log. Returns an invalid line (.data() ==
  // nullptr) on error or when the pos reaches the last.
  // CAUTION: Returned line gets invalidate when the file is remapped. We can
  // keep it only during this run loop.
  RawLogLineUnsafe Backward();
  // Set the position to read last.
  void SetPositionLast();
  // Add a observer to retrieve file change events.
  void AddObserver(Observer* obs);
  // Remove a observer to retrieve file change events.
  void RemoveObserver(Observer* obs);

  // Retrieve the current position in bytes.
  off_t position() const { return pos_; }

 private:
  void Remap();
  void OnChanged();

  base::File file_;
  base::FilePath file_path_;
  FileChangeWatcher* file_change_watcher_ = nullptr;

  std::unique_ptr<base::MemoryMappedFile> mmap_;

  const uint8_t* buffer_ = nullptr;
  uint64_t buffer_size_ = 0;

  // Position must be between [0, buffer_size_]. |buffer_[pos]| might be
  // invalid.
  off_t pos_ = 0;

  base::ObserverList<Observer> observers_;

  DISALLOW_COPY_AND_ASSIGN(LogEntryReader);
};

}  // namespace croslog

#endif  // CROSLOG_LOG_ENTRY_READER_H_

