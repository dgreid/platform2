// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef CRYPTOHOME_DIRCRYPTO_DATA_MIGRATOR_ATOMIC_FLAG_H_
#define CRYPTOHOME_DIRCRYPTO_DATA_MIGRATOR_ATOMIC_FLAG_H_

#include <base/atomicops.h>
#include <base/macros.h>
#include <base/sequence_checker.h>

namespace cryptohome {
namespace dircrypto_data_migrator {

// A flag that can safely be set from one thread and read from other threads.
//
// This class IS NOT intended for synchronization between threads.
//
// NOTE: This is a dead copy of base::AtomicFlag. We need this because the
// current version r395517S is too old and it doesn't include crrev.com/409020.
// TODO(hashimoto): Remove this once libchrome is upreved. b/37434548
class AtomicFlag {
 public:
  AtomicFlag();
  AtomicFlag(const AtomicFlag&) = delete;
  AtomicFlag& operator=(const AtomicFlag&) = delete;

  ~AtomicFlag() = default;

  // Set the flag. Must always be called from the same sequence.
  void Set();

  // Returns true iff the flag was set. If this returns true, the current thread
  // is guaranteed to be synchronized with all memory operations on the sequence
  // which invoked Set() up until at least the first call to Set() on it.
  bool IsSet() const;

  // Resets the flag. Be careful when using this: callers might not expect
  // IsSet() to return false after returning true once.
  void UnsafeResetForTesting();

 private:
  base::subtle::Atomic32 flag_ = 0;
  base::SequenceChecker set_sequence_checker_;
};

}  // namespace dircrypto_data_migrator
}  // namespace cryptohome

#endif  // CRYPTOHOME_DIRCRYPTO_DATA_MIGRATOR_ATOMIC_FLAG_H_
