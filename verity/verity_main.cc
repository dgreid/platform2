// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
//
// Driver program for creating verity hash images.
#include <stdio.h>

#include "verity/file_hasher.h"
#include "verity/logging.h"
#include "verity/simple_file/env.h"
#include "verity/simple_file/file.h"
#include "verity/utils.h"

namespace {
void print_usage(const char *name) {
  fprintf(stderr,
"Usage:\n"
"  %s mode depth alg image image_blocks hash_image [root_hexdigest]\n\n"
"Options:\n"
"- mode:	May be create or verify\n"
"		If create, the `hash_image' will be created.\n"
"		If verify, the `hash_image` will be used to verify `image'.\n"
"- depth:	Deprecated. Must be `0'.\n"
"- alg:		Cryptographic hash algorithm to use\n"
"		Valid values: sha512 sha384 sha256 sha224 sha1 sha\n"
"			      mdc2 ripemd160 md5 md4 md2\n"
"		(Algorithm choice depends on your target kernel)\n"
"- image:	Path to the image to be hashed or verified\n"
"- image_blocks: number of 4096 byte blocks to hash/verify\n"
"   If 0, the file size will be used.\n"
"- hash_image:	Path where a hash image file may be created or read\n"
"- root_hexdigest:	Digest of the root node (in hex) for verifying\n"
"\n", name);
}

typedef enum { VERITY_NONE = 0, VERITY_CREATE, VERITY_VERIFY } verity_mode_t;
static verity_mode_t parse_mode(const char *mode_s) {
  if (!strcmp(mode_s, "create")) return VERITY_CREATE;
  if (!strcmp(mode_s, "verify")) return VERITY_VERIFY;
  fprintf(stderr, "Unknown mode specified: %s\n", mode_s);
  return VERITY_NONE;
}

static unsigned int parse_depth(const char *depth_s) {
  return (unsigned int)strtoul(depth_s, NULL, 0);
}

static unsigned int parse_blocks(const char *block_s) {
  return (unsigned int)strtoul(block_s, NULL, 0);
}
}  // namespace

static int verity_create(const char *alg,
                         const char *image_path,
                         unsigned int image_blocks,
                         const char *hash_path);

int main(int argc, char **argv) {
  if (argc < 7) {
    print_usage(argv[0]);
    return 1;
  }

  if (parse_depth(argv[2]) != 0) {
    LOG(FATAL) << "depth must be 0";
    return -1;
  }

  if (parse_mode(argv[1]) == VERITY_CREATE) {
    return verity_create(argv[3],  // alg
                         argv[4],  // image_path
                         parse_blocks(argv[5]),
                         argv[6]);  // hash path
  } else {
    LOG(FATAL) << "Verification not done yet";
  }
  return -1;
}

static int verity_create(const char *alg,
                         const char *image_path,
                         unsigned int image_blocks,
                         const char *hash_path) {
  // Configure files
  simple_file::Env env;

  simple_file::File source;
  LOG_IF(FATAL, !source.Initialize(image_path, O_RDONLY, &env))
    << "Failed to open the source file: " << image_path;
  simple_file::File destination;
  LOG_IF(FATAL, !destination.Initialize(hash_path,
                                        O_CREAT|O_RDWR|O_TRUNC,
                                        &env))
    << "Failed to open destination file: " << hash_path;

  // Create the actual worker and create the hash image.
  verity::FileHasher hasher;
  LOG_IF(FATAL, !hasher.Initialize(&source,
                                   &destination,
                                   image_blocks,
                                   alg))
    << "Failed to initialize hasher";
  LOG_IF(FATAL, !hasher.Hash());
  LOG_IF(FATAL, !hasher.Store());
  hasher.PrintTable(true);
  return 0;
}
