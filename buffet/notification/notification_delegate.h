// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_NOTIFICATION_NOTIFICATION_DELEGATE_H_
#define BUFFET_NOTIFICATION_NOTIFICATION_DELEGATE_H_

#include <memory>
#include <string>

#include <base/values.h>

namespace buffet {

class NotificationDelegate {
 public:
  virtual void OnConnected(const std::string& channel_name) = 0;
  virtual void OnDisconnected() = 0;
  virtual void OnPermanentFailure() = 0;
  // Called when a new command is sent via the notification channel.
  virtual void OnCommandCreated(const base::DictionaryValue& command) = 0;

 protected:
  virtual ~NotificationDelegate() = default;
};

}  // namespace buffet

#endif  // BUFFET_NOTIFICATION_NOTIFICATION_DELEGATE_H_
