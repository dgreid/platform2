// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is the boilerplate implementation of the IBase HAL interface,
// generated by the hidl-gen tool and then modified for use on Chrome OS.
// Modifications include:
// - Removal of non boiler plate client and server related code.
// - Reformatting to meet the Chrome OS coding standards.
//
// Originally generated with the command:
// $ hidl-gen -o output -L c++ -r android.hidl:system/libhidl/transport \
//   android.hidl.base@1.0

#include <android/hidl/base/1.0/IBase.h>

namespace android {
namespace hidl {
namespace base {
namespace V1_0 {

using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::hidl_death_recipient;
using ::android::hardware::hidl_handle;
using ::android::hardware::hidl_vec;
using ::android::hardware::hidl_string;
using ::android::sp;

const char* IBase::descriptor("android.hidl.base@1.0::IBase");

Return<void> IBase::interfaceChain(interfaceChain_cb _hidl_cb) {
  _hidl_cb({
      ::android::hidl::base::V1_0::IBase::descriptor,
  });
  return Void();
}

Return<void> IBase::debug(const hidl_handle& , const hidl_vec<hidl_string>&) {
  return Void();
}

Return<void> IBase::interfaceDescriptor(interfaceDescriptor_cb _hidl_cb) {
  _hidl_cb(IBase::descriptor);
  return Void();
}

Return<void> IBase::getHashChain(getHashChain_cb _hidl_cb) {
  _hidl_cb({
    /* ec7fd79ed02dfa85bc499426adae3ebe23ef0524f3cd6957139324b83b18ca4c */
    (uint8_t[32]){236, 127, 215, 158, 208, 45, 250, 133, 188, 73, 148, 38, 173,
                  174, 62, 190, 35, 239, 5, 36, 243, 205, 105, 87, 19, 147, 36,
                  184, 59, 24, 202, 76}});
  return Void();
}

Return<void> IBase::setHALInstrumentation() {
  return Void();
}

Return<bool> IBase::linkToDeath(const sp<hidl_death_recipient>& recipient,
                                uint64_t) {
  return (recipient != nullptr);
}

Return<void> IBase::ping() {
  return Void();
}

Return<void> IBase::getDebugInfo(getDebugInfo_cb _hidl_cb) {
  ::android::hidl::base::V1_0::DebugInfo info = {};
  info.pid = -1;
  info.ptr = 0;
  info.arch =
  #if defined(__LP64__)
  ::android::hidl::base::V1_0::DebugInfo::Architecture::IS_64BIT;
  #else
  ::android::hidl::base::V1_0::DebugInfo::Architecture::IS_32BIT;
  #endif

  _hidl_cb(info);
  return Void();
}

Return<void> IBase::notifySyspropsChanged() {
  ::android::report_sysprop_change();
  return Void();
}

Return<bool> IBase::unlinkToDeath(const sp<hidl_death_recipient>& recipient) {
  return (recipient != nullptr);
}

Return<::android::sp<IBase>> IBase::castFrom(const sp<IBase>& parent, bool) {
  return parent;
}

}  // namespace V1_0
}  // namespace base
}  // namespace hidl
}  // namespace android