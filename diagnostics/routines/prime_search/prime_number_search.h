// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_ROUTINES_PRIME_SEARCH_PRIME_NUMBER_SEARCH_H_
#define DIAGNOSTICS_ROUTINES_PRIME_SEARCH_PRIME_NUMBER_SEARCH_H_

#include <cstdint>

#include <base/macros.h>

namespace diagnostics {

class PrimeNumberSearch {
 public:
  explicit PrimeNumberSearch(uint64_t max_num);
  virtual ~PrimeNumberSearch() = default;

  virtual bool IsPrime(uint64_t num) const;

  // Executes prime number search task. Returns true if searching is completed
  // without any error, false otherwise.
  bool Run();

 private:
  const uint64_t max_num_ = 0;

  uint64_t expected_prime_num_count_ = 0;

  DISALLOW_COPY_AND_ASSIGN(PrimeNumberSearch);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_ROUTINES_PRIME_SEARCH_PRIME_NUMBER_SEARCH_H_
