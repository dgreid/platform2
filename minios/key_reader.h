// Copyright 2021 The Chromium OS Authors. All rights reserved.
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
  // Default constructor can only access EvWaitForKeys.
  explicit KeyReader(bool include_usb)
      : include_usb_(include_usb), use_only_evwaitkey_(true) {}

  KeyReader(bool include_usb, bool print_length, std::string country_code)
      : backspace_counter_(0),
        return_pressed_(false),
        print_length_(print_length),
        include_usb_(include_usb),
        country_code_(country_code),
        use_only_evwaitkey_(false) {
    user_input_.reserve(kMaxInputLength);
  }

  ~KeyReader();

  // Creates the correct keyboard layout for a given country code.
  // Returns false for invalid keyboard layout, true otherwise.
  bool SetKeyboardContext();

  // Given a key code, does all conversions for the layout including
  // capitalization and special characters.
  bool GetInput();

  // GetChar takes in an input event and adds to user input if the key press
  // is a valid, printable ASCII. Returns false on return, true otherwise.
  bool GetChar(const struct input_event& ev);

  // A blocking call that will wait until one of the keys given is pressed. Sets
  // the value of key_press with the first key from the list that is recorded.
  // Returns true on success.  Key events not in the list are ignored.
  // TODO(vyshu): Change this call to be asynchronous.
  bool EvWaitForKeys(const std::vector<int>& keys, int* key_press);

  // Returns the input stored as a string. Used in unittests.
  std::string GetUserInputForTest();

 private:
  // Checks whether all the keys in `keys_` are supported by the fd. Returns
  // false on failure.
  bool SupportsAllKeys(const int fd);

  // Checks all the valid files under kDevInputEvent, stores the valid keyboard
  // devices to `fds_`. Will check if all keys are supported if input is true.
  // Returns false if there are no available file descriptors.
  virtual bool GetValidFds(bool check_supported_keys);

  // Creates the epoll and gets event data. Sets epoll file descriptor and on
  // returns true on success.
  virtual bool EpollCreate(base::ScopedFD* epfd);

  // Waits for a valid key event and reads it into the input event struct. Sets
  // fd index and returns true on success.
  virtual bool GetEpEvent(int epfd, struct input_event* ev, int* index);

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

  // Allows class to only access the EvWaitForKey function. GetInput will return
  // false.
  bool use_only_evwaitkey_;
  // A list of keys to listen for on the blocking call.
  std::vector<int> keys_;

  // XKB common keyboard layout members.
  struct xkb_context* ctx_{nullptr};
  struct xkb_rule_names names_;
  struct xkb_keymap* keymap_{nullptr};
  struct xkb_state* state_{nullptr};
};

}  // namespace key_reader

#endif  // MINIOS_KEY_READER_H_
