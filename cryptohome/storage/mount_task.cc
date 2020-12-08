// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/mount_task.h"

#include "cryptohome/install_attributes.h"

namespace cryptohome {

const char* kMountTaskResultEventType = "MountTaskResult";
const char* kPkcs11InitResultEventType = "Pkcs11InitResult";

MountTask::MountTask(MountTaskObserver* observer,
                     Mount* mount,
                     const Credentials& credentials,
                     int sequence_id)
    : mount_(mount),
      credentials_(credentials),
      sequence_id_(sequence_id),
      observer_(observer),
      default_result_(new MountTaskResult),
      result_(default_result_.get()),
      complete_event_(NULL) {
  result_->set_sequence_id(sequence_id_);
}

MountTask::MountTask(MountTaskObserver* observer, Mount* mount, int sequence_id)
    : mount_(mount),
      sequence_id_(sequence_id),
      observer_(observer),
      default_result_(new MountTaskResult),
      result_(default_result_.get()),
      complete_event_(NULL) {
  result_->set_sequence_id(sequence_id_);
}

MountTask::~MountTask() {}

void MountTask::Notify() {
  if (observer_) {
    if (observer_->MountTaskObserve(*result_)) {
      delete observer_;
      observer_ = NULL;
    }
  }
  Signal();
}

void MountTask::Signal() {
  if (complete_event_) {
    complete_event_->Signal();
  }
}

}  // namespace cryptohome
