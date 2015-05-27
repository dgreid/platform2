// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/notification/pull_channel.h"

#include <base/bind.h>

#include "buffet/notification/notification_delegate.h"

namespace buffet {

PullChannel::PullChannel(
    base::TimeDelta pull_interval,
    const scoped_refptr<base::SingleThreadTaskRunner>& task_runner)
    : pull_interval_{pull_interval},
      timer_{true, true} {
  timer_.SetTaskRunner(task_runner);
}

std::string PullChannel::GetName() const {
  return "pull";
}

void PullChannel::AddChannelParameters(base::DictionaryValue* channel_json) {
  // No extra parameters needed for "Pull" channel.
}

void PullChannel::Start(NotificationDelegate* delegate) {
  CHECK(delegate);
  delegate_ = delegate;
  timer_.Start(FROM_HERE, pull_interval_,
               base::Bind(&PullChannel::OnTimer,
                          weak_ptr_factory_.GetWeakPtr()));
}

void PullChannel::Stop() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  timer_.Stop();
}

void PullChannel::UpdatePullInterval(base::TimeDelta pull_interval) {
  timer_.Stop();
  pull_interval_ = pull_interval;
  Start(delegate_);
}

void PullChannel::OnTimer() {
  base::DictionaryValue empty_dict;
  delegate_->OnCommandCreated(empty_dict);
}

}  // namespace buffet
