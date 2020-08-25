// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <getopt.h>
#include <stdio.h>

#include "label_detect.h"

int verbose = 0;

/* detector's name and function. detect_func() returns true if the feature is
 * detected. */
struct detector {
  char* name;
  bool (*detect_func)(void);
};

struct detector detectors[] = {
    {"hw_jpeg_acc_dec", detect_jpeg_acc_dec},
    {"hw_jpeg_acc_enc", detect_jpeg_acc_enc},
    {"hw_video_acc_h264", detect_video_acc_h264},
    {"hw_video_acc_vp8", detect_video_acc_vp8},
    {"hw_video_acc_vp9", detect_video_acc_vp9},
    {"hw_video_acc_vp9_2", detect_video_acc_vp9_2},
    {"hw_video_acc_enc_h264", detect_video_acc_enc_h264},
    {"hw_video_acc_enc_vp8", detect_video_acc_enc_vp8},
    {"hw_video_acc_enc_vp9", detect_video_acc_enc_vp9},
    {"builtin_usb_camera", detect_builtin_usb_camera},
    {"builtin_mipi_camera", detect_builtin_mipi_camera},
    {"vivid_camera", detect_vivid_camera},
    {"builtin_camera", detect_builtin_camera},
    {"builtin_or_vivid_camera", detect_builtin_or_vivid_camera},
    {"hw_video_acc_h264_4k", detect_4k_device_h264},
    {"hw_video_acc_vp8_4k", detect_4k_device_vp8},
    {"hw_video_acc_vp9_4k", detect_4k_device_vp9},
    {"hw_video_acc_enc_h264_4k", detect_4k_device_enc_h264},
    {"hw_video_acc_enc_vp8_4k", detect_4k_device_enc_vp8},
    {"hw_video_acc_enc_vp9_4k", detect_4k_device_enc_vp9},
    {NULL, NULL}};

int main(int argc, char* argv[]) {
  int i;
  int opt;

  while ((opt = getopt(argc, argv, "vh")) != -1) {
    switch (opt) {
      case 'v':
        verbose = 1;
        break;
      case 'h':
        printf("Usage: %s [-vh]\n", argv[0]);
        return 0;
    }
  }

  for (i = 0; detectors[i].name; i++) {
    TRACE("Detecting [%s]\n", detectors[i].name);
    if (detectors[i].detect_func()) {
      printf("Detected label: %s\n", detectors[i].name);
    }
    TRACE("\n");
  }

  TRACE("Detect via table look up\n");
  detect_label_by_board_name();
  return 0;
}
