// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PEERD_SERVICE_PUBLISHER_INTERFACE_H_
#define PEERD_SERVICE_PUBLISHER_INTERFACE_H_

#include <string>

#include <chromeos/errors/error.h>

#include "peerd/service.h"

namespace peerd {

class ServicePublisherInterface {
 public:
  virtual ~ServicePublisherInterface() = default;

  // Should be called with each service we want to advertise over this
  // publisher.  Returns true if adding the service to the publisher
  // succeeded and false on error.  Note that while publishers
  // should make best efforts to inform peers of service changes in
  // a timely fashion, this is not guaranteed.
  virtual bool OnServiceUpdated(chromeos::ErrorPtr* error,
                                const Service& service) = 0;
  // Signals to a service publisher that we have remove a previously
  // added service (added via OnServiceUpdated()).  Returns true if
  // service was successfully removed.  Note that while publishers
  // should make best efforts to inform peers of service removal in
  // a timely fashion, this is not guaranteed.
  virtual bool OnServiceRemoved(chromeos::ErrorPtr* error,
                                const std::string& service_id) = 0;
};

}  // namespace peerd

#endif  // PEERD_SERVICE_PUBLISHER_INTERFACE_H_
