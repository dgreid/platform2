// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/multiplexer.h"

#include <utility>

#include "base/strings/string_util.h"

namespace croslog {

Multiplexer::LogSource::LogSource(base::FilePath log_file,
                                  bool install_change_watcher)
    : file_path(log_file) {
  reader.OpenFile(std::move(file_path), install_change_watcher);
}

Multiplexer::Multiplexer() = default;

void Multiplexer::AddSource(base::FilePath log_file,
                            bool install_change_watcher) {
  auto source =
      std::make_unique<LogSource>(std::move(log_file), install_change_watcher);
  source->reader.AddObserver(this);
  sources_.emplace_back(std::move(source));
}

void Multiplexer::OnFileChanged() {
  for (Observer& obs : observers_)
    obs.OnLogFileChanged();
}

RawLogLineUnsafe Multiplexer::Forward() {
  for (auto& source : sources_) {
    RawLogLineUnsafe s = source->reader.Forward();
    // TODO(yoshiki): Parse the read lines from the all sources and return the
    // oldest one.
    if (s.data() != nullptr)
      return s;
  }
  return RawLogLineUnsafe();
}

RawLogLineUnsafe Multiplexer::Backward() {
  for (auto& source : sources_) {
    RawLogLineUnsafe s = source->reader.Backward();
    // TODO(yoshiki): Parse the read lines from the all sources and return the
    // newest one.
    if (s.data() != nullptr)
      return s;
  }
  return RawLogLineUnsafe();
}

void Multiplexer::AddObserver(Observer* obs) {
  observers_.AddObserver(obs);
}

void Multiplexer::RemoveObserver(Observer* obs) {
  observers_.RemoveObserver(obs);
}

void Multiplexer::SetLinesFromLast(uint32_t pos) {
  for (auto& source : sources_) {
    source->reader.SetPositionLast();
  }

  for (int i = 0; i < pos; i++) {
    RawLogLineUnsafe s = Backward();
    if (!s.data())
      return;
  }
}

}  // namespace croslog
