// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_APPLY_IMPL_H_
#define LIBBRILLO_BRILLO_APPLY_IMPL_H_

#include <utility>

namespace brillo {
namespace internal {

template <typename U, typename ... Ts, class Tuple, std::size_t... I>
U ApplyImpl(base::OnceCallback<U(Ts...)> f, Tuple&& t,
  std::index_sequence<I...>) {
  return std::move(f).Run(std::move(std::get<I>(std::forward<Tuple>(t)))...);
}

template <typename U, typename ... Ts, class Tuple>
U Apply(base::OnceCallback<U(Ts...)> f, Tuple&& t) {
  return ApplyImpl(
    std::move(f), std::forward<Tuple>(t),
    std::make_index_sequence<sizeof...(Ts)>{});
}

template <typename U, typename T>
U Apply(base::OnceCallback<U(T)> f, T&& val) {
  return std::move(f).Run(std::forward<T>(val));
}

}  // namespace internal
}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_APPLY_IMPL_H_
