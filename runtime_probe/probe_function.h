// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_PROBE_FUNCTION_H_
#define RUNTIME_PROBE_PROBE_FUNCTION_H_

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <base/json/json_writer.h>
#include <base/values.h>
#include <base/strings/string_util.h>

#include "runtime_probe/probe_function_argument.h"

namespace runtime_probe {

class ProbeFunction {
  // Formally, a probe function will be represented as following structure::
  //   {
  //     <function_name:string>: <args:ArgsType>
  //   }
  //
  // where the top layer dictionary should have one and only one key.  For
  // example::
  //   {
  //     "sysfs": {
  //       "dir_path": "/sys/class/cool/device/dev*",
  //       "keys": ["key_1", "key_2"],
  //       "optional_keys": ["opt_key_1"]
  //     }
  //   }
  //
  // TODO(stimim): implement the following syntax.
  //
  // Alternative Syntax::
  //   1. single string ("<function_name:string>"), this is equivalent to::
  //      {
  //        <function_name:string>: {}
  //      }
  //
  //   2. single string ("<function_name:string>:<arg:string>"), this is
  //      equivalent to::
  //      {
  //        <function_name:string>: {
  //          "__only_required_argument": {
  //            <arg:string>
  //          }
  //        }
  //      }

 public:
  using DataType = std::vector<base::Value>;

  // Convert |value| to ProbeFunction.  Returns nullptr on failure.
  static std::unique_ptr<ProbeFunction> FromValue(const base::Value& dv);

  virtual std::string GetFunctionName() const { return {}; }

  // Evaluates this entire probe function.
  //
  // Output will be a list of base::Value.
  virtual DataType Eval() const = 0;

  // Serializes this probe function and passes it to helper. The output of the
  // helper will store in |result|.
  //
  // true if success on executing helper.
  bool InvokeHelper(std::string* result) const;

  // Serializes this probe function and passes it to helper.  Helper function
  // for InvokeHelper() where the output is known in advanced in JSON format.
  // The transform of JSON will be automatically applied.  If it fails,
  // the returned pointer points to NULL.
  base::Optional<base::Value> InvokeHelperToJSON() const;

  // Evaluates the helper part for this probe function. Helper part is
  // designed for portion that need extended sandbox. ProbeFunction will
  // be initialized with same json statement in the helper process, which
  // invokes EvalInHelper() instead of Eval(). Since execution of
  // EvalInHelper() implies a different sandbox, it is encouraged to keep work
  // that doesn't need a privilege out of this function.
  //
  // Output will be an integer and the interpretation of the integer on
  // purposely leaves to the caller because it might execute other binary
  // in sandbox environment and we might want to preserve the exit code.
  virtual int EvalInHelper(std::string* output) const;

  // Function prototype of |FromValue| that should be implemented by each
  // derived class.  See `functions/sysfs.h` about how to implement this
  // function.
  using FactoryFunctionType =
      std::function<std::unique_ptr<ProbeFunction>(const base::Value&)>;

  // Mapping from |function_name| to |FromValue| of each derived classes.
  static std::map<std::string_view, FactoryFunctionType> registered_functions_;

  template <class T>
  struct Register {
    Register() {
      static_assert(std::is_base_of<ProbeFunction, T>::value,
                    "Registered type must inherit ProbeFunction");
      registered_functions_[T::function_name] = T::FromValue;
    }
  };

  virtual ~ProbeFunction() = default;

 protected:
  ProbeFunction() = default;

 private:
  base::Optional<base::Value> raw_value_;

  // Each probe function must define their own args type.
};

#ifdef _RUNTIME_PROBE_GENERATE_PROBE_FUNCTIONS
// _RUNTIME_PROBE_GENERATE_PROBE_FUNCTIONS should only be defined by
// `all_functions.cc`.  So the following initialization will only have one
// instance.

std::map<std::string_view, ProbeFunction::FactoryFunctionType>
    ProbeFunction::registered_functions_{};
#define REGISTER_PROBE_FUNCTION(T) ProbeFunction::Register<T> T::register_

#else

#define REGISTER_PROBE_FUNCTION(T)

#endif

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_PROBE_FUNCTION_H_
