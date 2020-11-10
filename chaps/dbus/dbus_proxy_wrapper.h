// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_DBUS_DBUS_PROXY_WRAPPER_H_
#define CHAPS_DBUS_DBUS_PROXY_WRAPPER_H_

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/callback.h>
#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <base/single_thread_task_runner.h>
#include <base/synchronization/waitable_event.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <dbus/object_proxy.h>

#include "chaps/dbus/scoped_bus.h"
#include "chaps/dbus_bindings/constants.h"

namespace chaps {

// libchrome's D-Bus bindings have a lot of threading restrictions which
// force us to create the D-Bus objects and call them from the same
// sequence every time. Because of this, we attempt to serialize all D-Bus
// calls and constructions to one task runner. However, our API is limited
// by the PKCS#11 interface, so we can't expose this asynchrony at a higher
// level.
//
// This stuff below is tooling to try and hide this thread-jumping as much
// as possible.

using OnObjectProxyConstructedCallback = base::Callback<void(
    bool, chaps::ScopedBus, scoped_refptr<dbus::ObjectProxy>)>;

// Wrapper around the dbus::ObjectProxy which sets up a default
// method timeout and runs D-Bus calls on the given |task_runner|.
class DBusProxyWrapper : public base::RefCountedThreadSafe<DBusProxyWrapper> {
 public:
  // 5 minutes, since some TPM operations can take a while.
  static constexpr int kDBusTimeoutMs = 5 * 60 * 1000;

  DBusProxyWrapper(scoped_refptr<base::SingleThreadTaskRunner> task_runner,
                   ScopedBus bus,
                   scoped_refptr<dbus::ObjectProxy> dbus_proxy)
      : task_runner_(task_runner),
        bus_(std::move(bus)),
        dbus_proxy_(dbus_proxy) {}
  DBusProxyWrapper(const DBusProxyWrapper&) = delete;
  DBusProxyWrapper& operator=(const DBusProxyWrapper&) = delete;

  template <typename... Args>
  std::unique_ptr<dbus::Response> CallMethod(const std::string& method_name,
                                             const Args&... args) {
    DCHECK(!task_runner_->BelongsToCurrentThread());

    std::unique_ptr<dbus::Response> resp;
    base::WaitableEvent completion_event(
        base::WaitableEvent::ResetPolicy::AUTOMATIC,
        base::WaitableEvent::InitialState::NOT_SIGNALED);
    task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&DBusProxyWrapper::CallMethodOnTaskRunner<Args...>, this,
                   &resp, &completion_event, method_name, args...));
    completion_event.Wait();
    return resp;
  }

 private:
  // Hide the destructor so we can't delete this while a task might have a
  // reference to it.
  friend class base::RefCountedThreadSafe<DBusProxyWrapper>;
  virtual ~DBusProxyWrapper() {}

  template <typename... Args>
  void CallMethodOnTaskRunner(std::unique_ptr<dbus::Response>* out_resp,
                              base::WaitableEvent* completion_event,
                              const std::string& method_name,
                              const Args&... args) {
    DCHECK(task_runner_->BelongsToCurrentThread());

    *out_resp = brillo::dbus_utils::CallMethodAndBlockWithTimeout(
        kDBusTimeoutMs, dbus_proxy_.get(), kChapsInterface, method_name,
        nullptr, args...);
    completion_event->Signal();
  }

  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  ScopedBus bus_;
  scoped_refptr<dbus::ObjectProxy> dbus_proxy_;
};

// To ensure we don't block forever waiting for the chapsd service to
// show up.
class ProxyWrapperConstructionTask
    : public base::RefCountedThreadSafe<ProxyWrapperConstructionTask> {
 public:
  ProxyWrapperConstructionTask();
  ProxyWrapperConstructionTask(const ProxyWrapperConstructionTask&) = delete;
  ProxyWrapperConstructionTask& operator=(const ProxyWrapperConstructionTask&) =
      delete;

  scoped_refptr<DBusProxyWrapper> ConstructProxyWrapper(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner);

  void set_construction_callback_for_testing(
      base::Callback<void(const OnObjectProxyConstructedCallback&)> callback) {
    construction_callback_ = callback;
  }

 private:
  // Hide the destructor so we can't delete this while a task might have a
  // reference to it.
  friend class base::RefCountedThreadSafe<ProxyWrapperConstructionTask>;
  virtual ~ProxyWrapperConstructionTask() {}

  void SetObjectProxyCallback(bool success,
                              ScopedBus bus,
                              scoped_refptr<dbus::ObjectProxy> object_proxy);

  // Posted to a task runner to get an ObjectProxy.
  base::Callback<void(const OnObjectProxyConstructedCallback&)>
      construction_callback_;
  // Signaled when construction via |construction_callback_| is done.
  base::WaitableEvent completion_event_;

  // Bus and object proxy passed back from |construction_callback_|.
  bool success_;
  ScopedBus bus_;
  scoped_refptr<dbus::ObjectProxy> object_proxy_;
};

}  // namespace chaps

#endif  // CHAPS_DBUS_DBUS_PROXY_WRAPPER_H_
