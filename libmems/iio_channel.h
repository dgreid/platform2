// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBMEMS_IIO_CHANNEL_H_
#define LIBMEMS_IIO_CHANNEL_H_

#include <string>

#include <base/macros.h>
#include <base/optional.h>

#include "libmems/export.h"

namespace libmems {

class LIBMEMS_EXPORT IioDevice;

// The IioChannel represents a channel on an IIO device, for example the
// X axis on an accelerometer.
// Channels can be enabled and/or disabled via this class.
class IioChannel {
 public:
  virtual ~IioChannel() = default;

  // Returns the unique ID of this channel.
  virtual const char* GetId() const = 0;

  // Returns true if this channel is enabled.
  virtual bool IsEnabled() const = 0;

  // Sets this channel's enabled status to |en|.
  // Returns false on failure.
  virtual bool SetEnabled(bool en) = 0;

  // Sets the channel's enabled status to |en|,
  // and returns true if the channel's enabled status matches
  // what was set, false otherwise.
  bool SetEnabledAndCheck(bool en) {
    SetEnabled(en);
    return en == IsEnabled();
  }

  // Reads the |name| attribute of this channel and returns the value
  // as a string. It will return base::nullopt if the attribute cannot
  // be read.
  virtual base::Optional<std::string> ReadStringAttribute(
      const std::string& name) const = 0;

  // Reads the |name| attribute of this channel and returns the value
  // as a signed number. It will return base::nullopt if the attribute
  // cannot be read or is not a valid number.
  virtual base::Optional<int64_t> ReadNumberAttribute(
      const std::string& name) const = 0;

  // Reads the |name| attribute of this device and returns the value
  // as a double precision floating point. It will return base::nullopt
  // if the attribute cannot be read or is not a valid number.
  virtual base::Optional<double> ReadDoubleAttribute(
      const std::string& name) const = 0;

  // Writes the string |value| to the attribute |name| of this channel. Returns
  // false if an error occurs.
  virtual bool WriteStringAttribute(const std::string& name,
                                    const std::string& value) = 0;

  // Writes the number |value| to the attribute |name| of this channel. Returns
  // false if an error occurs.
  virtual bool WriteNumberAttribute(const std::string& name, int64_t value) = 0;

  // Writes the floating point |value| to the attribute |name| of this channel.
  // Returns false if an error occurs.
  virtual bool WriteDoubleAttribute(const std::string& name, double value) = 0;

 protected:
  IioChannel() = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(IioChannel);
};

}  // namespace libmems

#endif  // LIBMEMS_IIO_CHANNEL_H_
