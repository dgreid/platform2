// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/common/system/debugd_adapter_impl.h"

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <brillo/errors/error.h>

namespace diagnostics {

namespace {

constexpr char kSmartctlAttributesOption[] = "attributes";
constexpr char kNvmeIdentityOption[] = "identify_controller";

auto CreateSuccessCallback(
    const DebugdAdapter::StringResultCallback& callback) {
  return base::Bind(
      [](const DebugdAdapter::StringResultCallback& callback,
         const std::string& result) { callback.Run(result, nullptr); },
      callback);
}

auto CreateErrorCallback(const DebugdAdapter::StringResultCallback& callback) {
  return base::Bind(
      [](const DebugdAdapter::StringResultCallback& callback,
         brillo::Error* error) { callback.Run(std::string(), error); },
      callback);
}

}  // namespace

DebugdAdapterImpl::DebugdAdapterImpl(
    std::unique_ptr<org::chromium::debugdProxyInterface> debugd_proxy)
    : debugd_proxy_(std::move(debugd_proxy)) {
  DCHECK(debugd_proxy_);
}

DebugdAdapterImpl::~DebugdAdapterImpl() = default;

void DebugdAdapterImpl::GetSmartAttributes(
    const StringResultCallback& callback) {
  debugd_proxy_->SmartctlAsync(kSmartctlAttributesOption,
                               CreateSuccessCallback(callback),
                               CreateErrorCallback(callback));
}

void DebugdAdapterImpl::GetNvmeIdentity(const StringResultCallback& callback) {
  debugd_proxy_->NvmeAsync(kNvmeIdentityOption, CreateSuccessCallback(callback),
                           CreateErrorCallback(callback));
}

}  // namespace diagnostics
