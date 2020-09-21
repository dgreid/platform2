// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "foomatic_shell/shell.h"

#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

#include "foomatic_shell/parser.h"
#include "foomatic_shell/process_launcher.h"
#include "foomatic_shell/scanner.h"
#include "foomatic_shell/verifier.h"

namespace foomatic_shell {

namespace {

// Prints to the stderr an error message. |source| is a source of the script
// that failed. |position| points to the part of |source| where the error
// occurred. |msg| is an error message. Neither dot nor end-of-line is expected
// at the end of |msg|.
void PrintErrorMessage(const std::string& source,
                       std::string::const_iterator position,
                       const std::string& msg) {
  const std::string out = CreateErrorLog(source, position, msg);
  fprintf(stderr, "%s\n", out.c_str());
}

// Sets the position in the given file descriptor |fd| to the beginning and
// reads everything from it. Read content is saved to |out|. If the function
// succeeds the file descriptor is closed and true is returned. In case of an
// error, the content of |out| is replaced by an error message and the function
// returns false. |out| must not be nullptr; its initial content is always
// deleted at the beginning. The function also fails if the length of the
// content is larger than |kMaxSourceSize|.
bool ReadAndCloseFd(int fd, std::string* out) {
  DCHECK(out != nullptr);
  out->clear();
  if (lseek(fd, 0, SEEK_SET) < 0) {
    *out = "lseek failed: ";
    *out += strerror(errno);
    return false;
  }
  char buf[1024];
  while (true) {
    const ssize_t length = read(fd, buf, sizeof(buf));
    if (length < 0) {
      // Error occurred.
      *out = "read failed: ";
      *out += strerror(errno);
      return false;
    } else if (length == 0) {
      // End of stream was reached.
      break;
    }
    // Success. Add read data to the output string.
    out->append(buf, length);
    if (out->size() > kMaxSourceSize) {
      *out = "Generated script is too long";
      return false;
    }
  }
  close(fd);
  return true;
}

// Parse and execute a shell script in |source|. This routine works in the
// similar way as ExecuteShellScript(...) from shell.h. The only differences
// are that the output is saved to the given string |output| instead of a file
// descriptor and that the return value is bool instead of int. In case of an
// error, the function returns false and |output| is set to an error message.
bool ExecuteEmbeddedShellScript(const std::string& source,
                                const bool verbose_mode,
                                const int recursion_level,
                                std::string* output) {
  DCHECK(output != nullptr);

  // This limits the number of recursive `...` (backticks).
  if (recursion_level > 2) {
    *output = "Too many recursive executions of `...` operator";
    return false;
  }

  // Generate temporary file descriptor storing data in memory. The name is
  // set to "foomatic_shell_level_" + |recursion_level|.
  const std::string temp_name =
      "foomatic_shell_level_" + base::NumberToString(recursion_level);
  int temp_fd = memfd_create(temp_name.c_str(), 0);
  if (temp_fd == -1) {
    *output = std::string("memfd_create failed: ") + strerror(errno);
    return false;
  }

  // Execute the script.
  if (ExecuteShellScript(source, temp_fd, verbose_mode, recursion_level + 1)) {
    *output = "Error when executing `...` operator";
    return false;
  }

  // Read the generated output to |out|.
  if (!ReadAndCloseFd(temp_fd, output))
    return false;

  // The trailing end-of-line character is skipped - shell is suppose to
  // work this way.
  if (!output->empty() && output->back() == '\n')
    output->pop_back();

  // Success!
  return true;
}

}  // namespace

int ExecuteShellScript(const std::string& source,
                       const int output_fd,
                       const bool verbose_mode,
                       const int recursion_level) {
  DCHECK_NE(output_fd, 0);
  DCHECK_NE(output_fd, 2);

  if (verbose_mode)
    fprintf(stderr, "EXECUTE SCRIPT: %s\n", source.c_str());

  // Scan the source (the first phase of parsing).
  Scanner scanner(source);
  std::vector<Token> tokens;
  if (!scanner.ParseWholeInput(&tokens)) {
    PrintErrorMessage(source, scanner.GetPosition(), scanner.GetMessage());
    return kShellError;
  }

  // Execute scripts in `...` (backticks) and replace them with generated
  // output.
  for (auto& token : tokens) {
    if (token.type != Token::Type::kExecutedString)
      continue;

    // Execute the script inside `...` (backticks) operator.
    std::string out;
    if (!ExecuteEmbeddedShellScript(token.value, verbose_mode, recursion_level,
                                    &out)) {
      PrintErrorMessage(token.value, token.begin, out);
      return kShellError;
    }

    // Replace the token value (content of `...`) with the generated output.
    token.value = out;
  }

  // Parse the list of tokens (the second phase of parsing).
  Parser parser(tokens);
  Script parsed_script;
  if (!parser.ParseWholeInput(&parsed_script)) {
    PrintErrorMessage(source, parser.GetPosition(), parser.GetMessage());
    return kShellError;
  }

  // Verify all commands in the parsed script.
  Verifier verifier;
  if (!verifier.VerifyScript(&parsed_script)) {
    PrintErrorMessage(source, verifier.GetPosition(), verifier.GetMessage());
    return kShellError;
  }

  // Execute the parsed script and store returned code in |exit_code|.
  ProcessLauncher launcher(source, verbose_mode);
  const int exit_code = launcher.RunScript(parsed_script, 0, output_fd);

  // Log status and exit!
  if (verbose_mode) {
    if (exit_code == 0) {
      fprintf(stderr, "SCRIPT COMPLETED SUCCESSFULLY\n");
    } else {
      fprintf(stderr, "SCRIPT FAILED WITH EXIT CODE %d\n", exit_code);
    }
  }
  return exit_code;
}

}  // namespace foomatic_shell
