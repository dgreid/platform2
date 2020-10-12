// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymaster/context/cros_key.h"

#include <algorithm>
#include <utility>

#include <base/logging.h>
#include <base/optional.h>
#include <keymaster/keymaster_tags.h>

namespace arc {
namespace keymaster {
namespace context {

namespace {

OperationType ConvertKeymasterPurposeToOperationType(
    keymaster_purpose_t purpose) {
  switch (purpose) {
    case KM_PURPOSE_SIGN:
      return OperationType::kSign;
    case KM_PURPOSE_ENCRYPT:
    case KM_PURPOSE_DECRYPT:
    case KM_PURPOSE_VERIFY:
    case KM_PURPOSE_DERIVE_KEY:
    case KM_PURPOSE_WRAP:
      return OperationType::kUnsupported;
  }
}

Algorithm FindOperationAlgorithm(const ::keymaster::Operation& operation) {
  keymaster_algorithm_t algorithm;

  if (!operation.authorizations().GetTagValue(::keymaster::TAG_ALGORITHM,
                                              &algorithm)) {
    return Algorithm::kUnsupported;
  }

  switch (algorithm) {
    case KM_ALGORITHM_RSA:
      return Algorithm::kRsa;
    case KM_ALGORITHM_AES:
    case KM_ALGORITHM_EC:
    case KM_ALGORITHM_TRIPLE_DES:
    case KM_ALGORITHM_HMAC:
      return Algorithm::kUnsupported;
  }
}

Digest FindOperationDigest(const ::keymaster::Operation& operation) {
  keymaster_digest_t digest;

  if (!operation.authorizations().GetTagValue(::keymaster::TAG_DIGEST,
                                              &digest)) {
    return Digest::kNone;
  }

  switch (digest) {
    case KM_DIGEST_NONE:
      return Digest::kNone;
    case KM_DIGEST_SHA_2_256:
      return Digest::kSha256;
    case KM_DIGEST_MD5:
    case KM_DIGEST_SHA1:
    case KM_DIGEST_SHA_2_224:
    case KM_DIGEST_SHA_2_384:
    case KM_DIGEST_SHA_2_512:
      return Digest::kUnsupported;
  }
}

Padding FindOperationPadding(const ::keymaster::Operation& operation) {
  keymaster_padding_t padding;

  if (!operation.authorizations().GetTagValue(::keymaster::TAG_PADDING,
                                              &padding)) {
    return Padding ::kNone;
  }

  switch (padding) {
    case KM_PAD_NONE:
      return Padding::kNone;
    case KM_PAD_PKCS7:
      return Padding::kPkcs7;
    case KM_PAD_RSA_PKCS1_1_5_ENCRYPT:
    case KM_PAD_RSA_PKCS1_1_5_SIGN:
      return Padding::kPkcs1;
    case KM_PAD_RSA_OAEP:
    case KM_PAD_RSA_PSS:
      return Padding::kUnsupported;
  }
}

BlockMode FindOperationBlockMode(const ::keymaster::Operation& operation) {
  keymaster_block_mode_t block_mode;

  if (!operation.authorizations().GetTagValue(::keymaster::TAG_BLOCK_MODE,
                                              &block_mode)) {
    return BlockMode::kNone;
  }

  switch (block_mode) {
    case KM_MODE_CBC:
      return BlockMode::kCbc;
    case KM_MODE_ECB:
    case KM_MODE_CTR:
    case KM_MODE_GCM:
      return BlockMode::kUnsupported;
  }
}

MechanismDescription CreateOperationDescriptionFromOperation(
    const ::keymaster::Operation& operation) {
  return MechanismDescription(
      ConvertKeymasterPurposeToOperationType(operation.purpose()),
      FindOperationAlgorithm(operation), FindOperationDigest(operation),
      FindOperationPadding(operation), FindOperationBlockMode(operation));
}

}  // anonymous namespace

CrosKeyFactory::CrosKeyFactory(base::WeakPtr<ContextAdaptor> context_adaptor,
                               keymaster_algorithm_t algorithm)
    : context_adaptor_(context_adaptor),
      sign_factory_(
          std::make_unique<CrosOperationFactory>(algorithm, KM_PURPOSE_SIGN)) {}

keymaster_error_t CrosKeyFactory::LoadKey(
    KeyData&& key_data,
    ::keymaster::AuthorizationSet&& hw_enforced,
    ::keymaster::AuthorizationSet&& sw_enforced,
    ::keymaster::UniquePtr<::keymaster::Key>* key) const {
  switch (key_data.data_case()) {
    case KeyData::kArcKey:
      NOTREACHED() << "CrosKeyFactory cannot load ARC keys.";
      return KM_ERROR_UNIMPLEMENTED;
    case KeyData::DATA_NOT_SET:
      LOG(ERROR) << "Tried to load CrOS key but KeyData is not set.";
      return KM_ERROR_UNKNOWN_ERROR;
  }
}

keymaster_error_t CrosKeyFactory::LoadKey(
    ::keymaster::KeymasterKeyBlob&& key_material,
    const ::keymaster::AuthorizationSet& additional_params,
    ::keymaster::AuthorizationSet&& hw_enforced,
    ::keymaster::AuthorizationSet&& sw_enforced,
    ::keymaster::UniquePtr<::keymaster::Key>* key) const {
  NOTREACHED() << __func__ << " should never be called";
  return KM_ERROR_UNIMPLEMENTED;
}

::keymaster::OperationFactory* CrosKeyFactory::GetOperationFactory(
    keymaster_purpose_t purpose) const {
  switch (purpose) {
    case KM_PURPOSE_SIGN:
      return sign_factory_.get();
    case KM_PURPOSE_ENCRYPT:
    case KM_PURPOSE_DECRYPT:
    case KM_PURPOSE_VERIFY:
    case KM_PURPOSE_DERIVE_KEY:
    case KM_PURPOSE_WRAP:
      LOG(WARNING) << "No factory for purpose=" << purpose;
      return nullptr;
  }
}

keymaster_error_t CrosKeyFactory::GenerateKey(
    const ::keymaster::AuthorizationSet& key_description,
    ::keymaster::KeymasterKeyBlob* key_blob,
    ::keymaster::AuthorizationSet* hw_enforced,
    ::keymaster::AuthorizationSet* sw_enforced) const {
  NOTREACHED() << __func__ << " should never be called";
  return KM_ERROR_UNIMPLEMENTED;
}

keymaster_error_t CrosKeyFactory::ImportKey(
    const ::keymaster::AuthorizationSet& key_description,
    keymaster_key_format_t input_key_material_format,
    const ::keymaster::KeymasterKeyBlob& input_key_material,
    ::keymaster::KeymasterKeyBlob* output_key_blob,
    ::keymaster::AuthorizationSet* hw_enforced,
    ::keymaster::AuthorizationSet* sw_enforced) const {
  NOTREACHED() << __func__ << " should never be called";
  return KM_ERROR_UNIMPLEMENTED;
}

const keymaster_key_format_t* CrosKeyFactory::SupportedImportFormats(
    size_t* format_count) const {
  NOTREACHED() << __func__ << " should never be called";
  *format_count = 0;
  return nullptr;
}

const keymaster_key_format_t* CrosKeyFactory::SupportedExportFormats(
    size_t* format_count) const {
  NOTREACHED() << __func__ << " should never be called";
  *format_count = 0;
  return nullptr;
}

CrosKey::CrosKey(::keymaster::AuthorizationSet&& hw_enforced,
                 ::keymaster::AuthorizationSet&& sw_enforced,
                 const CrosKeyFactory* key_factory,
                 KeyData&& key_data)
    : ::keymaster::Key(
          std::move(hw_enforced), std::move(sw_enforced), key_factory),
      key_data_(std::move(key_data)) {}

CrosKey::~CrosKey() = default;

CrosOperationFactory::CrosOperationFactory(keymaster_algorithm_t algorithm,
                                           keymaster_purpose_t purpose)
    : algorithm_(algorithm), purpose_(purpose) {}

CrosOperationFactory::~CrosOperationFactory() = default;

::keymaster::OperationFactory::KeyType CrosOperationFactory::registry_key()
    const {
  return ::keymaster::OperationFactory::KeyType(algorithm_, purpose_);
}

::keymaster::OperationPtr CrosOperationFactory::CreateOperation(
    ::keymaster::Key&& key,
    const ::keymaster::AuthorizationSet& begin_params,
    keymaster_error_t* error) {
  NOTREACHED() << "No CrosOperation implementation for this key.";
  *error = KM_ERROR_UNIMPLEMENTED;
  return nullptr;
}

CrosOperation::CrosOperation(keymaster_purpose_t purpose, CrosKey&& key)
    : ::keymaster::Operation(
          purpose, key.hw_enforced_move(), key.sw_enforced_move()) {}

CrosOperation::~CrosOperation() = default;

keymaster_error_t CrosOperation::Begin(
    const ::keymaster::AuthorizationSet& /* input_params */,
    ::keymaster::AuthorizationSet* /* output_params */) {
  MechanismDescription d = CreateOperationDescriptionFromOperation(*this);

  base::Optional<uint64_t> handle = operation_->Begin(d);

  if (!handle.has_value())
    return KM_ERROR_UNKNOWN_ERROR;

  operation_handle_ = handle.value();
  return KM_ERROR_OK;
}

keymaster_error_t CrosOperation::Update(
    const ::keymaster::AuthorizationSet& /* input_params */,
    const ::keymaster::Buffer& input,
    ::keymaster::AuthorizationSet* /* output_params */,
    ::keymaster::Buffer* /* output */,
    size_t* input_consumed) {
  brillo::Blob input_blob(input.begin(), input.end());
  base::Optional<brillo::Blob> output = operation_->Update(input_blob);

  if (!output.has_value()) {
    *input_consumed = 0;
    return KM_ERROR_UNKNOWN_ERROR;
  }

  *input_consumed = input_blob.size();
  return KM_ERROR_OK;
}

keymaster_error_t CrosOperation::Finish(
    const ::keymaster::AuthorizationSet& /* input_params */,
    const ::keymaster::Buffer& input,
    const ::keymaster::Buffer& /* signature */,
    ::keymaster::AuthorizationSet* /* output_params */,
    ::keymaster::Buffer* output) {
  // Run an update with the last piece of input, if any.
  if (input.available_read() > 0) {
    brillo::Blob input_blob(input.begin(), input.end());
    base::Optional<brillo::Blob> updateResult = operation_->Update(input_blob);

    if (!updateResult.has_value())
      return KM_ERROR_UNKNOWN_ERROR;
  }

  base::Optional<brillo::Blob> finish_result = operation_->Finish();
  if (!finish_result.has_value())
    return KM_ERROR_UNKNOWN_ERROR;

  output->Reinitialize(finish_result->size());
  output->write(finish_result->data(), finish_result->size());
  return KM_ERROR_OK;
}

keymaster_error_t CrosOperation::Abort() {
  return operation_->Abort() ? KM_ERROR_OK : KM_ERROR_UNKNOWN_ERROR;
}

}  // namespace context
}  // namespace keymaster
}  // namespace arc
