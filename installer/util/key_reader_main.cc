// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>
#include <stdlib.h>

#include <brillo/flag_helper.h>

#include "installer/util/key_reader.h"

int main(int argc, char* argv[]) {
  DEFINE_string(
      country_code, "us",
      "The two letter country code for keyboard layout."
      "A list of available country codes can be found under X11/xkb/symbols.");
  DEFINE_bool(include_usb, false,
              "Includes USB devices when scanning for input.");
  DEFINE_bool(print_length, false, "Print input length to stdout.");

  brillo::FlagHelper::Init(argc, argv, "key_reader");

  if (FLAGS_country_code.length() > 2)
    FLAGS_country_code = FLAGS_country_code.substr(0, 2);

  key_reader::KeyReader key_reader(FLAGS_include_usb, FLAGS_print_length,
                                   FLAGS_country_code);
  // Returns true on success.
  return key_reader.KeyEventStart() ? 0 : 1;
}
