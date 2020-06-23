/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef RUNTIME_PROBE_COMPONENT_CATEGORY_H_
#define RUNTIME_PROBE_COMPONENT_CATEGORY_H_

#include <map>
#include <memory>
#include <string>

#include <base/values.h>
#include <gtest/gtest.h>

#include "runtime_probe/probe_statement.h"

namespace runtime_probe {

class ComponentCategory {
  /* A component category is defined in following format:
   *
   * {
   *   <component_name:string>: <statement:ProbeStatement>,
   *   ...
   * }
   *
   */
 public:
  static std::unique_ptr<ComponentCategory> FromValue(
      const std::string& category_name, const base::Value& dv);

  /* Evaluates this category and return a base::Value with type list. */
  base::Value Eval() const;

 private:
  std::string category_name_;
  std::map<std::string, std::unique_ptr<ProbeStatement>> component_;

  FRIEND_TEST(ProbeConfigTest, LoadConfig);
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_COMPONENT_CATEGORY_H_
