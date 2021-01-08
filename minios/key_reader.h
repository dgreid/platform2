// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_KEY_READER_H_
#define MINIOS_KEY_READER_H_

#include <linux/input.h>

#include <string>
#include <vector>

#include <base/files/scoped_file.h>
#include <xkbcommon/xkbcommon.h>

namespace key_reader {

constexpr int kMaxInputLength = 64;

// Increasing `kBackspaceSensitivity` will slow backspace speed.
constexpr int kBackspaceSensitivity = 2;

class KeyReader {
 public:
  KeyReader(bool include_usb, bool print_length, std::string country_code)
      : backspace_counter_(0),
        return_pressed_(false),
        print_length_(print_length),
        include_usb_(include_usb),
        country_code_(country_code) {
    user_input_.reserve(kMaxInputLength);
  }

  ~KeyReader();

  // Checks for valid keyboards and starts listening for input.
  // Returns false if there are no valid devices.
  bool KeyEventStart();

  // Creates the correct keyboard layout for a given country code.
  // Returns false for invalid keyboard layout, true otherwise.
  bool SetKeyboardContext();

  // Given a keycode, does all conversions for the layout including
  // capitalization and special characters.
  bool GetInput();

  // GetChar takes in an input event and adds to user input if the key press
  // is a valid, printable ASCII. Returns false on return, true otherwise.
  bool GetChar(const struct input_event& ev);

  // Returns the input stored as a string. Used in unittests.
  std::string GetUserInputForTest();

 private:
  std::string user_input_;
  // Counts and aggregates repeated backspace key events.
  int backspace_counter_;
  // Checks that enter key down was recorded before returning on key up.
  bool return_pressed_;
  // Outputs input length to stdout when true.
  bool print_length_;
  // Whether or not to include USB connections when scanning for events.
  bool include_usb_;
  // Keyboard layout for xkb common;
  std::string country_code_;
  // Stores open event connections.
  std::vector<base::ScopedFD> fds_;
  // XKB common keyboard layout members.
  struct xkb_context* ctx_;
  struct xkb_rule_names names_;
  struct xkb_keymap* keymap_;
  struct xkb_state* state_;
};

}  // namespace key_reader

#endif  // MINIOS_KEY_READER_H_
