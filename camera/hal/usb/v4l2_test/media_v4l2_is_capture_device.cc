// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hal/usb/v4l2_test/media_v4l2_device.h"

static bool IsCaptureDevice(uint32_t caps) {
  const uint32_t kCaptureMask =
      V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE;
  // Old drivers use (CAPTURE | OUTPUT) for memory-to-memory video devices.
  const uint32_t kOutputMask =
      V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_VIDEO_OUTPUT_MPLANE;
  const uint32_t kM2mMask = V4L2_CAP_VIDEO_M2M | V4L2_CAP_VIDEO_M2M_MPLANE;
  return (caps & kCaptureMask) && !(caps & kOutputMask) && !(caps & kM2mMask);
}

// Checks whether /dev/videoX is a video capture device. Return value 0 means
// it is a capture device. 1 otherwise.
int main(int argc, char** argv) {
  if (argc < 2) {
    printf("Usage: media_v4l2_is_capture_device /dev/videoX\n");
    return 1;
  }

  V4L2Device v4l2_dev(argv[1], 4);
  if (!v4l2_dev.OpenDevice()) {
    printf("[Error] Can not open device '%s'\n", argv[1]);
    return 1;
  }

  bool is_capture_device = false;
  v4l2_capability caps;
  if (!v4l2_dev.ProbeCaps(&caps, false)) {
    printf("[Error] Can not probe caps on device '%s'\n", argv[1]);
  } else {
    // Prefer to use available capabilities of that specific device node instead
    // of the physical device as a whole, so we can properly ignore the metadata
    // device node.
    if (caps.capabilities & V4L2_CAP_DEVICE_CAPS) {
      is_capture_device = IsCaptureDevice(caps.device_caps);
    } else {
      is_capture_device = IsCaptureDevice(caps.capabilities);
    }
  }
  v4l2_dev.CloseDevice();

  return is_capture_device ? 0 : 1;
}
