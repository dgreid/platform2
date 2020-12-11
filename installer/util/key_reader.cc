// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "installer/util/key_reader.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/strings/strcat.h>

namespace key_reader {

namespace {
constexpr char kDevInputEvent[] = "/dev/input";
constexpr char kEventDevName[] = "*event*";
constexpr char kXkbPathName[] = "/usr/share/X11/xkb";

// Offset between xkb layout codes and ev key codes.
constexpr int kXkbOffset = 8;

// Determines if the given |bit| is set in the |bitmask| array.
bool TestBit(const int bit, const uint8_t* bitmask) {
  return (bitmask[bit / 8] >> (bit % 8)) & 1;
}

bool IsUsbDevice(const int fd) {
  struct input_id id;
  if (ioctl(fd, EVIOCGID, &id) == -1) {
    PLOG(ERROR) << "Failed to ioctl to determine device bus";
    return false;
  }

  return id.bustype == BUS_USB;
}

bool IsKeyboardDevice(const int fd) {
  uint8_t evtype_bitmask[EV_MAX / 8 + 1];
  if (ioctl(fd, EVIOCGBIT(0, sizeof(evtype_bitmask)), evtype_bitmask) == -1) {
    PLOG(ERROR) << "Failed to ioctl to determine supported event types";
    return false;
  }

  // The device is a "keyboard" if it supports EV_KEY events. Though, it is not
  // necessarily a real keyboard; EV_KEY events could also be e.g. volume
  // up/down buttons on a device.
  return TestBit(EV_KEY, evtype_bitmask);
}

}  // namespace

KeyReader::~KeyReader() {
  // Release xkb references.
  xkb_state_unref(state_);
  xkb_keymap_unref(keymap_);
  xkb_context_unref(ctx_);
}

bool KeyReader::KeyEventStart() {
  base::FileEnumerator file_enumerator(base::FilePath(kDevInputEvent), true,
                                       base::FileEnumerator::FILES,
                                       FILE_PATH_LITERAL(kEventDevName));

  for (base::FilePath dir_path = file_enumerator.Next(); !dir_path.empty();
       dir_path = file_enumerator.Next()) {
    base::ScopedFD fd(open(dir_path.value().c_str(), O_RDONLY | O_CLOEXEC));
    if (!fd.is_valid()) {
      PLOG(INFO) << "Failed to open event device: " << fd.get();
      continue;
    }

    if ((include_usb_ || !IsUsbDevice(fd.get())) &&
        IsKeyboardDevice(fd.get())) {
      fds_.push_back(std::move(fd));
    }
  }

  // At least one valid keyboard.
  if (!fds_.empty()) {
    return GetInput();
  }

  return false;
}

bool KeyReader::SetKeyboardContext() {
  // Set xkb layout and get keymap.
  ctx_ = xkb_context_new(XKB_CONTEXT_NO_DEFAULT_INCLUDES);
  if (!ctx_) {
    LOG(ERROR) << "Unable to get new xkb context.";
    return false;
  }
  if (!xkb_context_include_path_append(ctx_, kXkbPathName)) {
    LOG(ERROR) << "Cannot add path " << kXkbPathName << " to context.";
    return false;
  }
  names_ = {.layout = country_code_.c_str()};
  keymap_ =
      xkb_keymap_new_from_names(ctx_, &names_, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (keymap_ == nullptr) {
    LOG(ERROR) << "No matching keyboard for " << country_code_
               << ". Make sure the two letter country code is valid.";
    return false;
  }
  state_ = xkb_state_new(keymap_);
  if (!state_) {
    LOG(ERROR) << "Unable to get xkbstate for " << country_code_;
    return false;
  }
  return true;
}

bool KeyReader::GetInput() {
  int epfd = epoll_create1(EPOLL_CLOEXEC);
  if (epfd < 0) {
    PLOG(ERROR) << "Epoll_create failed";
    return false;
  }

  for (int i = 0; i < fds_.size(); ++i) {
    struct epoll_event ep_event;
    ep_event.data.u32 = i;
    ep_event.events = EPOLLIN;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fds_[i].get(), &ep_event) < 0) {
      PLOG(ERROR) << "Epoll_ctl failed";
      return false;
    }
  }

  if (!SetKeyboardContext()) {
    return false;
  }

  while (true) {
    struct epoll_event ep_event;
    if (epoll_wait(epfd, &ep_event, 1, -1) <= 0) {
      PLOG(ERROR) << "epoll_wait failed";
      return false;
    }
    struct input_event ev;
    int rd = read(fds_[ep_event.data.u32].get(), &ev, sizeof(ev));
    if (rd != sizeof(ev)) {
      PLOG(ERROR) << "Could not read event";
      return false;
    }

    if (ev.type != EV_KEY || ev.code > KEY_MAX) {
      continue;
    }

    // Take in ev event and add to user input as appropriate.
    // Returns false to exit.
    if (!GetChar(ev)) {
      return true;
    }
  }
}

bool KeyReader::GetChar(const struct input_event& ev) {
  xkb_keycode_t keycode = ev.code + kXkbOffset;
  xkb_keysym_t sym = xkb_state_key_get_one_sym(state_, keycode);

  if (ev.value == 0) {
    // Key up event.
    if (sym == XKB_KEY_Return && return_pressed_) {
      // Only end if RETURN key press was already recorded.
      if (user_input_.empty()) {
        printf("\n");
      } else {
        user_input_.push_back('\0');
        printf("%s\n", user_input_.c_str());
      }
      return false;
    }

    // Put char representation in buffer.
    int size = xkb_state_key_get_utf8(state_, keycode, nullptr, 0) + 1;
    std::vector<char> buff(size);
    xkb_state_key_get_utf8(state_, keycode, buff.data(), size);

    if (sym == XKB_KEY_BackSpace && !user_input_.empty()) {
      user_input_.pop_back();
    } else if (isprint(buff[0]) &&
               user_input_.size() < key_reader::kMaxInputLength) {
      // Only printable ASCII characters stored in output.
      user_input_.push_back(buff[0]);
    }
    xkb_state_update_key(state_, keycode, XKB_KEY_UP);

    if (print_length_) {
      printf("%zu\n", user_input_.size());
      // Flush input so it can be read before program exits.
      fflush(stdout);
    }

  } else if (ev.value == 1) {
    // Key down event.
    if (sym == XKB_KEY_Return)
      return_pressed_ = true;

    xkb_state_update_key(state_, keycode, XKB_KEY_DOWN);

  } else if (ev.value == 2) {
    // Long press or repeating key event.
    if (sym == XKB_KEY_BackSpace && !user_input_.empty() &&
        ++backspace_counter_ >= key_reader::kBackspaceSensitivity) {
      // Remove characters until empty.
      user_input_.pop_back();
      backspace_counter_ = 0;
    }
    if (print_length_) {
      printf("%zu\n", user_input_.size());
      // Flush input so it can be read before program exits.
      fflush(stdout);
    }
  }
  return true;
}

std::string KeyReader::GetUserInputForTest() {
  return user_input_;
}

}  // namespace key_reader
