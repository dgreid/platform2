// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file provides helper classes to allow all new DBus methods
// added to Cryptohome to use the "org.freedesktop.DBus.GLib.Async"
// annotation.  By using the annotation, it makes the calls compatible
// with the new chrome/dbus/dbus.h mechanisms.  It will make
// transitioning any new methods more straightforward and provide a means
// to transition existing methods in an incremental fashion.
//
// To transition a method, it will drop OUT_* types from its
// signature, and replace GError with DBusGMethodInvocation
// allowing the handling function to return immediately.
// Any method playing along will PostTask its work directly
// to the mount_thread_.  Upon completion, the method implementation
// will then need to perform a PostTask-equivalent call back to
// the main/DBus thread to issue its reply -- be it success or
// failure.  CryptohomeEventBase is used as a knock-off PostTask
// and the classes in this file provide the glue.
#ifndef CRYPTOHOME_DBUS_TRANSITION_H_
#define CRYPTOHOME_DBUS_TRANSITION_H_

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback.h>
#include <brillo/glib/dbus.h>

#include "cryptohome/cryptohome_event_source.h"

namespace cryptohome {

extern const char* kDBusErrorReplyEventType;
extern const char* kDBusBlobReplyEventType;
extern const char* kDBusReplyEventType;

struct GErrorDeleter {
  inline void operator()(void* ptr) const {
    g_error_free(static_cast<GError*>(ptr));
  }
};

class DBusErrorReply : public CryptohomeEventBase {
 public:
  // Takes ownership of both pointers.
  DBusErrorReply(DBusGMethodInvocation* context, GError* error);
  virtual ~DBusErrorReply() {}
  virtual const char* GetEventName() const { return kDBusErrorReplyEventType; }
  virtual void Run() { dbus_g_method_return_error(context_, error_.get()); }
  const GError& error() const { return *error_; }

 private:
  // If this event is not serviced, the memory will be leaked.
  DBusGMethodInvocation* context_;
  std::unique_ptr<GError, GErrorDeleter> error_;
};

class DBusBlobReply : public CryptohomeEventBase {
 public:
  // Ownership is taken for both pointers.
  DBusBlobReply(DBusGMethodInvocation* context, std::string* reply);
  virtual ~DBusBlobReply() {}
  virtual const char* GetEventName() const { return kDBusBlobReplyEventType; }
  virtual void Run() {
    brillo::glib::ScopedArray tmp_array(g_array_new(FALSE, FALSE, 1));
    g_array_append_vals(tmp_array.get(), reply_->c_str(), reply_->size());
    dbus_g_method_return(context_, tmp_array.get());
  }
  const std::string& reply() const { return *reply_; }

 private:
  // If this event is not serviced, the memory will be leaked.
  DBusGMethodInvocation* context_;
  std::unique_ptr<std::string> reply_;
};

// This class will allow glib-dbus method calls to be asynchronous.
// Note that this is only a temporary solution until glib-dbus is retired.
class DBusReply : public CryptohomeEventBase {
 public:
  template <typename... Params>
  DBusReply(base::OnceCallback<void(Params...)> cleanup_callback,
            DBusGMethodInvocation* context,
            Params... params) {
    cleanup_callback_ = base::BindOnce(std::move(cleanup_callback), params...);
    send_reply_ = base::BindOnce(
        [](DBusGMethodInvocation* context_to_send, Params... params_to_send) {
          dbus_g_method_return(context_to_send, params_to_send...);
        },
        context, params...);
  }

  // No output argument version
  explicit DBusReply(DBusGMethodInvocation* context) {
    cleanup_callback_ = base::DoNothing::Once();
    send_reply_ = base::BindOnce(
        [](DBusGMethodInvocation* context_to_send) {
          dbus_g_method_return(context_to_send);
        },
        context);
  }

  virtual ~DBusReply() {}

  virtual const char* GetEventName() const { return kDBusReplyEventType; }

  virtual void Run() {
    std::move(send_reply_).Run();
    std::move(cleanup_callback_).Run();
  }

 private:
  base::OnceClosure cleanup_callback_;
  base::OnceClosure send_reply_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_DBUS_TRANSITION_H_
