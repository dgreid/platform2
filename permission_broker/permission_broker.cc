// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/permission_broker.h"

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <gflags/gflags.h>
#include <glib.h>
#include <grp.h>
#include <libudev.h>
#include <poll.h>
#include <stdint.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "base/logging.h"
#include "base/stl_util.h"
#include "base/strings/stringprintf.h"
#include "chromeos/dbus/dbus.h"
#include "chromeos/dbus/service_constants.h"
#include "permission_broker/rule.h"

DEFINE_string(access_group, "", "The group which has resource access granted "
              "to it. Must not be empty.");
DEFINE_int32(poll_interval, 100, "The interval at which to poll for udev "
             "events");
DEFINE_string(udev_run_path, "/run/udev", "The path to udev's run path");

using std::string;
using std::vector;

namespace permission_broker {

PermissionBroker::PermissionBroker(const gid_t access_group)
    : udev_(udev_new()), access_group_(access_group) {}

PermissionBroker::PermissionBroker() : udev_(udev_new()) {
  CHECK(udev_) << "Could not create udev context, is sysfs mounted?";
  CHECK(!FLAGS_access_group.empty()) << "You must specify a group name via the "
                                     << "--access_group flag.";

  struct group group_buffer;
  struct group *access_group = NULL;
  char buffer[256];
  getgrnam_r(FLAGS_access_group.c_str(), &group_buffer, buffer,
             sizeof(buffer), &access_group);
  CHECK(access_group) << "Could not resolve \"" << FLAGS_access_group << "\" "
                      << "to a named group.";
  access_group_ = access_group->gr_gid;
}

PermissionBroker::~PermissionBroker() {
  STLDeleteContainerPointers(rules_.begin(), rules_.end());
  udev_unref(udev_);
}

void PermissionBroker::Run() {
  DBusConnection *const connection = dbus_g_connection_get_connection(
      chromeos::dbus::GetSystemBusConnection().g_connection());
  CHECK(connection) << "Cannot connect to system bus";

  DBusError error;
  dbus_error_init(&error);
  dbus_bus_request_name(connection,
      permission_broker::kPermissionBrokerServiceName, 0, &error);
  if (dbus_error_is_set(&error)) {
    LOG(FATAL) << "Failed to register "
               << permission_broker::kPermissionBrokerServiceName
               << ": " << error.message;
    dbus_error_free(&error);
    return;
  }

  DBusObjectPathVTable vtable;
  memset(&vtable, 0, sizeof(vtable));
  vtable.message_function = &MainDBusMethodHandler;

  const dbus_bool_t registration_result = dbus_connection_register_object_path(
      connection, kPermissionBrokerServicePath, &vtable, this);
  CHECK(registration_result) << "Could not register object path";

  GMainLoop *const loop = g_main_loop_new(NULL, false);
  g_main_loop_run(loop);
}

void PermissionBroker::AddUsbException(const uint16_t vendor_id,
                                       const uint16_t product_id) {
  usb_exceptions_.insert(std::make_pair(vendor_id, product_id));
}

void PermissionBroker::AddRule(Rule *rule) {
  CHECK(rule) << "Cannot add NULL as a rule.";
  rules_.push_back(rule);
}

bool PermissionBroker::ProcessPath(const string &path,
                                   int interface_id) {
  WaitForEmptyUdevQueue();

  LOG(INFO) << "ProcessPath(" << path << ")";
  Rule::Result result = Rule::IGNORE;
  for (unsigned int i = 0; i < rules_.size(); ++i) {
    const Rule::Result rule_result = rules_[i]->Process(path,
                                                        interface_id);
    LOG(INFO) << "  " << rules_[i]->name() << ": "
              << Rule::ResultToString(rule_result);
    if (rule_result == Rule::DENY) {
      result = Rule::DENY;
      break;
    } else if (rule_result == Rule::ALLOW) {
      result = Rule::ALLOW;
    }
  }
  LOG(INFO) << "Verdict for " << path << ": " << Rule::ResultToString(result);

  if (result == Rule::ALLOW)
    return GrantAccess(path);
  return false;
}

bool PermissionBroker::GrantAccess(const std::string &path) {
  if (chown(path.c_str(), -1, access_group_)) {
    LOG(INFO) << "Could not grant access to " << path;
    return false;
  }
  return true;
}

bool PermissionBroker::ExpandUsbIdentifiersToPaths(
    const uint16_t vendor_id,
    const uint16_t product_id,
    vector<string> *paths) {
  CHECK(paths) << "Cannot invoke ExpandUsbIdentifiersToPaths with NULL paths.";
  paths->clear();

  struct udev_enumerate *const enumerate = udev_enumerate_new(udev_);
  udev_enumerate_add_match_is_initialized(enumerate);
  udev_enumerate_add_match_subsystem(enumerate, "usb");
  udev_enumerate_add_match_sysattr(
      enumerate, "idVendor", base::StringPrintf("%.4x", vendor_id).c_str());
  udev_enumerate_add_match_sysattr(
      enumerate, "idProduct", base::StringPrintf("%.4x", product_id).c_str());
  udev_enumerate_scan_devices(enumerate);

  struct udev_list_entry *entry = NULL;
  udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumerate)) {
    const char *const path = udev_list_entry_get_name(entry);
    struct udev_device *device = udev_device_new_from_syspath(udev_, path);
    paths->push_back(udev_device_get_devnode(device));
    udev_device_unref(device);
  }

  udev_enumerate_unref(enumerate);
  return !paths->empty();
}

void PermissionBroker::WaitForEmptyUdevQueue() {
  struct udev_queue *queue = udev_queue_new(udev_);
  if (udev_queue_get_queue_is_empty(queue)) {
    udev_queue_unref(queue);
    return;
  }

  struct pollfd udev_poll;
  memset(&udev_poll, 0, sizeof(udev_poll));
  udev_poll.fd = inotify_init();
  udev_poll.events = POLLIN;

  int watch = inotify_add_watch(udev_poll.fd, FLAGS_udev_run_path.c_str(),
                                IN_MOVED_TO);
  CHECK_NE(watch, -1) << "Could not add watch for udev run path.";

  while (!udev_queue_get_queue_is_empty(queue)) {
    if (poll(&udev_poll, 1, FLAGS_poll_interval) > 0) {
      char buffer[sizeof(struct inotify_event)];
      const ssize_t result = read(udev_poll.fd, buffer, sizeof(buffer));
      if (result < 0)
        LOG(WARNING) << "Did not read complete udev event.";
    }
  }
  udev_queue_unref(queue);
  close(udev_poll.fd);
}

DBusHandlerResult PermissionBroker::MainDBusMethodHandler(
    DBusConnection *connection, DBusMessage *message, void *data) {
  CHECK(connection) << "Missing connection.";
  CHECK(message) << "Missing method.";
  CHECK(data) << "Missing pointer to broker.";

  if (dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_METHOD_CALL)
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  string interface(dbus_message_get_interface(message));
  if (interface != kPermissionBrokerInterface)
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  DBusMessage *reply = NULL;
  string member(dbus_message_get_member(message));
  PermissionBroker *const broker = static_cast<PermissionBroker*>(data);
  if (member == kRequestPathAccess)
    reply = broker->HandleRequestPathAccessMethod(message);
  else if (member == kRequestUsbAccess)
    reply = broker->HandleRequestUsbAccessMethod(message);
  else
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  CHECK(dbus_connection_send(connection, reply, NULL));
  dbus_message_unref(reply);

  return DBUS_HANDLER_RESULT_HANDLED;
}

DBusMessage *PermissionBroker::HandleRequestPathAccessMethod(
    DBusMessage *message) {
  DBusMessage *reply = dbus_message_new_method_return(message);
  CHECK(reply) << "Could not allocate reply message for method call";

  dbus_bool_t success = false;
  char *path = NULL;
  int interface_id = Rule::ANY_INTERFACE;

  DBusError error;
  dbus_error_init(&error);
  if (!dbus_message_get_args(message, &error,
                             DBUS_TYPE_STRING, &path,
                             DBUS_TYPE_INT32, &interface_id,
                             DBUS_TYPE_INVALID)) {
    interface_id = Rule::ANY_INTERFACE;
    if (!dbus_message_get_args(message, &error,
                               DBUS_TYPE_STRING, &path,
                               DBUS_TYPE_INVALID)) {
      LOG(WARNING) << "Error parsing arguments: " << error.message;
      dbus_error_free(&error);

      dbus_message_append_args(reply,
                               DBUS_TYPE_BOOLEAN, &success,
                               DBUS_TYPE_INVALID);
      return reply;
    }
  }

  success = ProcessPath(path, interface_id);
  dbus_message_append_args(reply,
                           DBUS_TYPE_BOOLEAN, &success,
                           DBUS_TYPE_INVALID);
  return reply;
}

DBusMessage *PermissionBroker::HandleRequestUsbAccessMethod(
    DBusMessage *message) {
  DBusMessage *reply = dbus_message_new_method_return(message);
  CHECK(reply) << "Could not allocate reply message for method call";

  dbus_bool_t success = false;
  uint16_t vendor_id;
  uint16_t product_id;
  int interface_id = Rule::ANY_INTERFACE;

  DBusError error;
  dbus_error_init(&error);
  if (!dbus_message_get_args(message, &error,
                             DBUS_TYPE_UINT16, &vendor_id,
                             DBUS_TYPE_UINT16, &product_id,
                             DBUS_TYPE_INT32, &interface_id,
                             DBUS_TYPE_INVALID)) {
    interface_id = Rule::ANY_INTERFACE;
    if (!dbus_message_get_args(message, &error,
                               DBUS_TYPE_UINT16, &vendor_id,
                               DBUS_TYPE_UINT16, &product_id,
                               DBUS_TYPE_INVALID)) {
      LOG(WARNING) << "Error parsing arguments: " << error.message;
      dbus_error_free(&error);

      dbus_message_append_args(reply,
                               DBUS_TYPE_BOOLEAN, &success,
                               DBUS_TYPE_INVALID);
      return reply;
    }
  }

  if (ContainsKey(usb_exceptions_, std::make_pair(vendor_id, product_id))) {
    success = true;
  } else {
    vector<string> paths;
    if (ExpandUsbIdentifiersToPaths(vendor_id,
                                    product_id,
                                    &paths)) {
      success = true;
      for (unsigned int i = 0; i < paths.size(); ++i)
        success &= ProcessPath(paths[i], interface_id);
    } else {
      LOG(INFO) << "Could not expand (" << vendor_id << ", " << product_id
                << ") to a list of device nodes.";
    }
  }

  dbus_message_append_args(reply,
                           DBUS_TYPE_BOOLEAN, &success,
                           DBUS_TYPE_INVALID);
  return reply;
}

}  // namespace permission_broker
