// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MIST_USB_MANAGER_H_
#define MIST_USB_MANAGER_H_

#include <stdint.h>

#include <memory>
#include <vector>

#include <base/macros.h>
#include <base/memory/weak_ptr.h>

#include "mist/usb_error.h"

struct libusb_context;

namespace mist {

class EventDispatcher;
class UsbDevice;

// A USB manager for managing a USB session created by libusb 1.0.
class UsbManager {
 public:
  // Constructs a UsbManager object by taking a raw pointer to an
  // EventDispatcher as |dispatcher|. The ownership of |dispatcher| is not
  // transferred, and thus it should outlive this object.
  explicit UsbManager(EventDispatcher* dispatcher);
  UsbManager(const UsbManager&) = delete;
  UsbManager& operator=(const UsbManager&) = delete;

  ~UsbManager();

  // Initializes a USB session via libusb. Returns true on success.
  bool Initialize();

  // Sets the debug level of libusb to |level|.
  void SetDebugLevel(int level);

  // Gets the USB device that is currently connected to the bus numbered
  // |bus_number|, at the address |device_address| on the bus, with its vendor
  // ID equal to |vendor_id|, and its product ID equal to |product_id|. Returns
  // NULL if no such device is found. The returned UsbDevice object becomes
  // invalid, and thus should not be held, beyond the lifetime of this object.
  std::unique_ptr<UsbDevice> GetDevice(uint8_t bus_number,
                                       uint8_t device_address,
                                       uint16_t vendor_id,
                                       uint16_t product_id);

  // Gets the list of USB devices currently attached to the system. Returns true
  // on success. |devices| is always cleared before being updated. The returned
  // UsbDevice objects become invalid, and thus should not be held, beyond the
  // lifetime of this object.
  bool GetDevices(std::vector<std::unique_ptr<UsbDevice>>* devices);

  const UsbError& error() const { return error_; }

 private:
  static void OnPollFileDescriptorAdded(int file_descriptor,
                                        short events,  // NOLINT
                                        void* user_data);
  static void OnPollFileDescriptorRemoved(int file_descriptor, void* user_data);

  // Starts watching the file descriptors for libusb events. Returns true on
  // success.
  bool StartWatchingPollFileDescriptors();

  // Handles libusb events in non-blocking mode.
  void HandleEventsNonBlocking();

  EventDispatcher* const dispatcher_;
  libusb_context* context_;
  UsbError error_;

  base::WeakPtrFactory<UsbManager> weak_factory_{this};
};

}  // namespace mist

#endif  // MIST_USB_MANAGER_H_
