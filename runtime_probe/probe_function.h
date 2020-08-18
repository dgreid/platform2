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
  // ProbeFunction is the base class for all probe functions.  A derived
  // class should implement required virtual functions and contain some static
  // members: |function_name|, FromKwargsValue().
  //
  // FromKwargsValue is the main point to create a probe function instance.  It
  // takes a dictionary value in type base::Value as arguments and returns a
  // pointer to the instance of the probe function.
  //
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

  // Returns the name of the probe function.  The returned value should always
  // identical to the static member |function_name| of the derived class.
  //
  // A common implementation can be declared by macro NAME_PROBE_FUNCTION(name)
  // below.
  virtual const std::string& GetFunctionName() const = 0;

  // Converts |dv| with function name as key to ProbeFunction.  Returns nullptr
  // on failure.
  static std::unique_ptr<ProbeFunction> FromValue(const base::Value& dv);

  // A pre-defined factory function creating a probe function with empty
  // argument.
  template <typename T>
  static std::unique_ptr<T> FromEmptyKwargsValue(const base::Value& dv) {
    if (dv.DictSize() != 0) {
      LOG(ERROR) << T::function_name << " does not take any arguement";
      return nullptr;
    }
    return std::make_unique<T>();
  }

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
  // The transform of JSON will be automatically applied.  Returns base::nullopt
  // on failure.
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

  // Function prototype of FromKwargsValue() that should be implemented by each
  // derived class.  See `functions/sysfs.h` about how to implement this
  // function.
  using FactoryFunctionType =
      std::function<std::unique_ptr<ProbeFunction>(const base::Value&)>;

  // Mapping from |function_name| to FromKwargsValue() of each derived classes.
  static std::map<std::string_view, FactoryFunctionType> registered_functions_;

  virtual ~ProbeFunction() = default;

 protected:
  ProbeFunction() = default;

 private:
  base::Optional<base::Value> raw_value_;

  // Each probe function must define their own args type.
};

#define NAME_PROBE_FUNCTION(name)                       \
  const std::string& GetFunctionName() const override { \
    static const std::string instance(function_name);   \
    return instance;                                    \
  }                                                     \
  static constexpr auto function_name = name

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_PROBE_FUNCTION_H_
