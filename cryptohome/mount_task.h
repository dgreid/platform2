// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// MountTask - The basis for asynchronous API work items.  It inherits from
// Task, which allows it to be called on an event thread.  Subclasses define the
// specific asychronous request.
// Asynchronous tasks in cryptohome are serialized calls on a single worker
// thread separate from the dbus main event loop.  The synchronous versions of
// these APIs are also done on this worker thread, with the main thread waiting
// on a completion event to return.  This allows all of these calls to be
// serialized, as we use a common mount point for cryptohome.
//
// Also defined here is the MountTaskResult, which has the task result
// information, and the MountTaskObserver, which is called when a task is
// completed.
//
// Notifications can happen either by setting the completion event or providing
// a MountTaskObserver.  The former is used in Service (see service.cc) when
// faking synchronous versions of these tasks, and the latter is used in the
// asynchronous versions.

#ifndef CRYPTOHOME_MOUNT_TASK_H_
#define CRYPTOHOME_MOUNT_TASK_H_

#include <memory>

#include <base/atomic_sequence_num.h>
#include <base/atomicops.h>
#include <base/memory/ref_counted.h>
#include <base/observer_list_types.h>
#include <base/synchronization/atomic_flag.h>
#include <base/synchronization/waitable_event.h>
#include <base/threading/thread.h>
#include <brillo/process/process.h>
#include <brillo/secure_blob.h>

#include "cryptohome/credentials.h"
#include "cryptohome/cryptohome_event_source.h"
#include "cryptohome/mount.h"
#include "cryptohome/pkcs11_init.h"

namespace cryptohome {

extern const char* kMountTaskResultEventType;
extern const char* kPkcs11InitResultEventType;

class InstallAttributes;

class MountTaskResult : public CryptohomeEventBase {
 public:
  using SecureBlob = brillo::SecureBlob;

  MountTaskResult()
      : sequence_id_(-1),
        return_status_(false),
        return_code_(MOUNT_ERROR_NONE),
        event_name_(kMountTaskResultEventType),
        mount_(nullptr),
        pkcs11_init_(false),
        guest_(false) {}

  // Constructor which sets an alternative event name. Useful
  // for using MountTaskResult for other event types.
  explicit MountTaskResult(const char* event_name)
      : sequence_id_(-1),
        return_status_(false),
        return_code_(MOUNT_ERROR_NONE),
        event_name_(event_name),
        mount_(nullptr),
        pkcs11_init_(false),
        guest_(false) {}

  // Copy constructor is necessary for inserting MountTaskResult into the events
  // vector in CryptohomeEventSource.
  MountTaskResult(const MountTaskResult& rhs)
      : sequence_id_(rhs.sequence_id_),
        return_status_(rhs.return_status_),
        return_code_(rhs.return_code_),
        event_name_(rhs.event_name_),
        mount_(rhs.mount_),
        pkcs11_init_(rhs.pkcs11_init_),
        guest_(rhs.guest_) {
    if (rhs.return_data())
      set_return_data(*rhs.return_data());
  }

  virtual ~MountTaskResult() {}

  // Get the asynchronous task id
  int sequence_id() const { return sequence_id_; }

  // Set the asynchronous task id
  void set_sequence_id(int value) { sequence_id_ = value; }

  // Get the status of the call
  bool return_status() const { return return_status_; }

  // Set the status of the call
  void set_return_status(bool value) { return_status_ = value; }

  // Get the MountError for applicable calls (Mount, MountGuest)
  MountError return_code() const { return return_code_; }

  // Set the MountError for applicable calls (Mount, MountGuest)
  void set_return_code(MountError value) { return_code_ = value; }

  scoped_refptr<Mount> mount() const { return mount_; }

  void set_mount(const scoped_refptr<Mount>& value) { mount_ = value; }

  bool pkcs11_init() const { return pkcs11_init_; }

  void set_pkcs11_init(bool value) { pkcs11_init_ = value; }

  bool guest() const { return guest_; }

  void set_guest(bool value) { guest_ = value; }

  const SecureBlob* return_data() const { return return_data_.get(); }

  void set_return_data(const SecureBlob& data) {
    return_data_.reset(new SecureBlob(data.begin(), data.end()));
  }

  MountTaskResult& operator=(const MountTaskResult& rhs) {
    sequence_id_ = rhs.sequence_id_;
    return_status_ = rhs.return_status_;
    return_code_ = rhs.return_code_;
    return_data_.reset();
    if (rhs.return_data())
      set_return_data(*rhs.return_data());
    event_name_ = rhs.event_name_;
    mount_ = rhs.mount_;
    pkcs11_init_ = rhs.pkcs11_init_;
    guest_ = rhs.guest_;
    return *this;
  }

  virtual const char* GetEventName() const { return event_name_; }

 private:
  int sequence_id_;
  bool return_status_;
  MountError return_code_;
  std::unique_ptr<SecureBlob> return_data_;
  const char* event_name_;
  scoped_refptr<Mount> mount_;
  bool pkcs11_init_;
  bool guest_;
};

class MountTaskObserver : public base::CheckedObserver {
 public:
  MountTaskObserver() {}
  virtual ~MountTaskObserver() {}

  // Called by the MountTask when the task is complete. If this returns true,
  // the MountTaskObserver will be freed by the MountTask.
  virtual bool MountTaskObserve(const MountTaskResult& result) = 0;
};

class MountTask : public base::RefCountedThreadSafe<MountTask> {
 public:
  MountTask(MountTaskObserver* observer,
            Mount* mount,
            const Credentials& credentials,
            int sequence_id);
  MountTask(MountTaskObserver* observer, Mount* mount, int sequence_id);
  virtual ~MountTask();

  // Run is called by the worker thread when this task is being processed
  virtual void Run() { Notify(); }

  // Allow cancellation to be sent from the main thread. This must be checked
  // in each inherited Run().
  virtual void Cancel() { cancel_flag_.Set(); }

  // Indicate if cancellation was requested.
  virtual bool IsCanceled() { return cancel_flag_.IsSet(); }

  // Gets the asynchronous call id of this task
  int sequence_id() { return sequence_id_; }

  // Returns the mount this task is for.
  // TODO(wad) Figure out a better way. Queue per Mount?
  scoped_refptr<Mount> mount() { return mount_; }

  void set_mount(const scoped_refptr<Mount>& mount) {
    mount_ = mount;
    result_->set_mount(mount_);
  }

  void set_credentials(const Credentials& credentials) {
    credentials_ = credentials;
  }

  // Gets the MountTaskResult for this task
  MountTaskResult* result() { return result_; }

  // Sets the MountTaskResult for this task
  void set_result(MountTaskResult* result) {
    result_ = result;
    result_->set_sequence_id(sequence_id_);
  }

  // Sets the event to be signaled when the event is completed
  void set_complete_event(base::WaitableEvent* value) {
    complete_event_ = value;
  }

 protected:
  // Notify implements the default behavior when this task is complete
  void Notify();

  // The Mount instance that does the actual work
  scoped_refptr<Mount> mount_;

  // The Credentials associated with this task
  Credentials credentials_;

  // The asychronous call id for this task
  int sequence_id_;

  // Checked before all Run() calls to cancel.
  base::AtomicFlag cancel_flag_;

 private:
  // Signal will call Signal on the completion event if it is set
  void Signal();

  // The MountTaskObserver to be notified when this task is complete
  MountTaskObserver* observer_;

  // The default instance of MountTaskResult to use if it is not set by the
  // caller
  std::unique_ptr<MountTaskResult> default_result_;

  // The actual instance of MountTaskResult to use
  MountTaskResult* result_;

  // The completion event to signal when this task is complete
  base::WaitableEvent* complete_event_;

  DISALLOW_COPY_AND_ASSIGN(MountTask);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOUNT_TASK_H_
