// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/routines/prime_search/prime_number_search.h"

#include <cmath>

#include <base/stl_util.h>

#include "diagnostics/routines/prime_search/prime_number_list.h"

namespace diagnostics {

PrimeNumberSearch::PrimeNumberSearch(uint64_t max_num) : max_num_(max_num) {
  expected_prime_num_count_ = 0;
  for (auto prime : diagnostics::kPrimeNumberList) {
    if (prime <= max_num_)
      expected_prime_num_count_++;
    else
      break;
  }
}

bool PrimeNumberSearch::Run() {
  uint64_t prime_num_count = 0;
  for (uint64_t num = 2; num <= max_num_; num++) {
    if (!IsPrime(num)) {
      continue;
    }
    if (prime_num_count >= base::size(diagnostics::kPrimeNumberList) ||
        kPrimeNumberList[prime_num_count++] != num) {
      LOG(ERROR) << "incorrect number " << num << " is calculated as "
                 << prime_num_count << "th prime number";
      return false;
    }
  }
  if (prime_num_count != expected_prime_num_count_) {
    LOG(ERROR) << "incorrect total calculated prime number amount";
    return false;
  }

  return true;
}

bool PrimeNumberSearch::IsPrime(uint64_t num) const {
  if (num == 0 || num == 1)
    return false;

  uint64_t sqrt_root =
      static_cast<uint64_t>(std::sqrt(static_cast<double>(num)));
  for (uint64_t divisor = 2; divisor <= sqrt_root; divisor++)
    if (num % divisor == 0)
      return false;

  return true;
}

}  // namespace diagnostics
