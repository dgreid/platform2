// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/fuzzers/blob_mutator.h"

#include <algorithm>

#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>

using brillo::Blob;

namespace {

// The "commands" that the MutateBlob() function uses for enterpreting the
// fuzzer input and performing the mutations it implements.
enum class BlobMutatorCommand {
  kCopyRemainingData,
  kCopyChunk,
  kDeleteChunk,
  kInsertByte,

  kMaxValue = kInsertByte
};

}  // namespace

Blob MutateBlob(const Blob& input_blob,
                int max_length,
                FuzzedDataProvider* fuzzed_data_provider) {
  // Begin with an empty result blob. The code below will fill it with data,
  // according to the parsed "commands".
  Blob fuzzed_blob;
  fuzzed_blob.reserve(max_length);
  int input_index = 0;
  while (fuzzed_blob.size() < max_length) {
    switch (fuzzed_data_provider->ConsumeEnum<BlobMutatorCommand>()) {
      case BlobMutatorCommand::kCopyRemainingData: {
        // Take all remaining data from the input blob and stop.
        const int bytes_to_copy = std::min(input_blob.size() - input_index,
                                           max_length - fuzzed_blob.size());
        fuzzed_blob.insert(fuzzed_blob.end(), input_blob.begin() + input_index,
                           input_blob.begin() + input_index + bytes_to_copy);
        CHECK_LE(fuzzed_blob.size(), max_length);
        return fuzzed_blob;
      }
      case BlobMutatorCommand::kCopyChunk: {
        // Take the specified number of bytes from the current position in the
        // input blob.
        const int max_bytes_to_copy = std::min(input_blob.size() - input_index,
                                               max_length - fuzzed_blob.size());
        const int bytes_to_copy =
            fuzzed_data_provider->ConsumeIntegralInRange(0, max_bytes_to_copy);
        fuzzed_blob.insert(fuzzed_blob.end(), input_blob.begin() + input_index,
                           input_blob.begin() + input_index + bytes_to_copy);
        break;
      }
      case BlobMutatorCommand::kDeleteChunk: {
        // Skip (delete) the specified number of bytes from the current position
        // in the input blob.
        const int max_bytes_to_delete = input_blob.size() - input_index;
        const int bytes_to_delete =
            fuzzed_data_provider->ConsumeIntegralInRange(0,
                                                         max_bytes_to_delete);
        input_index += bytes_to_delete;
        break;
      }
      case BlobMutatorCommand::kInsertByte: {
        // Append the specified byte.
        fuzzed_blob.push_back(fuzzed_data_provider->ConsumeIntegral<uint8_t>());
        break;
      }
    }
  }
  CHECK_LE(fuzzed_blob.size(), max_length);
  return fuzzed_blob;
}
