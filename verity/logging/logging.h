// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
//
// A very simple logging library to fill the void when Chrome's libbase
// is not available.

#ifndef VERITY_LOGGING_LOGGING_H_
#define VERITY_LOGGING_LOGGING_H_

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>

namespace logging {

enum MessageType { TYPE_NORMAL, TYPE_DEBUG, TYPE_ERRNO, TYPE_NULL };
enum MessageLevel { LEVEL_INFO, LEVEL_WARNING, LEVEL_ERROR, LEVEL_FATAL };

static MessageLevel min_level = LEVEL_INFO;

class Message {
 public:
  Message(MessageLevel level,
          MessageType type,
          int errn,
          const char* file,
          unsigned long line)
      : type_(type), log_errno_(errn), file_(file), line_(line) {
#ifdef NDEBUG
    if (type == TYPE_DEBUG)
      return;
#endif
    if (level > LEVEL_FATAL)
      level = LEVEL_FATAL;
    level_ = level;

    if (type == TYPE_NULL || level < min_level)
      return;

    static const char* kLevels[] = {"INFO", "WARNING", "ERROR", "FATAL"};
    std::cerr << "[" << kLevels[level] << ":" << file << ":" << line << "] ";
  }

  // The Message object is expected to be destroyed at the end of the line.
  // So far this works reliably with the macros below, which is good enough
  // as a bandaid for when libbase is missing.
  virtual ~Message() {
    if (type_ == TYPE_NULL)
      return;
#ifdef NDEBUG
    if (type_ == TYPE_DEBUG)
      return;
#endif
    if (type_ == TYPE_ERRNO) {
      static const unsigned int kErrnoBufSize = 256;
      char errbuf[kErrnoBufSize];
      // This uses the GNU variant of strerror_r(3).
      std::cerr << ": " << strerror_r(log_errno_, errbuf, sizeof(errbuf));
    }

    std::cerr << std::endl;
    std::cerr.flush();
    if (level_ == LEVEL_FATAL)
      exit(1);
  }

  template <typename T>
  const Message& operator<<(const T& t) const {
#ifdef NDEBUG
    if (type() == TYPE_DEBUG)
      return *this;
#endif

    if (type() == TYPE_NULL || level() < min_level)
      return *this;

    std::cerr << t;
    return *this;
  }

  MessageLevel level() const { return level_; }
  MessageType type() const { return type_; }
  int log_errno() const { return log_errno_; }
  const char* file() const { return file_; }
  unsigned long line() const { return line_; }

  void set_level(MessageLevel l) { level_ = l; }

 private:
  MessageLevel level_;
  MessageType type_;
  int log_errno_;
  const char* file_;
  unsigned long line_;
};
}  // namespace logging

// Interface macros
#define LOG(_level)                                                            \
  logging::Message(logging::LEVEL_##_level, logging::TYPE_NORMAL, 0, __FILE__, \
                   __LINE__)
#define PLOG(_level)                                                    \
  logging::Message(logging::LEVEL_##_level, logging::TYPE_ERRNO, errno, \
                   __FILE__, __LINE__)
#define DLOG(_level)                                                          \
  logging::Message(logging::LEVEL_##_level, logging::TYPE_DEBUG, 0, __FILE__, \
                   __LINE__)
#define LOG_NULL                                                         \
  logging::Message(logging::LEVEL_INFO, logging::TYPE_NULL, 0, __FILE__, \
                   __LINE__)

#define LOG_IF(_level, cond) ((cond) ? LOG(_level) : LOG_NULL)
#define PLOG_IF(_level, cond) ((cond) ? PLOG(_level) : LOG_NULL)

#endif  // VERITY_LOGGING_LOGGING_H_
