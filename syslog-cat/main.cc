// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <base/command_line.h>
#include <base/strings/string_piece.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/process/process.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

namespace {
constexpr int kDefaultSeverityStdout = 6;
constexpr int kDefaultSeverityStderr = 4;
constexpr char kSyslogSocketPath[] = "/run/rsyslogd/stdout";

void ShowUsage() {
  fprintf(
      stderr,
      "Usage: syslog_cat [OPTION] -- target-command arguments...\n"
      "  options:\n"
      "    --identifier=IDENTIFIER     specify the identifier of log.\n"
      "    --severity-stdout=PRIORITY  specify the severity of log from\n"
      "                                stdout. PRIORITY is a number 0-8.\n"
      "    --severity-stderr=PRIORITY  specify the severity of log from\n"
      "                                stderr. PRIORITY is a number 0-8.\n");
}

base::ScopedFD PrepareSocket(const std::string& identifier,
                             int severity,
                             int pid) {
  DCHECK(!identifier.empty());
  DCHECK_GE(severity, 0);
  DCHECK_LE(severity, 7);

  // Open the unix socket to write logs.
  base::ScopedFD sock(HANDLE_EINTR(socket(AF_UNIX, SOCK_STREAM, 0)));
  if (!sock.is_valid()) {
    PLOG(ERROR) << "opening stream socket";
    return base::ScopedFD();
  }

  // Connect the syslog unix socket file.
  struct sockaddr_un server {};
  server.sun_family = AF_UNIX;
  std::strncpy(server.sun_path, kSyslogSocketPath, sizeof(kSyslogSocketPath));
  if (HANDLE_EINTR(connect(sock.get(), (struct sockaddr*)&server,
                           sizeof(struct sockaddr_un))) < 0) {
    PLOG(ERROR) << "connecting stream socket";
    return base::ScopedFD();
  }

  // Construct the header string to send.
  std::string header = base::StringPrintf("TAG=%s[%d]\nPRIORITY=%d\n\n",
                                          identifier.c_str(), pid, severity);

  // Send headers (tag and severity).
  if (!base::WriteFileDescriptor(sock.get(), header.c_str(), header.size())) {
    PLOG(ERROR) << "writing headers on stream socket";
    return base::ScopedFD();
  }

  return sock;
}

int SeverityFromString(base::StringPiece severity_str) {
  if (severity_str == "0" ||
      base::CompareCaseInsensitiveASCII(severity_str, "emerg") == 0) {
    return 0;
  }

  if (severity_str == "1" ||
      base::CompareCaseInsensitiveASCII(severity_str, "alert") == 0) {
    return 1;
  }

  if (severity_str == "2" ||
      base::CompareCaseInsensitiveASCII(severity_str, "critical") == 0 ||
      base::CompareCaseInsensitiveASCII(severity_str, "crit") == 0) {
    return 2;
  }

  if (severity_str == "3" ||
      base::CompareCaseInsensitiveASCII(severity_str, "err") == 0 ||
      base::CompareCaseInsensitiveASCII(severity_str, "error") == 0) {
    return 3;
  }

  if (severity_str == "4" ||
      base::CompareCaseInsensitiveASCII(severity_str, "warn") == 0 ||
      base::CompareCaseInsensitiveASCII(severity_str, "warning") == 0) {
    return 4;
  }

  if (severity_str == "5" ||
      base::CompareCaseInsensitiveASCII(severity_str, "notice") == 0) {
    return 5;
  }

  if (severity_str == "6" ||
      base::CompareCaseInsensitiveASCII(severity_str, "info") == 0) {
    return 6;
  }

  if (severity_str == "7" ||
      base::CompareCaseInsensitiveASCII(severity_str, "debug") == 0) {
    return 7;
  }

  return -1;
}

// Extract a severity from the command line argument.
// Return the default severity if the argument is not specified
// Return "-1" if an invalid value is specified.
int ExtractSeverityFromCommnadLine(const base::CommandLine* command_line,
                                   const char* switch_name,
                                   int default_severity) {
  if (!command_line->HasSwitch(switch_name))
    return default_severity;

  return SeverityFromString(command_line->GetSwitchValueASCII(switch_name));
}

bool CreateSocketAndBindToFD(const std::string& identifier,
                             int severity,
                             int pid,
                             int target_fd) {
  base::ScopedFD sock = PrepareSocket(identifier, severity, pid);
  if (!sock.is_valid()) {
    LOG(ERROR) << "Failed to open the rsyslog socket for stderr.";
    return false;
  }

  // Connect the socket to stderr.
  if (HANDLE_EINTR(dup2(sock.get(), target_fd)) == -1) {
    PLOG(ERROR) << "duping the stderr";
    return false;
  }

  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);

  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  const auto& sv = command_line->GetArgs();

  if (sv.size() == 0 || command_line->HasSwitch("help")) {
    ShowUsage();
    return 1;
  }

  // Prepare a identifier.
  constexpr char kIdentifierSwitchName[] = "identifier";
  std::string identifier;
  if (command_line->HasSwitch(kIdentifierSwitchName)) {
    identifier = command_line->GetSwitchValueASCII(kIdentifierSwitchName);
  } else {
    const std::string& target_command_str = sv[0].c_str();
    const base::FilePath& target_command_path =
        base::FilePath(target_command_str);
    identifier = target_command_path.BaseName().value();
  }
  if (identifier.empty()) {
    LOG(ERROR) << "Failed to extract a identifier string.";
    return 1;
  }

  // Prepare a severity for stdout.
  constexpr char kSeverityOutSwitchName[] = "severity-stdout";
  int severity_stdout = ExtractSeverityFromCommnadLine(
      command_line, kSeverityOutSwitchName, kDefaultSeverityStdout);
  if (severity_stdout < 0) {
    LOG(ERROR) << "Invalid severity value. It must be a number between "
               << "0 (EMERG) and 7 (DEBUG) or valid severity string.";
    return 1;
  }

  // Prepare a severity for stderr.
  constexpr char kSeverityErrSwitchName[] = "severity-stderr";
  int severity_stderr = ExtractSeverityFromCommnadLine(
      command_line, kSeverityErrSwitchName, kDefaultSeverityStderr);
  if (severity_stderr < 0) {
    LOG(ERROR) << "Invalid severity value. It must be a number between "
               << "0 (EMERG) and 7 (DEBUG) or valid severity string.";
    return 1;
  }

  // Prepare a pid.
  base::ProcessId pid = base::Process::Current().Pid();

  // Prepare a command line for the target process.
  int target_command_argc = sv.size();
  std::vector<const char*> target_command_argv(target_command_argc + 1);
  for (int i = 0; i < target_command_argc; i++)
    target_command_argv[i] = sv[i].c_str();
  target_command_argv[target_command_argc] = nullptr;

  // Open the unix socket to redirect logs from stdout (and maybe stderr).
  bool ret_stdout =
      CreateSocketAndBindToFD(identifier, severity_stdout, pid, STDOUT_FILENO);
  CHECK(ret_stdout) << "Failed to bind stdout.";

  // Open the unix socket to redirect logs from stderr.
  // We prepare a sock for stderr even if the severarities are same, in order to
  // prevent interleave of simultaneous lines.
  bool ret_stderr =
      CreateSocketAndBindToFD(identifier, severity_stderr, pid, STDERR_FILENO);
  CHECK(ret_stderr) << "Failed to bind stderr.";

  // Execute the target process.
  execvp(const_cast<char*>(sv[0].c_str()),
         const_cast<char**>(target_command_argv.data()));

  /////////////////////////////////////////////////////////////////////////////
  // The code below should not be executed if the execvp() above succeeds.

  PLOG(ERROR) << "execvp '" << sv[0] << "'";
  exit(1);
}
