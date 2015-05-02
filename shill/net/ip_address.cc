// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/net/ip_address.h"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <string>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>

#include "shill/net/byte_string.h"

using std::string;
using std::vector;

namespace shill {

namespace {
const size_t kBitsPerByte = 8;
}  // namespace

// static
const IPAddress::Family IPAddress::kFamilyUnknown = AF_UNSPEC;
// static
const IPAddress::Family IPAddress::kFamilyIPv4 = AF_INET;
// static
const IPAddress::Family IPAddress::kFamilyIPv6 = AF_INET6;

// static
const char IPAddress::kFamilyNameUnknown[] = "Unknown";
// static
const char IPAddress::kFamilyNameIPv4[] = "IPv4";
// static
const char IPAddress::kFamilyNameIPv6[] = "IPv6";

IPAddress::IPAddress(Family family, const ByteString &address)
    : family_(family) ,
      address_(address),
      prefix_(0) {}

IPAddress::IPAddress(Family family,
                     const ByteString &address,
                     unsigned int prefix)
    : family_(family) ,
      address_(address),
      prefix_(prefix) {}

IPAddress::IPAddress(Family family)
    : family_(family),
      prefix_(0) {}

IPAddress::IPAddress(std::string ip_string)
    : prefix_(0) {
  family_ = IPAddress::kFamilyIPv4;
  if (!SetAddressFromString(ip_string)) {
    family_ = IPAddress::kFamilyIPv6;
    if (!SetAddressFromString(ip_string)) {
      family_ = IPAddress::kFamilyUnknown;
    }
  }
}

IPAddress::~IPAddress() {}

// static
size_t IPAddress::GetAddressLength(Family family) {
  switch (family) {
  case kFamilyIPv4:
    return sizeof(in_addr);
  case kFamilyIPv6:
    return sizeof(in6_addr);
  default:
    return 0;
  }
}

// static
size_t IPAddress::GetMaxPrefixLength(Family family) {
  return GetAddressLength(family) * kBitsPerByte;
}

size_t IPAddress::GetMinPrefixLength() const {
  if (family() != kFamilyIPv4) {
    NOTIMPLEMENTED() << ": only implemented for IPv4";
    return GetMaxPrefixLength(family());
  }

  CHECK(IsValid());
  in_addr_t address_val;
  memcpy(&address_val, GetConstData(), sizeof(address_val));
  // IN_CLASSx() macros operate on addresses in host-order.
  address_val = ntohl(address_val);
  if (IN_CLASSA(address_val)) {
    return GetMaxPrefixLength(family()) - IN_CLASSA_NSHIFT;
  } else if (IN_CLASSB(address_val)) {
    return GetMaxPrefixLength(family()) - IN_CLASSB_NSHIFT;
  } else if (IN_CLASSC(address_val)) {
    return GetMaxPrefixLength(family()) - IN_CLASSC_NSHIFT;
  }

  LOG(ERROR) << "Invalid IPv4 address class";
  return GetMaxPrefixLength(family());
}

// static
size_t IPAddress::GetPrefixLengthFromMask(Family family, const string &mask) {
  switch (family) {
    case kFamilyIPv4: {
      in_addr_t mask_val = inet_network(mask.c_str());
      int subnet_prefix = 0;
      while (mask_val) {
        subnet_prefix++;
        mask_val <<= 1;
      }
      return subnet_prefix;
    }
    case kFamilyIPv6:
      NOTIMPLEMENTED();
      break;
    default:
      LOG(WARNING) << "Unexpected address family: " << family;
      break;
  }
  return 0;
}

// static
IPAddress IPAddress::GetAddressMaskFromPrefix(Family family, size_t prefix) {
  ByteString address_bytes(GetAddressLength(family));
  unsigned char *address_ptr = address_bytes.GetData();

  size_t bits = prefix;
  if (bits > GetMaxPrefixLength(family)) {
    bits = GetMaxPrefixLength(family);
  }

  while (bits > kBitsPerByte) {
    bits -= kBitsPerByte;
    *address_ptr++ = kuint8max;
  }

  // We are guaranteed to be before the end of the address data since even
  // if the prefix is the maximum, the loop above will end before we assign
  // and increment past the last byte.
  *address_ptr = ~((1 << (kBitsPerByte - bits)) - 1);

  return IPAddress(family, address_bytes);
}

// static
string IPAddress::GetAddressFamilyName(Family family) {
  switch (family) {
  case kFamilyIPv4:
    return kFamilyNameIPv4;
  case kFamilyIPv6:
    return kFamilyNameIPv6;
  default:
    return kFamilyNameUnknown;
  }
}

bool IPAddress::SetAddressFromString(const string &address_string) {
  size_t address_length = GetAddressLength(family_);

  if (!address_length) {
    return false;
  }

  ByteString address(address_length);
  if (inet_pton(family_, address_string.c_str(), address.GetData()) <= 0) {
    return false;
  }
  address_ = address;
  return true;
}

bool IPAddress::SetAddressAndPrefixFromString(const string &address_string) {
  vector<string> address_parts;
  base::SplitString(address_string, '/', &address_parts);
  if (address_parts.size() != 2) {
    LOG(ERROR) << "Cannot split address " << address_string;
    return false;
  }
  if (!SetAddressFromString(address_parts[0])) {
    LOG(ERROR) << "Cannot parse address string " << address_parts[0];
    return false;
  }
  size_t prefix;
  if (!base::StringToSizeT(address_parts[1], &prefix) ||
      prefix > GetMaxPrefixLength(family_)) {
    LOG(ERROR) << "Cannot parse address prefix " << address_parts[1];
    return false;
  }
  set_prefix(prefix);
  return true;
}

void IPAddress::SetAddressToDefault() {
  address_ = ByteString(GetAddressLength(family_));
}

bool IPAddress::IntoString(string *address_string) const {
  // Noting that INET6_ADDRSTRLEN > INET_ADDRSTRLEN
  char address_buf[INET6_ADDRSTRLEN];
  if (GetLength() != GetAddressLength(family_) ||
      !inet_ntop(family_, GetConstData(), address_buf, sizeof(address_buf))) {
    return false;
  }
  *address_string = address_buf;
  return true;
}

string IPAddress::ToString() const {
  string out("<unknown>");
  IntoString(&out);
  return out;
}

bool IPAddress::Equals(const IPAddress &b) const {
  return family_ == b.family_ && address_.Equals(b.address_) &&
      prefix_ == b.prefix_;
}

bool IPAddress::HasSameAddressAs(const IPAddress &b) const {
  return family_ == b.family_ && address_.Equals(b.address_);
}

IPAddress IPAddress::MaskWith(const IPAddress &b) const {
  CHECK(IsValid());
  CHECK(b.IsValid());
  CHECK_EQ(family(), b.family());

  ByteString address_bytes(address());
  address_bytes.BitwiseAnd(b.address());

  return IPAddress(family(), address_bytes);
}

IPAddress IPAddress::MergeWith(const IPAddress &b) const {
  CHECK(IsValid());
  CHECK(b.IsValid());
  CHECK_EQ(family(), b.family());

  ByteString address_bytes(address());
  address_bytes.BitwiseOr(b.address());

  return IPAddress(family(), address_bytes);
}

IPAddress IPAddress::GetNetworkPart() const {
  return MaskWith(GetAddressMaskFromPrefix(family(), prefix()));
}

IPAddress IPAddress::GetDefaultBroadcast() {
  ByteString broadcast_bytes(
    GetAddressMaskFromPrefix(family(), prefix()).address());
  broadcast_bytes.BitwiseInvert();
  return MergeWith(IPAddress(family(), broadcast_bytes));
}

bool IPAddress::CanReachAddress(const IPAddress &b) const {
  CHECK_EQ(family(), b.family());
  IPAddress b_prefixed(b);
  b_prefixed.set_prefix(prefix());
  return GetNetworkPart().Equals(b_prefixed.GetNetworkPart());
}

}  // namespace shill
