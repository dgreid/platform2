// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/power_manager_proxy.h"

#include <chromeos/dbus/service_constants.h>
#include <google/protobuf/message_lite.h>

#include "power_manager/proto_bindings/suspend.pb.h"
#include "shill/logging.h"

using std::string;
using std::vector;

namespace shill {

namespace {

// Serializes |protobuf| to |out| and returns true on success.
bool SerializeProtocolBuffer(const google::protobuf::MessageLite &protobuf,
                             vector<uint8_t> *out) {
  CHECK(out);
  out->clear();
  string serialized_protobuf;
  if (!protobuf.SerializeToString(&serialized_protobuf))
    return false;
  out->assign(serialized_protobuf.begin(), serialized_protobuf.end());
  return true;
}

// Deserializes |serialized_protobuf| to |protobuf_out| and returns true on
// success.
bool DeserializeProtocolBuffer(const vector<uint8_t> &serialized_protobuf,
                               google::protobuf::MessageLite *protobuf_out) {
  CHECK(protobuf_out);
  if (serialized_protobuf.empty())
    return false;
  return protobuf_out->ParseFromArray(&serialized_protobuf.front(),
                                      serialized_protobuf.size());
}

}  // namespace

PowerManagerProxy::PowerManagerProxy(PowerManagerProxyDelegate *delegate,
                                     DBus::Connection *connection)
    : proxy_(delegate, connection) {}

PowerManagerProxy::~PowerManagerProxy() {}

bool PowerManagerProxy::RegisterSuspendDelay(
    base::TimeDelta timeout,
    const string &description,
    int *delay_id_out) {
  return RegisterSuspendDelayInternal(false,
                                      timeout,
                                      description,
                                      delay_id_out);
}

bool PowerManagerProxy::UnregisterSuspendDelay(int delay_id) {
  return UnregisterSuspendDelayInternal(false, delay_id);
}

bool PowerManagerProxy::ReportSuspendReadiness(int delay_id, int suspend_id) {
  return ReportSuspendReadinessInternal(false, delay_id, suspend_id);
}

bool PowerManagerProxy::RegisterDarkSuspendDelay(
    base::TimeDelta timeout,
    const string &description,
    int *delay_id_out) {
  return RegisterSuspendDelayInternal(true,
                                      timeout,
                                      description,
                                      delay_id_out);
}

bool PowerManagerProxy::UnregisterDarkSuspendDelay(int delay_id) {
  return UnregisterSuspendDelayInternal(true, delay_id);
}

bool PowerManagerProxy::ReportDarkSuspendReadiness(int delay_id,
                                                   int suspend_id ) {
  return ReportSuspendReadinessInternal(true, delay_id, suspend_id);
}

bool PowerManagerProxy::RecordDarkResumeWakeReason(const string &wake_reason) {
  LOG(INFO) << __func__;

  power_manager::DarkResumeWakeReason proto;
  proto.set_wake_reason(wake_reason);
  vector<uint8_t> serialized_proto;
  CHECK(SerializeProtocolBuffer(proto, &serialized_proto));

  try {
    proxy_.RecordDarkResumeWakeReason(serialized_proto);
  } catch (const DBus::Error &e) {
    LOG(ERROR) << "DBus exception: " << e.name() << ": " << e.what();
    return false;
  }
  return true;
}

bool PowerManagerProxy::RegisterSuspendDelayInternal(
    bool is_dark,
    base::TimeDelta timeout,
    const string &description,
    int *delay_id_out) {
  const string is_dark_arg = (is_dark ? "dark=true" : "dark=false");
  LOG(INFO) << __func__ << "(" << timeout.InMilliseconds()
            << ", " << is_dark_arg <<")";

  power_manager::RegisterSuspendDelayRequest request_proto;
  request_proto.set_timeout(timeout.ToInternalValue());
  request_proto.set_description(description);
  vector<uint8_t> serialized_request;
  CHECK(SerializeProtocolBuffer(request_proto, &serialized_request));

  vector<uint8_t> serialized_reply;
  try {
    if (is_dark)
      serialized_reply = proxy_.RegisterDarkSuspendDelay(serialized_request);
    else
      serialized_reply = proxy_.RegisterSuspendDelay(serialized_request);
  } catch (const DBus::Error &e) {
    LOG(ERROR) << "DBus exception: " << e.name() << ": " << e.what();
    return false;
  }

  power_manager::RegisterSuspendDelayReply reply_proto;
  if (!DeserializeProtocolBuffer(serialized_reply, &reply_proto)) {
    LOG(ERROR) << "Failed to register "
               << (is_dark ? "dark " : "")
               << "suspend delay.  Couldn't parse response.";
    return false;
  }
  *delay_id_out = reply_proto.delay_id();
  return true;
}

bool PowerManagerProxy::UnregisterSuspendDelayInternal(bool is_dark,
                                                       int delay_id) {
  const string is_dark_arg = (is_dark ? "dark=true" : "dark=false");
  LOG(INFO) << __func__ << "(" << delay_id << ", " << is_dark_arg << ")";

  power_manager::UnregisterSuspendDelayRequest request_proto;
  request_proto.set_delay_id(delay_id);
  vector<uint8_t> serialized_request;
  CHECK(SerializeProtocolBuffer(request_proto, &serialized_request));

  try {
    if (is_dark)
      proxy_.UnregisterDarkSuspendDelay(serialized_request);
    else
      proxy_.UnregisterSuspendDelay(serialized_request);
  } catch (const DBus::Error &e) {
    LOG(ERROR) << "DBus exception: " << e.name() << ": " << e.what();
    return false;
  }
  return true;
}

bool PowerManagerProxy::ReportSuspendReadinessInternal(bool is_dark,
                                                       int delay_id,
                                                       int suspend_id) {
  const string is_dark_arg = (is_dark ? "dark=true" : "dark=false");
  LOG(INFO) << __func__
            << "(" << delay_id
            << ", " << suspend_id
            << ", " << is_dark_arg << ")";

  power_manager::SuspendReadinessInfo proto;
  proto.set_delay_id(delay_id);
  proto.set_suspend_id(suspend_id);
  vector<uint8_t> serialized_proto;
  CHECK(SerializeProtocolBuffer(proto, &serialized_proto));

  try {
    if (is_dark)
      proxy_.HandleDarkSuspendReadiness(serialized_proto);
    else
      proxy_.HandleSuspendReadiness(serialized_proto);
  } catch (const DBus::Error &e) {
    LOG(ERROR) << "DBus exception: " << e.name() << ": " << e.what();
    return false;
  }
  return true;
}

PowerManagerProxy::Proxy::Proxy(PowerManagerProxyDelegate *delegate,
                                DBus::Connection *connection)
    : DBus::ObjectProxy(*connection,
                        power_manager::kPowerManagerServicePath,
                        power_manager::kPowerManagerServiceName),
      delegate_(delegate) {}

PowerManagerProxy::Proxy::~Proxy() {}

void PowerManagerProxy::Proxy::SuspendImminent(
    const vector<uint8_t> &serialized_proto) {
  LOG(INFO) << __func__;
  power_manager::SuspendImminent proto;
  if (!DeserializeProtocolBuffer(serialized_proto, &proto)) {
    LOG(ERROR) << "Failed to parse SuspendImminent signal.";
    return;
  }
  delegate_->OnSuspendImminent(proto.suspend_id());
}

void PowerManagerProxy::Proxy::SuspendDone(
    const vector<uint8_t> &serialized_proto) {
  LOG(INFO) << __func__;
  power_manager::SuspendDone proto;
  if (!DeserializeProtocolBuffer(serialized_proto, &proto)) {
    LOG(ERROR) << "Failed to parse SuspendDone signal.";
    return;
  }
  delegate_->OnSuspendDone(proto.suspend_id());
}

void PowerManagerProxy::Proxy::DarkSuspendImminent(
    const vector<uint8_t> &serialized_proto) {
  LOG(INFO) << __func__;
  power_manager::SuspendImminent proto;
  if (!DeserializeProtocolBuffer(serialized_proto, &proto)) {
    LOG(ERROR) << "Failed to parse DarkSuspendImminent signal.";
    return;
  }
  delegate_->OnDarkSuspendImminent(proto.suspend_id());
}

}  // namespace shill
