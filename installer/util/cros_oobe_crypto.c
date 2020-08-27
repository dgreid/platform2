// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <err.h>
#include <stdio.h>

#include <openssl/ec.h>
#include <openssl/pem.h>

static void GenerateKeyPair(void) {
  // We use a P256 curve.
  EC_KEY* key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  if (key == NULL) {
    errx(EXIT_FAILURE, "Failed to create a new EC key.");
  }

  EC_KEY_set_asn1_flag(key, OPENSSL_EC_NAMED_CURVE);
  if (EC_KEY_generate_key(key) == 0) {
    errx(EXIT_FAILURE, "Failed to generate the EC key.");
  }

  if (EC_KEY_check_key(key) == 0) {
    errx(EXIT_FAILURE, "Failed to validate the EC key.");
  }

  if (PEM_write_ECPrivateKey(stdout, key, NULL, NULL, 0, 0, NULL) == 0) {
    errx(EXIT_FAILURE, "Failed to print the private key.");
  }
  if (PEM_write_EC_PUBKEY(stdout, key) == 0) {
    errx(EXIT_FAILURE, "Failed to print the public key.");
  }

  EC_KEY_free(key);
}

static void ShowHelpAndExit(void) {
  fprintf(
      stderr,
      "cros_oobe_crypto\n"
      "\n"
      "\tGenerates a prime256v1 key pair for OOBE autoconfiguration signing.\n"
      "\n"
      "\tThe private key is printed in PEM format on the first 5 lines of\n"
      "\toutput. The public key printed out in PEM format on the following 4\n"
      "\tlines.\n"
      "\n"
      "\tEach invocation of `cros_oobe_crypto` will create a new keypair.\n");
  exit(EXIT_FAILURE);
}

int main(int argc, char** argv) {
  if (argc > 1) {
    ShowHelpAndExit();
  }

  GenerateKeyPair();
  return EXIT_SUCCESS;
}
