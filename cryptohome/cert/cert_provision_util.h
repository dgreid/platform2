// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Utility classes for cert_provision library.

#ifndef CRYPTOHOME_CERT_CERT_PROVISION_UTIL_H_
#define CRYPTOHOME_CERT_CERT_PROVISION_UTIL_H_

#include <memory>
#include <string>

// This group goes first so the next group can see the needed definitions.
#include <attestation/proto_bindings/interface.pb.h>

#include <attestation-client/attestation/dbus-proxies.h>
#include <brillo/secure_blob.h>

#include "cryptohome/cert_provision.h"

namespace cert_provision {

// Stores operation status.
struct OpResult {
  // Returns true in case of success.
  operator bool() const { return status == Status::Success; }

  Status status;
  std::string message;
};

// Tracks the operation progress and reported errors.
class ProgressReporter {
 public:
  ProgressReporter(const ProgressCallback& callback, int total_steps)
      : callback_(callback), total_steps_(total_steps) {}
  ProgressReporter(const ProgressReporter&) = delete;
  ProgressReporter& operator=(const ProgressReporter&) = delete;

  // Sets the number of steps to take. The number of steps can change
  // mid-flight if an optional path is added or deleted.
  void SetSteps(int total_steps) { total_steps_ = total_steps; }

  // Reports that a new step of the operatuon has started. |message| describes
  // the started step.
  void Step(const std::string& message);

  // Reports an error capturing the |status| code and the corresponding
  // error |message|.
  // Returns |status|.
  Status ReportAndReturn(Status status, const std::string& message) {
    Report(status, total_steps_, total_steps_, message);
    return status;
  }

  // Reports the error captured in |error|.
  // Returns the status from |error|.
  Status ReportAndReturn(const OpResult& error) {
    return ReportAndReturn(error.status, error.message);
  }

  // Reports that the operation has been successfully completed, sets the
  // progress to 100%.
  void Done() { Report(Status::Success, total_steps_, total_steps_, "Done"); }

 private:
  void Report(Status status,
              int cur_step,
              int total_steps,
              const std::string& message);

  const ProgressCallback& callback_;
  int total_steps_;
  int cur_step_ = 0;
};

// Class allowing substituting mocks in place of real-life proxy implementaions.
template <typename T>
class Scoped {
 public:
  explicit Scoped(T* ptr) : holder_(nullptr), ptr_(ptr) {}
  explicit Scoped(std::unique_ptr<T> holder)
      : holder_(holder.release()), ptr_(holder_.get()) {}
  T* operator->() const { return ptr_; }

 private:
  std::unique_ptr<T> holder_;
  T* ptr_;
};

class AttestationProxyFactory {
 public:
  // Creates an object with this class, or defers to the factory set by
  // `DeferToFake()`.
  static std::unique_ptr<org::chromium::AttestationProxyInterface> Create();
  // Defers the job to the `fake_factory`; useful for testing. This call doesn't
  // transfer the ownership of `fake_factory`.
  static void DeferToFake(AttestationProxyFactory* fake_factory);

  // Creates an object used for production.
  virtual std::unique_ptr<org::chromium::AttestationProxyInterface>
  CreateObject();

 protected:
  AttestationProxyFactory() = default;
  virtual ~AttestationProxyFactory() = default;
};

// Returns the |id| generated from the |public_key| for accessing the given
// registered keypair in the keystore. Uses the same algorithm as RegisterKey()
// that picks a unique id for a keypair.
std::string GetKeyID(const brillo::SecureBlob& public_key);

}  // namespace cert_provision

#endif  // CRYPTOHOME_CERT_CERT_PROVISION_UTIL_H_
