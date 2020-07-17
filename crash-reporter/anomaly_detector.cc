// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/anomaly_detector.h"

#include <utility>

#include <anomaly_detector/proto_bindings/anomaly_detector.pb.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>
#include <re2/re2.h>

namespace {

// This hashing algorithm dates back to before this was migrated from C to C++.
// We're stuck with it now because we would like the hashes to remain the same
// over time for a given crash as the hashes are used in the crash signatures.
uint32_t StringHash(const std::string& input) {
  uint32_t hash = 0;
  for (auto& c : input) {
    hash = (hash << 5) + hash + c;
  }
  return hash;
}

std::string OnlyAsciiAlpha(std::string s) {
  s.erase(std::remove_if(s.begin(), s.end(),
                         [](char c) -> bool { return !base::IsAsciiAlpha(c); }),
          s.end());
  return s;
}

}  // namespace

namespace anomaly {

Parser::~Parser() {}

// We expect only a handful of different anomalies per boot session, so the
// probability of a collision is very low, and statistically it won't matter
// (unless anomalies with the same hash also happens in tandem, which is even
// rarer).
bool Parser::WasAlreadySeen(uint32_t hash) {
  size_t bit_index = hash % HASH_BITMAP_SIZE;
  bool return_val = hash_bitmap_[bit_index];
  hash_bitmap_.set(bit_index, true);
  return return_val;
}

void Parser::PeriodicUpdate() {}

constexpr LazyRE2 service_failure = {
    R"((\S+) \S+ process \(\d+\) terminated with status (\d+))"};

MaybeCrashReport ServiceParser::ParseLogEntry(const std::string& line) {
  std::string service_name;
  std::string exit_status;
  if (!RE2::FullMatch(line, *service_failure, &service_name, &exit_status))
    return base::nullopt;

  uint32_t hash = StringHash(service_name.c_str());

  if (WasAlreadySeen(hash))
    return base::nullopt;

  std::string text = base::StringPrintf(
      "%08x-exit%s-%s\n", hash, exit_status.c_str(), service_name.c_str());
  std::string flag;
  if (base::StartsWith(service_name, "arc-", base::CompareCase::SENSITIVE))
    flag = "--arc_service_failure=" + service_name;
  else
    flag = "--service_failure=" + service_name;
  return {{std::move(text), std::move(flag)}};
}

std::string GetField(const std::string& line, std::string pattern) {
  std::string field_value;
  RE2::PartialMatch(line, pattern, &field_value);
  // This will return the empty string if there wasn't a match.
  return field_value;
}

constexpr LazyRE2 granted = {"avc:[ ]*granted"};

MaybeCrashReport SELinuxParser::ParseLogEntry(const std::string& line) {
  // Ignore permissive "errors". These are extremely common and don't have any
  // real impact. The noise from them would crowd out other crashes that have
  // a more significant impact.
  if (line.find("permissive=1") != std::string::npos) {
    return base::nullopt;
  }

  std::string only_alpha = OnlyAsciiAlpha(line);
  uint32_t hash = StringHash(only_alpha.c_str());
  if (WasAlreadySeen(hash))
    return base::nullopt;

  std::string signature;

  // This case is strange: the '-' is only added if 'granted' was present.
  if (RE2::PartialMatch(line, *granted))
    signature += "granted-";

  std::string scontext = GetField(line, R"(scontext=(\S*))");
  std::string tcontext = GetField(line, R"(tcontext=(\S*))");
  std::string permission = GetField(line, R"(\{ (\S*) \})");
  std::string comm = GetField(line, R"'(comm="([^"]*)")'");
  std::string name = GetField(line, R"'(name="([^"]*)")'");

  signature += base::JoinString({scontext, tcontext, permission,
                                 OnlyAsciiAlpha(comm), OnlyAsciiAlpha(name)},
                                "-");
  std::string text =
      base::StringPrintf("%08x-selinux-%s\n", hash, signature.c_str());

  if (!comm.empty())
    text += "comm\x01" + comm + "\x02";
  if (!name.empty())
    text += "name\x01" + name + "\x02";
  if (!scontext.empty())
    text += "scontext\x01" + scontext + "\x02";
  if (!tcontext.empty())
    text += "tcontext\x01" + tcontext + "\x02";
  text += "\n";
  text += line;

  return {{std::move(text), "--selinux_violation"}};
}

std::string DetermineFlag(const std::string& info) {
  if (info.find("drivers/net/wireless") != std::string::npos)
    return "--kernel_wifi_warning";
  if (info.find("drivers/idle") != std::string::npos)
    return "--kernel_suspend_warning";
  return "--kernel_warning";
}

constexpr char cut_here[] = "------------[ cut here";
constexpr char end_trace[] = "---[ end trace";
constexpr char crash_report_rlimit[] =
    "(crash_reporter) has RLIMIT_CORE set to";

// The CPU and PID information got added in the 3.11 kernel development cycle
// per commit dcb6b45254e2281b6f99ea7f2d51343954aa3ba8. That part is marked
// optional to make sure the old format still gets accepted. Once we no longer
// care about kernel version 3.10 and earlier, we can update the code to require
// CPU and PID to be present unconditionally.
constexpr LazyRE2 header = {
    R"(^\[\s*\S+\] WARNING:(?: CPU: \d+ PID: \d+)? at (.+))"};

MaybeCrashReport KernelParser::ParseLogEntry(const std::string& line) {
  if (last_line_ == LineType::None) {
    if (line.find(cut_here) != std::string::npos)
      last_line_ = LineType::Start;
  } else if (last_line_ == LineType::Start || last_line_ == LineType::Header) {
    std::string info;
    if (RE2::FullMatch(line, *header, &info)) {
      // The info string looks like: "file:line func+offset/offset() [mod]".
      // The [mod] suffix is only present if the address is located within a
      // kernel module.
      uint32_t hash = StringHash(info.c_str());
      if (WasAlreadySeen(hash)) {
        last_line_ = LineType::None;
        return base::nullopt;
      }
      flag_ = DetermineFlag(info);

      size_t spacep = info.find(" ");
      bool found = spacep != std::string::npos;
      auto function = found ? info.substr(spacep + 1) : "unknown-function";

      text_ += base::StringPrintf("%08x-%s\n", hash, function.c_str());
      text_ += base::StringPrintf("%s\n", info.c_str());
      last_line_ = LineType::Body;
    } else if (last_line_ == LineType::Start) {
      // Allow for a single header line between the "cut here" and the "WARNING"
      last_line_ = LineType::Header;
      text_ += line + "\n";
    } else {
      last_line_ = LineType::None;
    }
  } else if (last_line_ == LineType::Body) {
    if (line.find(end_trace) != std::string::npos) {
      last_line_ = LineType::None;
      std::string text_tmp;
      text_tmp.swap(text_);
      return {{std::move(text_tmp), std::move(flag_)}};
    }
    text_ += line + "\n";
  }

  if (line.find(crash_report_rlimit) != std::string::npos) {
    LOG(INFO) << "crash_reporter crashed!";
    // Rate limit reporting crash_reporter failures to prevent crash loops.
    if (crash_reporter_last_crashed_.is_null() ||
        (base::TimeTicks::Now() - crash_reporter_last_crashed_) >
            base::TimeDelta::FromHours(1)) {
      crash_reporter_last_crashed_ = base::TimeTicks::Now();
      return {{"", "--crash_reporter_crashed"}};
    }
  }

  return base::nullopt;
}

constexpr char begin_suspend_error_stats[] =
    "Error writing to /sys/power/state: ";
constexpr char end_suspend_error_stats[] =
    "--- end /sys/kernel/debug/suspend_stats ---";
constexpr LazyRE2 last_failed_dev = {R"(\s*last_failed_dev: (.+))"};
constexpr LazyRE2 last_failed_errno = {R"(\s*last_failed_errno: (.+))"};
constexpr LazyRE2 last_failed_step = {R"(\s*last_failed_step: (.+))"};

MaybeCrashReport SuspendParser::ParseLogEntry(const std::string& line) {
  if (last_line_ == LineType::None &&
      line.find(begin_suspend_error_stats) == 0) {
    last_line_ = LineType::Start;
    dev_str_ = "none";
    errno_str_ = "unknown";
    step_str_ = "unknown";
    return base::nullopt;
  }

  if (last_line_ != LineType::Start && last_line_ != LineType::Body) {
    return base::nullopt;
  }

  if (line.find(end_suspend_error_stats) != 0) {
    std::string info;
    if (RE2::FullMatch(line, *last_failed_dev, &info)) {
      dev_str_ = info;
    } else if (RE2::FullMatch(line, *last_failed_errno, &info)) {
      errno_str_ = info;
    } else if (RE2::FullMatch(line, *last_failed_step, &info)) {
      step_str_ = info;
    }

    last_line_ = LineType::Body;
    return base::nullopt;
  }

  uint32_t hash = StringHash((dev_str_ + errno_str_ + step_str_).c_str());
  std::string text = base::StringPrintf(
      "%08x-suspend failure: device: %s step: %s errno: %s\n", hash,
      dev_str_.c_str(), step_str_.c_str(), errno_str_.c_str());
  return {{std::move(text), "--suspend_failure"}};
}

TerminaParser::TerminaParser(scoped_refptr<dbus::Bus> dbus) : dbus_(dbus) {}

constexpr LazyRE2 btrfs_extent_corruption = {
    R"(BTRFS warning \(device .*\): csum failed root [[:digit:]]+ )"
    R"(ino [[:digit:]]+ off [[:digit:]]+ csum 0x[[:xdigit:]]+ expected )"
    R"(csum 0x[[:xdigit:]]+ mirror [[:digit:]]+)"};
constexpr LazyRE2 btrfs_tree_node_corruption = {
    R"(BTRFS warning \(device .*\): .* checksum verify failed on )"
    R"([[:digit:]]+ wanted [[:xdigit:]]+ found [[:xdigit:]]+ level )"
    R"([[:digit:]]+)"};
constexpr LazyRE2 vsock_cid = {R"(VM\(([[:digit:]]+)\))"};

MaybeCrashReport TerminaParser::ParseLogEntry(const std::string& tag,
                                              const std::string& line) {
  if (!RE2::PartialMatch(line, *btrfs_extent_corruption) &&
      !RE2::PartialMatch(line, *btrfs_tree_node_corruption)) {
    return base::nullopt;
  }

  int cid;
  anomaly_detector::GuestFileCorruptionSignal message;
  if (!RE2::PartialMatch(tag, *vsock_cid, &cid)) {
    LOG(ERROR) << "Was unable to parse vsock cid out of tag";
  } else {
    message.set_vsock_cid(cid);
  }

  dbus::Signal signal(anomaly_detector::kAnomalyEventServiceInterface,
                      anomaly_detector::kAnomalyGuestFileCorruptionSignalName);

  dbus::MessageWriter writer(&signal);
  writer.AppendProtoAsArrayOfBytes(message);

  dbus::ExportedObject* exported_object = dbus_->GetExportedObject(
      dbus::ObjectPath(anomaly_detector::kAnomalyEventServicePath));
  exported_object->SendSignal(&signal);

  // Don't send a crash report here, because the gap between when the
  // corruption occurs and when we detect it can be arbitrarily large.
  return base::nullopt;
}

}  // namespace anomaly
