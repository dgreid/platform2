// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "kerberos/config_parser.h"
#include "kerberos/krb5_interface_impl.h"

#include <stddef.h>
#include <stdint.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "base/logging.h"

struct Environment {
  Environment() { logging::SetMinLogLevel(logging::LOGGING_FATAL); }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  const std::string krb5conf(reinterpret_cast<const char*>(data), size);

  // Note: Krb5InterfaceImpl owns and calls a ConfigParser, but it also runs
  // the MIT krb5 parsing code, so we use that.
  kerberos::Krb5InterfaceImpl krb5;

  kerberos::ConfigErrorInfo error_info;
  krb5.ValidateConfig(krb5conf, &error_info);

  return 0;
}
