// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/service_distributed.h"

#include <base/bind.h>
#include <base/message_loop/message_pump_type.h>
#include <base/strings/string_number_conversions.h>
#include <tpm_manager/client/tpm_manager_utility.h>

#include "attestation/client/dbus_proxy.h"
#include "cryptohome/cryptolib.h"

#include <utility>
using attestation::AttestationInterface;
using attestation::AttestationStatus;

namespace cryptohome {

// A helper function which maps an integer to a valid ACAType.
gboolean ServiceDistributed::ConvertIntegerToACAType(
    gint type, attestation::ACAType* aca_type, GError** error) {
  if (!attestation::ACAType_IsValid(type)) {
    ReportUnsupportedACAType(error, type);
    return FALSE;
  }
  *aca_type = static_cast< ::attestation::ACAType>(type);
  return TRUE;
}

// A helper function which maps an integer to a valid VAType.
gboolean ServiceDistributed::ConvertIntegerToVAType(
    gint type, attestation::VAType* va_type, GError** error) {
  if (!attestation::VAType_IsValid(type)) {
    ReportUnsupportedVAType(error, type);
    return FALSE;
  }
  *va_type = static_cast< ::attestation::VAType>(type);
  return TRUE;
}

// A helper function which maps an integer to a valid KeyType.
gboolean ServiceDistributed::ConvertIntegerToKeyType(
    gint type, attestation::KeyType* key_type, GError** error) {
  if (type < attestation::KeyType_MIN || type > attestation::KeyType_MAX) {
    ReportUnsupportedKeyType(error, type);
    return FALSE;
  }
  *key_type = static_cast<attestation::KeyType>(type);
  return TRUE;
}

// A helper function which maps an integer to a valid CertificateProfile.
attestation::CertificateProfile ServiceDistributed::GetProfile(
    int profile_value) {
  // The protobuf compiler generates the _IsValid function.
  if (!attestation::CertificateProfile_IsValid(profile_value)) {
    return attestation::CertificateProfile::ENTERPRISE_USER_CERTIFICATE;
  }
  return static_cast<attestation::CertificateProfile>(profile_value);
}

ServiceDistributed::ServiceDistributed()
    : default_attestation_interface_(new attestation::DBusProxy()),
      attestation_interface_(default_attestation_interface_.get()),
      weak_factory_(this) {}

ServiceDistributed::~ServiceDistributed() {
  attestation_thread_.Stop();
  // Must be called here. Otherwise, after this destructor,
  // all pure virtual functions from Service overloaded here
  // and all members defined for this class will be gone, while
  // mount_thread_ will continue running tasks until stopped in
  // ~Service.
  StopTasks();
}

bool ServiceDistributed::PrepareInterface() {
  if (attestation_thread_.IsRunning()) {
    return true;
  }
  if (!attestation_thread_.StartWithOptions(base::Thread::Options(
          base::MessagePumpType::IO, 0 /* Default stack size. */))) {
    LOG(ERROR) << "Failed to start attestation thread.";
    return false;
  }
  DLOG(INFO) << "Started attestation thread.";
  return true;
}

bool ServiceDistributed::ObtainTpmStatus(attestation::GetStatusReply* reply,
                                         GError** error) {
  attestation::GetStatusRequest request;
  request.set_extended_status(false);
  auto method = base::Bind(&AttestationInterface::GetStatus,
                           base::Unretained(attestation_interface_), request);
  if (!SendRequestAndWait(method, reply)) {
    ReportSendFailure(error);
    return false;
  }
  if (reply->status() != AttestationStatus::STATUS_SUCCESS) {
    ReportErrorFromStatus(error, reply->status());
    return false;
  }
  return true;
}

base::WeakPtr<ServiceDistributed> ServiceDistributed::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void ServiceDistributed::ReportErrorFromStatus(
    GError** error, attestation::AttestationStatus status) {
  VLOG(1) << "Attestation daemon returned status " << status;
  g_set_error(error, DBUS_GERROR, DBUS_GERROR_FAILED,
              "Attestation daemon returned status %d", status);
}

void ServiceDistributed::ReportSendFailure(GError** error) {
  g_set_error_literal(error, DBUS_GERROR, DBUS_GERROR_FAILED,
                      "Failed sending to attestation daemon");
}

void ServiceDistributed::ReportUnsupportedACAType(GError** error, int type) {
  VLOG(1) << "ACA type is not supported: " << type;
  g_set_error(error, DBUS_GERROR, DBUS_GERROR_NOT_SUPPORTED,
              "Requested ACA type is not supported");
}

void ServiceDistributed::ReportUnsupportedVAType(GError** error, int type) {
  VLOG(1) << "VA type is not supported: " << type;
  g_set_error(error, DBUS_GERROR, DBUS_GERROR_NOT_SUPPORTED,
              "Requested VA type is not supported");
}

void ServiceDistributed::ReportUnsupportedKeyType(GError** error, int type) {
  VLOG(1) << "Key type is not supported: " << type;
  g_set_error(error, DBUS_GERROR, DBUS_GERROR_NOT_SUPPORTED,
              "Requested Key type is not supported");
}

template <typename MethodType>
bool ServiceDistributed::Post(const MethodType& method) {
  VLOG(2) << __func__;
  if (!PrepareInterface()) {
    return false;
  }
  attestation_thread_.task_runner()->PostTask(FROM_HERE, method);
  VLOG(2) << __func__ << ": posted";
  return true;
}

template <typename MethodType>
bool ServiceDistributed::PostAndWait(const MethodType& method) {
  VLOG(2) << __func__;
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  auto sync_method = base::Bind(
      [](const MethodType& method, base::WaitableEvent* event) {
        method.Run();
        event->Signal();
      },
      method, &event);
  if (!Post(sync_method)) {
    return false;
  }
  VLOG(2) << __func__ << ": posted";
  event.Wait();
  VLOG(2) << __func__ << ": completed";
  return true;
}

template <typename ReplyProtoType, typename MethodType>
bool ServiceDistributed::SendRequestAndWait(const MethodType& method,
                                            ReplyProtoType* reply_proto) {
  VLOG(2) << __func__;
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  auto callback = base::Bind(
      [](ReplyProtoType* reply_proto, base::WaitableEvent* event,
         const ReplyProtoType& reply) {
        *reply_proto = reply;
        event->Signal();
      },
      reply_proto, &event);
  if (!Post(base::Bind(method, callback))) {
    return false;
  }
  event.Wait();
  VLOG(2) << __func__ << ": completed";
  return true;
}

template <typename ReplyProtoType>
void ServiceDistributed::ProcessStatusReply(int async_id,
                                            const ReplyProtoType& reply) {
  VLOG(1) << __func__;
  auto r = std::make_unique<MountTaskResult>();
  VLOG(3) << "attestationd reply:"
          << " async_id=" << async_id << " status=" << reply.status();
  VLOG_IF(1, reply.status() != AttestationStatus::STATUS_SUCCESS)
      << "Attestation daemon returned status " << reply.status();
  r->set_sequence_id(async_id);
  r->set_return_status(reply.status() == AttestationStatus::STATUS_SUCCESS);
  event_source_.AddEvent(std::move(r));
}

template <typename ReplyProtoType>
void ServiceDistributed::ProcessDataReply(
    const std::string& (ReplyProtoType::*func)() const,
    int async_id,
    const ReplyProtoType& reply) {
  VLOG(1) << __func__;
  auto r = std::make_unique<MountTaskResult>();
  VLOG(3) << "attestationd reply:"
          << " async_id=" << async_id << " status=" << reply.status();
  VLOG_IF(1, reply.status() != AttestationStatus::STATUS_SUCCESS)
      << "Attestation daemon returned status " << reply.status();
  r->set_sequence_id(async_id);
  r->set_return_status(reply.status() == AttestationStatus::STATUS_SUCCESS);
  brillo::SecureBlob blob((reply.*func)());
  r->set_return_data(blob);
  event_source_.AddEvent(std::move(r));
}

void ServiceDistributed::ProcessGetEndorsementInfoReply(
    DBusGMethodInvocation* context,
    const attestation::GetEndorsementInfoReply& reply) {
  VLOG(1) << __func__;
  BaseReply reply_out;
  if (reply.status() == AttestationStatus::STATUS_SUCCESS) {
    GetEndorsementInfoReply* extension =
        reply_out.MutableExtension(GetEndorsementInfoReply::reply);
    extension->set_ek_public_key(reply.ek_public_key());
    if (!reply.ek_certificate().empty()) {
      extension->set_ek_certificate(reply.ek_certificate());
    }
  } else {
    VLOG(1) << "Attestation daemon returned status " << reply.status();
    reply_out.set_error(CRYPTOHOME_ERROR_TPM_EK_NOT_AVAILABLE);
  }
  SendReply(context, reply_out);
}

bool ServiceDistributed::GetKeyInfo(gboolean is_user_specific,
                                    gchar* username,
                                    gchar* key_name,
                                    attestation::GetKeyInfoReply* key_info) {
  VLOG(3) << __func__;
  attestation::GetKeyInfoRequest request;
  request.set_key_label(key_name);
  if (is_user_specific) {
    request.set_username(username);
  }
  auto method = base::Bind(&AttestationInterface::GetKeyInfo,
                           base::Unretained(attestation_interface_), request);
  return SendRequestAndWait(method, key_info);
}

void ServiceDistributed::AttestationInitialize() {
  VLOG(1) << __func__;
  auto method = base::Bind(&AttestationInterface::Initialize,
                           base::Unretained(attestation_interface_));
  PostAndWait(method);
}

void ServiceDistributed::AttestationInitializeTpm() {
  VLOG(1) << __func__;
}

void ServiceDistributed::AttestationInitializeTpmComplete() {
  VLOG(1) << __func__;
  // PrepareForEnrollment is done by attestationd. It will remove
  // the Attestation dependency with tpm_manager. Here we just clear
  // it in local TpmStatus stored by cryptohomed, so that it doesn't
  // prevent ClearStoredOwnerPassword from being sent to tpm_manager.
  tpm_init_->RemoveTpmOwnerDependency(
      TpmPersistentState::TpmOwnerDependency::kAttestation);
}

bool ServiceDistributed::AttestationGetEnrollmentPreparations(
    const AttestationGetEnrollmentPreparationsRequest& request_in,
    AttestationGetEnrollmentPreparationsReply* reply_out) {
  VLOG(1) << __func__;
  attestation::GetEnrollmentPreparationsRequest request;
  if (request_in.has_pca_type()) {
    request.set_aca_type(
        static_cast<attestation::ACAType>(request_in.pca_type()));
  }
  attestation::GetEnrollmentPreparationsReply reply;
  if (ObtainTpmAttestationEnrollmentPreparations(request, &reply, nullptr) &&
      reply.status() == AttestationStatus::STATUS_SUCCESS) {
    auto map = reply.enrollment_preparations();
    for (auto it = map.cbegin(), end = map.cend(); it != end; ++it) {
      (*reply_out->mutable_enrollment_preparations())[it->first] = it->second;
    }
    return true;
  } else {
    VLOG(1) << "Attestation daemon returned status " << reply.status();
    return false;
  }
}

void ServiceDistributed::AttestationGetTpmStatus(GetTpmStatusReply* reply_out) {
  VLOG(1) << __func__;
  attestation::GetStatusRequest request;
  request.set_extended_status(true);
  attestation::GetStatusReply reply;
  auto method = base::Bind(&AttestationInterface::GetStatus,
                           base::Unretained(attestation_interface_), request);
  if (SendRequestAndWait(method, &reply) &&
      reply.status() == AttestationStatus::STATUS_SUCCESS) {
    reply_out->set_attestation_prepared(reply.prepared_for_enrollment());
    reply_out->set_attestation_enrolled(reply.enrolled());
    reply_out->set_verified_boot_measured(reply.verified_boot());
    for (auto it = reply.identities().cbegin(), end = reply.identities().cend();
         it != end; ++it) {
      auto* identity = reply_out->mutable_identities()->Add();
      identity->set_features(it->features());
    }
    for (auto it = reply.identity_certificates().cbegin(),
              end = reply.identity_certificates().cend();
         it != end; ++it) {
      GetTpmStatusReply::IdentityCertificate identity_certificate;
      identity_certificate.set_identity(it->second.identity());
      identity_certificate.set_aca(it->second.aca());
      reply_out->mutable_identity_certificates()->insert(
          google::protobuf::Map<int, GetTpmStatusReply::IdentityCertificate>::
              value_type(it->first, identity_certificate));
    }
  } else {
    reply_out->set_attestation_prepared(false);
    reply_out->set_attestation_enrolled(false);
    reply_out->set_verified_boot_measured(false);
  }
}

bool ServiceDistributed::AttestationGetDelegateCredentials(
    brillo::Blob* blob,
    brillo::Blob* secret,
    bool* has_reset_lock_permissions) {
  if (!use_tpm_ || !tpm_) {
    LOG(WARNING) << __func__
                 << ": without TPM this function takes effect only to DA reset "
                    "permission flag.";
    *has_reset_lock_permissions = true;
  } else {
    if (!tpm_->GetDelegate(blob, secret, has_reset_lock_permissions)) {
      LOG(ERROR) << __func__ << ": Couldn't get auth delegate.";
      return false;
    }
  }
  return true;
}

gboolean ServiceDistributed::TpmIsAttestationPrepared(gboolean* OUT_prepared,
                                                      GError** error) {
  VLOG(1) << __func__;
  attestation::GetEnrollmentPreparationsRequest request;
  attestation::GetEnrollmentPreparationsReply reply;
  if (!ObtainTpmAttestationEnrollmentPreparations(request, &reply, error)) {
    return FALSE;
  }
  *OUT_prepared = FALSE;
  for (const auto& preparation : reply.enrollment_preparations()) {
    if (preparation.second) {
      *OUT_prepared = TRUE;
      break;
    }
  }
  return TRUE;
}

gboolean ServiceDistributed::TpmVerifyAttestationData(gboolean is_cros_core,
                                                      gboolean* OUT_verified,
                                                      GError** error) {
  VLOG(1) << __func__;
  attestation::VerifyRequest request;
  request.set_cros_core(is_cros_core);
  request.set_ek_only(false);
  attestation::VerifyReply reply;
  auto method = base::Bind(&AttestationInterface::Verify,
                           base::Unretained(attestation_interface_), request);
  if (!SendRequestAndWait(method, &reply)) {
    ReportSendFailure(error);
    return FALSE;
  }
  if (reply.status() != AttestationStatus::STATUS_SUCCESS) {
    ReportErrorFromStatus(error, reply.status());
    return FALSE;
  }
  *OUT_verified = reply.verified();
  return TRUE;
}

gboolean ServiceDistributed::TpmVerifyEK(gboolean is_cros_core,
                                         gboolean* OUT_verified,
                                         GError** error) {
  VLOG(1) << __func__;
  attestation::VerifyRequest request;
  request.set_cros_core(is_cros_core);
  request.set_ek_only(true);
  attestation::VerifyReply reply;
  auto method = base::Bind(&AttestationInterface::Verify,
                           base::Unretained(attestation_interface_), request);
  if (!SendRequestAndWait(method, &reply)) {
    ReportSendFailure(error);
    return FALSE;
  }
  if (reply.status() != AttestationStatus::STATUS_SUCCESS) {
    ReportErrorFromStatus(error, reply.status());
    return FALSE;
  }
  *OUT_verified = reply.verified();
  return TRUE;
}

gboolean ServiceDistributed::TpmAttestationCreateEnrollRequest(
    gint pca_type, GArray** OUT_pca_request, GError** error) {
  VLOG(1) << __func__;
  attestation::ACAType aca_type;
  if (!ConvertIntegerToACAType(pca_type, &aca_type, error)) {
    return FALSE;
  }
  attestation::CreateEnrollRequestRequest request;
  request.set_aca_type(aca_type);
  attestation::CreateEnrollRequestReply reply;
  auto method = base::Bind(&AttestationInterface::CreateEnrollRequest,
                           base::Unretained(attestation_interface_), request);
  // We must set the GArray now because if we return without setting it,
  // dbus-glib loops forever.
  *OUT_pca_request =
      g_array_new(false, false, sizeof(brillo::SecureBlob::value_type));
  if (!SendRequestAndWait(method, &reply)) {
    ReportSendFailure(error);
    return FALSE;
  }
  if (reply.status() != AttestationStatus::STATUS_SUCCESS) {
    ReportErrorFromStatus(error, reply.status());
    return FALSE;
  }
  g_array_append_vals(*OUT_pca_request, reply.pca_request().data(),
                      reply.pca_request().size());
  return TRUE;
}

gboolean ServiceDistributed::AsyncTpmAttestationCreateEnrollRequest(
    gint pca_type, gint* OUT_async_id, GError** error) {
  VLOG(1) << __func__;
  attestation::ACAType aca_type;
  if (!ConvertIntegerToACAType(pca_type, &aca_type, error)) {
    return FALSE;
  }
  *OUT_async_id = NextSequence();
  LogAsyncIdInfo(*OUT_async_id, __func__, base::Time::Now());
  attestation::CreateEnrollRequestRequest request;
  request.set_aca_type(aca_type);
  auto callback = base::Bind(
      &ServiceDistributed::ProcessDataReply<
          attestation::CreateEnrollRequestReply>,
      GetWeakPtr(), &attestation::CreateEnrollRequestReply::pca_request,
      *OUT_async_id);
  auto method =
      base::Bind(&AttestationInterface::CreateEnrollRequest,
                 base::Unretained(attestation_interface_), request, callback);
  if (!Post(method)) {
    ReportSendFailure(error);
    return FALSE;
  }
  return TRUE;
}

gboolean ServiceDistributed::TpmAttestationEnroll(gint pca_type,
                                                  GArray* pca_response,
                                                  gboolean* OUT_success,
                                                  GError** error) {
  VLOG(1) << __func__;
  attestation::ACAType aca_type;
  if (!ConvertIntegerToACAType(pca_type, &aca_type, error)) {
    return FALSE;
  }
  attestation::FinishEnrollRequest request;
  request.set_aca_type(aca_type);
  request.set_pca_response(pca_response->data, pca_response->len);
  attestation::FinishEnrollReply reply;
  auto method = base::Bind(&AttestationInterface::FinishEnroll,
                           base::Unretained(attestation_interface_), request);
  if (!SendRequestAndWait(method, &reply)) {
    ReportSendFailure(error);
    return FALSE;
  }
  VLOG_IF(1, reply.status() != AttestationStatus::STATUS_SUCCESS)
      << "Attestation daemon returned status " << reply.status();
  *OUT_success = (reply.status() == AttestationStatus::STATUS_SUCCESS);
  return TRUE;
}

gboolean ServiceDistributed::AsyncTpmAttestationEnroll(gint pca_type,
                                                       GArray* pca_response,
                                                       gint* OUT_async_id,
                                                       GError** error) {
  VLOG(1) << __func__;
  attestation::ACAType aca_type;
  if (!ConvertIntegerToACAType(pca_type, &aca_type, error)) {
    return FALSE;
  }
  *OUT_async_id = NextSequence();
  LogAsyncIdInfo(*OUT_async_id, __func__, base::Time::Now());
  attestation::FinishEnrollRequest request;
  request.set_aca_type(aca_type);
  request.set_pca_response(pca_response->data, pca_response->len);
  auto callback = base::Bind(
      &ServiceDistributed::ProcessStatusReply<attestation::FinishEnrollReply>,
      GetWeakPtr(), *OUT_async_id);
  auto method =
      base::Bind(&AttestationInterface::FinishEnroll,
                 base::Unretained(attestation_interface_), request, callback);
  if (!Post(method)) {
    ReportSendFailure(error);
    return FALSE;
  }
  return TRUE;
}

gboolean ServiceDistributed::TpmAttestationCreateCertRequest(
    gint pca_type,
    gint certificate_profile,
    gchar* username,
    gchar* request_origin,
    GArray** OUT_pca_request,
    GError** error) {
  VLOG(1) << __func__;
  attestation::ACAType aca_type;
  if (!ConvertIntegerToACAType(pca_type, &aca_type, error)) {
    return FALSE;
  }
  attestation::CreateCertificateRequestRequest request;
  request.set_aca_type(aca_type);
  request.set_certificate_profile(GetProfile(certificate_profile));
  request.set_username(username);
  request.set_request_origin(request_origin);
  attestation::CreateCertificateRequestReply reply;
  auto method = base::Bind(&AttestationInterface::CreateCertificateRequest,
                           base::Unretained(attestation_interface_), request);
  // We must set the GArray now because if we return without setting it,
  // dbus-glib loops forever.
  *OUT_pca_request =
      g_array_new(false, false, sizeof(brillo::SecureBlob::value_type));
  if (!SendRequestAndWait(method, &reply)) {
    ReportSendFailure(error);
    return FALSE;
  }
  if (reply.status() != AttestationStatus::STATUS_SUCCESS) {
    ReportErrorFromStatus(error, reply.status());
    return FALSE;
  }
  g_array_append_vals(*OUT_pca_request, reply.pca_request().data(),
                      reply.pca_request().size());
  return TRUE;
}

gboolean ServiceDistributed::AsyncTpmAttestationCreateCertRequest(
    gint pca_type,
    gint certificate_profile,
    gchar* username,
    gchar* request_origin,
    gint* OUT_async_id,
    GError** error) {
  VLOG(1) << __func__;
  attestation::ACAType aca_type;
  if (!ConvertIntegerToACAType(pca_type, &aca_type, error)) {
    return FALSE;
  }
  *OUT_async_id = NextSequence();
  LogAsyncIdInfo(*OUT_async_id, __func__, base::Time::Now());
  attestation::CreateCertificateRequestRequest request;
  request.set_aca_type(aca_type);
  request.set_certificate_profile(GetProfile(certificate_profile));
  request.set_username(username);
  request.set_request_origin(request_origin);
  auto callback = base::Bind(
      &ServiceDistributed::ProcessDataReply<
          attestation::CreateCertificateRequestReply>,
      GetWeakPtr(), &attestation::CreateCertificateRequestReply::pca_request,
      *OUT_async_id);
  auto method =
      base::Bind(&AttestationInterface::CreateCertificateRequest,
                 base::Unretained(attestation_interface_), request, callback);
  if (!Post(method)) {
    ReportSendFailure(error);
    return FALSE;
  }
  return TRUE;
}

gboolean ServiceDistributed::TpmAttestationFinishCertRequest(
    GArray* pca_response,
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    GArray** OUT_cert,
    gboolean* OUT_success,
    GError** error) {
  VLOG(1) << __func__;
  attestation::FinishCertificateRequestRequest request;
  request.set_pca_response(pca_response->data, pca_response->len);
  request.set_key_label(key_name);
  if (is_user_specific) {
    request.set_username(username);
  }
  attestation::FinishCertificateRequestReply reply;
  auto method = base::Bind(&AttestationInterface::FinishCertificateRequest,
                           base::Unretained(attestation_interface_), request);
  // We must set the GArray now because if we return without setting it,
  // dbus-glib loops forever.
  *OUT_cert = g_array_new(false, false, sizeof(brillo::SecureBlob::value_type));
  if (!SendRequestAndWait(method, &reply)) {
    ReportSendFailure(error);
    return FALSE;
  }
  VLOG_IF(1, reply.status() != AttestationStatus::STATUS_SUCCESS)
      << "Attestation daemon returned status " << reply.status();
  *OUT_success = (reply.status() == AttestationStatus::STATUS_SUCCESS);
  if (*OUT_success) {
    g_array_append_vals(*OUT_cert, reply.certificate().data(),
                        reply.certificate().size());
  }
  return TRUE;
}

gboolean ServiceDistributed::AsyncTpmAttestationFinishCertRequest(
    GArray* pca_response,
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    gint* OUT_async_id,
    GError** error) {
  VLOG(1) << __func__;
  *OUT_async_id = NextSequence();
  LogAsyncIdInfo(*OUT_async_id, __func__, base::Time::Now());
  attestation::FinishCertificateRequestRequest request;
  request.set_pca_response(pca_response->data, pca_response->len);
  request.set_key_label(key_name);
  if (is_user_specific) {
    request.set_username(username);
  }
  auto callback = base::Bind(
      &ServiceDistributed::ProcessDataReply<
          attestation::FinishCertificateRequestReply>,
      GetWeakPtr(), &attestation::FinishCertificateRequestReply::certificate,
      *OUT_async_id);
  auto method =
      base::Bind(&AttestationInterface::FinishCertificateRequest,
                 base::Unretained(attestation_interface_), request, callback);
  if (!Post(method)) {
    ReportSendFailure(error);
    return FALSE;
  }
  return TRUE;
}

gboolean ServiceDistributed::TpmIsAttestationEnrolled(gboolean* OUT_is_enrolled,
                                                      GError** error) {
  VLOG(1) << __func__;
  attestation::GetStatusReply reply;
  if (!ObtainTpmStatus(&reply, error)) {
    return FALSE;
  }
  *OUT_is_enrolled = reply.enrolled();
  return TRUE;
}

bool ServiceDistributed::ObtainTpmAttestationEnrollmentPreparations(
    const attestation::GetEnrollmentPreparationsRequest& request,
    attestation::GetEnrollmentPreparationsReply* reply,
    GError** error) {
  auto method = base::Bind(&AttestationInterface::GetEnrollmentPreparations,
                           base::Unretained(attestation_interface_), request);
  if (!SendRequestAndWait(method, reply)) {
    ReportSendFailure(error);
    return false;
  }
  if (reply->status() != AttestationStatus::STATUS_SUCCESS) {
    ReportErrorFromStatus(error, reply->status());
    return false;
  }
  return true;
}

gboolean ServiceDistributed::TpmAttestationDoesKeyExist(
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    gboolean* OUT_exists,
    GError** error) {
  VLOG(1) << __func__;
  attestation::GetKeyInfoReply key_info;
  if (!GetKeyInfo(is_user_specific, username, key_name, &key_info)) {
    ReportSendFailure(error);
    return FALSE;
  }
  if (key_info.status() == AttestationStatus::STATUS_SUCCESS) {
    *OUT_exists = TRUE;
    return TRUE;
  } else if (key_info.status() == AttestationStatus::STATUS_INVALID_PARAMETER) {
    *OUT_exists = FALSE;
    return TRUE;
  }
  ReportErrorFromStatus(error, key_info.status());
  return FALSE;
}

gboolean ServiceDistributed::TpmAttestationGetCertificate(
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    GArray** OUT_certificate,
    gboolean* OUT_success,
    GError** error) {
  VLOG(1) << __func__;
  attestation::GetKeyInfoReply key_info;
  // We must set the GArray now because if we return without setting it,
  // dbus-glib loops forever.
  *OUT_certificate =
      g_array_new(false, false, sizeof(brillo::SecureBlob::value_type));
  if (!GetKeyInfo(is_user_specific, username, key_name, &key_info)) {
    ReportSendFailure(error);
    return FALSE;
  }
  if (key_info.status() == AttestationStatus::STATUS_SUCCESS) {
    *OUT_success = TRUE;
    g_array_append_vals(*OUT_certificate, key_info.certificate().data(),
                        key_info.certificate().size());
  } else {
    VLOG(1) << "Attestation daemon returned status " << key_info.status();
    *OUT_success = FALSE;
  }
  return TRUE;
}

gboolean ServiceDistributed::TpmAttestationGetPublicKey(
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    GArray** OUT_public_key,
    gboolean* OUT_success,
    GError** error) {
  VLOG(1) << __func__;
  attestation::GetKeyInfoReply key_info;
  // We must set the GArray now because if we return without setting it,
  // dbus-glib loops forever.
  *OUT_public_key =
      g_array_new(false, false, sizeof(brillo::SecureBlob::value_type));
  if (!GetKeyInfo(is_user_specific, username, key_name, &key_info)) {
    ReportSendFailure(error);
    return FALSE;
  }
  if (key_info.status() == AttestationStatus::STATUS_SUCCESS) {
    *OUT_success = TRUE;
    g_array_append_vals(*OUT_public_key, key_info.public_key().data(),
                        key_info.public_key().size());
  } else {
    VLOG(1) << "Attestation daemon returned status " << key_info.status();
    *OUT_success = FALSE;
  }
  return TRUE;
}

gboolean ServiceDistributed::TpmAttestationRegisterKey(
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    gint* OUT_async_id,
    GError** error) {
  VLOG(1) << __func__;
  *OUT_async_id = NextSequence();
  LogAsyncIdInfo(*OUT_async_id, __func__, base::Time::Now());
  attestation::RegisterKeyWithChapsTokenRequest request;
  request.set_key_label(key_name);
  if (is_user_specific) {
    request.set_username(username);
  }
  auto callback = base::Bind(&ServiceDistributed::ProcessStatusReply<
                                 attestation::RegisterKeyWithChapsTokenReply>,
                             GetWeakPtr(), *OUT_async_id);
  auto method =
      base::Bind(&AttestationInterface::RegisterKeyWithChapsToken,
                 base::Unretained(attestation_interface_), request, callback);
  if (!Post(method)) {
    ReportSendFailure(error);
    return FALSE;
  }
  return TRUE;
}

gboolean ServiceDistributed::TpmAttestationSignEnterpriseChallenge(
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    gchar* domain,
    GArray* device_id,
    gboolean include_signed_public_key,
    GArray* challenge,
    gint* OUT_async_id,
    GError** error) {
  VLOG(1) << __func__;
  return TpmAttestationSignEnterpriseVaChallenge(
      attestation::DEFAULT_VA, is_user_specific, username, key_name, domain,
      device_id, include_signed_public_key, challenge, nullptr, OUT_async_id,
      error);
}

gboolean ServiceDistributed::TpmAttestationSignEnterpriseVaChallenge(
    gint va_type,
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    gchar* domain,
    GArray* device_id,
    gboolean include_signed_public_key,
    GArray* challenge,
    gchar* key_name_for_spkac,
    gint* OUT_async_id,
    GError** error) {
  VLOG(1) << __func__;
  *OUT_async_id = NextSequence();
  LogAsyncIdInfo(*OUT_async_id, __func__, base::Time::Now());
  attestation::VAType att_va_type;
  if (!ConvertIntegerToVAType(va_type, &att_va_type, error)) {
    return FALSE;
  }
  attestation::SignEnterpriseChallengeRequest request;
  request.set_va_type(att_va_type);
  request.set_key_label(key_name);
  if (is_user_specific) {
    request.set_username(username);
  }
  request.set_domain(domain);
  request.set_device_id(device_id->data, device_id->len);
  request.set_include_signed_public_key(include_signed_public_key);
  request.set_challenge(challenge->data, challenge->len);
  if (key_name_for_spkac) {
    request.set_key_name_for_spkac(key_name_for_spkac);
  }
  auto callback =
      base::Bind(&ServiceDistributed::ProcessDataReply<
                     attestation::SignEnterpriseChallengeReply>,
                 GetWeakPtr(),
                 &attestation::SignEnterpriseChallengeReply::challenge_response,
                 *OUT_async_id);
  auto method =
      base::Bind(&AttestationInterface::SignEnterpriseChallenge,
                 base::Unretained(attestation_interface_), request, callback);
  if (!Post(method)) {
    ReportSendFailure(error);
    return FALSE;
  }
  return TRUE;
}

gboolean ServiceDistributed::TpmAttestationSignSimpleChallenge(
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    GArray* challenge,
    gint* OUT_async_id,
    GError** error) {
  VLOG(1) << __func__;
  *OUT_async_id = NextSequence();
  LogAsyncIdInfo(*OUT_async_id, __func__, base::Time::Now());
  attestation::SignSimpleChallengeRequest request;
  request.set_key_label(key_name);
  if (is_user_specific) {
    request.set_username(username);
  }
  request.set_challenge(challenge->data, challenge->len);
  auto callback = base::Bind(
      &ServiceDistributed::ProcessDataReply<
          attestation::SignSimpleChallengeReply>,
      GetWeakPtr(), &attestation::SignSimpleChallengeReply::challenge_response,
      *OUT_async_id);
  auto method =
      base::Bind(&AttestationInterface::SignSimpleChallenge,
                 base::Unretained(attestation_interface_), request, callback);
  if (!Post(method)) {
    ReportSendFailure(error);
    return FALSE;
  }
  return TRUE;
}

gboolean ServiceDistributed::TpmAttestationGetKeyPayload(
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    GArray** OUT_payload,
    gboolean* OUT_success,
    GError** error) {
  VLOG(1) << __func__;
  attestation::GetKeyInfoReply key_info;
  // We must set the GArray now because if we return without setting it,
  // dbus-glib loops forever.
  *OUT_payload =
      g_array_new(false, false, sizeof(brillo::SecureBlob::value_type));
  if (!GetKeyInfo(is_user_specific, username, key_name, &key_info)) {
    ReportSendFailure(error);
    return FALSE;
  }
  if (key_info.status() == AttestationStatus::STATUS_SUCCESS) {
    *OUT_success = TRUE;
    g_array_append_vals(*OUT_payload, key_info.payload().data(),
                        key_info.payload().size());
  } else {
    VLOG(1) << "Attestation daemon returned status " << key_info.status();
    *OUT_success = FALSE;
  }
  return TRUE;
}

gboolean ServiceDistributed::TpmAttestationSetKeyPayload(
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    GArray* payload,
    gboolean* OUT_success,
    GError** error) {
  VLOG(1) << __func__;
  attestation::SetKeyPayloadRequest request;
  request.set_key_label(key_name);
  if (is_user_specific) {
    request.set_username(username);
  }
  request.set_payload(payload->data, payload->len);
  attestation::SetKeyPayloadReply reply;
  auto method = base::Bind(&AttestationInterface::SetKeyPayload,
                           base::Unretained(attestation_interface_), request);
  if (!SendRequestAndWait(method, &reply)) {
    ReportSendFailure(error);
    return FALSE;
  }
  VLOG_IF(1, reply.status() != AttestationStatus::STATUS_SUCCESS)
      << "Attestation daemon returned status " << reply.status();
  *OUT_success = (reply.status() == AttestationStatus::STATUS_SUCCESS);
  return TRUE;
}

gboolean ServiceDistributed::TpmAttestationDeleteKeys(gboolean is_user_specific,
                                                      gchar* username,
                                                      gchar* key_prefix,
                                                      gboolean* OUT_success,
                                                      GError** error) {
  VLOG(1) << __func__;
  attestation::DeleteKeysRequest request;
  request.set_key_label_match(key_prefix);
  request.set_match_behavior(
      attestation::DeleteKeysRequest::MATCH_BEHAVIOR_PREFIX);
  if (is_user_specific) {
    request.set_username(username);
  }
  attestation::DeleteKeysReply reply;
  auto method = base::Bind(&AttestationInterface::DeleteKeys,
                           base::Unretained(attestation_interface_), request);
  if (!SendRequestAndWait(method, &reply)) {
    ReportSendFailure(error);
    return FALSE;
  }
  VLOG_IF(1, reply.status() != AttestationStatus::STATUS_SUCCESS)
      << "Attestation daemon returned status " << reply.status();
  *OUT_success = (reply.status() == AttestationStatus::STATUS_SUCCESS);
  return TRUE;
}

gboolean ServiceDistributed::TpmAttestationDeleteKey(gboolean is_user_specific,
                                                     gchar* username,
                                                     gchar* key_name,
                                                     gboolean* OUT_success,
                                                     GError** error) {
  VLOG(1) << __func__;
  attestation::DeleteKeysRequest request;
  request.set_key_label_match(key_name);
  request.set_match_behavior(
      attestation::DeleteKeysRequest::MATCH_BEHAVIOR_EXACT);
  if (is_user_specific) {
    request.set_username(username);
  }
  attestation::DeleteKeysReply reply;
  auto method = base::Bind(&AttestationInterface::DeleteKeys,
                           base::Unretained(attestation_interface_), request);
  if (!SendRequestAndWait(method, &reply)) {
    ReportSendFailure(error);
    return FALSE;
  }
  VLOG_IF(1, reply.status() != AttestationStatus::STATUS_SUCCESS)
      << "Attestation daemon returned status " << reply.status();
  *OUT_success = (reply.status() == AttestationStatus::STATUS_SUCCESS);
  return TRUE;
}

gboolean ServiceDistributed::TpmAttestationGetEK(gchar** OUT_ek_info,
                                                 gboolean* OUT_success,
                                                 GError** error) {
  VLOG(1) << __func__;
  attestation::GetEndorsementInfoRequest request;
  attestation::GetEndorsementInfoReply reply;
  auto method = base::Bind(&AttestationInterface::GetEndorsementInfo,
                           base::Unretained(attestation_interface_), request);
  if (!SendRequestAndWait(method, &reply)) {
    ReportSendFailure(error);
    return FALSE;
  }
  VLOG_IF(1, reply.status() != AttestationStatus::STATUS_SUCCESS)
      << "Attestation daemon returned status " << reply.status();
  *OUT_success = (reply.status() == AttestationStatus::STATUS_SUCCESS);
  *OUT_ek_info = g_strndup(reply.ek_info().data(), reply.ek_info().size());
  return TRUE;
}

gboolean ServiceDistributed::TpmAttestationResetIdentity(
    gchar* reset_token,
    GArray** OUT_reset_request,
    gboolean* OUT_success,
    GError** error) {
  VLOG(1) << __func__;
  attestation::ResetIdentityRequest request;
  request.set_reset_token(reset_token);
  attestation::ResetIdentityReply reply;
  auto method = base::Bind(&AttestationInterface::ResetIdentity,
                           base::Unretained(attestation_interface_), request);
  // We must set the GArray now because if we return without setting it,
  // dbus-glib loops forever.
  *OUT_reset_request =
      g_array_new(false, false, sizeof(brillo::SecureBlob::value_type));
  if (!SendRequestAndWait(method, &reply)) {
    ReportSendFailure(error);
    return FALSE;
  }
  VLOG_IF(1, reply.status() != AttestationStatus::STATUS_SUCCESS)
      << "Attestation daemon returned status " << reply.status();
  *OUT_success = (reply.status() == AttestationStatus::STATUS_SUCCESS);
  if (*OUT_success) {
    g_array_append_vals(*OUT_reset_request, reply.reset_request().data(),
                        reply.reset_request().size());
  }
  return TRUE;
}

void ServiceDistributed::DoGetEndorsementInfo(
    const brillo::SecureBlob& request_array, DBusGMethodInvocation* context) {
  VLOG(1) << __func__;
  cryptohome::GetEndorsementInfoRequest request_in;
  if (!request_in.ParseFromArray(request_array.data(), request_array.size())) {
    SendInvalidArgsReply(context, "Bad GetEndorsementInfoRequest");
    return;
  }

  attestation::GetEndorsementInfoRequest request;
  auto callback =
      base::Bind(&ServiceDistributed::ProcessGetEndorsementInfoReply,
                 GetWeakPtr(), context);
  auto method =
      base::Bind(&AttestationInterface::GetEndorsementInfo,
                 base::Unretained(attestation_interface_), request, callback);
  if (!Post(method)) {
    SendFailureReply(context, "Failed to call GetEndorsementInfo");
  }
}

gboolean ServiceDistributed::GetEndorsementInfo(
    const GArray* request, DBusGMethodInvocation* context) {
  VLOG(1) << __func__;
  auto method = base::Bind(
      &ServiceDistributed::DoGetEndorsementInfo, GetWeakPtr(),
      brillo::SecureBlob(request->data, request->data + request->len),
      base::Unretained(context));
  if (!Post(method)) {
    return FALSE;
  }
  return TRUE;
}

void ServiceDistributed::DoInitializeCastKey(
    const brillo::SecureBlob& request_array, DBusGMethodInvocation* context) {
  ReportDeprecatedApiCalled(DeprecatedApiEvent::kInitializeCastKey);
  VLOG(1) << __func__;
  cryptohome::InitializeCastKeyRequest request_in;
  if (!request_in.ParseFromArray(request_array.data(), request_array.size())) {
    SendInvalidArgsReply(context, "Bad InitializeCastKeyRequest");
    return;
  }

  SendNotSupportedReply(context, "InitializeCastKeyRequest is not supported");
}

gboolean ServiceDistributed::InitializeCastKey(const GArray* request,
                                               DBusGMethodInvocation* context) {
  VLOG(1) << __func__;
  auto method = base::Bind(
      &ServiceDistributed::DoInitializeCastKey, GetWeakPtr(),
      brillo::SecureBlob(request->data, request->data + request->len),
      base::Unretained(context));
  if (!Post(method)) {
    return FALSE;
  }
  return TRUE;
}

gboolean ServiceDistributed::TpmAttestationGetEnrollmentId(
    gboolean ignore_cache,
    GArray** OUT_enrollment_id,
    gboolean* OUT_success,
    GError** error) {
  attestation::GetEnrollmentIdRequest request;
  attestation::GetEnrollmentIdReply reply;
  request.set_ignore_cache(ignore_cache);
  auto method = base::Bind(&AttestationInterface::GetEnrollmentId,
                           base::Unretained(attestation_interface_), request);
  // We must set the GArray now because if we return without setting it,
  // dbus-glib loops forever.
  *OUT_enrollment_id =
      g_array_new(false, false, sizeof(brillo::SecureBlob::value_type));
  if (!SendRequestAndWait(method, &reply)) {
    ReportSendFailure(error);
    return FALSE;
  }
  VLOG_IF(1, reply.status() != AttestationStatus::STATUS_SUCCESS)
      << "Attestation daemon returned status " << reply.status();
  *OUT_success = (reply.status() == AttestationStatus::STATUS_SUCCESS);
  g_array_append_vals(*OUT_enrollment_id, reply.enrollment_id().data(),
                      reply.enrollment_id().size());
  return TRUE;
}

gboolean ServiceDistributed::TpmAttestationEnrollEx(gint pca_type,
                                                    gboolean forced,
                                                    gboolean* OUT_success,
                                                    GError** error) {
  VLOG(1) << __func__;
  attestation::ACAType aca_type;
  if (!ConvertIntegerToACAType(pca_type, &aca_type, error)) {
    return FALSE;
  }
  attestation::EnrollRequest request;
  request.set_aca_type(aca_type);
  request.set_forced(forced);
  attestation::EnrollReply reply;
  auto method = base::Bind(&AttestationInterface::Enroll,
                           base::Unretained(attestation_interface_), request);
  if (!SendRequestAndWait(method, &reply)) {
    ReportSendFailure(error);
    return FALSE;
  }
  VLOG_IF(1, reply.status() != AttestationStatus::STATUS_SUCCESS)
      << "Attestation daemon returned status " << reply.status();
  *OUT_success = (reply.status() == AttestationStatus::STATUS_SUCCESS);
  return TRUE;
}

gboolean ServiceDistributed::AsyncTpmAttestationEnrollEx(gint pca_type,
                                                         gboolean forced,
                                                         gint* OUT_async_id,
                                                         GError** error) {
  VLOG(1) << __func__;
  attestation::ACAType aca_type;
  if (!ConvertIntegerToACAType(pca_type, &aca_type, error)) {
    return FALSE;
  }
  *OUT_async_id = NextSequence();
  LogAsyncIdInfo(*OUT_async_id, __func__, base::Time::Now());
  attestation::EnrollRequest request;
  request.set_aca_type(aca_type);
  request.set_forced(forced);
  auto callback = base::Bind(
      &ServiceDistributed::ProcessStatusReply<attestation::EnrollReply>,
      GetWeakPtr(), *OUT_async_id);
  auto method =
      base::Bind(&AttestationInterface::Enroll,
                 base::Unretained(attestation_interface_), request, callback);
  if (!Post(method)) {
    ReportSendFailure(error);
    return FALSE;
  }
  return TRUE;
}

gboolean ServiceDistributed::TpmAttestationGetCertificateEx(
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
  VLOG(1) << __func__;
  attestation::ACAType aca_type;
  if (!ConvertIntegerToACAType(pca_type, &aca_type, error)) {
    return FALSE;
  }
  attestation::KeyType typed_key_type;
  if (!ConvertIntegerToKeyType(key_type, &typed_key_type, error)) {
    return FALSE;
  }
  attestation::GetCertificateRequest request;
  request.set_certificate_profile(GetProfile(certificate_profile));
  request.set_username(username);
  request.set_request_origin(request_origin);
  request.set_aca_type(aca_type);
  request.set_key_type(typed_key_type);
  request.set_key_label(key_name);
  request.set_forced(forced);
  request.set_shall_trigger_enrollment(shall_trigger_enrollment);
  attestation::GetCertificateReply reply;
  auto method = base::Bind(&AttestationInterface::GetCertificate,
                           base::Unretained(attestation_interface_), request);
  // We must set the GArray now because if we return without setting it,
  // dbus-glib loops forever.
  *OUT_certificate =
      g_array_new(false, false, sizeof(brillo::SecureBlob::value_type));
  if (!SendRequestAndWait(method, &reply)) {
    ReportSendFailure(error);
    return FALSE;
  }
  VLOG_IF(1, reply.status() != AttestationStatus::STATUS_SUCCESS)
      << "Attestation daemon returned status " << reply.status();
  *OUT_success = (reply.status() == AttestationStatus::STATUS_SUCCESS);
  if (*OUT_success) {
    g_array_append_vals(*OUT_certificate, reply.certificate().data(),
                        reply.certificate().size());
  }
  return TRUE;
}

gboolean ServiceDistributed::AsyncTpmAttestationGetCertificateEx(
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
  VLOG(1) << __func__;
  attestation::ACAType aca_type;
  if (!ConvertIntegerToACAType(pca_type, &aca_type, error)) {
    return FALSE;
  }
  attestation::KeyType typed_key_type;
  if (!ConvertIntegerToKeyType(key_type, &typed_key_type, error)) {
    return FALSE;
  }
  *OUT_async_id = NextSequence();
  LogAsyncIdInfo(*OUT_async_id, __func__, base::Time::Now());
  attestation::GetCertificateRequest request;
  request.set_certificate_profile(GetProfile(certificate_profile));
  request.set_username(username);
  request.set_request_origin(request_origin);
  request.set_aca_type(aca_type);
  request.set_key_type(typed_key_type);
  request.set_key_label(key_name);
  request.set_forced(forced);
  request.set_shall_trigger_enrollment(shall_trigger_enrollment);
  auto callback = base::Bind(
      &ServiceDistributed::ProcessDataReply<attestation::GetCertificateReply>,
      GetWeakPtr(), &attestation::GetCertificateReply::certificate,
      *OUT_async_id);
  auto method =
      base::Bind(&AttestationInterface::GetCertificate,
                 base::Unretained(attestation_interface_), request, callback);
  if (!Post(method)) {
    ReportSendFailure(error);
    return FALSE;
  }
  return TRUE;
}

void ServiceDistributed::ConnectOwnershipTakenSignal() {
  tpm_manager::TpmManagerUtility* tpm_manager_util =
      tpm_manager::TpmManagerUtility::GetSingleton();
  if (tpm_manager_util) {
    tpm_manager_util->AddOwnershipCallback(
        base::Bind(&ServiceDistributed::OwnershipTakenSignalCallback,
                   base::Unretained(this)));
  } else {
    LOG(ERROR) << __func__ << ": Failed to get TpmManagerUtility singleton!";
  }
}

void ServiceDistributed::OwnershipTakenSignalCallback() {
  OwnershipCallback(true, true);
}

}  // namespace cryptohome
