// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/event_device.h"

#include <fcntl.h>
#include <linux/input.h>

#include <base/posix/eintr_wrapper.h>

// Helper macros for accessing the bitfields returned by the kernel interface,
// compare with include/linux/bitops.h.
#define BITS_PER_LONG (sizeof(long) * 8)  // NOLINT(runtime/int)
#define BITS_TO_LONGS(bits) (((bits) - 1) / BITS_PER_LONG + 1)
#define BITMASK_GET_BIT(bitmask, bit) \
    ((bitmask[bit / BITS_PER_LONG] >> (bit % BITS_PER_LONG)) & 1)

#define MAX(a, b) ((a) > (b) ? (a) : (b))

namespace power_manager {
namespace system {

namespace {
// C++14's <algorithm> could do std::max(EV_MAX, KEY_MAX, SW_MAX);
static constexpr int kMaxBit = MAX(MAX(EV_MAX, KEY_MAX), SW_MAX);
};

// EventDevice

EventDevice::EventDevice(int fd, const base::FilePath& path)
    : fd_(fd),
      path_(path) {
}

EventDevice::~EventDevice() {
  fd_watcher_.reset();
  // ENODEV is expected if the device was just unplugged.
  if (close(fd_) != 0 && errno != ENODEV)
    PLOG(ERROR) << "Unable to close FD " << fd_;
}

std::string EventDevice::GetDebugName() {
  return path_.value();
}

std::string EventDevice::GetPhysPath() {
  char phys[256] = "";

  if (ioctl(fd_, EVIOCGPHYS(sizeof(phys)), phys) < 0 && errno != ENOENT)
    PLOG(ERROR) << "Could not get topo phys path of " << path_.value();

  return phys;
}

bool EventDevice::IsLidSwitch() {
  return HasEventBit(0, EV_SW) && HasEventBit(EV_SW, SW_LID);
}

bool EventDevice::IsTabletModeSwitch() {
  return HasEventBit(0, EV_SW) && HasEventBit(EV_SW, SW_TABLET_MODE);
}

bool EventDevice::IsPowerButton() {
  return HasEventBit(0, EV_KEY) && HasEventBit(EV_KEY, KEY_POWER);
}

bool EventDevice::HoverSupported() {
  // Multitouch hover uses just the ABS_MT_DISTANCE event in addition to
  // the normal multi-touch events.
  if (HasEventBit(0, EV_ABS) && HasEventBit(EV_ABS, ABS_MT_DISTANCE))
    return true;

  // Simple single-touch hover presence-only detection uses 3 events:
  // ABS_DISTANCE, BTN_TOUCH, and BTN_TOOL_FINGER.
  if (HasEventBit(0, EV_ABS) && HasEventBit(EV_ABS, ABS_DISTANCE) &&
      HasEventBit(0, EV_KEY) && HasEventBit(EV_KEY, BTN_TOUCH) &&
      HasEventBit(EV_KEY, BTN_TOOL_FINGER))
    return true;

  return false;
}

bool EventDevice::HasLeftButton() {
  return HasEventBit(0, EV_KEY) && HasEventBit(EV_KEY, BTN_LEFT);
}

LidState EventDevice::GetInitialLidState() {
  CHECK(!fd_watcher_) << "GetInitialLidState called after WatchForEvents";
  return GetSwitchBit(SW_LID) ? LID_CLOSED : LID_OPEN;
}

TabletMode EventDevice::GetInitialTabletMode() {
  CHECK(!fd_watcher_) << "GetInitialTabletMode called after WatchForEvents";
  return GetSwitchBit(SW_TABLET_MODE) ? TABLET_MODE_ON : TABLET_MODE_OFF;
}

bool EventDevice::HasEventBit(int event_type, int bit) {
  DCHECK(bit <= kMaxBit);

  // bitmask needs to hold kMaxBit+1 bits
  unsigned long bitmask[BITS_TO_LONGS(kMaxBit+1)];  // NOLINT(runtime/int)
  memset(bitmask, 0, sizeof(bitmask));
  if (ioctl(fd_, EVIOCGBIT(event_type, sizeof(bitmask)), bitmask) < 0) {
    PLOG(ERROR) << "EVIOCGBIT failed for " << path_.value();
    return false;
  }
  return BITMASK_GET_BIT(bitmask, bit);
}

bool EventDevice::GetSwitchBit(int bit) {
  DCHECK(bit <= kMaxBit);

  // bitmask needs to hold SW_MAX+1 bits
  unsigned long bitmask[BITS_TO_LONGS(SW_MAX+1)];  // NOLINT(runtime/int)
  memset(bitmask, 0, sizeof(bitmask));
  if (ioctl(fd_, EVIOCGSW(sizeof(bitmask)), bitmask) < 0) {
    PLOG(ERROR) << "EVIOCGBIT failed for " << path_.value();
    return false;
  }
  return BITMASK_GET_BIT(bitmask, bit);
}

bool EventDevice::ReadEvents(std::vector<input_event>* events_out) {
  DCHECK(events_out);
  events_out->clear();

  struct input_event events[64];
  ssize_t read_size = HANDLE_EINTR(read(fd_, events, sizeof(events)));
  if (read_size < 0) {
    // ENODEV is expected if the device was just unplugged.
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENODEV)
      PLOG(ERROR) << "Reading events from " << path_.value() << " failed";
    return false;
  } else if (read_size == 0) {
    LOG(ERROR) << "Read returned 0 when reading events from " << path_.value();
    return false;
  }

  const size_t num_events = read_size / sizeof(struct input_event);
  if (read_size % sizeof(struct input_event)) {
    LOG(ERROR) << "Read " << read_size << " byte(s) while expecting "
               << sizeof(struct input_event) << "-byte events";
    return false;
  }

  events_out->reserve(num_events);
  for (size_t i = 0; i < num_events; ++i)
    events_out->push_back(events[i]);
  return true;
}

void EventDevice::WatchForEvents(base::Closure new_events_cb) {
  fd_watcher_.reset(new base::MessageLoopForIO::FileDescriptorWatcher);
  new_events_cb_ = new_events_cb;
  if (!base::MessageLoopForIO::current()->WatchFileDescriptor(
          fd_, true, base::MessageLoopForIO::WATCH_READ, fd_watcher_.get(),
          this)) {
    LOG(ERROR) << "Unable to watch FD " << fd_;
  }
}

void EventDevice::OnFileCanReadWithoutBlocking(int fd) {
  CHECK(fd == fd_);
  new_events_cb_.Run();
}

void EventDevice::OnFileCanWriteWithoutBlocking(int fd) {
  NOTREACHED() << "Unexpected non-blocking write notification for FD " << fd;
}

// EventDeviceFactory

EventDeviceFactory::EventDeviceFactory() {}

EventDeviceFactory::~EventDeviceFactory() {}

linked_ptr<EventDeviceInterface> EventDeviceFactory::Open(
    const base::FilePath& path) {
  int fd = open(path.value().c_str(), O_RDONLY | O_NONBLOCK);
  if (fd < 0) {
    PLOG(ERROR) << "open() failed for " << path.value();
    return linked_ptr<EventDeviceInterface>();
  }
  return linked_ptr<EventDeviceInterface>(new EventDevice(fd, path));
}

}  // namespace system
}  // namespace power_manager
