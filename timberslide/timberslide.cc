// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <sstream>
#include <string>

#include <sysexits.h>
#include <sys/types.h>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/files/file_util.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <brillo/daemons/daemon.h>
#include "timberslide/log_listener_factory.h"
#include "timberslide/timberslide.h"

namespace timberslide {
namespace {

const char kCurrentLogExt[] = ".log";
const char kPreviousLogExt[] = ".previous";
const int kMaxCurrentLogSize = 10 * 1024 * 1024;

// String proxy class for dealing with lines.
class LineExtractor {
 public:
  // Extract one line from beginning of string stream.
  friend std::istream& operator>>(std::istream& is, LineExtractor& l) {
    std::getline(is, l.str_);
    return is;
  }

  // Transparently convert to std::string.
  operator std::string() const { return str_; }

 private:
  std::string str_;
};

//
// Has a member function which adds the host timestamp
// to the beginning of each line passed to it.
//
class StringTransformer {
 public:
  explicit StringTransformer(int64_t ec_uptime_ms, const base::Time& now)
      : ec_current_uptime_ms_(ec_uptime_ms), timestamp_(now) {}

  // Matching lines look like: [1234.5678 EC message goes here] .
  std::string add_host_ts(const std::string& s) {
    const base::TimeDelta ec_sync(
        base::TimeDelta::FromMilliseconds(ec_current_uptime_ms_));

    std::string::size_type bracket = s.find('[');
    if (bracket == std::string::npos)
      return s;

    std::string::size_type space = s.find(' ', bracket + 1);
    if (space == std::string::npos)
      return s;

    double ec_ts;
    const std::string potential_ts(s.substr(bracket + 1, space - bracket - 1));

    if (!base::StringToDouble(potential_ts, &ec_ts)) {
      LOG(WARNING) << "Unable to convert " << potential_ts << " to a double";
      return s;
    }

    // Calculate delta from EC's uptime.
    const base::TimeDelta logline_tm(base::TimeDelta::FromSecondsD(ec_ts));
    const base::TimeDelta logline_delta(ec_sync - logline_tm);
    const base::Time logline_host_tm = timestamp_ - logline_delta;

    return FormatTime(logline_host_tm).append(" ").append(s);
  }

 private:
  std::string FormatTime(const base::Time& time) {
    base::Time::Exploded e;

    // This format matches the format in libchrome/base/logging.cc
    time.UTCExplode(&e);
    return base::StringPrintf("%02d%02d/%02d%02d%02d.%06d", e.month,
                              e.day_of_month, e.hour, e.minute, e.second,
                              e.millisecond * 1000);
  }

  int64_t ec_current_uptime_ms_;
  base::Time timestamp_;
};

}  // namespace

TimberSlide::TimberSlide(const std::string& ec_type,
                         base::File device_file,
                         base::File uptime_file,
                         const base::FilePath& log_dir)
    : device_file_(std::move(device_file)),
      total_size_(0),
      uptime_file_(std::move(uptime_file)),
      uptime_file_valid_(uptime_file_.IsValid()) {
  current_log_ = log_dir.Append(ec_type + kCurrentLogExt);
  previous_log_ = log_dir.Append(ec_type + kPreviousLogExt);
  log_listener_ = LogListenerFactory::Create(ec_type);
}

TimberSlide::TimberSlide(std::unique_ptr<LogListener> log_listener)
    : log_listener_(std::move(log_listener)) {}

int TimberSlide::OnInit() {
  LOG(INFO) << "Starting timberslide daemon";
  int ret = brillo::Daemon::OnInit();
  if (ret != EX_OK)
    return ret;

  if (uptime_file_valid_)
    LOG(INFO) << "EC uptime file is valid";
  else
    LOG(WARNING) << "EC uptime file is not valid; ignoring";

  RotateLogs(previous_log_, current_log_);

  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      device_file_.GetPlatformFile(),
      base::BindRepeating(&TimberSlide::OnEventReadable,
                          base::Unretained(this)));

  return watcher_ ? EX_OK : EX_OSERR;
}

//
// From kernel's Documentation/filesystems/sysfs.txt: If userspace seeks back
// to zero or does a pread(2) with an offset of '0' the show() method will
// be called again, rearmed, to fill the buffer.
//
// Therefore, the 'uptime' file will be kept open and just seeked back to
// 0 when new uptime is needed.
//
bool TimberSlide::GetEcUptime(int64_t* ec_uptime_ms) {
  char uptime_buf[64] = {0};

  if (!uptime_file_valid_ || uptime_file_.Seek(base::File::FROM_BEGIN, 0) != 0)
    return false;

  // Read single line from file and parse as a number.
  int count = uptime_file_.ReadAtCurrentPos(uptime_buf, sizeof(uptime_buf) - 1);

  if (count <= 0)
    return false;

  uptime_buf[count] = '\0';
  base::StringToInt64(uptime_buf, ec_uptime_ms);

  // If the 'uptime' file contains zero, that means the kernel patch is
  // available, but the EC doesn't support EC_CMD_GET_UPTIME_INFO.  In
  // that case, this returns false so that incorrect times aren't reported
  // in the EC log file.
  return (*ec_uptime_ms > 0);
}

std::string TimberSlide::ProcessLogBuffer(const char* buffer,
                                          const base::Time& now) {
  int64_t ec_current_uptime_ms = 0;
  std::istringstream iss(buffer);

  bool have_ec_uptime = GetEcUptime(&ec_current_uptime_ms);

  auto fn_xfrm = [this, ec_current_uptime_ms, have_ec_uptime,
                  now](const std::string& line) {
    if (log_listener_) {
      log_listener_->OnLogLine(line);
    }
    if (!have_ec_uptime) {
      return line;
    }
    StringTransformer xfrm(ec_current_uptime_ms, now);
    return xfrm.add_host_ts(line);
  };

  // Iterate over each line and prepend the corresponding host timestamp if we
  // have it
  std::ostringstream oss;
  std::transform(std::istream_iterator<LineExtractor>(iss),
                 std::istream_iterator<LineExtractor>(),
                 std::ostream_iterator<std::string>(oss, "\n"), fn_xfrm);

  return oss.str();
}

void TimberSlide::OnEventReadable() {
  char buffer[4096];
  int ret;

  memset(buffer, 0, sizeof(buffer));

  ret = TEMP_FAILURE_RETRY(
      device_file_.ReadAtCurrentPosNoBestEffort(buffer, sizeof(buffer)));
  if (ret == 0)
    return;

  if (ret < 0) {
    PLOG(ERROR) << "Read error";
    Quit();
    return;
  }

  std::string str = ProcessLogBuffer(buffer, base::Time::Now());
  ret = str.size();

  if (!base::AppendToFile(current_log_, str.c_str(), ret)) {
    PLOG(ERROR) << "Could not append to log file";
    Quit();
    return;
  }

  total_size_ += ret;
  if (total_size_ >= kMaxCurrentLogSize) {
    RotateLogs(previous_log_, current_log_);
    total_size_ = 0;
  }
}

void TimberSlide::RotateLogs(const base::FilePath& previous_log,
                             const base::FilePath& current_log) {
  CHECK(base::DeleteFile(previous_log, /* recursive = */ false));

  if (base::PathExists(current_log))
    CHECK(base::Move(current_log, previous_log));

  base::WriteFile(current_log, "", 0);
}

}  // namespace timberslide
