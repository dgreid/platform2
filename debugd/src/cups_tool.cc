// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Tool to manipulate CUPS.
#include "debugd/src/cups_tool.h"

#include <signal.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <chromeos/dbus/debugd/dbus-constants.h>

#include "debugd/src/constants.h"
#include "debugd/src/helper_utils.h"
#include "debugd/src/process_with_output.h"
#include "debugd/src/sandboxed_process.h"

namespace debugd {

namespace {

constexpr char kLpadminCommand[] = "/usr/sbin/lpadmin";
constexpr char kLpadminSeccompPolicy[] =
    "/usr/share/policy/lpadmin-seccomp.policy";
constexpr char kTestPPDCommand[] = "/usr/bin/cupstestppd";
constexpr char kTestPPDSeccompPolicy[] =
    "/usr/share/policy/cupstestppd-seccomp.policy";

constexpr char kLpadminUser[] = "lpadmin";
constexpr char kLpadminGroup[] = "lpadmin";
constexpr char kLpGroup[] = "lp";

constexpr char kUriHelperBasename[] = "cups_uri_helper";
constexpr char kUriHelperSeccompPolicy[] =
    "/usr/share/policy/cups-uri-helper.policy";

// lpadmin returns 1 for all failures and 0 for success as deteremined by
// surveying systemv/lpadmin.c in the codebase.  So, any other error is
// an abnormal failure.
constexpr int kLpadminNormalFailure = 1;

// Returns the exit code for the executed process.
// By default disallow root mount namespace. Passing true as optional argument
// enables root mount namespace.
int RunAsUser(const std::string& user,
              const std::string& group,
              const std::string& command,
              const std::string& seccomp_policy,
              const ProcessWithOutput::ArgList& arg_list,
              const std::vector<uint8_t>* std_input = nullptr,
              bool root_mount_ns = false,
              bool inherit_usergroups = false) {
  ProcessWithOutput process;
  process.set_separate_stderr(true);
  process.SandboxAs(user, group);

  if (!seccomp_policy.empty())
    process.SetSeccompFilterPolicyFile(seccomp_policy);

  if (root_mount_ns)
    process.AllowAccessRootMountNamespace();

  if (inherit_usergroups)
    process.InheritUsergroups();

  if (!process.Init())
    return ProcessWithOutput::kRunError;

  process.AddArg(command);
  for (const std::string& arg : arg_list) {
    process.AddArg(arg);
  }

  // Prepares a buffer with standard input.
  std::vector<char> buf;
  if (std_input != nullptr) {
    buf.reserve(std_input->size());
    for (uint8_t byte : *std_input) {
      buf.push_back(static_cast<char>(byte));
    }
  }

  // Starts a process, writes data from the buffer to its standard input and
  // waits for the process to finish.
  int result = ProcessWithOutput::kRunError;
  process.RedirectUsingPipe(STDIN_FILENO, true);
  if (process.Start()) {
    int stdin_fd = process.GetPipe(STDIN_FILENO);
    // Kill the process if writing to or closing the pipe fails.
    if (!base::WriteFileDescriptor(stdin_fd, buf.data(), buf.size()) ||
        IGNORE_EINTR(close(stdin_fd)) < 0) {
      process.Kill(SIGKILL, 0);
    }
    result = process.Wait();
  }

  if (result != 0) {
    std::string error_msg;
    process.GetError(&error_msg);
    PLOG(ERROR) << "Child process failed" << error_msg;
  }

  return result;
}

// Runs cupstestppd on |ppd_content| returns the result code.  0 is the expected
// success code.
int TestPPD(const std::vector<uint8_t>& ppd_content) {
  return RunAsUser(kLpadminUser, kLpadminGroup, kTestPPDCommand,
                   kTestPPDSeccompPolicy, {"-"}, &(ppd_content),
                   true /* root_mount_ns */);
}

// Runs lpadmin with the provided |arg_list| and |std_input|.
int Lpadmin(const ProcessWithOutput::ArgList& arg_list,
            bool inherit_usergroups = false,
            const std::vector<uint8_t>* std_input = nullptr) {
  // Run in lp group so we can read and write /run/cups/cups.sock.
  return RunAsUser(kLpadminUser, kLpGroup, kLpadminCommand,
                   kLpadminSeccompPolicy, arg_list, std_input, false,
                   inherit_usergroups);
}

// Checks whether the scheme for the given |uri| is one of the required schemes
// for IPP Everywhere.
bool IppEverywhereURI(const std::string& uri) {
  static const char* const kValidSchemes[] = {"ipp://", "ipps://", "ippusb://"};
  for (const char* scheme : kValidSchemes) {
    if (base::StartsWith(uri, scheme, base::CompareCase::INSENSITIVE_ASCII))
      return true;
  }

  return false;
}

}  // namespace

// Invokes lpadmin with arguments to configure a new printer using '-m
// everywhere'.  Returns 0 for success, 1 for failure, 4 if configuration
// failed.
int32_t CupsTool::AddAutoConfiguredPrinter(const std::string& name,
                                           const std::string& uri) {
  if (!IppEverywhereURI(uri)) {
    LOG(WARNING) << "IPP, IPPS or IPPUSB required for IPP Everywhere: " << uri;
    return CupsResult::CUPS_FATAL;
  }

  if (!CupsTool::UriSeemsReasonable(uri)) {
    LOG(WARNING) << "Invalid URI: " << uri;
    return CupsResult::CUPS_BAD_URI;
  }

  int32_t result;
  if (base::StartsWith(uri, "ippusb://",
                       base::CompareCase::INSENSITIVE_ASCII)) {
    // In the case of printing with the ippusb scheme, we want to run lpadmin in
    // a minijail with the inherit usergroups option set.
    result = Lpadmin({"-v", uri, "-p", name, "-m", "everywhere", "-E"}, true);
  } else {
    result = Lpadmin({"-v", uri, "-p", name, "-m", "everywhere", "-E"});
  }

  if (result == EXIT_SUCCESS) {
    return CupsResult::CUPS_SUCCESS;
  }

  if (result == kLpadminNormalFailure) {
    return CupsResult::CUPS_AUTOCONF_FAILURE;
  }

  // Lpadmin may have crashed.  Something is very wrong.
  LOG(WARNING) << "Lpadmin failed: " << result;
  return CupsResult::CUPS_FATAL;
}

int32_t CupsTool::AddManuallyConfiguredPrinter(
    const std::string& name,
    const std::string& uri,
    const std::vector<uint8_t>& ppd_contents) {
  int result = TestPPD(ppd_contents);
  if (result != EXIT_SUCCESS) {
    LOG(ERROR) << "PPD failed validation";
    return CupsResult::CUPS_INVALID_PPD;
  }

  if (!CupsTool::UriSeemsReasonable(uri)) {
    LOG(WARNING) << "Invalid URI: " << uri;
    return CupsResult::CUPS_BAD_URI;
  }

  // lpadmin only returns 0 for success and 1 for failure.
  result =
      Lpadmin({"-v", uri, "-p", name, "-P", "-", "-E"}, false, &ppd_contents);
  if (result != EXIT_SUCCESS) {
    return CupsResult::CUPS_LPADMIN_FAILURE;
  }

  return CupsResult::CUPS_SUCCESS;
}

// Invokes lpadmin with -x to delete a printer.
bool CupsTool::RemovePrinter(const std::string& name) {
  return Lpadmin({"-x", name}) == EXIT_SUCCESS;
}

// Tests a URI's visual similarity with an HTTP URI.
// This function observes a subset of RFC 3986 but is _not_ meant to serve
// as a general-purpose URI validator (prefer Chromium's GURL).
bool CupsTool::UriSeemsReasonable(const std::string& uri) {
  ProcessWithOutput::ArgList args = {uri};
  std::string helper_path;

  if (!GetHelperPath(kUriHelperBasename, &helper_path)) {
    DCHECK(false) << "GetHelperPath() failed to return the CUPS URI helper!";
    return false;
  }

  int cups_uri_helper_exit_code =
      RunAsUser(SandboxedProcess::kDefaultUser, SandboxedProcess::kDefaultGroup,
                helper_path, kUriHelperSeccompPolicy, args);
  if (cups_uri_helper_exit_code == 0) {
    return true;
  }
  return false;
}

}  // namespace debugd
