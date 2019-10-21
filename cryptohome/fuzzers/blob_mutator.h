// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_FUZZERS_BLOB_MUTATOR_H_
#define CRYPTOHOME_FUZZERS_BLOB_MUTATOR_H_

#include <brillo/secure_blob.h>

class FuzzedDataProvider;

// Returns the mutated version of the provided |input_blob|.
// The following mutations are applied:
// * Removing chunk(s) from the input blob;
// * Inserting "random" bytes into the input blob.
// The size of the resulting blob is guaranteed to be within [0; max_length].
brillo::Blob MutateBlob(const brillo::Blob& input_blob,
                        int max_length,
                        FuzzedDataProvider* fuzzed_data_provider);

#endif  // CRYPTOHOME_FUZZERS_BLOB_MUTATOR_H_
