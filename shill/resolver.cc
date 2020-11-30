// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/resolver.h"

#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_util.h>

#include "shill/dns_util.h"
#include "shill/ipconfig.h"
#include "shill/logging.h"
#include "shill/net/ip_address.h"

using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kResolver;
static string ObjectID(const Resolver* r) {
  return "(resolver)";
}
}  // namespace Logging

const char Resolver::kDefaultIgnoredSearchList[] = "gateway.2wire.net";

Resolver::Resolver() = default;

Resolver::~Resolver() = default;

Resolver* Resolver::GetInstance() {
  static base::NoDestructor<Resolver> instance;
  return instance.get();
}

bool Resolver::SetDNSFromLists(const std::vector<std::string>& dns_servers,
                               const std::vector<std::string>& domain_search) {
  SLOG(this, 2) << __func__;

  if (dns_servers.empty() && domain_search.empty()) {
    SLOG(this, 2) << "DNS list is empty";
    return ClearDNS();
  }

  vector<string> lines;
  for (const auto& server : dns_servers) {
    IPAddress addr(server);
    std::string canonical_ip;
    if (addr.family() != IPAddress::kFamilyUnknown &&
        addr.IntoString(&canonical_ip)) {
      lines.push_back("nameserver " + canonical_ip);
    } else {
      LOG(WARNING) << "Malformed nameserver IP: " << server;
    }
  }

  vector<string> filtered_domain_search;
  for (const auto& domain : domain_search) {
    if (base::Contains(ignored_search_list_, domain)) {
      continue;
    }
    if (IsValidDNSDomain(domain)) {
      filtered_domain_search.push_back(domain);
    } else {
      LOG(WARNING) << "Malformed search domain: " << domain;
    }
  }

  if (!filtered_domain_search.empty()) {
    lines.push_back("search " + base::JoinString(filtered_domain_search, " "));
  }

  // - Send queries one-at-a-time, rather than parallelizing IPv4
  //   and IPv6 queries for a single host.
  // - Override the default 5-second request timeout and use a
  //   1-second timeout instead. (NOTE: Chrome's ADNS will use
  //   one second, regardless of what we put here.)
  // - Allow 5 attempts, rather than the default of 2.
  //   - For glibc, the worst case number of queries will be
  //        attempts * count(servers) * (count(search domains)+1)
  //   - For Chrome, the worst case number of queries will be
  //        attempts * count(servers) + 3 * glibc
  //   See crbug.com/224756 for supporting data.
  lines.push_back("options single-request timeout:1 attempts:5");

  // Newline at end of file
  lines.push_back("");

  string contents = base::JoinString(lines, "\n");

  SLOG(this, 2) << "Writing DNS out to " << path_.value();
  int count = base::WriteFile(path_, contents.c_str(), contents.size());

  return count == static_cast<int>(contents.size());
}

bool Resolver::ClearDNS() {
  SLOG(this, 2) << __func__;

  CHECK(!path_.empty());

  return base::DeleteFile(path_);
}

}  // namespace shill
