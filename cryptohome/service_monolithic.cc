// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/command_line.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "cryptohome/service_monolithic.h"

#include "cryptohome/attestation_task.h"

namespace cryptohome {

const char kRetainEndorsementDataSwitch[] = "retain_endorsement_data";

// A helper function which maps an integer to a valid CertificateProfile.
CertificateProfile GetProfile(int profile_value) {
  // The protobuf compiler generates the _IsValid function.
  if (!CertificateProfile_IsValid(profile_value))
    return ENTERPRISE_USER_CERTIFICATE;
  return static_cast<CertificateProfile>(profile_value);
}

// A helper function which maps an integer to a valid Attestation::PCAType
// lower than kMaxPCAType.
Attestation::PCAType GetPCAType(int value) {
  if (value < 0 || value >= Attestation::kMaxPCAType)
    return Attestation::kDefaultPCA;
  return static_cast<Attestation::PCAType>(value);
}

// A helper function which maps an integer to a valid Attestation::VAType.
// lower than kMaxVAType.
Attestation::VAType GetVAType(int value) {
  if (value < 0 || value >= Attestation::kMaxVAType) {
    return Attestation::kDefaultVA;
  }
  return static_cast<Attestation::VAType>(value);
}

ServiceMonolithic::ServiceMonolithic(const std::string& abe_data)
    : default_attestation_(new Attestation()),
      attestation_(default_attestation_.get()) {
  if (!GetAttestationBasedEnterpriseEnrollmentData(abe_data, &abe_data_)) {
    LOG(FATAL) << "Invalid attestation-based enterprise enrollment data.";
  }
}

ServiceMonolithic::~ServiceMonolithic() {
  // Must be called here. Otherwise, after this destructor,
  // all pure virtual functions from Service overloaded here
  // and all members defined for this class will be gone, while
  // mount_thread_ will continue running tasks until stopped in
  // ~Service.
  StopTasks();
}

bool ServiceMonolithic::GetAttestationBasedEnterpriseEnrollmentData(
    const std::string& data, brillo::SecureBlob* abe_data) {
  // Remove trailing newlines.
  std::string trimmed;
  base::TrimWhitespaceASCII(data, base::TRIM_TRAILING, &trimmed);
  if (trimmed.empty()) {
    return true;   // Empty is ok.
  }
  // The data must be a valid 32 bytes (256 bits) hexadecimal string.
  if (!brillo::SecureBlob::HexStringToSecureBlob(trimmed, abe_data) ||
      abe_data->size() != 32) {
    abe_data->clear();
    return false;
  }
  return true;
}

void ServiceMonolithic::AttestationInitialize() {
  // Get data for attestation-based enterprise enrollment.
  if (abe_data_.empty())
    LOG(WARNING) << "Attestation-based enterprise enrollment"
                 << " will not be available.";

  // Pass in all the shared dependencies here rather than
  // needing to always get the Attestation object to set them
  // during testing.
  attestation_->Initialize(tpm_, tpm_init_, platform_, crypto_, install_attrs_,
     abe_data_, base::CommandLine::ForCurrentProcess()->HasSwitch(
         kRetainEndorsementDataSwitch));
}

void ServiceMonolithic::AttestationInitializeTpm() {
  attestation_->CacheEndorsementData();
  brillo::SecureBlob password;
  if (tpm_init_->IsTpmReady() && tpm_init_->GetTpmPassword(&password)) {
    attestation_->PrepareForEnrollmentAsync();
  }
}

void ServiceMonolithic::AttestationInitializeTpmComplete() {
  attestation_->PrepareForEnrollment();
}

void ServiceMonolithic::AttestationGetTpmStatus(GetTpmStatusReply* reply) {
  reply->set_attestation_prepared(attestation_->IsPreparedForEnrollment());
  reply->set_attestation_enrolled(attestation_->IsEnrolled());
  for (int i = 0, count = attestation_->GetIdentitiesCount(); i < count; ++i) {
    GetTpmStatusReply::Identity* identity = reply->mutable_identities()->Add();
    identity->set_features(attestation_->GetIdentityFeatures(i));
  }
  Attestation::IdentityCertificateMap map =
      attestation_->GetIdentityCertificateMap();
  for (auto it = map.cbegin(), end = map.cend(); it != end; ++it) {
    GetTpmStatusReply::IdentityCertificate identity_certificate;
    identity_certificate.set_identity(it->second.identity());
    identity_certificate.set_aca(it->second.aca());
    reply->mutable_identity_certificates()->insert(
        google::protobuf::Map<int, GetTpmStatusReply::IdentityCertificate>::
            value_type(it->first, identity_certificate));
  }
  for (int pca_type = Attestation::kDefaultPCA;
       pca_type < Attestation::kMaxPCAType; ++pca_type) {
    (*reply->mutable_enrollment_preparations())[pca_type] =
      attestation_->IsPreparedForEnrollmentWith(GetPCAType(pca_type));
  }
  reply->set_verified_boot_measured(attestation_->IsPCR0VerifiedMode());
}

bool ServiceMonolithic::AttestationGetDelegateCredentials(
      brillo::Blob* blob,
      brillo::Blob* secret,
      bool* has_reset_lock_permissions) {
  return attestation_->GetDelegateCredentials(blob, secret,
                                              has_reset_lock_permissions);
}

gboolean ServiceMonolithic::TpmIsAttestationPrepared(
    gboolean* OUT_prepared,
    GError** error) {
  *OUT_prepared = attestation_->IsPreparedForEnrollment();
  return TRUE;
}

bool ServiceMonolithic::AttestationGetEnrollmentPreparations(
    const AttestationGetEnrollmentPreparationsRequest& request,
    AttestationGetEnrollmentPreparationsReply* reply) {
  for (int pca_type = Attestation::kDefaultPCA;
       pca_type < Attestation::kMaxPCAType; ++pca_type) {
    if (!request.has_pca_type() || request.pca_type() == pca_type) {
      if (attestation_->IsPreparedForEnrollmentWith(GetPCAType(pca_type))) {
        (*reply->mutable_enrollment_preparations())[pca_type] = true;
      }
    }
  }
  return true;
}

gboolean ServiceMonolithic::TpmVerifyAttestationData(
    gboolean is_cros_core,
    gboolean* OUT_verified,
    GError** error) {
  *OUT_verified = attestation_->Verify(is_cros_core);
  return TRUE;
}

gboolean ServiceMonolithic::TpmVerifyEK(
    gboolean is_cros_core,
    gboolean* OUT_verified,
    GError** error) {
  *OUT_verified = attestation_->VerifyEK(is_cros_core);
  return TRUE;
}

gboolean ServiceMonolithic::TpmAttestationCreateEnrollRequest(
    gint pca_type,
    GArray** OUT_pca_request,
    GError** error) {
  // We must set the GArray now because if we return without setting it,
  // dbus-glib loops forever.
  *OUT_pca_request =
      g_array_new(false, false, sizeof(brillo::SecureBlob::value_type));
  brillo::SecureBlob blob;
  if (attestation_->CreateEnrollRequest(GetPCAType(pca_type), &blob))
    g_array_append_vals(*OUT_pca_request, blob.data(), blob.size());
  return TRUE;
}

gboolean ServiceMonolithic::AsyncTpmAttestationCreateEnrollRequest(
    gint pca_type,
    gint* OUT_async_id,
    GError** error) {
  AttestationTaskObserver* observer =
      new MountTaskObserverBridge(NULL, &event_source_);
  scoped_refptr<CreateEnrollRequestTask> task = new CreateEnrollRequestTask(
      observer, attestation_, GetPCAType(pca_type), NextSequence());
  *OUT_async_id = task->sequence_id();
  LogAsyncIdInfo(*OUT_async_id, __func__, base::Time::Now());
  PostTask(FROM_HERE, base::Bind(&CreateEnrollRequestTask::Run, task.get()));
  return TRUE;
}

gboolean ServiceMonolithic::TpmAttestationEnroll(
    gint pca_type,
    GArray* pca_response,
    gboolean* OUT_success,
    GError** error) {
  brillo::SecureBlob blob(pca_response->data,
                            pca_response->data + pca_response->len);
  *OUT_success = attestation_->Enroll(GetPCAType(pca_type), blob);
  return TRUE;
}

gboolean ServiceMonolithic::AsyncTpmAttestationEnroll(
    gint pca_type,
    GArray* pca_response,
    gint* OUT_async_id,
    GError** error) {
  brillo::SecureBlob blob(pca_response->data,
                            pca_response->data + pca_response->len);
  AttestationTaskObserver* observer =
      new MountTaskObserverBridge(NULL, &event_source_);
  scoped_refptr<EnrollTask> task = new EnrollTask(
      observer, attestation_, GetPCAType(pca_type), blob, NextSequence());
  *OUT_async_id = task->sequence_id();
  LogAsyncIdInfo(*OUT_async_id, __func__, base::Time::Now());
  PostTask(FROM_HERE, base::Bind(&EnrollTask::Run, task.get()));
  return TRUE;
}

gboolean ServiceMonolithic::TpmAttestationCreateCertRequest(
    gint pca_type,
    gint certificate_profile,
    gchar* username,
    gchar* request_origin,
    GArray** OUT_pca_request,
    GError** error) {
  // We must set the GArray now because if we return without setting it,
  // dbus-glib loops forever.
  *OUT_pca_request =
      g_array_new(false, false, sizeof(brillo::SecureBlob::value_type));
  brillo::SecureBlob blob;
  if (attestation_->CreateCertRequest(GetPCAType(pca_type),
                                      GetProfile(certificate_profile),
                                      username,
                                      request_origin,
                                      &blob))
    g_array_append_vals(*OUT_pca_request, blob.data(), blob.size());
  return TRUE;
}

gboolean ServiceMonolithic::AsyncTpmAttestationCreateCertRequest(
    gint pca_type,
    gint certificate_profile,
    gchar* username,
    gchar* request_origin,
    gint* OUT_async_id,
    GError** error) {
  AttestationTaskObserver* observer =
      new MountTaskObserverBridge(NULL, &event_source_);
  scoped_refptr<CreateCertRequestTask> task =
      new CreateCertRequestTask(observer,
                                attestation_,
                                GetPCAType(pca_type),
                                GetProfile(certificate_profile),
                                username,
                                request_origin,
                                NextSequence());
  *OUT_async_id = task->sequence_id();
  LogAsyncIdInfo(*OUT_async_id, __func__, base::Time::Now());
  PostTask(FROM_HERE, base::Bind(&CreateCertRequestTask::Run, task.get()));
  return TRUE;
}

gboolean ServiceMonolithic::TpmAttestationFinishCertRequest(
    GArray* pca_response,
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    GArray** OUT_cert,
    gboolean* OUT_success,
    GError** error) {
  // We must set the GArray now because if we return without setting it,
  // dbus-glib loops forever.
  *OUT_cert = g_array_new(false, false, sizeof(brillo::SecureBlob::value_type));
  brillo::SecureBlob response_blob(pca_response->data,
                                     pca_response->data + pca_response->len);
  brillo::SecureBlob cert_blob;
  *OUT_success = attestation_->FinishCertRequest(response_blob,
                                                 is_user_specific,
                                                 username,
                                                 key_name,
                                                 &cert_blob);
  if (*OUT_success)
    g_array_append_vals(*OUT_cert, cert_blob.data(), cert_blob.size());
  return TRUE;
}

gboolean ServiceMonolithic::AsyncTpmAttestationFinishCertRequest(
    GArray* pca_response,
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    gint* OUT_async_id,
    GError** error) {
  brillo::SecureBlob blob(pca_response->data,
                            pca_response->data + pca_response->len);
  AttestationTaskObserver* observer =
      new MountTaskObserverBridge(NULL, &event_source_);
  scoped_refptr<FinishCertRequestTask> task =
      new FinishCertRequestTask(observer,
                                attestation_,
                                blob,
                                is_user_specific,
                                username,
                                key_name,
                                NextSequence());
  *OUT_async_id = task->sequence_id();
  LogAsyncIdInfo(*OUT_async_id, __func__, base::Time::Now());
  PostTask(FROM_HERE, base::Bind(&FinishCertRequestTask::Run, task.get()));
  return TRUE;
}

gboolean ServiceMonolithic::TpmIsAttestationEnrolled(
    gboolean* OUT_is_enrolled,
    GError** error) {
  *OUT_is_enrolled = attestation_->IsEnrolled();
  return TRUE;
}

gboolean ServiceMonolithic::TpmAttestationDoesKeyExist(
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    gboolean *OUT_exists,
    GError** error) {
  *OUT_exists = attestation_->DoesKeyExist(is_user_specific,
                                           username,
                                           key_name);
  return TRUE;
}

gboolean ServiceMonolithic::TpmAttestationGetCertificate(
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    GArray **OUT_certificate,
    gboolean* OUT_success,
    GError** error) {
  *OUT_certificate =
      g_array_new(false, false, sizeof(brillo::SecureBlob::value_type));
  brillo::SecureBlob blob;
  *OUT_success = attestation_->GetCertificateChain(is_user_specific,
                                                   username,
                                                   key_name,
                                                   &blob);
  if (*OUT_success)
    g_array_append_vals(*OUT_certificate, blob.data(), blob.size());
  return TRUE;
}

gboolean ServiceMonolithic::TpmAttestationGetPublicKey(
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    GArray **OUT_public_key,
    gboolean* OUT_success,
    GError** error) {
  *OUT_public_key =
      g_array_new(false, false, sizeof(brillo::SecureBlob::value_type));
  brillo::SecureBlob blob;
  *OUT_success = attestation_->GetPublicKey(is_user_specific,
                                            username,
                                            key_name,
                                            &blob);
  if (*OUT_success)
    g_array_append_vals(*OUT_public_key, blob.data(), blob.size());
  return TRUE;
}

gboolean ServiceMonolithic::TpmAttestationRegisterKey(
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    gint *OUT_async_id,
    GError** error) {
  AttestationTaskObserver* observer =
      new MountTaskObserverBridge(NULL, &event_source_);
  scoped_refptr<RegisterKeyTask> task =
      new RegisterKeyTask(observer,
                          attestation_,
                          is_user_specific,
                          username,
                          key_name,
                          NextSequence());
  *OUT_async_id = task->sequence_id();
  LogAsyncIdInfo(*OUT_async_id, __func__, base::Time::Now());
  PostTask(FROM_HERE, base::Bind(&RegisterKeyTask::Run, task.get()));
  return TRUE;
}

gboolean ServiceMonolithic::TpmAttestationSignEnterpriseChallenge(
      gboolean is_user_specific,
      gchar* username,
      gchar* key_name,
      gchar* domain,
      GArray* device_id,
      gboolean include_signed_public_key,
      GArray* challenge,
      gint *OUT_async_id,
      GError** error) {
  return TpmAttestationSignEnterpriseVaChallenge(
      Attestation::kDefaultVA,
      is_user_specific,
      username,
      key_name,
      domain,
      device_id,
      include_signed_public_key,
      challenge,
      NULL,
      OUT_async_id,
      error);
}

gboolean ServiceMonolithic::TpmAttestationSignEnterpriseVaChallenge(
      gint va_type,
      gboolean is_user_specific,
      gchar* username,
      gchar* key_name,
      gchar* domain,
      GArray* device_id,
      gboolean include_signed_public_key,
      GArray* challenge,
      gchar* key_name_for_spkac,
      gint *OUT_async_id,
      GError** error) {
  brillo::SecureBlob device_id_blob(device_id->data,
                                      device_id->data + device_id->len);
  brillo::SecureBlob challenge_blob(challenge->data,
                                      challenge->data + challenge->len);
  AttestationTaskObserver* observer =
      new MountTaskObserverBridge(NULL, &event_source_);
  scoped_refptr<SignChallengeTask> task =
      new SignChallengeTask(observer,
                            attestation_,
                            GetVAType(va_type),
                            is_user_specific,
                            username,
                            key_name,
                            domain,
                            device_id_blob,
                            include_signed_public_key,
                            challenge_blob,
                            key_name_for_spkac ? key_name_for_spkac : "",
                            NextSequence());
  *OUT_async_id = task->sequence_id();
  LogAsyncIdInfo(*OUT_async_id, __func__, base::Time::Now());
  PostTask(FROM_HERE, base::Bind(&SignChallengeTask::Run, task.get()));
  return TRUE;
}

gboolean ServiceMonolithic::TpmAttestationSignSimpleChallenge(
      gboolean is_user_specific,
      gchar* username,
      gchar* key_name,
      GArray* challenge,
      gint *OUT_async_id,
      GError** error) {
  brillo::SecureBlob challenge_blob(challenge->data,
                                      challenge->data + challenge->len);
  AttestationTaskObserver* observer =
      new MountTaskObserverBridge(NULL, &event_source_);
  scoped_refptr<SignChallengeTask> task =
      new SignChallengeTask(observer,
                            attestation_,
                            is_user_specific,
                            username,
                            key_name,
                            challenge_blob,
                            NextSequence());
  *OUT_async_id = task->sequence_id();
  LogAsyncIdInfo(*OUT_async_id, __func__, base::Time::Now());
  PostTask(FROM_HERE, base::Bind(&SignChallengeTask::Run, task.get()));
  return TRUE;
}

gboolean ServiceMonolithic::TpmAttestationGetKeyPayload(
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    GArray** OUT_payload,
    gboolean* OUT_success,
    GError** error) {
  // We must set the GArray now because if we return without setting it,
  // dbus-glib loops forever.
  *OUT_payload =
      g_array_new(false, false, sizeof(brillo::SecureBlob::value_type));
  brillo::SecureBlob blob;
  *OUT_success = attestation_->GetKeyPayload(is_user_specific,
                                             username,
                                             key_name,
                                             &blob);
  if (*OUT_success)
    g_array_append_vals(*OUT_payload, blob.data(), blob.size());
  return TRUE;
}

gboolean ServiceMonolithic::TpmAttestationSetKeyPayload(
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    GArray* payload,
    gboolean* OUT_success,
    GError** error) {
  brillo::SecureBlob blob(payload->data, payload->data + payload->len);
  *OUT_success = attestation_->SetKeyPayload(is_user_specific,
                                             username,
                                             key_name,
                                             blob);
  return TRUE;
}

gboolean ServiceMonolithic::TpmAttestationDeleteKeys(
    gboolean is_user_specific,
    gchar* username,
    gchar* key_prefix,
    gboolean* OUT_success,
    GError** error) {
  *OUT_success = attestation_->DeleteKeysByPrefix(is_user_specific,
                                                  username,
                                                  key_prefix);
  return TRUE;
}

gboolean ServiceMonolithic::TpmAttestationDeleteKey(
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    gboolean* OUT_success,
    GError** error) {
  *OUT_success = attestation_->DeleteKey(is_user_specific,
                                         username,
                                         key_name);
  return TRUE;
}

gboolean ServiceMonolithic::TpmAttestationGetEK(
    gchar** OUT_ek_info,
    gboolean* OUT_success,
    GError** error) {
  std::string ek_info;
  *OUT_success = attestation_->GetEKInfo(&ek_info);
  *OUT_ek_info = g_strndup(ek_info.data(), ek_info.size());
  return TRUE;
}

gboolean ServiceMonolithic::TpmAttestationResetIdentity(
    gchar* reset_token,
    GArray** OUT_reset_request,
    gboolean* OUT_success,
    GError** error) {
  // We must set the GArray now because if we return without setting it,
  // dbus-glib loops forever.
  *OUT_reset_request =
      g_array_new(false, false, sizeof(brillo::SecureBlob::value_type));
  brillo::SecureBlob reset_request;
  *OUT_success = attestation_->GetIdentityResetRequest(
      std::string(reinterpret_cast<char*>(reset_token)),
      &reset_request);
  if (*OUT_success)
    g_array_append_vals(*OUT_reset_request,
                        reset_request.data(),
                        reset_request.size());
  return TRUE;
}

void ServiceMonolithic::DoGetEndorsementInfo(const brillo::SecureBlob& request,
                                   DBusGMethodInvocation* context) {
  GetEndorsementInfoRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context, "Bad GetEndorsementInfoRequest");
    return;
  }
  BaseReply reply;
  brillo::SecureBlob public_key;
  brillo::SecureBlob certificate;
  if (attestation_->GetCachedEndorsementData(&public_key, &certificate) ||
      (tpm_->GetEndorsementPublicKey(&public_key) == Tpm::kTpmRetryNone &&
       tpm_->GetEndorsementCredential(&certificate))) {
    GetEndorsementInfoReply* extension = reply.MutableExtension(
        GetEndorsementInfoReply::reply);
    extension->set_ek_public_key(public_key.to_string());
    if (!certificate.empty()) {
      extension->set_ek_certificate(certificate.to_string());
    }
  } else {
    reply.set_error(CRYPTOHOME_ERROR_TPM_EK_NOT_AVAILABLE);
  }
  SendReply(context, reply);
}

gboolean ServiceMonolithic::GetEndorsementInfo(const GArray* request,
                                     DBusGMethodInvocation* context) {
  PostTask(FROM_HERE,
           base::Bind(
               &ServiceMonolithic::DoGetEndorsementInfo, base::Unretained(this),
               brillo::SecureBlob(request->data, request->data + request->len),
               base::Unretained(context)));
  return TRUE;
}

void ServiceMonolithic::DoInitializeCastKey(const brillo::SecureBlob& request,
                                  DBusGMethodInvocation* context) {
  const char kCastCertificateOrigin[] = "CAST";
  const char kCastKeyLabel[] = "CERTIFIED_CAST_KEY";

  ReportDeprecatedApiCalled(DeprecatedApiEvent::kInitializeCastKey);

  LOG(INFO) << "Initializing Cast Key";
  InitializeCastKeyRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context, "Bad InitializeCastKeyRequest");
    return;
  }
  BaseReply reply;
  if (!attestation_->IsPreparedForEnrollment() ||
      !pkcs11_init_->IsSystemTokenOK()) {
    reply.set_error(CRYPTOHOME_ERROR_ATTESTATION_NOT_READY);
    SendReply(context, reply);
    return;
  }
  if (!attestation_->IsEnrolled()) {
    brillo::SecureBlob enroll_request;
    if (!attestation_->CreateEnrollRequest(Attestation::kDefaultPCA,
                                           &enroll_request)) {
      reply.set_error(CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR);
      SendReply(context, reply);
      return;
    }
    brillo::SecureBlob enroll_reply;
    if (!attestation_->SendPCARequestAndBlock(Attestation::kDefaultPCA,
                                              Attestation::kEnroll,
                                              enroll_request,
                                              &enroll_reply)) {
      reply.set_error(CRYPTOHOME_ERROR_CANNOT_CONNECT_TO_CA);
      SendReply(context, reply);
      return;
    }
    if (!attestation_->Enroll(Attestation::kDefaultPCA, enroll_reply)) {
      reply.set_error(CRYPTOHOME_ERROR_CA_REFUSED_ENROLLMENT);
      SendReply(context, reply);
      return;
    }
  }
  if (!attestation_->DoesKeyExist(false,  // is_user_specific
                                  "",     // username
                                  kCastKeyLabel)) {
    brillo::SecureBlob certificate_request;
    if (!attestation_->CreateCertRequest(Attestation::kDefaultPCA,
                                         CAST_CERTIFICATE,
                                         "",  // username
                                         kCastCertificateOrigin,
                                         &certificate_request)) {
      reply.set_error(CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR);
      SendReply(context, reply);
      return;
    }
    brillo::SecureBlob certificate_reply;
    if (!attestation_->SendPCARequestAndBlock(Attestation::kDefaultPCA,
                                              Attestation::kGetCertificate,
                                              certificate_request,
                                              &certificate_reply)) {
      reply.set_error(CRYPTOHOME_ERROR_CANNOT_CONNECT_TO_CA);
      SendReply(context, reply);
      return;
    }
    brillo::SecureBlob certificate_chain;
    if (!attestation_->FinishCertRequest(certificate_reply,
                                         false,  // is_user_specific
                                         "",     // username
                                         kCastKeyLabel,
                                         &certificate_chain)) {
      reply.set_error(CRYPTOHOME_ERROR_CA_REFUSED_CERTIFICATE);
      SendReply(context, reply);
      return;
    }
  }
  if (!attestation_->RegisterKey(false,    // is_user_specific
                                 "",       // username
                                 kCastKeyLabel,
                                 true)) {  // include_certificates
    reply.set_error(CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR);
    SendReply(context, reply);
    return;
  }
  SendReply(context, reply);
}

gboolean ServiceMonolithic::InitializeCastKey(const GArray* request,
                                    DBusGMethodInvocation* context) {
  PostTask(FROM_HERE,
           base::Bind(
               &ServiceMonolithic::DoInitializeCastKey, base::Unretained(this),
               brillo::SecureBlob(request->data, request->data + request->len),
               base::Unretained(context)));
  return TRUE;
}

gboolean ServiceMonolithic::TpmAttestationGetEnrollmentId(
    gboolean ignore_cache,
    GArray** OUT_enrollment_id,
    gboolean* OUT_success,
    GError** error) {
  *OUT_enrollment_id =
      g_array_new(false, false, sizeof(brillo::SecureBlob::value_type));
  brillo::SecureBlob enrollment_id;
  if (ignore_cache) {
    *OUT_success =
        attestation_->ComputeEnterpriseEnrollmentId(&enrollment_id);
  } else {
    *OUT_success =
        attestation_->GetEnterpriseEnrollmentId(&enrollment_id);
  }
  if (*OUT_success) {
    g_array_append_vals(*OUT_enrollment_id,
                        enrollment_id.data(),
                        enrollment_id.size());
  }
  return TRUE;
}

gboolean ServiceMonolithic::TpmAttestationEnrollEx(gint pca_type,
                                                   gboolean forced,
                                                   gboolean* OUT_success,
                                                   GError** error) {
  return FALSE;
}

gboolean ServiceMonolithic::AsyncTpmAttestationEnrollEx(gint pca_type,
                                                        gboolean forced,
                                                        gint* OUT_async_id,
                                                        GError** error) {
  return FALSE;
}

gboolean ServiceMonolithic::TpmAttestationGetCertificateEx(
    gint certificate_profile,
    gchar* username,
    gchar* request_origin,
    gint pca_type,
    gint key_type,
    gchar* key_name,
    gboolean forced,
    gboolean shall_trigger_enrollment,
    GArray** OUT_certificate,
    gboolean* OUT_success,
    GError** error) {
  return FALSE;
}

gboolean ServiceMonolithic::AsyncTpmAttestationGetCertificateEx(
    gint certificate_profile,
    gchar* username,
    gchar* request_origin,
    gint pca_type,
    gint key_type,
    gchar* key_name,
    gboolean forced,
    gboolean shall_trigger_enrollment,
    gint* OUT_async_id,
    GError** error) {
  return FALSE;
}

void ServiceMonolithic::ConnectOwnershipTakenSignal() {
  // Not supported by ServiceMonolithic, either because tpm_managerd doesn't
  // exist or cryptohomed doesn't talk to tpm_managerd.
}

void ServiceMonolithic::DoAutoCleanup() {
  Service::DoAutoCleanup();
  // In the monolithic mode we also have to reset da because we don't have
  // tpm_manager.
  LOG(INFO) << "Reset DA triggered in " << __func__;
  ResetDictionaryAttackMitigation();
}

}  // namespace cryptohome
