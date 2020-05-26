// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/multiplexer.h"

#include <utility>

#include "base/strings/string_util.h"

#include "croslog/log_parser_syslog.h"

namespace croslog {

Multiplexer::LogSource::LogSource(base::FilePath log_file,
                                  std::unique_ptr<LogParser> parser_in,
                                  bool install_change_watcher)
    : file_path(log_file),
      reader(install_change_watcher ? LogLineReader::Backend::FILE_FOLLOW
                                    : LogLineReader::Backend::FILE),
      parser(std::move(parser_in)) {
  reader.OpenFile(std::move(file_path));
}

Multiplexer::Multiplexer() = default;

void Multiplexer::AddSource(base::FilePath log_file,
                            std::unique_ptr<LogParser> parser,
                            bool install_change_watcher) {
  auto source = std::make_unique<LogSource>(
      std::move(log_file), std::move(parser), install_change_watcher);
  source->reader.AddObserver(this);
  sources_.emplace_back(std::move(source));
}

void Multiplexer::OnFileChanged(LogLineReader* reader) {
  for (auto&& source : sources_) {
    if (&source->reader != reader)
      continue;

    // Invalidate caches, since the backed buffer may be invalid.
    if (source->cache_next_backward.has_value()) {
      CHECK(!source->cache_next_forward.has_value());
      source->cache_next_backward.reset();
      source->reader.Forward();
    } else if (source->cache_next_forward.has_value()) {
      source->cache_next_forward.reset();
      source->reader.Backward();
    }
  }

  for (Observer& obs : observers_)
    obs.OnLogFileChanged();
}

MaybeLogEntry Multiplexer::Forward() {
  for (auto&& source : sources_) {
    if (source->cache_next_backward.has_value()) {
      CHECK(!source->cache_next_forward.has_value());
      source->cache_next_backward.reset();
      source->reader.Forward();
    }

    if (!source->cache_next_forward.has_value()) {
      base::Optional<std::string> log = source->reader.Forward();
      if (log.has_value())
        source->cache_next_forward =
            source->parser->Parse(std::move(log.value()));
      else
        source->cache_next_backward = base::nullopt;
    }
  }

  Multiplexer::LogSource* next_source = nullptr;
  for (auto&& source : sources_) {
    if (!source->cache_next_forward.has_value()) {
      continue;
    }

    if (next_source == nullptr || next_source->cache_next_forward->time() >
                                      source->cache_next_forward->time()) {
      next_source = source.get();
    }
  }

  if (next_source == nullptr) {
    return base::nullopt;
  }

  MaybeLogEntry entry;
  entry.swap(next_source->cache_next_forward);
  return entry;
}

MaybeLogEntry Multiplexer::Backward() {
  for (auto&& source : sources_) {
    if (source->cache_next_forward.has_value()) {
      CHECK(!source->cache_next_backward.has_value());
      source->cache_next_forward.reset();
      source->reader.Backward();
    }

    if (!source->cache_next_backward.has_value()) {
      base::Optional<std::string> log = source->reader.Backward();
      if (log.has_value())
        source->cache_next_backward =
            source->parser->Parse(std::move(log.value()));
      else
        source->cache_next_backward = base::nullopt;
    }
  }

  Multiplexer::LogSource* next_source = nullptr;
  for (auto&& source : sources_) {
    if (!source->cache_next_backward.has_value()) {
      continue;
    }

    if (next_source == nullptr || next_source->cache_next_backward->time() <=
                                      source->cache_next_backward->time()) {
      next_source = source.get();
    }
  }

  if (next_source == nullptr) {
    return base::nullopt;
  }

  MaybeLogEntry entry;
  entry.swap(next_source->cache_next_backward);
  return entry;
}

void Multiplexer::AddObserver(Observer* obs) {
  observers_.AddObserver(obs);
}

void Multiplexer::RemoveObserver(Observer* obs) {
  observers_.RemoveObserver(obs);
}

void Multiplexer::SetLinesFromLast(uint32_t pos) {
  for (auto& source : sources_) {
    source->cache_next_backward.reset();
    source->cache_next_forward.reset();
    source->reader.SetPositionLast();
  }

  for (int i = 0; i < pos; i++) {
    MaybeLogEntry s = Backward();
    if (!s.has_value())
      return;
  }
}

}  // namespace croslog
