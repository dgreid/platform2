// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
//
// Driver program for creating verity hash images.
#include <stdio.h>
#include <stdlib.h>

#include "verity/file_hasher.h"
#include "verity/logging.h"
#include "verity/simple_file/env.h"
#include "verity/simple_file/file.h"
#include "verity/utils.h"

namespace {
void print_usage(const char* name) {
  // We used to advertise more algorithms, but they've never been implemented:
  // sha512 sha384 sha mdc2 ripemd160 md4 md2
  fprintf(
      stderr,
      "Usage:\n"
      "  %s <arg>=<value>...\n"
      "Options:\n"
      "  mode              One of 'create' or 'verify'\n"
      "  alg               Hash algorithm to use. One of:\n"
      "                      sha256 sha224 sha1 md5\n"
      "  payload           Path to the image to hash\n"
      "  payload_blocks    Size of the image, in blocks (4096 bytes)\n"
      "  hashtree          Path to a hash tree to create or read from\n"
      "  root_hexdigest    Digest of the root node (in hex) for verification\n"
      "  salt              Salt (in hex)\n"
      "\n",
      name);
}

typedef enum { VERITY_NONE = 0, VERITY_CREATE, VERITY_VERIFY } verity_mode_t;

static unsigned int parse_blocks(const char* block_s) {
  return (unsigned int)strtoul(block_s, NULL, 0);
}
}  // namespace

static int verity_create(const char* alg,
                         const char* image_path,
                         unsigned int image_blocks,
                         const char* hash_path,
                         const char* salt);

void splitarg(char* arg, char** key, char** val) {
  char* sp = NULL;
  *key = strtok_r(arg, "=", &sp);
  *val = strtok_r(NULL, "=", &sp);
}

int main(int argc, char** argv) {
  verity_mode_t mode = VERITY_CREATE;
  const char* alg = NULL;
  const char* payload = NULL;
  const char* hashtree = NULL;
  const char* salt = NULL;
  unsigned int payload_blocks = 0;
  int i;
  char *key, *val;

  for (i = 1; i < argc; i++) {
    splitarg(argv[i], &key, &val);
    if (!key)
      continue;
    if (!val) {
      fprintf(stderr, "missing value: %s\n", key);
      print_usage(argv[0]);
      return -1;
    }
    if (!strcmp(key, "alg")) {
      alg = val;
    } else if (!strcmp(key, "payload")) {
      payload = val;
    } else if (!strcmp(key, "payload_blocks")) {
      payload_blocks = parse_blocks(val);
    } else if (!strcmp(key, "hashtree")) {
      hashtree = val;
    } else if (!strcmp(key, "root_hexdigest")) {
      // Silently drop root_hexdigest for now...
    } else if (!strcmp(key, "mode")) {
      // Silently drop the mode for now...
    } else if (!strcmp(key, "salt")) {
      salt = val;
    } else {
      fprintf(stderr, "bogus key: '%s'\n", key);
      print_usage(argv[0]);
      return -1;
    }
  }

  if (!alg || !payload || !hashtree) {
    fprintf(stderr, "missing data: %s%s%s\n", alg ? "" : "alg ",
            payload ? "" : "payload ", hashtree ? "" : "hashtree");
    print_usage(argv[0]);
    return -1;
  }

  if (mode == VERITY_CREATE) {
    return verity_create(alg, payload, payload_blocks, hashtree, salt);
  } else {
    LOG(FATAL) << "Verification not done yet";
  }
  return -1;
}

static int verity_create(const char* alg,
                         const char* image_path,
                         unsigned int image_blocks,
                         const char* hash_path,
                         const char* salt) {
  // Configure files
  simple_file::Env env;

  simple_file::File source;
  LOG_IF(FATAL, !source.Initialize(image_path, O_RDONLY, &env))
      << "Failed to open the source file: " << image_path;
  simple_file::File destination;
  LOG_IF(FATAL,
         !destination.Initialize(hash_path, O_CREAT | O_RDWR | O_TRUNC, &env))
      << "Failed to open destination file: " << hash_path;

  // Create the actual worker and create the hash image.
  verity::FileHasher hasher;
  LOG_IF(FATAL, !hasher.Initialize(&source, &destination, image_blocks, alg))
      << "Failed to initialize hasher";
  if (salt)
    hasher.set_salt(salt);
  LOG_IF(FATAL, !hasher.Hash()) << "Failed to hash hasher";
  LOG_IF(FATAL, !hasher.Store()) << "Failed to store hasher";
  hasher.PrintTable(true);
  return 0;
}
