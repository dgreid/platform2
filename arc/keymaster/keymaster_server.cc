// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymaster/keymaster_server.h"

#include <utility>

#include "arc/keymaster/conversion.h"

namespace arc {
namespace keymaster {

namespace {

constexpr size_t kOperationTableSize = 16;

}  // namespace

KeymasterServer::KeymasterServer(const scoped_refptr<dbus::Bus>& bus)
    : context_(new context::ArcKeymasterContext(bus)),
      keymaster_(context_, kOperationTableSize) {}

void KeymasterServer::SetSystemVersion(uint32_t os_version,
                                       uint32_t os_patchlevel) {
  context_->SetSystemVersion(os_version, os_patchlevel);
}

void KeymasterServer::AddRngEntropy(const std::vector<uint8_t>& data,
                                    AddRngEntropyCallback callback) {
  // Prepare keymaster request.
  ::keymaster::AddEntropyRequest km_request;
  ConvertToMessage(data, &km_request.random_data);

  // Call keymaster.
  ::keymaster::AddEntropyResponse km_response;
  keymaster_.AddRngEntropy(km_request, &km_response);

  // Run callback.
  std::move(callback).Run(km_response.error);
}

void KeymasterServer::GetKeyCharacteristics(
    ::arc::mojom::GetKeyCharacteristicsRequestPtr request,
    GetKeyCharacteristicsCallback callback) {
  // Prepare keymaster request.
  auto km_request = MakeGetKeyCharacteristicsRequest(request);

  // Call keymaster.
  ::keymaster::GetKeyCharacteristicsResponse km_response;
  keymaster_.GetKeyCharacteristics(*km_request, &km_response);

  // Prepare mojo response.
  auto response = MakeGetKeyCharacteristicsResult(km_response);

  // Run callback.
  std::move(callback).Run(std::move(response));
}

void KeymasterServer::GenerateKey(
    std::vector<mojom::KeyParameterPtr> key_params,
    GenerateKeyCallback callback) {
  // Prepare keymaster request.
  auto km_request = MakeGenerateKeyRequest(key_params);

  // Call keymaster.
  ::keymaster::GenerateKeyResponse km_response;
  keymaster_.GenerateKey(*km_request, &km_response);

  // Prepare mojo response.
  auto response = MakeGenerateKeyResult(km_response);

  // Run callback.
  std::move(callback).Run(std::move(response));
}

void KeymasterServer::ImportKey(arc::mojom::ImportKeyRequestPtr request,
                                ImportKeyCallback callback) {
  // Prepare keymaster request.
  auto km_request = MakeImportKeyRequest(request);

  // Call keymaster.
  ::keymaster::ImportKeyResponse km_response;
  keymaster_.ImportKey(*km_request, &km_response);

  // Prepare mojo response.
  auto response = MakeImportKeyResult(km_response);

  // Run callback.
  std::move(callback).Run(std::move(response));
}

void KeymasterServer::ExportKey(arc::mojom::ExportKeyRequestPtr request,
                                ExportKeyCallback callback) {
  // Prepare keymaster request.
  auto km_request = MakeExportKeyRequest(request);

  // Call keymaster.
  ::keymaster::ExportKeyResponse km_response;
  keymaster_.ExportKey(*km_request, &km_response);

  // Prepare mojo response.
  auto response = MakeExportKeyResult(km_response);

  // Run callback.
  std::move(callback).Run(std::move(response));
}

void KeymasterServer::AttestKey(arc::mojom::AttestKeyRequestPtr request,
                                AttestKeyCallback callback) {
  // Prepare keymaster request.
  auto km_request = MakeAttestKeyRequest(request);

  // Call keymaster.
  ::keymaster::AttestKeyResponse km_response;
  keymaster_.AttestKey(*km_request, &km_response);

  // Prepare mojo response.
  auto response = MakeAttestKeyResult(km_response);

  // Run callback.
  std::move(callback).Run(std::move(response));
}

void KeymasterServer::UpgradeKey(arc::mojom::UpgradeKeyRequestPtr request,
                                 UpgradeKeyCallback callback) {
  // Prepare keymaster request.
  auto km_request = MakeUpgradeKeyRequest(request);

  // Call keymaster.
  ::keymaster::UpgradeKeyResponse km_response;
  keymaster_.UpgradeKey(*km_request, &km_response);

  // Prepare mojo response.
  auto response = MakeUpgradeKeyResult(km_response);

  // Run callback.
  std::move(callback).Run(std::move(response));
}

void KeymasterServer::DeleteKey(const std::vector<uint8_t>& key_blob,
                                DeleteKeyCallback callback) {
  // Prepare keymaster request.
  ::keymaster::DeleteKeyRequest km_request;
  km_request.SetKeyMaterial(key_blob.data(), key_blob.size());

  // Call keymaster.
  ::keymaster::DeleteKeyResponse km_response;
  keymaster_.DeleteKey(km_request, &km_response);

  // Run callback.
  std::move(callback).Run(km_response.error);
}

void KeymasterServer::DeleteAllKeys(DeleteAllKeysCallback callback) {
  // Call keymaster (nothing to prepare on DeleteAllKeys).
  ::keymaster::DeleteAllKeysRequest km_request;
  ::keymaster::DeleteAllKeysResponse km_response;
  keymaster_.DeleteAllKeys(km_request, &km_response);

  // Run callback.
  std::move(callback).Run(km_response.error);
}

void KeymasterServer::Begin(arc::mojom::BeginRequestPtr request,
                            BeginCallback callback) {
  // Prepare keymaster request.
  auto km_request = MakeBeginOperationRequest(request);

  // Call keymaster.
  ::keymaster::BeginOperationResponse km_response;
  keymaster_.BeginOperation(*km_request, &km_response);

  // Prepare mojo response.
  auto response = MakeBeginResult(km_response);

  // Run callback.
  std::move(callback).Run(std::move(response));
}

void KeymasterServer::Update(arc::mojom::UpdateRequestPtr request,
                             UpdateCallback callback) {
  // Prepare keymaster request.
  auto km_request = MakeUpdateOperationRequest(request);

  // Call keymaster.
  ::keymaster::UpdateOperationResponse km_response;
  keymaster_.UpdateOperation(*km_request, &km_response);

  // Prepare mojo response.
  auto response = MakeUpdateResult(km_response);

  // Run callback.
  std::move(callback).Run(std::move(response));
}

void KeymasterServer::Finish(arc::mojom::FinishRequestPtr request,
                             FinishCallback callback) {
  // Prepare keymaster request.
  auto km_request = MakeFinishOperationRequest(request);

  // Call keymaster.
  ::keymaster::FinishOperationResponse km_response;
  keymaster_.FinishOperation(*km_request, &km_response);

  // Prepare mojo response.
  auto response = MakeFinishResult(km_response);

  // Run callback.
  std::move(callback).Run(std::move(response));
}

void KeymasterServer::Abort(uint64_t op_handle, AbortCallback callback) {
  // Prepare keymaster request.
  ::keymaster::AbortOperationRequest km_request;
  km_request.op_handle = op_handle;

  // Call keymaster.
  ::keymaster::AbortOperationResponse km_response;
  keymaster_.AbortOperation(km_request, &km_response);

  // Run callback.
  std::move(callback).Run(km_response.error);
}

}  // namespace keymaster
}  // namespace arc
