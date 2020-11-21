// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Tool to manipulate CUPS.
#include "debugd/src/cups_tool.h"

#include <pwd.h>
#include <signal.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/environment.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <chromeos/dbus/debugd/dbus-constants.h>

#include "debugd/src/constants.h"
#include "debugd/src/helper_utils.h"
#include "debugd/src/process_with_output.h"
#include "debugd/src/sandboxed_process.h"

namespace debugd {

namespace {

constexpr char kPdfContent[] = R"(%PDF-1.0
1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj 2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj 3 0 obj<</Type/Page/MediaBox[0 0 3 3]>>endobj
xref
0 4
0000000000 65535 f 
0000000009 00000 n 
0000000052 00000 n 
0000000101 00000 n 
trailer<</Size 4/Root 1 0 R>>
startxref
147
%EOF)";

constexpr char kGzipCommand[] = "/bin/gzip";
constexpr char kFoomaticCommand[] = "/usr/bin/foomatic-rip";
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

// Returns the exit code for the executed process.
// By default disallow root mount namespace. Passing true as optional argument
// enables root mount namespace.
int RunAsUser(const std::string& user,
              const std::string& group,
              const std::string& command,
              const std::string& seccomp_policy,
              const ProcessWithOutput::ArgList& arg_list,
              const std::vector<uint8_t>* std_input = nullptr,
              bool inherit_usergroups = false,
              std::string* out = nullptr) {
  ProcessWithOutput process;
  process.set_separate_stderr(true);
  process.SandboxAs(user, group);

  if (!seccomp_policy.empty())
    process.SetSeccompFilterPolicyFile(seccomp_policy);

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
    // Ignore SIGPIPE.
    const struct sigaction kSigIgn = {.sa_handler = SIG_IGN,
                                      .sa_flags = SA_RESTART};
    struct sigaction old_sa;
    if (sigaction(SIGPIPE, &kSigIgn, &old_sa)) {
      PLOG(ERROR) << "sigaction failed";
      return 1;
    }
    // Restore the old signal handler at the end of the scope.
    const base::ScopedClosureRunner kRestoreSignal(base::BindOnce(
        [](const struct sigaction& sa) {
          if (sigaction(SIGPIPE, &sa, nullptr)) {
            PLOG(ERROR) << "sigaction failed";
          }
        },
        old_sa));
    int stdin_fd = process.GetPipe(STDIN_FILENO);
    // Kill the process if writing to or closing the pipe fails.
    if (!base::WriteFileDescriptor(stdin_fd, buf.data(), buf.size()) ||
        IGNORE_EINTR(close(stdin_fd)) < 0) {
      process.Kill(SIGKILL, 0);
    }
    result = process.Wait();
    if (out && !process.GetOutput(out)) {
      PLOG(ERROR) << "Failed to get process output";
      return 1;
    }
  }

  if (result != 0) {
    std::string error_msg;
    process.GetError(&error_msg);
    PLOG(ERROR) << "Child process failed" << error_msg;
  }

  return result;
}

// Runs cupstestppd on |ppd_content| returns the result code.  0 is the expected
// success code. Verify the foomatic command is valid if the PPD uses the
// foomatic-rip filter.
int TestPPD(const std::vector<uint8_t>& ppd_data) {
  std::vector<uint8_t> ppd_content = ppd_data;
  if (ppd_content[0] == 0x1f && ppd_content[1] == 0x8b) {  // gzip header
    std::string out;
    int ret = RunAsUser(kLpadminUser, kLpadminGroup, kGzipCommand, "", {"-cfd"},
                        &ppd_content, false, &out);
    if (ret || out.empty()) {
      LOG(ERROR) << "gzip failed";
      return ret ? ret : 1;
    }
    ppd_content.assign(out.begin(), out.end());
  }
  int ret = RunAsUser(
      kLpadminUser, kLpadminGroup, kTestPPDCommand, kTestPPDSeccompPolicy,
      {"-W", "translations", "-W", "constraints", "-"}, &ppd_content);
  // Check if the foomatic-rip cups filter is present in the PPD file.
  constexpr uint8_t kFoomaticRip[] = "foomatic-rip\"";
  // Subtract 1 to exclude the null terminator.
  if (!ret && std::search(ppd_content.begin(), ppd_content.end(),
                          std::begin(kFoomaticRip),
                          std::end(kFoomaticRip) - 1) != ppd_content.end()) {
    base::ScopedTempDir tmp;
    if (!tmp.CreateUniqueTempDir()) {
      PLOG(ERROR) << "Could not create temporary directory";
      return 1;
    }
    base::FilePath ppd_file = tmp.GetPath().Append("ppd.ppd");
    if (!base::WriteFile(ppd_file, ppd_content)) {
      PLOG(ERROR) << "Could not write to file";
      return 1;
    }
    if (chown(tmp.GetPath().MaybeAsASCII().c_str(),
              getpwnam(kLpadminUser)->pw_uid, -1)) {
      PLOG(ERROR) << "Could not set directory ownership";
      return 1;
    }
    auto env = base::Environment::Create();
    env->SetVar("FOOMATIC_VERIFY_MODE", "true");
    env->SetVar("PATH", "/bin:/usr/bin:/usr/libexec/cups/filter");
    env->SetVar("PPD", ppd_file.MaybeAsASCII());
    const std::vector<uint8_t> kPdf(std::begin(kPdfContent),
                                    std::end(kPdfContent));
    ret = RunAsUser(kLpadminUser, kLpadminGroup, kFoomaticCommand, "",
                    {"1" /*jobID*/, "chronos" /*user*/, "Untitled" /*title*/,
                     "1" /*copies*/, "" /*options*/},
                    &kPdf);
  }
  return ret;
}

// Runs lpadmin with the provided |arg_list| and |std_input|.
int Lpadmin(const ProcessWithOutput::ArgList& arg_list,
            bool inherit_usergroups = false,
            const std::vector<uint8_t>* std_input = nullptr) {
  // Run in lp group so we can read and write /run/cups/cups.sock.
  return RunAsUser(kLpadminUser, kLpGroup, kLpadminCommand,
                   kLpadminSeccompPolicy, arg_list, std_input,
                   inherit_usergroups);
}

// Translates a return code from lpadmin to a CupsResult value.
CupsResult LpadminReturnCodeToCupsResult(int return_code, bool autoconf) {
  if (return_code != 0)
    LOG(WARNING) << "lpadmin failed: " << return_code;

  switch (return_code) {
    case 0:  // OK
      return CupsResult::CUPS_SUCCESS;
    case 1:  // UNKNOWN_ERROR
      return (autoconf ? CupsResult::CUPS_AUTOCONF_FAILURE
                       : CupsResult::CUPS_LPADMIN_FAILURE);
    case 2:  // WRONG_PARAMETERS
      return CupsResult::CUPS_FATAL;
    case 3:  // IO_ERROR
      return CupsResult::CUPS_IO_ERROR;
    case 4:  // MEMORY_ALLOC_ERROR
      return CupsResult::CUPS_MEMORY_ALLOC_ERROR;
    case 5:  // INVALID_PPD_FILE
      return (autoconf ? CupsResult::CUPS_FATAL : CupsResult::CUPS_INVALID_PPD);
    case 6:  // SERVER_UNREACHABLE
      return CupsResult::CUPS_FATAL;
    case 7:  // PRINTER_UNREACHABLE
      return CupsResult::CUPS_PRINTER_UNREACHABLE;
    case 8:  // PRINTER_WRONG_RESPONSE
      return CupsResult::CUPS_PRINTER_WRONG_RESPONSE;
    case 9:  // PRINTER_NOT_AUTOCONFIGURABLE
      return (autoconf ? CupsResult::CUPS_PRINTER_NOT_AUTOCONF
                       : CupsResult::CUPS_FATAL);
    default:
      // unexpected return code
      return CupsResult::CUPS_FATAL;
  }
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
// everywhere'.
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

  const bool is_ippusb =
      base::StartsWith(uri, "ippusb://", base::CompareCase::INSENSITIVE_ASCII);
  const int result =
      Lpadmin({"-v", uri, "-p", name, "-m", "everywhere", "-E"}, is_ippusb);
  return LpadminReturnCodeToCupsResult(result, /*autoconf=*/true);
}

int32_t CupsTool::AddManuallyConfiguredPrinter(
    const std::string& name,
    const std::string& uri,
    const std::vector<uint8_t>& ppd_contents) {
  if (TestPPD(ppd_contents) != EXIT_SUCCESS) {
    LOG(ERROR) << "PPD failed validation";
    return CupsResult::CUPS_INVALID_PPD;
  }

  if (!CupsTool::UriSeemsReasonable(uri)) {
    LOG(WARNING) << "Invalid URI: " << uri;
    return CupsResult::CUPS_BAD_URI;
  }

  const int result =
      Lpadmin({"-v", uri, "-p", name, "-P", "-", "-E"}, false, &ppd_contents);
  return LpadminReturnCodeToCupsResult(result, /*autoconf=*/false);
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
