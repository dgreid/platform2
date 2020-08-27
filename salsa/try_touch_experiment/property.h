// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SALSA_TRY_TOUCH_EXPERIMENT_PROPERTY_H_
#define SALSA_TRY_TOUCH_EXPERIMENT_PROPERTY_H_

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include <X11/Xatom.h>
#include <X11/extensions/XInput.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>

#include <base/strings/stringprintf.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#define MAX_RETRIES 5
#define MAX_ALLOWABLE_DIFFERENCE 0.0001

class Property {
 public:
  Property();
  explicit Property(const std::string& property_string);

  bool Apply() const;
  bool Reset() const;

  bool valid() const;

 private:
  double GetCurrentValue() const;
  int GetDeviceNumber() const;
  bool SetValue(double new_value) const;

  Display* GetX11Display() const;
  XDevice* GetX11TouchpadDevice(Display* display) const;
  double GetPropertyValue(Display* display,
                          XDevice* device,
                          const char* name) const;

  std::string name_;
  double value_;
  double old_value_;

  bool is_valid_;
};

#endif  // SALSA_TRY_TOUCH_EXPERIMENT_PROPERTY_H_
