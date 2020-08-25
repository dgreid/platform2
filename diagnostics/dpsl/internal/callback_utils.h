// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_DPSL_INTERNAL_CALLBACK_UTILS_H_
#define DIAGNOSTICS_DPSL_INTERNAL_CALLBACK_UTILS_H_

#include <functional>
#include <utility>

#include <base/bind.h>
#include <base/callback.h>
#include <base/location.h>
#include <base/memory/ref_counted.h>
#include <base/task_runner.h>
#include <base/threading/thread_task_runner_handle.h>
#include <grpcpp/grpcpp.h>

namespace diagnostics {

// A function that transforms base::Callback into std::function

template <typename ReturnType, typename... ArgType>
inline std::function<ReturnType(ArgType...)> MakeStdFunctionFromCallback(
    base::Callback<ReturnType(ArgType...)> callback) {
  return [callback](ArgType&&... args) {
    return callback.Run(std::forward<ArgType>(args)...);
  };
}

// A function that transforms base::Callback into std::function, and
// automatically adds grpc::Status::OK
template <typename ReturnType, typename... ArgType>
inline std::function<ReturnType(ArgType...)> MakeStdFunctionFromCallbackGrpc(
    base::Callback<ReturnType(grpc::Status, ArgType...)> callback) {
  return [callback](ArgType&&... args) {
    return callback.Run(grpc::Status::OK, std::forward<ArgType>(args)...);
  };
}

namespace internal {

template <typename ReturnType, typename... ArgTypes>
inline ReturnType RunStdFunctionWithArgs(
    std::function<ReturnType(ArgTypes...)> function, ArgTypes... args) {
  return function(std::forward<ArgTypes>(args)...);
}

template <typename ReturnType, typename... ArgTypes>
inline ReturnType RunStdFunctionWithArgsGrpc(
    std::function<ReturnType(ArgTypes...)> function,
    grpc::Status status,
    ArgTypes... args) {
  return function(std::forward<ArgTypes>(args)...);
}

}  // namespace internal

// Transforms std::function into base::Callback.
template <typename ReturnType, typename... ArgTypes>
inline base::Callback<ReturnType(ArgTypes...)> MakeCallbackFromStdFunction(
    std::function<ReturnType(ArgTypes...)> function) {
  return base::Bind(&internal::RunStdFunctionWithArgs<ReturnType, ArgTypes...>,
                    base::Passed(std::move(function)));
}

// Transforms std::function into base::Callback, and ignores grpc::Status
template <typename ReturnType, typename... ArgTypes>
inline base::Callback<ReturnType(grpc::Status, ArgTypes...)>
MakeCallbackFromStdFunctionGrpc(
    std::function<ReturnType(ArgTypes...)> function) {
  return base::Bind(
      &internal::RunStdFunctionWithArgsGrpc<ReturnType, ArgTypes...>,
      base::Passed(std::move(function)));
}

namespace internal {

template <typename... ArgTypes>
inline void RunCallbackOnTaskRunner(scoped_refptr<base::TaskRunner> task_runner,
                                    const base::Location& location,
                                    base::Callback<void(ArgTypes...)> callback,
                                    ArgTypes... args) {
  task_runner->PostTask(location, base::Bind(std::move(callback),
                                             base::Passed(std::move(args))...));
}

}  // namespace internal

// Returns a callback that remembers the current task runner and, when called,
// posts |callback| to it (with all arguments forwarded).
template <typename... ArgTypes>
inline base::Callback<void(ArgTypes...)> MakeOriginTaskRunnerPostingCallback(
    const base::Location& location,
    base::Callback<void(ArgTypes...)> callback) {
  return base::Bind(&internal::RunCallbackOnTaskRunner<ArgTypes...>,
                    base::ThreadTaskRunnerHandle::Get(), location,
                    std::move(callback));
}

}  // namespace diagnostics

#endif  // DIAGNOSTICS_DPSL_INTERNAL_CALLBACK_UTILS_H_
