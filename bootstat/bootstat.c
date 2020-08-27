// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the 'bootstat' command, part of the Chromium OS
// 'bootstat' facility.  The command provides a command line wrapper
// around the key functionality declared in "bootstat.h"

#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bootstat.h"

static void usage(char* cmd) {
  fprintf(stderr, "usage: %s <event-name>\n", basename(cmd));
  exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
  if (argc != 2)
    usage(argv[0]);

  bootstat_log(argv[1]);
  return EXIT_SUCCESS;
}
