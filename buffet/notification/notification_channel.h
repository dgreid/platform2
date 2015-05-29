// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_NOTIFICATION_NOTIFICATION_CHANNEL_H_
#define BUFFET_NOTIFICATION_NOTIFICATION_CHANNEL_H_

#include <string>

namespace base {
class DictionaryValue;
}  // namespace base

namespace buffet {

class NotificationDelegate;

class NotificationChannel {
 public:
  virtual ~NotificationChannel() = default;

  virtual std::string GetName() const = 0;
  virtual bool IsConnected() const = 0;
  virtual void AddChannelParameters(base::DictionaryValue* channel_json) = 0;

  virtual void Start(NotificationDelegate* delegate) = 0;
  virtual void Stop() = 0;
};

}  // namespace buffet

#endif  // BUFFET_NOTIFICATION_NOTIFICATION_CHANNEL_H_
