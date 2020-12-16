// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_LOG_TOOL_H_
#define DEBUGD_SRC_LOG_TOOL_H_

#include <sys/types.h>
#include <map>
#include <memory>
#include <set>
#include <string>

#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <cryptohome/proto_bindings/rpc.pb.h>
#include <cryptohome-client/cryptohome/dbus-proxies.h>
#include <dbus/bus.h>

#include "debugd/src/sandboxed_process.h"

namespace debugd {

class LogTool {
 public:
  // The encoding for a particular log.
  enum class Encoding {
    // Tries to see if the log output is valid UTF-8. Outputs it as-is if it is,
    // or base64-encodes it otherwise.
    kAutodetect,

    // Replaces any characters that are not valid UTF-8 encoded with the
    // replacement character.
    kUtf8,

    // base64-encodes the output.
    kBase64,

    // Doesn't apply an encoding. Copies the data as is.
    kBinary,
  };

  class Log {
   public:
    enum LogType { kCommand, kFile, kGlob };

    static constexpr int64_t kDefaultMaxBytes = 512 * 1024;

    Log(LogType type,
        std::string name,
        std::string data,
        std::string user = SandboxedProcess::kDefaultUser,
        std::string group = SandboxedProcess::kDefaultGroup,
        int64_t max_bytes = kDefaultMaxBytes,
        LogTool::Encoding encoding = LogTool::Encoding::kAutodetect,
        bool access_root_mount_ns = false);

    virtual ~Log() = default;

    std::string GetName() const;
    virtual std::string GetLogData() const;

    std::string GetCommandLogData() const;
    std::string GetFileLogData() const;
    std::string GetGlobLogData() const;

    void DisableMinijailForTest();

   protected:
    Log() = default;  // For testing only.

   private:
    static uid_t UidForUser(const std::string& name);
    static gid_t GidForGroup(const std::string& group);
    static std::string GetFileData(const base::FilePath& path,
                                   int64_t max_bytes,
                                   const std::string& user,
                                   const std::string& group);

    LogType type_;
    std::string name_;
    // For kCommand logs, this is the command to run.
    // For kFile logs, this is the file path to read.
    std::string data_;
    std::string user_;
    std::string group_;
    int64_t max_bytes_;  // passed as arg to 'tail -c'
    LogTool::Encoding encoding_;
    bool access_root_mount_ns_;

    bool minijail_disabled_for_test_ = false;
  };

  explicit LogTool(scoped_refptr<dbus::Bus> bus);

  ~LogTool() = default;

  using LogMap = std::map<std::string, std::string>;

  std::string GetLog(const std::string& name);
  LogMap GetAllLogs();
  LogMap GetAllDebugLogs();
  void GetBigFeedbackLogs(const base::ScopedFD& fd,
                          const std::string& username);
  void BackupArcBugReport(const std::string& username);
  void DeleteArcBugReportBackup(const std::string& username);
  void GetJournalLog(const base::ScopedFD& fd);

  // Returns a representation of |value| with the specified encoding.
  static std::string EncodeString(std::string value, Encoding source_encoding);

 private:
  friend class LogToolTest;

  // For testing only.
  LogTool(scoped_refptr<dbus::Bus> bus,
          std::unique_ptr<org::chromium::CryptohomeInterfaceProxyInterface>
              cryptohome_proxy,
          const std::unique_ptr<LogTool::Log> arc_bug_report_log,
          const base::FilePath& daemon_store_base_dir);
  LogTool(const LogTool&) = delete;
  LogTool& operator=(const LogTool&) = delete;

  void CreateConnectivityReport(bool wait_for_results);

  // Returns the output of arc-bugreport program in ARC.
  // Returns cached output if it is available for this user.
  std::string GetArcBugReport(const std::string& username, bool* is_backup);
  bool IsUserHashValid(const std::string& userhash);
  base::FilePath GetArcBugReportBackupFilePath(const std::string& userhash);

  scoped_refptr<dbus::Bus> bus_;
  std::unique_ptr<org::chromium::CryptohomeInterfaceProxyInterface>
      cryptohome_proxy_;

  std::unique_ptr<LogTool::Log> arc_bug_report_log_;

  base::FilePath daemon_store_base_dir_;
  // Set containing userhash of all users for which
  // ARC bug report has been backed up.
  std::set<std::string> arc_bug_report_backups_;
};

}  // namespace debugd

#endif  // DEBUGD_SRC_LOG_TOOL_H_
