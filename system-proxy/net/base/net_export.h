// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSTEM_PROXY_NET_BASE_NET_EXPORT_H_
#define SYSTEM_PROXY_NET_BASE_NET_EXPORT_H_

// The chromium net module can be built as a component build which exposes
// functionality to consumers using NET_EXPORT and NET_EXPORT_PRIVATE macros.
// System-proxy builds the //net/ntlm code as part of libsystemproxy so
// exporting the functionality is not necessary.
#define NET_EXPORT
#define NET_EXPORT_PRIVATE

#endif  // SYSTEM_PROXY_NET_BASE_NET_EXPORT_H_
