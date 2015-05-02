// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NET_IP_ADDRESS_H_
#define SHILL_NET_IP_ADDRESS_H_

#include <string>

#include "shill/net/byte_string.h"
#include "shill/shill_export.h"

namespace shill {

class SHILL_EXPORT IPAddress {
 public:
  typedef unsigned char Family;
  static const Family kFamilyUnknown;
  static const Family kFamilyIPv4;
  static const Family kFamilyIPv6;
  static const char kFamilyNameUnknown[];
  static const char kFamilyNameIPv4[];
  static const char kFamilyNameIPv6[];

  explicit IPAddress(Family family);
  // Constructs an IPAdress object given a standard string representation of an
  // IP address (e.g. "192.144.30.54").
  explicit IPAddress(std::string ip_string);

  IPAddress(Family family, const ByteString &address);
  IPAddress(Family family, const ByteString &address, unsigned int prefix);
  ~IPAddress();

  // Since this is a copyable datatype...
  IPAddress(const IPAddress &b)
    : family_(b.family_),
      address_(b.address_),
      prefix_(b.prefix_) {}
  IPAddress &operator=(const IPAddress &b) {
    family_ = b.family_;
    address_ = b.address_;
    prefix_ = b.prefix_;
    return *this;
  }

  // Static utilities
  // Get the length in bytes of addresses of the given family
  static size_t GetAddressLength(Family family);

  // Returns the maximum prefix length for address family |family|, i.e.,
  // the length of this address type in bits.
  static size_t GetMaxPrefixLength(Family family);

  // Provides a guideline for the minimum sensible prefix for this IP
  // address.  As opposed to GetMaxPrefixLength() above, this function
  // takes into account the class of this IP address to determine the
  // smallest prefix that makes sense for this class of address to have.
  // Since this function uses classful (pre-CIDR) rules to perform this
  // estimate, this is not an absolute rule and others methods like
  // IsValid() do not consider this a criteria.  It is only useful for
  // making guesses as to the mimimal plausible prefix that might be
  // viable for an address when the supplied prefix is obviously incorrect.
  size_t GetMinPrefixLength() const;

  // Returns the prefix length given an address |family| and a |mask|. For
  // example, returns 24 for an IPv4 mask 255.255.255.0.
  static size_t GetPrefixLengthFromMask(Family family, const std::string &mask);

  // Returns an IPAddress of type |family| that has all the high-order |prefix|
  // bits set.
  static IPAddress GetAddressMaskFromPrefix(Family family, size_t prefix);

  // Returns the name of an address family.
  static std::string GetAddressFamilyName(Family family);

  // Getters and Setters
  Family family() const { return family_; }
  void set_family(Family family) { family_ = family; }
  const ByteString &address() const { return address_; }
  unsigned int prefix() const { return prefix_; }
  void set_prefix(unsigned int prefix) { prefix_ = prefix; }
  const unsigned char *GetConstData() const { return address_.GetConstData(); }
  size_t GetLength() const { return address_.GetLength(); }
  bool IsDefault() const { return address_.IsZero(); }
  bool IsValid() const {
    return family_ != kFamilyUnknown &&
        GetLength() == GetAddressLength(family_);
  }

  // Parse an IP address string.
  bool SetAddressFromString(const std::string &address_string);
  // Parse an "address/prefix" IP address and prefix pair from a string.
  bool SetAddressAndPrefixFromString(const std::string &address_string);
  // An uninitialized IPAddress is empty and invalid when constructed.
  // Use SetAddressToDefault() to set it to the default or "all-zeroes" address.
  void SetAddressToDefault();
  // Return the string equivalent of the address.  Returns true if the
  // conversion succeeds in which case |address_string| is set to the
  // result.  Otherwise the function returns false and |address_string|
  // is left unmodified.
  bool IntoString(std::string *address_string) const;
  // Similar to IntoString, but returns by value. Convenient for logging.
  std::string ToString() const;

  // Returns whether |b| has the same family, address and prefix as |this|.
  bool Equals(const IPAddress &b) const;

  // Returns whether |b| has the same family and address as |this|.
  bool HasSameAddressAs(const IPAddress &b) const;

  // Perform an AND operation between the address data of |this| and that
  // of |b|.  Returns an IPAddress containing the result of the operation.
  // It is an error if |this| and |b| are not of the same address family
  // or if either are not valid,
  IPAddress MaskWith(const IPAddress &b) const;

  // Perform an OR operation between the address data of |this| and that
  // of |b|.  Returns an IPAddress containing the result of the operation.
  // It is an error if |this| and |b| are not of the same address family
  // or if either are not valid,
  IPAddress MergeWith(const IPAddress &b) const;

  // Return an address that represents the network-part of the address,
  // i.e, the address with all but the prefix bits masked out.
  IPAddress GetNetworkPart() const;

  // Return the default broadcast address for the IP address, by setting
  // all of the host-part bits to 1.
  IPAddress GetDefaultBroadcast();

  // Tests whether this IPAddress is able to directly access the address
  // |b| without an intervening gateway.  It tests whether the network
  // part of |b| is the same as the network part of |this|, using the
  // prefix of |this|.  Returns true if |b| is reachable, false otherwise.
  bool CanReachAddress(const IPAddress &b) const;

 private:
  Family family_;
  ByteString address_;
  unsigned int prefix_;
  // NO DISALLOW_COPY_AND_ASSIGN -- we assign IPAddresses in STL datatypes
};

}  // namespace shill

#endif  // SHILL_NET_IP_ADDRESS_H_
