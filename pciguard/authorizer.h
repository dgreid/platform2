// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PCIGUARD_AUTHORIZER_H_
#define PCIGUARD_AUTHORIZER_H_

#include <base/files/file_path.h>
#include <queue>

namespace pciguard {

// A class for handling all authorization jobs. It maintains a queue of jobs
// and forks a thread to process that queue (that can be killed when needed)
class Authorizer {
 public:
  Authorizer();
  Authorizer(const Authorizer&) = delete;
  Authorizer& operator=(const Authorizer&) = delete;
  ~Authorizer();

  enum JobType {
    AUTHORIZE_ALL_DEVICES,
    AUTHORIZE_1_DEVICE,
  };

  void SubmitJob(JobType type, base::FilePath path);

 private:
  struct Job {
    JobType type_;
    base::FilePath syspath_;  // syspath for AUTHORIZE_1_DEVICE
  };

  std::queue<Job> queue_;  // Queue of authorization jobs
  pthread_mutex_t mutex_;  // To protect the queue
  pthread_cond_t job_available_;

  pthread_t authorizer_thread_;
  static void* AuthorizerThread(void* ptr);
  bool GetNextJob(Job* job);
};

}  // namespace pciguard

#endif  // PCIGUARD_AUTHORIZER_H_
