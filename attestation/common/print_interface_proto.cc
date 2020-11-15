// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// THIS CODE IS GENERATED.
// Generated with command:
// ./proto_print.py --subdir common --proto-include attestation/proto_bindings
// ../../system_api/dbus/attestation/interface.proto

#include "attestation/common/print_interface_proto.h"

#include <inttypes.h>

#include <string>

#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>

#include "attestation/common/print_attestation_ca_proto.h"
#include "attestation/common/print_keystore_proto.h"

namespace attestation {

std::string GetProtoDebugString(AttestationStatus value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(AttestationStatus value,
                                          int indent_size) {
  if (value == STATUS_SUCCESS) {
    return "STATUS_SUCCESS";
  }
  if (value == STATUS_UNEXPECTED_DEVICE_ERROR) {
    return "STATUS_UNEXPECTED_DEVICE_ERROR";
  }
  if (value == STATUS_NOT_AVAILABLE) {
    return "STATUS_NOT_AVAILABLE";
  }
  if (value == STATUS_NOT_READY) {
    return "STATUS_NOT_READY";
  }
  if (value == STATUS_NOT_ALLOWED) {
    return "STATUS_NOT_ALLOWED";
  }
  if (value == STATUS_INVALID_PARAMETER) {
    return "STATUS_INVALID_PARAMETER";
  }
  if (value == STATUS_REQUEST_DENIED_BY_CA) {
    return "STATUS_REQUEST_DENIED_BY_CA";
  }
  if (value == STATUS_CA_NOT_AVAILABLE) {
    return "STATUS_CA_NOT_AVAILABLE";
  }
  if (value == STATUS_NOT_SUPPORTED) {
    return "STATUS_NOT_SUPPORTED";
  }
  if (value == STATUS_DBUS_ERROR) {
    return "STATUS_DBUS_ERROR";
  }
  return "<unknown>";
}

std::string GetProtoDebugString(ACAType value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(ACAType value, int indent_size) {
  if (value == DEFAULT_ACA) {
    return "DEFAULT_ACA";
  }
  if (value == TEST_ACA) {
    return "TEST_ACA";
  }
  return "<unknown>";
}

std::string GetProtoDebugString(VAType value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(VAType value, int indent_size) {
  if (value == DEFAULT_VA) {
    return "DEFAULT_VA";
  }
  if (value == TEST_VA) {
    return "TEST_VA";
  }
  return "<unknown>";
}

std::string GetProtoDebugString(const GetKeyInfoRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const GetKeyInfoRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_key_label()) {
    output += indent + "  key_label: ";
    base::StringAppendF(&output, "%s", value.key_label().c_str());
    output += "\n";
  }
  if (value.has_username()) {
    output += indent + "  username: ";
    base::StringAppendF(&output, "%s", value.username().c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const GetKeyInfoReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const GetKeyInfoReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_key_type()) {
    output += indent + "  key_type: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.key_type(), indent_size + 2)
            .c_str());
    output += "\n";
  }
  if (value.has_key_usage()) {
    output += indent + "  key_usage: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.key_usage(), indent_size + 2)
            .c_str());
    output += "\n";
  }
  if (value.has_public_key()) {
    output += indent + "  public_key: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.public_key().data(), value.public_key().size())
            .c_str());
    output += "\n";
  }
  if (value.has_certify_info()) {
    output += indent + "  certify_info: ";
    base::StringAppendF(&output, "%s",
                        base::HexEncode(value.certify_info().data(),
                                        value.certify_info().size())
                            .c_str());
    output += "\n";
  }
  if (value.has_certify_info_signature()) {
    output += indent + "  certify_info_signature: ";
    base::StringAppendF(&output, "%s",
                        base::HexEncode(value.certify_info_signature().data(),
                                        value.certify_info_signature().size())
                            .c_str());
    output += "\n";
  }
  if (value.has_certificate()) {
    output += indent + "  certificate: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.certificate().data(), value.certificate().size())
            .c_str());
    output += "\n";
  }
  if (value.has_payload()) {
    output += indent + "  payload: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.payload().data(), value.payload().size())
            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const GetEndorsementInfoRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const GetEndorsementInfoRequest& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const GetEndorsementInfoReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const GetEndorsementInfoReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_ek_public_key()) {
    output += indent + "  ek_public_key: ";
    base::StringAppendF(&output, "%s",
                        base::HexEncode(value.ek_public_key().data(),
                                        value.ek_public_key().size())
                            .c_str());
    output += "\n";
  }
  if (value.has_ek_certificate()) {
    output += indent + "  ek_certificate: ";
    base::StringAppendF(&output, "%s",
                        base::HexEncode(value.ek_certificate().data(),
                                        value.ek_certificate().size())
                            .c_str());
    output += "\n";
  }
  if (value.has_ek_info()) {
    output += indent + "  ek_info: ";
    base::StringAppendF(&output, "%s", value.ek_info().c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const GetAttestationKeyInfoRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const GetAttestationKeyInfoRequest& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_aca_type()) {
    output += indent + "  aca_type: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.aca_type(), indent_size + 2)
            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const GetAttestationKeyInfoReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const GetAttestationKeyInfoReply& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_public_key()) {
    output += indent + "  public_key: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.public_key().data(), value.public_key().size())
            .c_str());
    output += "\n";
  }
  if (value.has_public_key_tpm_format()) {
    output += indent + "  public_key_tpm_format: ";
    base::StringAppendF(&output, "%s",
                        base::HexEncode(value.public_key_tpm_format().data(),
                                        value.public_key_tpm_format().size())
                            .c_str());
    output += "\n";
  }
  if (value.has_certificate()) {
    output += indent + "  certificate: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.certificate().data(), value.certificate().size())
            .c_str());
    output += "\n";
  }
  if (value.has_pcr0_quote()) {
    output += indent + "  pcr0_quote: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.pcr0_quote(), indent_size + 2)
            .c_str());
    output += "\n";
  }
  if (value.has_pcr1_quote()) {
    output += indent + "  pcr1_quote: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.pcr1_quote(), indent_size + 2)
            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const ActivateAttestationKeyRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const ActivateAttestationKeyRequest& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_encrypted_certificate()) {
    output += indent + "  encrypted_certificate: ";
    base::StringAppendF(&output, "%s",
                        GetProtoDebugStringWithIndent(
                            value.encrypted_certificate(), indent_size + 2)
                            .c_str());
    output += "\n";
  }
  if (value.has_save_certificate()) {
    output += indent + "  save_certificate: ";
    base::StringAppendF(&output, "%s",
                        value.save_certificate() ? "true" : "false");
    output += "\n";
  }
  if (value.has_aca_type()) {
    output += indent + "  aca_type: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.aca_type(), indent_size + 2)
            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const ActivateAttestationKeyReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const ActivateAttestationKeyReply& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_certificate()) {
    output += indent + "  certificate: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.certificate().data(), value.certificate().size())
            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const CreateCertifiableKeyRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const CreateCertifiableKeyRequest& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_key_label()) {
    output += indent + "  key_label: ";
    base::StringAppendF(&output, "%s", value.key_label().c_str());
    output += "\n";
  }
  if (value.has_username()) {
    output += indent + "  username: ";
    base::StringAppendF(&output, "%s", value.username().c_str());
    output += "\n";
  }
  if (value.has_key_type()) {
    output += indent + "  key_type: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.key_type(), indent_size + 2)
            .c_str());
    output += "\n";
  }
  if (value.has_key_usage()) {
    output += indent + "  key_usage: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.key_usage(), indent_size + 2)
            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const CreateCertifiableKeyReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const CreateCertifiableKeyReply& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_public_key()) {
    output += indent + "  public_key: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.public_key().data(), value.public_key().size())
            .c_str());
    output += "\n";
  }
  if (value.has_certify_info()) {
    output += indent + "  certify_info: ";
    base::StringAppendF(&output, "%s",
                        base::HexEncode(value.certify_info().data(),
                                        value.certify_info().size())
                            .c_str());
    output += "\n";
  }
  if (value.has_certify_info_signature()) {
    output += indent + "  certify_info_signature: ";
    base::StringAppendF(&output, "%s",
                        base::HexEncode(value.certify_info_signature().data(),
                                        value.certify_info_signature().size())
                            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const DecryptRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const DecryptRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_key_label()) {
    output += indent + "  key_label: ";
    base::StringAppendF(&output, "%s", value.key_label().c_str());
    output += "\n";
  }
  if (value.has_username()) {
    output += indent + "  username: ";
    base::StringAppendF(&output, "%s", value.username().c_str());
    output += "\n";
  }
  if (value.has_encrypted_data()) {
    output += indent + "  encrypted_data: ";
    base::StringAppendF(&output, "%s",
                        base::HexEncode(value.encrypted_data().data(),
                                        value.encrypted_data().size())
                            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const DecryptReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const DecryptReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_decrypted_data()) {
    output += indent + "  decrypted_data: ";
    base::StringAppendF(&output, "%s",
                        base::HexEncode(value.decrypted_data().data(),
                                        value.decrypted_data().size())
                            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const SignRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const SignRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_key_label()) {
    output += indent + "  key_label: ";
    base::StringAppendF(&output, "%s", value.key_label().c_str());
    output += "\n";
  }
  if (value.has_username()) {
    output += indent + "  username: ";
    base::StringAppendF(&output, "%s", value.username().c_str());
    output += "\n";
  }
  if (value.has_data_to_sign()) {
    output += indent + "  data_to_sign: ";
    base::StringAppendF(&output, "%s",
                        base::HexEncode(value.data_to_sign().data(),
                                        value.data_to_sign().size())
                            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const SignReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const SignReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_signature()) {
    output += indent + "  signature: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.signature().data(), value.signature().size())
            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const RegisterKeyWithChapsTokenRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const RegisterKeyWithChapsTokenRequest& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_key_label()) {
    output += indent + "  key_label: ";
    base::StringAppendF(&output, "%s", value.key_label().c_str());
    output += "\n";
  }
  if (value.has_username()) {
    output += indent + "  username: ";
    base::StringAppendF(&output, "%s", value.username().c_str());
    output += "\n";
  }
  if (value.has_include_certificates()) {
    output += indent + "  include_certificates: ";
    base::StringAppendF(&output, "%s",
                        value.include_certificates() ? "true" : "false");
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const RegisterKeyWithChapsTokenReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const RegisterKeyWithChapsTokenReply& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const GetEnrollmentPreparationsRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const GetEnrollmentPreparationsRequest& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_aca_type()) {
    output += indent + "  aca_type: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.aca_type(), indent_size + 2)
            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const GetEnrollmentPreparationsReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const GetEnrollmentPreparationsReply& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const GetStatusRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const GetStatusRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_extended_status()) {
    output += indent + "  extended_status: ";
    base::StringAppendF(&output, "%s",
                        value.extended_status() ? "true" : "false");
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const GetStatusReply::Identity& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const GetStatusReply::Identity& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_features()) {
    output += indent + "  features: ";
    base::StringAppendF(&output, "%" PRId32, value.features());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(
    const GetStatusReply::IdentityCertificate& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const GetStatusReply::IdentityCertificate& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_identity()) {
    output += indent + "  identity: ";
    base::StringAppendF(&output, "%" PRId32, value.identity());
    output += "\n";
  }
  if (value.has_aca()) {
    output += indent + "  aca: ";
    base::StringAppendF(&output, "%" PRId32, value.aca());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const GetStatusReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const GetStatusReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_prepared_for_enrollment()) {
    output += indent + "  prepared_for_enrollment: ";
    base::StringAppendF(&output, "%s",
                        value.prepared_for_enrollment() ? "true" : "false");
    output += "\n";
  }
  if (value.has_enrolled()) {
    output += indent + "  enrolled: ";
    base::StringAppendF(&output, "%s", value.enrolled() ? "true" : "false");
    output += "\n";
  }
  if (value.has_verified_boot()) {
    output += indent + "  verified_boot: ";
    base::StringAppendF(&output, "%s",
                        value.verified_boot() ? "true" : "false");
    output += "\n";
  }
  output += indent + "  identities: {";
  for (int i = 0; i < value.identities_size(); ++i) {
    if (i > 0) {
      base::StringAppendF(&output, ", ");
    }
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.identities(i), indent_size + 2)
            .c_str());
  }
  output += "}\n";
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const VerifyRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const VerifyRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_cros_core()) {
    output += indent + "  cros_core: ";
    base::StringAppendF(&output, "%s", value.cros_core() ? "true" : "false");
    output += "\n";
  }
  if (value.has_ek_only()) {
    output += indent + "  ek_only: ";
    base::StringAppendF(&output, "%s", value.ek_only() ? "true" : "false");
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const VerifyReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const VerifyReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_verified()) {
    output += indent + "  verified: ";
    base::StringAppendF(&output, "%s", value.verified() ? "true" : "false");
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const CreateEnrollRequestRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const CreateEnrollRequestRequest& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_aca_type()) {
    output += indent + "  aca_type: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.aca_type(), indent_size + 2)
            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const CreateEnrollRequestReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const CreateEnrollRequestReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_pca_request()) {
    output += indent + "  pca_request: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.pca_request().data(), value.pca_request().size())
            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const FinishEnrollRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const FinishEnrollRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_pca_response()) {
    output += indent + "  pca_response: ";
    base::StringAppendF(&output, "%s",
                        base::HexEncode(value.pca_response().data(),
                                        value.pca_response().size())
                            .c_str());
    output += "\n";
  }
  if (value.has_aca_type()) {
    output += indent + "  aca_type: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.aca_type(), indent_size + 2)
            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const FinishEnrollReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const FinishEnrollReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const EnrollRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const EnrollRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_aca_type()) {
    output += indent + "  aca_type: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.aca_type(), indent_size + 2)
            .c_str());
    output += "\n";
  }
  if (value.has_forced()) {
    output += indent + "  forced: ";
    base::StringAppendF(&output, "%s", value.forced() ? "true" : "false");
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const EnrollReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const EnrollReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const CreateCertificateRequestRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const CreateCertificateRequestRequest& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_certificate_profile()) {
    output += indent + "  certificate_profile: ";
    base::StringAppendF(&output, "%s",
                        GetProtoDebugStringWithIndent(
                            value.certificate_profile(), indent_size + 2)
                            .c_str());
    output += "\n";
  }
  if (value.has_username()) {
    output += indent + "  username: ";
    base::StringAppendF(&output, "%s", value.username().c_str());
    output += "\n";
  }
  if (value.has_request_origin()) {
    output += indent + "  request_origin: ";
    base::StringAppendF(&output, "%s", value.request_origin().c_str());
    output += "\n";
  }
  if (value.has_aca_type()) {
    output += indent + "  aca_type: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.aca_type(), indent_size + 2)
            .c_str());
    output += "\n";
  }
  if (value.has_key_type()) {
    output += indent + "  key_type: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.key_type(), indent_size + 2)
            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const CreateCertificateRequestReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const CreateCertificateRequestReply& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_pca_request()) {
    output += indent + "  pca_request: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.pca_request().data(), value.pca_request().size())
            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const FinishCertificateRequestRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const FinishCertificateRequestRequest& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_pca_response()) {
    output += indent + "  pca_response: ";
    base::StringAppendF(&output, "%s",
                        base::HexEncode(value.pca_response().data(),
                                        value.pca_response().size())
                            .c_str());
    output += "\n";
  }
  if (value.has_key_label()) {
    output += indent + "  key_label: ";
    base::StringAppendF(&output, "%s", value.key_label().c_str());
    output += "\n";
  }
  if (value.has_username()) {
    output += indent + "  username: ";
    base::StringAppendF(&output, "%s", value.username().c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const FinishCertificateRequestReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const FinishCertificateRequestReply& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_certificate()) {
    output += indent + "  certificate: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.certificate().data(), value.certificate().size())
            .c_str());
    output += "\n";
  }
  if (value.has_public_key()) {
    output += indent + "  public_key: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.public_key().data(), value.public_key().size())
            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const GetCertificateRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const GetCertificateRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_certificate_profile()) {
    output += indent + "  certificate_profile: ";
    base::StringAppendF(&output, "%s",
                        GetProtoDebugStringWithIndent(
                            value.certificate_profile(), indent_size + 2)
                            .c_str());
    output += "\n";
  }
  if (value.has_username()) {
    output += indent + "  username: ";
    base::StringAppendF(&output, "%s", value.username().c_str());
    output += "\n";
  }
  if (value.has_request_origin()) {
    output += indent + "  request_origin: ";
    base::StringAppendF(&output, "%s", value.request_origin().c_str());
    output += "\n";
  }
  if (value.has_aca_type()) {
    output += indent + "  aca_type: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.aca_type(), indent_size + 2)
            .c_str());
    output += "\n";
  }
  if (value.has_key_type()) {
    output += indent + "  key_type: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.key_type(), indent_size + 2)
            .c_str());
    output += "\n";
  }
  if (value.has_key_label()) {
    output += indent + "  key_label: ";
    base::StringAppendF(&output, "%s", value.key_label().c_str());
    output += "\n";
  }
  if (value.has_forced()) {
    output += indent + "  forced: ";
    base::StringAppendF(&output, "%s", value.forced() ? "true" : "false");
    output += "\n";
  }
  if (value.has_shall_trigger_enrollment()) {
    output += indent + "  shall_trigger_enrollment: ";
    base::StringAppendF(&output, "%s",
                        value.shall_trigger_enrollment() ? "true" : "false");
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const GetCertificateReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const GetCertificateReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_certificate()) {
    output += indent + "  certificate: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.certificate().data(), value.certificate().size())
            .c_str());
    output += "\n";
  }
  if (value.has_public_key()) {
    output += indent + "  public_key: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.public_key().data(), value.public_key().size())
            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const SignEnterpriseChallengeRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const SignEnterpriseChallengeRequest& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_key_label()) {
    output += indent + "  key_label: ";
    base::StringAppendF(&output, "%s", value.key_label().c_str());
    output += "\n";
  }
  if (value.has_username()) {
    output += indent + "  username: ";
    base::StringAppendF(&output, "%s", value.username().c_str());
    output += "\n";
  }
  if (value.has_domain()) {
    output += indent + "  domain: ";
    base::StringAppendF(&output, "%s", value.domain().c_str());
    output += "\n";
  }
  if (value.has_device_id()) {
    output += indent + "  device_id: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.device_id().data(), value.device_id().size())
            .c_str());
    output += "\n";
  }
  if (value.has_include_signed_public_key()) {
    output += indent + "  include_signed_public_key: ";
    base::StringAppendF(&output, "%s",
                        value.include_signed_public_key() ? "true" : "false");
    output += "\n";
  }
  if (value.has_challenge()) {
    output += indent + "  challenge: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.challenge().data(), value.challenge().size())
            .c_str());
    output += "\n";
  }
  if (value.has_va_type()) {
    output += indent + "  va_type: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.va_type(), indent_size + 2)
            .c_str());
    output += "\n";
  }
  if (value.has_key_name_for_spkac()) {
    output += indent + "  key_name_for_spkac: ";
    base::StringAppendF(&output, "%s", value.key_name_for_spkac().c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const SignEnterpriseChallengeReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const SignEnterpriseChallengeReply& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_challenge_response()) {
    output += indent + "  challenge_response: ";
    base::StringAppendF(&output, "%s",
                        base::HexEncode(value.challenge_response().data(),
                                        value.challenge_response().size())
                            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const SignSimpleChallengeRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const SignSimpleChallengeRequest& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_key_label()) {
    output += indent + "  key_label: ";
    base::StringAppendF(&output, "%s", value.key_label().c_str());
    output += "\n";
  }
  if (value.has_username()) {
    output += indent + "  username: ";
    base::StringAppendF(&output, "%s", value.username().c_str());
    output += "\n";
  }
  if (value.has_challenge()) {
    output += indent + "  challenge: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.challenge().data(), value.challenge().size())
            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const SignSimpleChallengeReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const SignSimpleChallengeReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_challenge_response()) {
    output += indent + "  challenge_response: ";
    base::StringAppendF(&output, "%s",
                        base::HexEncode(value.challenge_response().data(),
                                        value.challenge_response().size())
                            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const SetKeyPayloadRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const SetKeyPayloadRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_key_label()) {
    output += indent + "  key_label: ";
    base::StringAppendF(&output, "%s", value.key_label().c_str());
    output += "\n";
  }
  if (value.has_username()) {
    output += indent + "  username: ";
    base::StringAppendF(&output, "%s", value.username().c_str());
    output += "\n";
  }
  if (value.has_payload()) {
    output += indent + "  payload: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.payload().data(), value.payload().size())
            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const SetKeyPayloadReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const SetKeyPayloadReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const DeleteKeysRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const DeleteKeysRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_key_label_match()) {
    output += indent + "  key_label_match: ";
    base::StringAppendF(&output, "%s", value.key_label_match().c_str());
    output += "\n";
  }
  if (value.has_username()) {
    output += indent + "  username: ";
    base::StringAppendF(&output, "%s", value.username().c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const DeleteKeysReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const DeleteKeysReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const ResetIdentityRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const ResetIdentityRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_reset_token()) {
    output += indent + "  reset_token: ";
    base::StringAppendF(&output, "%s", value.reset_token().c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const ResetIdentityReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const ResetIdentityReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_reset_request()) {
    output += indent + "  reset_request: ";
    base::StringAppendF(&output, "%s",
                        base::HexEncode(value.reset_request().data(),
                                        value.reset_request().size())
                            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const GetEnrollmentIdRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const GetEnrollmentIdRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_ignore_cache()) {
    output += indent + "  ignore_cache: ";
    base::StringAppendF(&output, "%s", value.ignore_cache() ? "true" : "false");
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const GetEnrollmentIdReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const GetEnrollmentIdReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_enrollment_id()) {
    output += indent + "  enrollment_id: ";
    base::StringAppendF(&output, "%s", value.enrollment_id().c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const GetCertifiedNvIndexRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(
    const GetCertifiedNvIndexRequest& value, int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_nv_index()) {
    output += indent + "  nv_index: ";
    base::StringAppendF(&output, "%" PRId32, value.nv_index());
    output += "\n";
  }
  if (value.has_nv_size()) {
    output += indent + "  nv_size: ";
    base::StringAppendF(&output, "%" PRId32, value.nv_size());
    output += "\n";
  }
  if (value.has_key_label()) {
    output += indent + "  key_label: ";
    base::StringAppendF(&output, "%s", value.key_label().c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const GetCertifiedNvIndexReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const GetCertifiedNvIndexReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_certified_data()) {
    output += indent + "  certified_data: ";
    base::StringAppendF(&output, "%s",
                        base::HexEncode(value.certified_data().data(),
                                        value.certified_data().size())
                            .c_str());
    output += "\n";
  }
  if (value.has_signature()) {
    output += indent + "  signature: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.signature().data(), value.signature().size())
            .c_str());
    output += "\n";
  }
  if (value.has_key_certificate()) {
    output += indent + "  key_certificate: ";
    base::StringAppendF(&output, "%s",
                        base::HexEncode(value.key_certificate().data(),
                                        value.key_certificate().size())
                            .c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

}  // namespace attestation
