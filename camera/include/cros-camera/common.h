/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_INCLUDE_CROS_CAMERA_COMMON_H_
#define CAMERA_INCLUDE_CROS_CAMERA_COMMON_H_

#include <string>

#include <base/logging.h>
#include <base/threading/thread.h>

#define LOGF(level)                                              \
  LOG(level) << "(" << base::PlatformThread::CurrentId() << ") " \
             << __FUNCTION__ << "(): "
#define LOGFID(level, id) LOG(level) << __FUNCTION__ << "(): id: " << id << ": "
#define LOGF_IF(level, res) LOG_IF(level, res) << __FUNCTION__ << "(): "

#define PLOGF(level) PLOG(level) << __FUNCTION__ << "(): "
#define PLOGFID(level, id) \
  PLOG(level) << __FUNCTION__ << "(): id: " << id << ": "
#define PLOGF_IF(level, res) PLOG_IF(level, res) << __FUNCTION__ << "(): "

#define VLOGF(level)                                              \
  VLOG(level) << "(" << base::PlatformThread::CurrentId() << ") " \
              << __FUNCTION__ << "(): "
#define VLOGFID(level, id) \
  VLOG(level) << __FUNCTION__ << "(): id: " << id << ": "

#define VLOGF_ENTER() VLOGF(1) << "enter"
#define VLOGF_EXIT() VLOGF(1) << "exit"

inline std::string FormatToString(int32_t format) {
  return std::string(reinterpret_cast<char*>(&format), 4);
}

#endif  // CAMERA_INCLUDE_CROS_CAMERA_COMMON_H_
