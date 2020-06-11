// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is the boilerplate implementation of the IAllocator HAL interface,
// generated by the hidl-gen tool and then modified for use on Chrome OS.
// Modifications include:
// - Removal of non boiler plate client and server related code.
// - Reformatting to meet the Chrome OS coding standards.
//
// Originally generated with the command:
// $ hidl-gen -o output -L c++ -r android.hardware:hardware/interfaces \
//   android.hardware.neuralnetworks@1.3

#define LOG_TAG "android.hardware.neuralnetworks@1.3::types"

#include <android/hardware/neuralnetworks/1.3/types.h>
#include <cutils/compiler.h>
#include <hidl/HidlInternal.h>
#include <log/log.h>

#include <inttypes.h>
#include <utility>

namespace android {
namespace hardware {
namespace neuralnetworks {
namespace V1_3 {

::android::hardware::neuralnetworks::V1_3::Request::MemoryPool::MemoryPool() {
  static_assert(
      offsetof(::android::hardware::neuralnetworks::V1_3::Request::MemoryPool,
               hidl_d) == 0,
      "wrong offset");
  static_assert(
      offsetof(::android::hardware::neuralnetworks::V1_3::Request::MemoryPool,
               hidl_u) == 8,
      "wrong offset");

  ::std::memset(&hidl_u, 0, sizeof(hidl_u));
  ::std::memset(reinterpret_cast<uint8_t*>(this) + 1, 0, 7);
  // no padding to zero starting at offset 48

  hidl_d = hidl_discriminator::hidlMemory;
  new (&hidl_u.hidlMemory)::android::hardware::hidl_memory();
}

::android::hardware::neuralnetworks::V1_3::Request::MemoryPool::~MemoryPool() {
  hidl_destructUnion();
}

::android::hardware::neuralnetworks::V1_3::Request::MemoryPool::MemoryPool(
    MemoryPool&& other)
    : ::android::hardware::neuralnetworks::V1_3::Request::MemoryPool() {
  switch (other.hidl_d) {
    case hidl_discriminator::hidlMemory: {
      new (&hidl_u.hidlMemory)::android::hardware::hidl_memory(
          std::move(other.hidl_u.hidlMemory));
      break;
    }
    case hidl_discriminator::token: {
      new (&hidl_u.token) uint32_t(std::move(other.hidl_u.token));
      break;
    }
    default: {
      ::android::hardware::details::logAlwaysFatal(
          ("Unknown union discriminator (value: " +
           std::to_string((uint8_t)other.hidl_d) + ").")
              .c_str());
    }
  }

  hidl_d = other.hidl_d;
}

::android::hardware::neuralnetworks::V1_3::Request::MemoryPool::MemoryPool(
    const MemoryPool& other)
    : ::android::hardware::neuralnetworks::V1_3::Request::MemoryPool() {
  switch (other.hidl_d) {
    case hidl_discriminator::hidlMemory: {
      new (&hidl_u.hidlMemory)::android::hardware::hidl_memory(
          other.hidl_u.hidlMemory);
      break;
    }
    case hidl_discriminator::token: {
      new (&hidl_u.token) uint32_t(other.hidl_u.token);
      break;
    }
    default: {
      ::android::hardware::details::logAlwaysFatal(
          ("Unknown union discriminator (value: " +
           std::to_string((uint8_t)other.hidl_d) + ").")
              .c_str());
    }
  }

  hidl_d = other.hidl_d;
}

::android::hardware::neuralnetworks::V1_3::Request::MemoryPool&(
    ::android::hardware::neuralnetworks::V1_3::Request::MemoryPool::operator=)(
    MemoryPool&& other) {
  if (this == &other) {
    return *this;
  }

  switch (other.hidl_d) {
    case hidl_discriminator::hidlMemory: {
      hidlMemory(std::move(other.hidl_u.hidlMemory));
      break;
    }
    case hidl_discriminator::token: {
      token(std::move(other.hidl_u.token));
      break;
    }
    default: {
      ::android::hardware::details::logAlwaysFatal(
          ("Unknown union discriminator (value: " +
           std::to_string((uint8_t)other.hidl_d) + ").")
              .c_str());
    }
  }
  return *this;
}

::android::hardware::neuralnetworks::V1_3::Request::MemoryPool&(
    ::android::hardware::neuralnetworks::V1_3::Request::MemoryPool::operator=)(
    const MemoryPool& other) {
  if (this == &other) {
    return *this;
  }

  switch (other.hidl_d) {
    case hidl_discriminator::hidlMemory: {
      hidlMemory(other.hidl_u.hidlMemory);
      break;
    }
    case hidl_discriminator::token: {
      token(other.hidl_u.token);
      break;
    }
    default: {
      ::android::hardware::details::logAlwaysFatal(
          ("Unknown union discriminator (value: " +
           std::to_string((uint8_t)other.hidl_d) + ").")
              .c_str());
    }
  }
  return *this;
}

void ::android::hardware::neuralnetworks::V1_3::Request::MemoryPool::
    hidl_destructUnion() {
  switch (hidl_d) {
    case hidl_discriminator::hidlMemory: {
      ::android::hardware::details::destructElement(&(hidl_u.hidlMemory));
      break;
    }
    case hidl_discriminator::token: {
      ::android::hardware::details::destructElement(&(hidl_u.token));
      break;
    }
    default: {
      ::android::hardware::details::logAlwaysFatal(
          ("Unknown union discriminator (value: " +
           std::to_string((uint8_t)hidl_d) + ").")
              .c_str());
    }
  }
}

void ::android::hardware::neuralnetworks::V1_3::Request::MemoryPool::hidlMemory(
    const ::android::hardware::hidl_memory& o) {
  if (hidl_d != hidl_discriminator::hidlMemory) {
    hidl_destructUnion();
    ::std::memset(&hidl_u, 0, sizeof(hidl_u));

    new (&hidl_u.hidlMemory)::android::hardware::hidl_memory(o);
    hidl_d = hidl_discriminator::hidlMemory;
  } else if (&(hidl_u.hidlMemory) != &o) {
    hidl_u.hidlMemory = o;
  }
}

void ::android::hardware::neuralnetworks::V1_3::Request::MemoryPool::hidlMemory(
    ::android::hardware::hidl_memory&& o) {
  if (hidl_d != hidl_discriminator::hidlMemory) {
    hidl_destructUnion();
    ::std::memset(&hidl_u, 0, sizeof(hidl_u));

    new (&hidl_u.hidlMemory)::android::hardware::hidl_memory(std::move(o));
    hidl_d = hidl_discriminator::hidlMemory;
  } else if (&(hidl_u.hidlMemory) != &o) {
    hidl_u.hidlMemory = std::move(o);
  }
}

::android::hardware::hidl_memory&(::android::hardware::neuralnetworks::V1_3::
                                      Request::MemoryPool::hidlMemory)() {
  if (CC_UNLIKELY(hidl_d != hidl_discriminator::hidlMemory)) {
    LOG_ALWAYS_FATAL(
        "Bad safe_union access: safe_union has discriminator %" PRIu64
        " but discriminator %" PRIu64 " was accessed.",
        static_cast<uint64_t>(hidl_d),
        static_cast<uint64_t>(hidl_discriminator::hidlMemory));
  }

  return hidl_u.hidlMemory;
}

const ::android::hardware::hidl_memory&(
    ::android::hardware::neuralnetworks::V1_3::Request::MemoryPool::
        hidlMemory)() const {
  if (CC_UNLIKELY(hidl_d != hidl_discriminator::hidlMemory)) {
    LOG_ALWAYS_FATAL(
        "Bad safe_union access: safe_union has discriminator %" PRIu64
        " but discriminator %" PRIu64 " was accessed.",
        static_cast<uint64_t>(hidl_d),
        static_cast<uint64_t>(hidl_discriminator::hidlMemory));
  }

  return hidl_u.hidlMemory;
}

void ::android::hardware::neuralnetworks::V1_3::Request::MemoryPool::token(
    uint32_t o) {
  if (hidl_d != hidl_discriminator::token) {
    hidl_destructUnion();
    ::std::memset(&hidl_u, 0, sizeof(hidl_u));

    new (&hidl_u.token) uint32_t(o);
    hidl_d = hidl_discriminator::token;
  } else if (&(hidl_u.token) != &o) {
    hidl_u.token = o;
  }
}

uint32_t&(
    ::android::hardware::neuralnetworks::V1_3::Request::MemoryPool::token)() {
  if (CC_UNLIKELY(hidl_d != hidl_discriminator::token)) {
    LOG_ALWAYS_FATAL(
        "Bad safe_union access: safe_union has discriminator %" PRIu64
        " but discriminator %" PRIu64 " was accessed.",
        static_cast<uint64_t>(hidl_d),
        static_cast<uint64_t>(hidl_discriminator::token));
  }

  return hidl_u.token;
}

uint32_t(
    ::android::hardware::neuralnetworks::V1_3::Request::MemoryPool::token)()
    const {
  if (CC_UNLIKELY(hidl_d != hidl_discriminator::token)) {
    LOG_ALWAYS_FATAL(
        "Bad safe_union access: safe_union has discriminator %" PRIu64
        " but discriminator %" PRIu64 " was accessed.",
        static_cast<uint64_t>(hidl_d),
        static_cast<uint64_t>(hidl_discriminator::token));
  }

  return hidl_u.token;
}

::android::hardware::neuralnetworks::V1_3::Request::MemoryPool::hidl_union::
    hidl_union() {}

::android::hardware::neuralnetworks::V1_3::Request::MemoryPool::hidl_union::
    ~hidl_union() {}

::android::hardware::neuralnetworks::V1_3::Request::MemoryPool::
    hidl_discriminator(::android::hardware::neuralnetworks::V1_3::Request::
                           MemoryPool::getDiscriminator)() const {
  return hidl_d;
}

::android::hardware::neuralnetworks::V1_3::OptionalTimePoint::
    OptionalTimePoint() {
  static_assert(
      offsetof(::android::hardware::neuralnetworks::V1_3::OptionalTimePoint,
               hidl_d) == 0,
      "wrong offset");
  static_assert(
      offsetof(::android::hardware::neuralnetworks::V1_3::OptionalTimePoint,
               hidl_u) == 8,
      "wrong offset");

  ::std::memset(&hidl_u, 0, sizeof(hidl_u));
  ::std::memset(reinterpret_cast<uint8_t*>(this) + 1, 0, 7);
  // no padding to zero starting at offset 16

  hidl_d = hidl_discriminator::none;
  new (&hidl_u.none)::android::hidl::safe_union::V1_0::Monostate();
}

::android::hardware::neuralnetworks::V1_3::OptionalTimePoint::
    ~OptionalTimePoint() {
  hidl_destructUnion();
}

::android::hardware::neuralnetworks::V1_3::OptionalTimePoint::OptionalTimePoint(
    OptionalTimePoint&& other)
    : ::android::hardware::neuralnetworks::V1_3::OptionalTimePoint() {
  switch (other.hidl_d) {
    case hidl_discriminator::none: {
      new (&hidl_u.none)::android::hidl::safe_union::V1_0::Monostate(
          std::move(other.hidl_u.none));
      break;
    }
    case hidl_discriminator::nanosecondsSinceEpoch: {
      new (&hidl_u.nanosecondsSinceEpoch)
          uint64_t(std::move(other.hidl_u.nanosecondsSinceEpoch));
      break;
    }
    default: {
      ::android::hardware::details::logAlwaysFatal(
          ("Unknown union discriminator (value: " +
           std::to_string((uint8_t)other.hidl_d) + ").")
              .c_str());
    }
  }

  hidl_d = other.hidl_d;
}

::android::hardware::neuralnetworks::V1_3::OptionalTimePoint::OptionalTimePoint(
    const OptionalTimePoint& other)
    : ::android::hardware::neuralnetworks::V1_3::OptionalTimePoint() {
  switch (other.hidl_d) {
    case hidl_discriminator::none: {
      new (&hidl_u.none)::android::hidl::safe_union::V1_0::Monostate(
          other.hidl_u.none);
      break;
    }
    case hidl_discriminator::nanosecondsSinceEpoch: {
      new (&hidl_u.nanosecondsSinceEpoch)
          uint64_t(other.hidl_u.nanosecondsSinceEpoch);
      break;
    }
    default: {
      ::android::hardware::details::logAlwaysFatal(
          ("Unknown union discriminator (value: " +
           std::to_string((uint8_t)other.hidl_d) + ").")
              .c_str());
    }
  }

  hidl_d = other.hidl_d;
}

::android::hardware::neuralnetworks::V1_3::OptionalTimePoint&(
    ::android::hardware::neuralnetworks::V1_3::OptionalTimePoint::operator=)(
    OptionalTimePoint&& other) {
  if (this == &other) {
    return *this;
  }

  switch (other.hidl_d) {
    case hidl_discriminator::none: {
      none(std::move(other.hidl_u.none));
      break;
    }
    case hidl_discriminator::nanosecondsSinceEpoch: {
      nanosecondsSinceEpoch(std::move(other.hidl_u.nanosecondsSinceEpoch));
      break;
    }
    default: {
      ::android::hardware::details::logAlwaysFatal(
          ("Unknown union discriminator (value: " +
           std::to_string((uint8_t)other.hidl_d) + ").")
              .c_str());
    }
  }
  return *this;
}

::android::hardware::neuralnetworks::V1_3::OptionalTimePoint&(
    ::android::hardware::neuralnetworks::V1_3::OptionalTimePoint::operator=)(
    const OptionalTimePoint& other) {
  if (this == &other) {
    return *this;
  }

  switch (other.hidl_d) {
    case hidl_discriminator::none: {
      none(other.hidl_u.none);
      break;
    }
    case hidl_discriminator::nanosecondsSinceEpoch: {
      nanosecondsSinceEpoch(other.hidl_u.nanosecondsSinceEpoch);
      break;
    }
    default: {
      ::android::hardware::details::logAlwaysFatal(
          ("Unknown union discriminator (value: " +
           std::to_string((uint8_t)other.hidl_d) + ").")
              .c_str());
    }
  }
  return *this;
}

void ::android::hardware::neuralnetworks::V1_3::OptionalTimePoint::
    hidl_destructUnion() {
  switch (hidl_d) {
    case hidl_discriminator::none: {
      ::android::hardware::details::destructElement(&(hidl_u.none));
      break;
    }
    case hidl_discriminator::nanosecondsSinceEpoch: {
      ::android::hardware::details::destructElement(
          &(hidl_u.nanosecondsSinceEpoch));
      break;
    }
    default: {
      ::android::hardware::details::logAlwaysFatal(
          ("Unknown union discriminator (value: " +
           std::to_string((uint8_t)hidl_d) + ").")
              .c_str());
    }
  }
}

void ::android::hardware::neuralnetworks::V1_3::OptionalTimePoint::none(
    const ::android::hidl::safe_union::V1_0::Monostate& o) {
  if (hidl_d != hidl_discriminator::none) {
    hidl_destructUnion();
    ::std::memset(&hidl_u, 0, sizeof(hidl_u));

    new (&hidl_u.none)::android::hidl::safe_union::V1_0::Monostate(o);
    hidl_d = hidl_discriminator::none;
  } else if (&(hidl_u.none) != &o) {
    hidl_u.none = o;
  }
}

void ::android::hardware::neuralnetworks::V1_3::OptionalTimePoint::none(
    ::android::hidl::safe_union::V1_0::Monostate&& o) {
  if (hidl_d != hidl_discriminator::none) {
    hidl_destructUnion();
    ::std::memset(&hidl_u, 0, sizeof(hidl_u));

    new (&hidl_u.none)::android::hidl::safe_union::V1_0::Monostate(
        std::move(o));
    hidl_d = hidl_discriminator::none;
  } else if (&(hidl_u.none) != &o) {
    hidl_u.none = std::move(o);
  }
}

::android::hidl::safe_union::V1_0::Monostate&(
    ::android::hardware::neuralnetworks::V1_3::OptionalTimePoint::none)() {
  if (CC_UNLIKELY(hidl_d != hidl_discriminator::none)) {
    LOG_ALWAYS_FATAL(
        "Bad safe_union access: safe_union has discriminator %" PRIu64
        " but discriminator %" PRIu64 " was accessed.",
        static_cast<uint64_t>(hidl_d),
        static_cast<uint64_t>(hidl_discriminator::none));
  }

  return hidl_u.none;
}

const ::android::hidl::safe_union::V1_0::Monostate&(
    ::android::hardware::neuralnetworks::V1_3::OptionalTimePoint::none)()
    const {
  if (CC_UNLIKELY(hidl_d != hidl_discriminator::none)) {
    LOG_ALWAYS_FATAL(
        "Bad safe_union access: safe_union has discriminator %" PRIu64
        " but discriminator %" PRIu64 " was accessed.",
        static_cast<uint64_t>(hidl_d),
        static_cast<uint64_t>(hidl_discriminator::none));
  }

  return hidl_u.none;
}

void ::android::hardware::neuralnetworks::V1_3::OptionalTimePoint::
    nanosecondsSinceEpoch(uint64_t o) {
  if (hidl_d != hidl_discriminator::nanosecondsSinceEpoch) {
    hidl_destructUnion();
    ::std::memset(&hidl_u, 0, sizeof(hidl_u));

    new (&hidl_u.nanosecondsSinceEpoch) uint64_t(o);
    hidl_d = hidl_discriminator::nanosecondsSinceEpoch;
  } else if (&(hidl_u.nanosecondsSinceEpoch) != &o) {
    hidl_u.nanosecondsSinceEpoch = o;
  }
}

uint64_t&(::android::hardware::neuralnetworks::V1_3::OptionalTimePoint::
              nanosecondsSinceEpoch)() {
  if (CC_UNLIKELY(hidl_d != hidl_discriminator::nanosecondsSinceEpoch)) {
    LOG_ALWAYS_FATAL(
        "Bad safe_union access: safe_union has discriminator %" PRIu64
        " but discriminator %" PRIu64 " was accessed.",
        static_cast<uint64_t>(hidl_d),
        static_cast<uint64_t>(hidl_discriminator::nanosecondsSinceEpoch));
  }

  return hidl_u.nanosecondsSinceEpoch;
}

uint64_t(::android::hardware::neuralnetworks::V1_3::OptionalTimePoint::
             nanosecondsSinceEpoch)() const {
  if (CC_UNLIKELY(hidl_d != hidl_discriminator::nanosecondsSinceEpoch)) {
    LOG_ALWAYS_FATAL(
        "Bad safe_union access: safe_union has discriminator %" PRIu64
        " but discriminator %" PRIu64 " was accessed.",
        static_cast<uint64_t>(hidl_d),
        static_cast<uint64_t>(hidl_discriminator::nanosecondsSinceEpoch));
  }

  return hidl_u.nanosecondsSinceEpoch;
}

::android::hardware::neuralnetworks::V1_3::OptionalTimePoint::hidl_union::
    hidl_union() {}

::android::hardware::neuralnetworks::V1_3::OptionalTimePoint::hidl_union::
    ~hidl_union() {}

::android::hardware::neuralnetworks::V1_3::OptionalTimePoint::
    hidl_discriminator(::android::hardware::neuralnetworks::V1_3::
                           OptionalTimePoint::getDiscriminator)() const {
  return hidl_d;
}

::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration::
    OptionalTimeoutDuration() {
  static_assert(
      offsetof(
          ::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration,
          hidl_d) == 0,
      "wrong offset");
  static_assert(
      offsetof(
          ::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration,
          hidl_u) == 8,
      "wrong offset");

  ::std::memset(&hidl_u, 0, sizeof(hidl_u));
  ::std::memset(reinterpret_cast<uint8_t*>(this) + 1, 0, 7);
  // no padding to zero starting at offset 16

  hidl_d = hidl_discriminator::none;
  new (&hidl_u.none)::android::hidl::safe_union::V1_0::Monostate();
}

::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration::
    ~OptionalTimeoutDuration() {
  hidl_destructUnion();
}

::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration::
    OptionalTimeoutDuration(OptionalTimeoutDuration&& other)
    : ::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration() {
  switch (other.hidl_d) {
    case hidl_discriminator::none: {
      new (&hidl_u.none)::android::hidl::safe_union::V1_0::Monostate(
          std::move(other.hidl_u.none));
      break;
    }
    case hidl_discriminator::nanoseconds: {
      new (&hidl_u.nanoseconds) uint64_t(std::move(other.hidl_u.nanoseconds));
      break;
    }
    default: {
      ::android::hardware::details::logAlwaysFatal(
          ("Unknown union discriminator (value: " +
           std::to_string((uint8_t)other.hidl_d) + ").")
              .c_str());
    }
  }

  hidl_d = other.hidl_d;
}

::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration::
    OptionalTimeoutDuration(const OptionalTimeoutDuration& other)
    : ::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration() {
  switch (other.hidl_d) {
    case hidl_discriminator::none: {
      new (&hidl_u.none)::android::hidl::safe_union::V1_0::Monostate(
          other.hidl_u.none);
      break;
    }
    case hidl_discriminator::nanoseconds: {
      new (&hidl_u.nanoseconds) uint64_t(other.hidl_u.nanoseconds);
      break;
    }
    default: {
      ::android::hardware::details::logAlwaysFatal(
          ("Unknown union discriminator (value: " +
           std::to_string((uint8_t)other.hidl_d) + ").")
              .c_str());
    }
  }

  hidl_d = other.hidl_d;
}

::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration&(
    ::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration::
    operator=)(OptionalTimeoutDuration&& other) {
  if (this == &other) {
    return *this;
  }

  switch (other.hidl_d) {
    case hidl_discriminator::none: {
      none(std::move(other.hidl_u.none));
      break;
    }
    case hidl_discriminator::nanoseconds: {
      nanoseconds(std::move(other.hidl_u.nanoseconds));
      break;
    }
    default: {
      ::android::hardware::details::logAlwaysFatal(
          ("Unknown union discriminator (value: " +
           std::to_string((uint8_t)other.hidl_d) + ").")
              .c_str());
    }
  }
  return *this;
}

::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration&(
    ::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration::
    operator=)(const OptionalTimeoutDuration& other) {
  if (this == &other) {
    return *this;
  }

  switch (other.hidl_d) {
    case hidl_discriminator::none: {
      none(other.hidl_u.none);
      break;
    }
    case hidl_discriminator::nanoseconds: {
      nanoseconds(other.hidl_u.nanoseconds);
      break;
    }
    default: {
      ::android::hardware::details::logAlwaysFatal(
          ("Unknown union discriminator (value: " +
           std::to_string((uint8_t)other.hidl_d) + ").")
              .c_str());
    }
  }
  return *this;
}

void ::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration::
    hidl_destructUnion() {
  switch (hidl_d) {
    case hidl_discriminator::none: {
      ::android::hardware::details::destructElement(&(hidl_u.none));
      break;
    }
    case hidl_discriminator::nanoseconds: {
      ::android::hardware::details::destructElement(&(hidl_u.nanoseconds));
      break;
    }
    default: {
      ::android::hardware::details::logAlwaysFatal(
          ("Unknown union discriminator (value: " +
           std::to_string((uint8_t)hidl_d) + ").")
              .c_str());
    }
  }
}

void ::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration::none(
    const ::android::hidl::safe_union::V1_0::Monostate& o) {
  if (hidl_d != hidl_discriminator::none) {
    hidl_destructUnion();
    ::std::memset(&hidl_u, 0, sizeof(hidl_u));

    new (&hidl_u.none)::android::hidl::safe_union::V1_0::Monostate(o);
    hidl_d = hidl_discriminator::none;
  } else if (&(hidl_u.none) != &o) {
    hidl_u.none = o;
  }
}

void ::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration::none(
    ::android::hidl::safe_union::V1_0::Monostate&& o) {
  if (hidl_d != hidl_discriminator::none) {
    hidl_destructUnion();
    ::std::memset(&hidl_u, 0, sizeof(hidl_u));

    new (&hidl_u.none)::android::hidl::safe_union::V1_0::Monostate(
        std::move(o));
    hidl_d = hidl_discriminator::none;
  } else if (&(hidl_u.none) != &o) {
    hidl_u.none = std::move(o);
  }
}

::android::hidl::safe_union::V1_0::Monostate&(
    ::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration::
        none)() {
  if (CC_UNLIKELY(hidl_d != hidl_discriminator::none)) {
    LOG_ALWAYS_FATAL(
        "Bad safe_union access: safe_union has discriminator %" PRIu64
        " but discriminator %" PRIu64 " was accessed.",
        static_cast<uint64_t>(hidl_d),
        static_cast<uint64_t>(hidl_discriminator::none));
  }

  return hidl_u.none;
}

const ::android::hidl::safe_union::V1_0::Monostate&(
    ::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration::none)()
    const {
  if (CC_UNLIKELY(hidl_d != hidl_discriminator::none)) {
    LOG_ALWAYS_FATAL(
        "Bad safe_union access: safe_union has discriminator %" PRIu64
        " but discriminator %" PRIu64 " was accessed.",
        static_cast<uint64_t>(hidl_d),
        static_cast<uint64_t>(hidl_discriminator::none));
  }

  return hidl_u.none;
}

void ::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration::
    nanoseconds(uint64_t o) {
  if (hidl_d != hidl_discriminator::nanoseconds) {
    hidl_destructUnion();
    ::std::memset(&hidl_u, 0, sizeof(hidl_u));

    new (&hidl_u.nanoseconds) uint64_t(o);
    hidl_d = hidl_discriminator::nanoseconds;
  } else if (&(hidl_u.nanoseconds) != &o) {
    hidl_u.nanoseconds = o;
  }
}

uint64_t&(::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration::
              nanoseconds)() {
  if (CC_UNLIKELY(hidl_d != hidl_discriminator::nanoseconds)) {
    LOG_ALWAYS_FATAL(
        "Bad safe_union access: safe_union has discriminator %" PRIu64
        " but discriminator %" PRIu64 " was accessed.",
        static_cast<uint64_t>(hidl_d),
        static_cast<uint64_t>(hidl_discriminator::nanoseconds));
  }

  return hidl_u.nanoseconds;
}

uint64_t(::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration::
             nanoseconds)() const {
  if (CC_UNLIKELY(hidl_d != hidl_discriminator::nanoseconds)) {
    LOG_ALWAYS_FATAL(
        "Bad safe_union access: safe_union has discriminator %" PRIu64
        " but discriminator %" PRIu64 " was accessed.",
        static_cast<uint64_t>(hidl_d),
        static_cast<uint64_t>(hidl_discriminator::nanoseconds));
  }

  return hidl_u.nanoseconds;
}

::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration::hidl_union::
    hidl_union() {}

::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration::hidl_union::
    ~hidl_union() {}

::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration::
    hidl_discriminator(::android::hardware::neuralnetworks::V1_3::
                           OptionalTimeoutDuration::getDiscriminator)() const {
  return hidl_d;
}

}  // namespace V1_3
}  // namespace neuralnetworks
}  // namespace hardware
}  // namespace android