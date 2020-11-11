// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_SYSTEM_PROPERTY_H_
#define RUNTIME_PROBE_SYSTEM_PROPERTY_H_

#include <string>

// Light-weight interface to system property with std::string semantics.
class SystemProperty {
 public:
  virtual ~SystemProperty() = default;

  // Reads a system property integer into |value_out|.
  //
  // Returns true on success
  virtual bool GetInt(const std::string& key, int* value_out) = 0;

  // Sets the system property integer |name| to |value|.
  //
  // Returns true on success.
  virtual bool SetInt(const std::string& key, int value) = 0;

  // Reads a system property string and stores it in |value_out|.
  //
  // Returns true on success.
  virtual bool GetString(const std::string& key, std::string* value_out) = 0;

  // Sets a system property string.
  //
  // The maximum length of the value accepted depends on the specific property.
  //
  // Returns true on success.
  virtual bool SetString(const std::string& key, const std::string& value) = 0;
};

#endif  // RUNTIME_PROBE_SYSTEM_PROPERTY_H_
