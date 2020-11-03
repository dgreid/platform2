// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "foomatic_shell/verifier.h"

#include <set>

#include <base/logging.h>
#include <base/no_destructor.h>

namespace foomatic_shell {

namespace {

// A set of allowed environment variables that may be set for executed commands.
const std::set<std::string> AllowedVariables() {
  static const base::NoDestructor<std::set<std::string>> variables({"NOPDF"});
  return *variables;
}

bool HasPrefix(const std::string& str, const std::string& prefix) {
  if (prefix.size() > str.size())
    return false;
  return (str.compare(0, prefix.size(), prefix) == 0);
}

}  // namespace

bool Verifier::VerifyScript(Script* script, int recursion_level) {
  DCHECK(script != nullptr);
  if (recursion_level > 5) {
    message_ = "too many recursive subshell invocations";
    return false;
  }

  for (auto& pipeline : script->pipelines) {
    for (auto& segment : pipeline.segments) {
      // Save the position of the current segment (in case of an error).
      position_ = Position(segment);
      // Verify the segment.
      bool result = false;
      if (segment.command) {
        // It is a Command.
        result = VerifyCommand(segment.command.get());
      } else {
        // It is a Script.
        DCHECK(segment.script);
        result = VerifyScript(segment.script.get(), recursion_level + 1);
      }
      if (!result)
        return false;
    }
  }
  return true;
}

bool Verifier::VerifyCommand(Command* command) {
  DCHECK(command != nullptr);

  // Verify variables set for this command.
  for (auto& var : command->variables_with_values) {
    if (AllowedVariables().count(var.variable.value) == 0) {
      message_ = "variable " + var.variable.value + " is not allowed";
      return false;
    }
  }

  const std::string& cmd = command->application.value;

  // The "cat" command is allowed <=> it has no parameters or it has only a
  // single parameter "-".
  if (cmd == "cat") {
    if (command->parameters.empty())
      return true;
    if (command->parameters.size() == 1 &&
        Value(command->parameters.front()) == "-")
      return true;
    message_ = "cat: disallowed parameter";
    return false;
  }

  // The "cut" command is always allowed.
  if (cmd == "cut")
    return true;

  // The "date" command is allowed <=> it has no parameters with prefixes "-s"
  // or "--set".
  if (cmd == "date") {
    for (auto& parameter : command->parameters) {
      const std::string param = Value(parameter);
      if (HasPrefix(param, "-s") || HasPrefix(param, "--set")) {
        message_ = "date: disallowed parameter";
        return false;
      }
    }
    return true;
  }

  // The "echo" command is always allowed.
  if (cmd == "echo")
    return true;

  // The "gs" command is verified in separate method.
  if (cmd == "gs")
    return VerifyGs(command->parameters);

  // The "pdftops" command used by foomatic-rip is located at
  // /usr/libexec/cups/filter/pdftops, not /usr/bin/pdftops (a default one).
  // It takes 5 or 6 parameters.
  if (cmd == "pdftops")
    return true;

  // The "printf" command is always allowed.
  if (cmd == "printf")
    return true;

  // The "sed" command is allowed <=> it has no parameters with prefixes "-i"
  // or "--in-place". Moreover, the "--sandbox" parameter is added if not
  // already present.
  if (cmd == "sed") {
    bool sandbox = false;
    for (auto& parameter : command->parameters) {
      const std::string param = Value(parameter);
      if (param == "--sandbox") {
        sandbox = true;
        continue;
      }
      if (HasPrefix(param, "-i") || HasPrefix(param, "--in-place")) {
        message_ = "sed: disallowed parameter";
        return false;
      }
    }
    if (!sandbox) {
      Token token;
      token.type = Token::Type::kNativeString;
      token.value = "--sandbox";
      token.begin = token.end = command->application.end;
      const StringAtom string_atom = {{token}};
      command->parameters.push_back(string_atom);
    }
    return true;
  }

  // All other commands are disallowed.
  message_ = "disallowed command: " + command->application.value;
  return false;
}

// Parameters “-dSAFER” and “-sOutputFile=-” must be present.
// No other “-sOutputFile=” parameters are allowed.
// Parameters “-dNOSAFER” and “-dALLOWPSTRANSPARENCY” are disallowed.
bool Verifier::VerifyGs(const std::vector<StringAtom>& parameters) {
  bool safer = false;
  bool output_file = false;
  for (auto& parameter : parameters) {
    const std::string param = Value(parameter);
    if (param == "-dPARANOIDSAFER" || param == "-dSAFER") {
      safer = true;
      continue;
    }
    if (param == "-sOutputFile=-") {
      output_file = true;
      continue;
    }
    if (HasPrefix(param, "-sOutputFile=") || param == "-dNOSAFER" ||
        param == "-dALLOWPSTRANSPARENCY") {
      message_ = "gs: disallowed parameter";
      return false;
    }
  }
  if (!safer) {
    message_ = "gs: the parameter -dSAFER is missing";
    return false;
  }
  if (!output_file) {
    message_ = "gs: the parameter -sOutputFile=- is missing";
    return false;
  }
  return true;
}

}  // namespace foomatic_shell
