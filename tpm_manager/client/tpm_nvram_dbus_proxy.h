// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM_MANAGER_CLIENT_TPM_NVRAM_DBUS_PROXY_H_
#define TPM_MANAGER_CLIENT_TPM_NVRAM_DBUS_PROXY_H_

#include "tpm_manager/common/tpm_nvram_interface.h"

#include <string>

#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <dbus/bus.h>
#include <dbus/object_proxy.h>

#include "tpm_manager/common/export.h"

namespace tpm_manager {

// An implementation of TpmNvramInterface that forwards requests to
// tpm_managerd over D-Bus.
// Usage:
// std::unique_ptr<TpmNvramInterface> tpm_manager = new TpmNvramDBusProxy();
// tpm_manager->DefineSpace(...);
class TPM_MANAGER_EXPORT TpmNvramDBusProxy : public TpmNvramInterface {
 public:
  TpmNvramDBusProxy() = default;
  TpmNvramDBusProxy(const TpmNvramDBusProxy&) = delete;
  TpmNvramDBusProxy& operator=(const TpmNvramDBusProxy&) = delete;

  ~TpmNvramDBusProxy() override;

  // Performs initialization tasks. This method must be called before calling
  // any other method in this class. Returns true on success.
  bool Initialize();

  // TpmNvramInterface methods.
  void DefineSpace(const DefineSpaceRequest& request,
                   const DefineSpaceCallback& callback) override;
  void DestroySpace(const DestroySpaceRequest& request,
                    const DestroySpaceCallback& callback) override;
  void WriteSpace(const WriteSpaceRequest& request,
                  const WriteSpaceCallback& callback) override;
  void ReadSpace(const ReadSpaceRequest& request,
                 const ReadSpaceCallback& callback) override;
  void LockSpace(const LockSpaceRequest& request,
                 const LockSpaceCallback& callback) override;
  void ListSpaces(const ListSpacesRequest& request,
                  const ListSpacesCallback& callback) override;
  void GetSpaceInfo(const GetSpaceInfoRequest& request,
                    const GetSpaceInfoCallback& callback) override;

  void set_object_proxy(dbus::ObjectProxy* object_proxy) {
    object_proxy_ = object_proxy;
  }

 private:
  // Template method to call a given |method_name| remotely via dbus.
  template <typename ReplyProtobufType,
            typename RequestProtobufType,
            typename CallbackType>
  void CallMethod(const std::string& method_name,
                  const RequestProtobufType& request,
                  const CallbackType& callback);

  scoped_refptr<dbus::Bus> bus_;
  dbus::ObjectProxy* object_proxy_;
};

}  // namespace tpm_manager

#endif  // TPM_MANAGER_CLIENT_TPM_NVRAM_DBUS_PROXY_H_
