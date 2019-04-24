// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/bind.h>
#include <base/threading/thread_task_runner_handle.h>

#include "cryptohome/tpm.h"
#include "cryptohome/user_oldest_activity_timestamp_cache.h"
#include "cryptohome/userdataauth.h"

namespace cryptohome {

const char kMountThreadName[] = "MountThread";

UserDataAuth::UserDataAuth()
    : origin_task_runner_(base::ThreadTaskRunnerHandle::Get()),
      origin_thread_id_(base::PlatformThread::CurrentId()),
      mount_thread_(kMountThreadName),
      system_salt_(),
      default_platform_(new Platform()),
      platform_(default_platform_.get()),
      default_crypto_(new Crypto(platform_)),
      crypto_(default_crypto_.get()),
      default_homedirs_(new cryptohome::HomeDirs()),
      homedirs_(default_homedirs_.get()),
      user_timestamp_cache_(new UserOldestActivityTimestampCache()) {}

UserDataAuth::~UserDataAuth() {
  mount_thread_.Stop();
}

bool UserDataAuth::Initialize() {
  if (!tpm_) {
    tpm_ = Tpm::GetSingleton();
  }

  if (!tpm_init_) {
    default_tpm_init_.reset(new TpmInit(tpm_, platform_));
    tpm_init_ = default_tpm_init_.get();
  }

  crypto_->set_use_tpm(true);
  if (!crypto_->Init(tpm_init_)) {
    return false;
  }

  if (!homedirs_->Init(platform_, crypto_, user_timestamp_cache_.get())) {
    return false;
  }

  if (!homedirs_->GetSystemSalt(&system_salt_)) {
    return false;
  }

  base::Thread::Options options;
  options.message_loop_type = base::MessageLoop::TYPE_IO;
  mount_thread_.StartWithOptions(options);

  if (tpm_ && tpm_init_) {
    // Ownerhip is not handled by tpm_init_ in this class, so we don't care
    // about the ownership callback
    tpm_init_->Init(base::Bind([](bool, bool) {}));
  }

  return true;
}

}  // namespace cryptohome
