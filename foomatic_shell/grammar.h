// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FOOMATIC_SHELL_GRAMMAR_H_
#define FOOMATIC_SHELL_GRAMMAR_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

// This is a definition of the grammar.
//
// First, a list of all symbols corresponding to a single byte:
//
//  c' - character ' (single quote)
//  c" - character " (double quotes)
//  c` - character ` (grave accent)
//  c\ - character \ (backslash)
//  c; - character ; (semicolon)
//  cn - character \n (end-of-line)
//  c| - character | (pipe)
//  c( - character ( (open parenthesis)
//  c) - character ) (close parenthesis)
//  c= - character = (equals)
//  cs - character 0x20 (space)
//  ct - character \t (tabulator)
//
//  ByteAny - any byte
//  ByteCommon - any byte different than c', c", c` and c\ .
//  ByteNative - any alphanumeric character (0-9,A-Z,a-z) or . (dot) or
//               / (slash) or _ (underscore) or - (minus) or + (plus) or
//               @ (at) or % (percent)
//
//
// The rules below are using the following notation:
//  - A | B - means A or B
//  - *( A ) - means "zero or more" A elements (Kleene operator)
//  - +( A ) - means "one or more" A elements (Kleene plus operator)
// Example:
//   +( A | B C | D ) matches any of the following:
//       A B C D
//       A A A D D B C
//       B C D A D B C
//   but does not match:
//       B B
//       B A C
//
//
// These are the tokens extracted by the scanner (see scanner.h):
//
//  LiteralString = c' *( ByteCommon | c" | c` | c\ ) c'
//  ExecutedString = c` *( ByteCommon | c' | c" | c\ ByteAny ) c`
//  InterpretedString = c" *( ByteCommon | c' | c\ | c\ c" | c\ c` | c\ c\ |
//                            | ExecutedString ) c"
//  NativeString = +( ByteNative | c\ ByteAny )
//  Space = +( cs | ct )
//
//
// These are the nodes of the parsing tree built by the parser (see parser.h).
//
//  StringAtom = +( LiteralString | ExecutedString | InterpretedString
//              | NativeString | c= )
//
//  Command = *( Variable c= StringAtom Space ) Application *( Space Parameter )
//  Variable = NativeString
//  Application = NativeString
//  Parameter = StringAtom
//
//  Pipeline = PipeSegment OptSpace *( c| OptSpace PipeSegment OptSpace )
//  PipeSegment = c( Script c) | Command
//  OptSpace = Space | Empty
//
//  Script = OptSpace *( SepP OptSpace ) Pipeline
//           *( +( SepP OptSpace ) Pipeline ) *( SepP OptSpace )
//  Script = OptSpace *( SepP OptSpace )
//  SepP = c; | cn
//
//
// All conflicts are solved by choosing the largest possible match.

namespace foomatic_shell {

// This represents a single token extracted by the scanner. All bytes from the
// input that are not a part of LiteralString, ExecutedString, NativeString,
// InterpretedString or Space are represented as token of type kByte.
struct Token {
  enum Type {
    kLiteralString,
    kExecutedString,
    kInterpretedString,
    kNativeString,
    kSpace,
    kByte,
    kEOF
  } type;
  // For |type|=k*String, the range below points directly to the string
  // content (without ', " or `).
  // For |type|=kSpace, the range corresponds to the longest possible
  // sequence of spaces and tabulators.
  // For |type|=kByte, the range points to exactly one character.
  // For |type|=kEOF, the range points to the end iterator.
  std::string::const_iterator begin;
  std::string::const_iterator end;
  std::string value;
};

// Represents StringAtom node.
struct StringAtom {
  std::vector<Token> components;
};

struct VariableAssignment {
  Token variable;
  StringAtom new_value;
};

// Represents Command node.
struct Command {
  std::vector<VariableAssignment> variables_with_values;
  Token application;
  std::vector<StringAtom> parameters;
};

struct Script;

// Represents PipeSegment node. Only one of the fields is set.
struct PipeSegment {
  std::unique_ptr<Command> command;
  std::unique_ptr<Script> script;
};

// Represents Pipeline node.
struct Pipeline {
  std::vector<PipeSegment> segments;
};

// Represents Script node.
struct Script {
  std::vector<Pipeline> pipelines;
};

}  // namespace foomatic_shell

#endif  // FOOMATIC_SHELL_GRAMMAR_H_
