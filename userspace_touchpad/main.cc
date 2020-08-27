// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// We want to use assert in production environment
#undef NDEBUG

#include <cassert>
#include <cstdlib>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include <vector>

#include "userspace_touchpad/i2c-device.h"
#include "userspace_touchpad/touch_emulator.h"

// A50 board specific settings
#define I2C_BUS "/dev/i2c-8"
#define I2C_SLAVE_ADDRESS 0x49

// Maximum size of an input report in bytes.
constexpr size_t kMaxReportSize = 144;

// Polling intervals.
constexpr int kMinWaitMs = 1;
constexpr int kWaitMs = 8;  // ~120 fps

// Read an n-bytes integer in little-endian format from a buffer.
int ReadIntegerFromBuffer(const uint8_t* data, int n) {
  int ret = 0;
  for (int i = n - 1; i >= 0; i--) {
    ret <<= 8;
    ret += data[i];
  }
  return ret;
}

// Extract the input report from received data.
void ParseInputReport(const uint8_t* data,
                      std::vector<TouchEvent>* fingers,
                      bool* button_down) {
  *button_down = data[0];
  int count = data[1];
  int start = 2;
  fingers->resize(count);
  for (int i = 0; i < count; i++) {
    (*fingers)[i].x = ReadIntegerFromBuffer(data + start + 0, 2);
    (*fingers)[i].y = ReadIntegerFromBuffer(data + start + 2, 2);
    (*fingers)[i].pressure = ReadIntegerFromBuffer(data + start + 4, 1);
    (*fingers)[i].tracking_id = ReadIntegerFromBuffer(data + start + 5, 2);

    // We use 0xff to represent -1 when sending the input report.
    if ((*fingers)[i].tracking_id == 0xffff)
      (*fingers)[i].tracking_id = -1;

    // Proceed onto the next finger.
    start += 7;
  }
}

int main() {
  // Initialize the I2C communication with touchpad.
  I2cDevice touchpad(I2C_BUS, I2C_SLAVE_ADDRESS);

  // Setup virtual touchpad.
  TouchEmulator touch_emulator;

  // Dummy command for requesting input report. This is enough since we have
  // only one command.
  uint8_t dummy_command[1] = {0};

  // Poll for input reports forever.
  uint8_t data[kMaxReportSize + 1];
  bool button_down;
  std::vector<TouchEvent> fingers;
  while (true) {
    struct timespec start = {0};
    assert(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
    struct timeval startval;
    TIMESPEC_TO_TIMEVAL(&startval, &start);

    // Send a dummy byte to touchpad through I2C.
    touchpad.Write(dummy_command, 1);

    // Read kMaxReportSize bytes from touchpad.
    memset(data, 0, kMaxReportSize);
    touchpad.Read(data, kMaxReportSize);

    if (data[0] == 0x66) {
      uint8_t checksum = 0;
      for (int i = 0; i < kMaxReportSize - 1; i++) {
        checksum ^= data[i];
      }

      if (checksum == data[kMaxReportSize - 1] && data[2]) {
        // Parse received data and send the events.
        ParseInputReport(data + 1, &fingers, &button_down);
        touch_emulator.FlushEvents(fingers, button_down);
      }
    }

    struct timespec now = {0};
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    struct timeval nowval;
    TIMESPEC_TO_TIMEVAL(&nowval, &now);

    struct timeval wait = {0, kWaitMs * 1000};
    struct timeval minwait = {0, kMinWaitMs * 1000};
    struct timeval next;
    timeradd(&startval, &wait, &next);
    struct timeval minnext;
    timeradd(&nowval, &minwait, &minnext);
    if (timercmp(&next, &minnext, <))
      next = minnext;
    struct timeval delay;
    timersub(&next, &nowval, &delay);
    assert(delay.tv_sec == 0);
    usleep(delay.tv_usec);
  }
}
