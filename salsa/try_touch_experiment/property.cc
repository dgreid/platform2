// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "salsa/try_touch_experiment/property.h"

using base::SplitString;
using base::StringPrintf;
using std::string;
using std::vector;

Property::Property() : is_valid_(false) {}

bool Property::valid() const {
  return is_valid_;
}

Property::Property(string const& property_string) : is_valid_(false) {
  vector<string> parts = SplitString(
      property_string, ":", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);

  name_ = "";
  if (parts.size() == 2) {
    // Check that the property name contains only legal characters
    is_valid_ = (parts[0].length() > 0);
    for (size_t i = 0; i < parts[0].length(); i++) {
      is_valid_ &=
          (base::IsAsciiAlpha(parts[0][i]) || base::IsAsciiDigit(parts[0][i]) ||
           parts[0][i] == ' ' || parts[0][i] == '-');
    }

    if (is_valid_) {
      name_ = parts[0];
      value_ = atof(parts[1].c_str());
      old_value_ = GetCurrentValue();
    }
  }
}

bool Property::Reset() const {
  return SetValue(old_value_);
}

bool Property::Apply() const {
  return SetValue(value_);
}

bool Property::SetValue(double new_value) const {
  // Connect to the X display and device we need
  Display* display = GetX11Display();
  XDevice* device = GetX11TouchpadDevice(display);
  if (!display || !device)
    return false;

  // Find the id of the property we want
  Atom this_prop = XInternAtom(display, name_.c_str(), true);

  // Select the property and see what format it is in
  Atom type;
  int format;
  unsigned long nitems, bytes_after;  // NOLINT(runtime/int)
  unsigned char* data;
  unsigned char* new_data = NULL;
  if (XGetDeviceProperty(display, device, this_prop, 0, 1000, False,
                         AnyPropertyType, &type, &format, &nitems, &bytes_after,
                         &data) == Success) {
    if (nitems == 1) {
      // Re-format the value into the type X is expecting
      int8_t int8_t_val = (int8_t)new_value;
      int16_t int16_t_val = (int16_t)new_value;
      int32_t int32_t_val = (int32_t)new_value;
      float float_val = static_cast<float>(new_value);
      Atom float_atom = XInternAtom(display, "FLOAT", True);
      if (type == XA_INTEGER) {
        switch (format) {
          case 8:
            new_data = (unsigned char*)&int8_t_val;
            break;
          case 16:
            new_data = (unsigned char*)&int16_t_val;
            break;
          case 32:
            new_data = (unsigned char*)&int32_t_val;
            break;
        }
      } else if (float_atom != None && type == float_atom) {
        new_data = (unsigned char*)&float_val;
      }

      // If everything went okay, apply the change
      if (new_data) {
        XChangeDeviceProperty(display, device, this_prop, type, format,
                              PropModeReplace, new_data, 1);
      }
    }
  }

  // Clean up
  XCloseDevice(display, device);
  XCloseDisplay(display);

  return (new_data != NULL);
}

Display* Property::GetX11Display() const {
  return XOpenDisplay(NULL);
}

XDevice* Property::GetX11TouchpadDevice(Display* display) const {
  if (!display)
    return NULL;

  // Get a list of the details of all the X devices available
  int num_device_infos;
  XDeviceInfo* device_infos = XListInputDevices(display, &num_device_infos);

  // Go through the list and find our touchpad
  XDevice* touchpad_device = NULL;
  for (int i = 0; i < num_device_infos; i++) {
    if (device_infos[i].use != IsXExtensionPointer)
      continue;

    XDevice* tmp_device = XOpenDevice(display, device_infos[i].id);
    if (!tmp_device)
      continue;

    // The touchpad will have a "Device Touchpad" Property set to 1.0
    double val = GetPropertyValue(display, tmp_device, "Device Touchpad");
    if (val == 1.0) {
      touchpad_device = tmp_device;
      break;
    } else {
      XCloseDevice(display, tmp_device);
    }
  }

  return touchpad_device;
}

double Property::GetPropertyValue(Display* display,
                                  XDevice* device,
                                  const char* name) const {
  // Find the id of the property we want
  Atom this_prop = XInternAtom(display, name, true);

  // Parse the value of this property
  Atom type;
  int format;
  unsigned long nitems, bytes_after;  // NOLINT(runtime/int)
  unsigned char* data;
  double val = -1;
  if (XGetDeviceProperty(display, device, this_prop, 0, 1000, False,
                         AnyPropertyType, &type, &format, &nitems, &bytes_after,
                         &data) == Success) {
    if (nitems == 1) {
      Atom float_atom = XInternAtom(display, "FLOAT", True);
      if (type == XA_INTEGER) {
        switch (format) {
          case 8:
            val = *(reinterpret_cast<const int8_t*>(data));
            break;
          case 16:
            val = *(reinterpret_cast<const int16_t*>(data));
            break;
          case 32:
            val = *(reinterpret_cast<const int32_t*>(data));
            break;
        }
      } else if (float_atom != None && type == float_atom) {
        val = *(reinterpret_cast<const float*>(data));
      }
    }
  }

  return val;
}

double Property::GetCurrentValue() const {
  Display* display = GetX11Display();
  XDevice* device = GetX11TouchpadDevice(display);
  if (!display || !device)
    return -1.0;

  double val = GetPropertyValue(display, device, name_.c_str());

  XCloseDevice(display, device);
  XCloseDisplay(display);
  return val;
}
