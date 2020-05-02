// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libipp/ipp_operations.h"

#include <memory>

namespace ipp {
std::unique_ptr<Request> Request::NewRequest(Operation id) {
  switch (id) {
    case Operation::CUPS_Add_Modify_Class:
      return std::make_unique<Request_CUPS_Add_Modify_Class>();
    case Operation::CUPS_Add_Modify_Printer:
      return std::make_unique<Request_CUPS_Add_Modify_Printer>();
    case Operation::CUPS_Authenticate_Job:
      return std::make_unique<Request_CUPS_Authenticate_Job>();
    case Operation::CUPS_Create_Local_Printer:
      return std::make_unique<Request_CUPS_Create_Local_Printer>();
    case Operation::CUPS_Delete_Class:
      return std::make_unique<Request_CUPS_Delete_Class>();
    case Operation::CUPS_Delete_Printer:
      return std::make_unique<Request_CUPS_Delete_Printer>();
    case Operation::CUPS_Get_Classes:
      return std::make_unique<Request_CUPS_Get_Classes>();
    case Operation::CUPS_Get_Default:
      return std::make_unique<Request_CUPS_Get_Default>();
    case Operation::CUPS_Get_Document:
      return std::make_unique<Request_CUPS_Get_Document>();
    case Operation::CUPS_Get_Printers:
      return std::make_unique<Request_CUPS_Get_Printers>();
    case Operation::CUPS_Move_Job:
      return std::make_unique<Request_CUPS_Move_Job>();
    case Operation::CUPS_Set_Default:
      return std::make_unique<Request_CUPS_Set_Default>();
    case Operation::Cancel_Job:
      return std::make_unique<Request_Cancel_Job>();
    case Operation::Create_Job:
      return std::make_unique<Request_Create_Job>();
    case Operation::Get_Job_Attributes:
      return std::make_unique<Request_Get_Job_Attributes>();
    case Operation::Get_Jobs:
      return std::make_unique<Request_Get_Jobs>();
    case Operation::Get_Printer_Attributes:
      return std::make_unique<Request_Get_Printer_Attributes>();
    case Operation::Hold_Job:
      return std::make_unique<Request_Hold_Job>();
    case Operation::Pause_Printer:
      return std::make_unique<Request_Pause_Printer>();
    case Operation::Print_Job:
      return std::make_unique<Request_Print_Job>();
    case Operation::Print_URI:
      return std::make_unique<Request_Print_URI>();
    case Operation::Release_Job:
      return std::make_unique<Request_Release_Job>();
    case Operation::Resume_Printer:
      return std::make_unique<Request_Resume_Printer>();
    case Operation::Send_Document:
      return std::make_unique<Request_Send_Document>();
    case Operation::Send_URI:
      return std::make_unique<Request_Send_URI>();
    case Operation::Validate_Job:
      return std::make_unique<Request_Validate_Job>();
    default:
      return nullptr;
  }
}
std::unique_ptr<Response> Response::NewResponse(Operation id) {
  switch (id) {
    case Operation::CUPS_Add_Modify_Class:
      return std::make_unique<Response_CUPS_Add_Modify_Class>();
    case Operation::CUPS_Add_Modify_Printer:
      return std::make_unique<Response_CUPS_Add_Modify_Printer>();
    case Operation::CUPS_Authenticate_Job:
      return std::make_unique<Response_CUPS_Authenticate_Job>();
    case Operation::CUPS_Create_Local_Printer:
      return std::make_unique<Response_CUPS_Create_Local_Printer>();
    case Operation::CUPS_Delete_Class:
      return std::make_unique<Response_CUPS_Delete_Class>();
    case Operation::CUPS_Delete_Printer:
      return std::make_unique<Response_CUPS_Delete_Printer>();
    case Operation::CUPS_Get_Classes:
      return std::make_unique<Response_CUPS_Get_Classes>();
    case Operation::CUPS_Get_Default:
      return std::make_unique<Response_CUPS_Get_Default>();
    case Operation::CUPS_Get_Document:
      return std::make_unique<Response_CUPS_Get_Document>();
    case Operation::CUPS_Get_Printers:
      return std::make_unique<Response_CUPS_Get_Printers>();
    case Operation::CUPS_Move_Job:
      return std::make_unique<Response_CUPS_Move_Job>();
    case Operation::CUPS_Set_Default:
      return std::make_unique<Response_CUPS_Set_Default>();
    case Operation::Cancel_Job:
      return std::make_unique<Response_Cancel_Job>();
    case Operation::Create_Job:
      return std::make_unique<Response_Create_Job>();
    case Operation::Get_Job_Attributes:
      return std::make_unique<Response_Get_Job_Attributes>();
    case Operation::Get_Jobs:
      return std::make_unique<Response_Get_Jobs>();
    case Operation::Get_Printer_Attributes:
      return std::make_unique<Response_Get_Printer_Attributes>();
    case Operation::Hold_Job:
      return std::make_unique<Response_Hold_Job>();
    case Operation::Pause_Printer:
      return std::make_unique<Response_Pause_Printer>();
    case Operation::Print_Job:
      return std::make_unique<Response_Print_Job>();
    case Operation::Print_URI:
      return std::make_unique<Response_Print_URI>();
    case Operation::Release_Job:
      return std::make_unique<Response_Release_Job>();
    case Operation::Resume_Printer:
      return std::make_unique<Response_Resume_Printer>();
    case Operation::Send_Document:
      return std::make_unique<Response_Send_Document>();
    case Operation::Send_URI:
      return std::make_unique<Response_Send_URI>();
    case Operation::Validate_Job:
      return std::make_unique<Response_Validate_Job>();
    default:
      return nullptr;
  }
}
Request_CUPS_Add_Modify_Class::Request_CUPS_Add_Modify_Class()
    : Request(Operation::CUPS_Add_Modify_Class) {}
std::vector<Group*> Request_CUPS_Add_Modify_Class::GetKnownGroups() {
  return {&operation_attributes, &printer_attributes};
}
std::vector<const Group*> Request_CUPS_Add_Modify_Class::GetKnownGroups()
    const {
  return {&operation_attributes, &printer_attributes};
}
std::vector<Attribute*>
Request_CUPS_Add_Modify_Class::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset, &attributes_natural_language, &printer_uri};
}
std::vector<const Attribute*>
Request_CUPS_Add_Modify_Class::G_operation_attributes::GetKnownAttributes()
    const {
  return {&attributes_charset, &attributes_natural_language, &printer_uri};
}
const std::map<AttrName, AttrDef>
    Request_CUPS_Add_Modify_Class::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::printer_uri, {AttrType::uri, InternalType::kString, false}}};
std::vector<Attribute*>
Request_CUPS_Add_Modify_Class::G_printer_attributes::GetKnownAttributes() {
  return {&auth_info_required,
          &member_uris,
          &printer_is_accepting_jobs,
          &printer_info,
          &printer_location,
          &printer_more_info,
          &printer_state,
          &printer_state_message,
          &requesting_user_name_allowed,
          &requesting_user_name_denied};
}
std::vector<const Attribute*>
Request_CUPS_Add_Modify_Class::G_printer_attributes::GetKnownAttributes()
    const {
  return {&auth_info_required,
          &member_uris,
          &printer_is_accepting_jobs,
          &printer_info,
          &printer_location,
          &printer_more_info,
          &printer_state,
          &printer_state_message,
          &requesting_user_name_allowed,
          &requesting_user_name_denied};
}
const std::map<AttrName, AttrDef>
    Request_CUPS_Add_Modify_Class::G_printer_attributes::defs_{
        {AttrName::auth_info_required,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::member_uris, {AttrType::uri, InternalType::kString, true}},
        {AttrName::printer_is_accepting_jobs,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::printer_info,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_location,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_more_info,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_state,
         {AttrType::enum_, InternalType::kInteger, false}},
        {AttrName::printer_state_message,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::requesting_user_name_allowed,
         {AttrType::name, InternalType::kStringWithLanguage, true}},
        {AttrName::requesting_user_name_denied,
         {AttrType::name, InternalType::kStringWithLanguage, true}}};
Response_CUPS_Add_Modify_Class::Response_CUPS_Add_Modify_Class()
    : Response(Operation::CUPS_Add_Modify_Class) {}
std::vector<Group*> Response_CUPS_Add_Modify_Class::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Response_CUPS_Add_Modify_Class::GetKnownGroups()
    const {
  return {&operation_attributes};
}
std::vector<Attribute*>
Response_CUPS_Add_Modify_Class::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset, &attributes_natural_language, &status_message,
          &detailed_status_message};
}
std::vector<const Attribute*>
Response_CUPS_Add_Modify_Class::G_operation_attributes::GetKnownAttributes()
    const {
  return {&attributes_charset, &attributes_natural_language, &status_message,
          &detailed_status_message};
}
const std::map<AttrName, AttrDef>
    Response_CUPS_Add_Modify_Class::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::status_message,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::detailed_status_message,
         {AttrType::text, InternalType::kStringWithLanguage, false}}};
Request_CUPS_Add_Modify_Printer::Request_CUPS_Add_Modify_Printer()
    : Request(Operation::CUPS_Add_Modify_Printer) {}
std::vector<Group*> Request_CUPS_Add_Modify_Printer::GetKnownGroups() {
  return {&operation_attributes, &printer_attributes};
}
std::vector<const Group*> Request_CUPS_Add_Modify_Printer::GetKnownGroups()
    const {
  return {&operation_attributes, &printer_attributes};
}
std::vector<Attribute*>
Request_CUPS_Add_Modify_Printer::G_printer_attributes::GetKnownAttributes() {
  return {&auth_info_required,
          &job_sheets_default,
          &device_uri,
          &printer_is_accepting_jobs,
          &printer_info,
          &printer_location,
          &printer_more_info,
          &printer_state,
          &printer_state_message,
          &requesting_user_name_allowed,
          &requesting_user_name_denied};
}
std::vector<const Attribute*>
Request_CUPS_Add_Modify_Printer::G_printer_attributes::GetKnownAttributes()
    const {
  return {&auth_info_required,
          &job_sheets_default,
          &device_uri,
          &printer_is_accepting_jobs,
          &printer_info,
          &printer_location,
          &printer_more_info,
          &printer_state,
          &printer_state_message,
          &requesting_user_name_allowed,
          &requesting_user_name_denied};
}
const std::map<AttrName, AttrDef>
    Request_CUPS_Add_Modify_Printer::G_printer_attributes::defs_{
        {AttrName::auth_info_required,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::job_sheets_default,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::device_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_is_accepting_jobs,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::printer_info,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_location,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_more_info,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_state,
         {AttrType::enum_, InternalType::kInteger, false}},
        {AttrName::printer_state_message,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::requesting_user_name_allowed,
         {AttrType::name, InternalType::kStringWithLanguage, true}},
        {AttrName::requesting_user_name_denied,
         {AttrType::name, InternalType::kStringWithLanguage, true}}};
Response_CUPS_Add_Modify_Printer::Response_CUPS_Add_Modify_Printer()
    : Response(Operation::CUPS_Add_Modify_Printer) {}
std::vector<Group*> Response_CUPS_Add_Modify_Printer::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Response_CUPS_Add_Modify_Printer::GetKnownGroups()
    const {
  return {&operation_attributes};
}
Request_CUPS_Authenticate_Job::Request_CUPS_Authenticate_Job()
    : Request(Operation::CUPS_Authenticate_Job) {}
std::vector<Group*> Request_CUPS_Authenticate_Job::GetKnownGroups() {
  return {&operation_attributes, &job_attributes};
}
std::vector<const Group*> Request_CUPS_Authenticate_Job::GetKnownGroups()
    const {
  return {&operation_attributes, &job_attributes};
}
std::vector<Attribute*>
Request_CUPS_Authenticate_Job::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset, &attributes_natural_language, &printer_uri,
          &job_id, &job_uri};
}
std::vector<const Attribute*>
Request_CUPS_Authenticate_Job::G_operation_attributes::GetKnownAttributes()
    const {
  return {&attributes_charset, &attributes_natural_language, &printer_uri,
          &job_id, &job_uri};
}
const std::map<AttrName, AttrDef>
    Request_CUPS_Authenticate_Job::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::printer_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::job_id, {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_uri, {AttrType::uri, InternalType::kString, false}}};
std::vector<Attribute*>
Request_CUPS_Authenticate_Job::G_job_attributes::GetKnownAttributes() {
  return {&auth_info, &job_hold_until};
}
std::vector<const Attribute*>
Request_CUPS_Authenticate_Job::G_job_attributes::GetKnownAttributes() const {
  return {&auth_info, &job_hold_until};
}
const std::map<AttrName, AttrDef>
    Request_CUPS_Authenticate_Job::G_job_attributes::defs_{
        {AttrName::auth_info,
         {AttrType::text, InternalType::kStringWithLanguage, true}},
        {AttrName::job_hold_until,
         {AttrType::keyword, InternalType::kString, false}}};
Response_CUPS_Authenticate_Job::Response_CUPS_Authenticate_Job()
    : Response(Operation::CUPS_Authenticate_Job) {}
std::vector<Group*> Response_CUPS_Authenticate_Job::GetKnownGroups() {
  return {&operation_attributes, &unsupported_attributes};
}
std::vector<const Group*> Response_CUPS_Authenticate_Job::GetKnownGroups()
    const {
  return {&operation_attributes, &unsupported_attributes};
}
std::vector<Attribute*>
Response_CUPS_Authenticate_Job::G_unsupported_attributes::GetKnownAttributes() {
  return {};
}
std::vector<const Attribute*>
Response_CUPS_Authenticate_Job::G_unsupported_attributes::GetKnownAttributes()
    const {
  return {};
}
const std::map<AttrName, AttrDef>
    Response_CUPS_Authenticate_Job::G_unsupported_attributes::defs_{};
Request_CUPS_Create_Local_Printer::Request_CUPS_Create_Local_Printer()
    : Request(Operation::CUPS_Create_Local_Printer) {}
std::vector<Group*> Request_CUPS_Create_Local_Printer::GetKnownGroups() {
  return {&operation_attributes, &printer_attributes};
}
std::vector<const Group*> Request_CUPS_Create_Local_Printer::GetKnownGroups()
    const {
  return {&operation_attributes, &printer_attributes};
}
std::vector<Attribute*> Request_CUPS_Create_Local_Printer::
    G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset, &attributes_natural_language};
}
std::vector<const Attribute*>
Request_CUPS_Create_Local_Printer::G_operation_attributes::GetKnownAttributes()
    const {
  return {&attributes_charset, &attributes_natural_language};
}
const std::map<AttrName, AttrDef>
    Request_CUPS_Create_Local_Printer::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}}};
std::vector<Attribute*>
Request_CUPS_Create_Local_Printer::G_printer_attributes::GetKnownAttributes() {
  return {&printer_name,         &device_uri,   &printer_device_id,
          &printer_geo_location, &printer_info, &printer_location};
}
std::vector<const Attribute*>
Request_CUPS_Create_Local_Printer::G_printer_attributes::GetKnownAttributes()
    const {
  return {&printer_name,         &device_uri,   &printer_device_id,
          &printer_geo_location, &printer_info, &printer_location};
}
const std::map<AttrName, AttrDef>
    Request_CUPS_Create_Local_Printer::G_printer_attributes::defs_{
        {AttrName::printer_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::device_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_device_id,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_geo_location,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_info,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_location,
         {AttrType::text, InternalType::kStringWithLanguage, false}}};
Response_CUPS_Create_Local_Printer::Response_CUPS_Create_Local_Printer()
    : Response(Operation::CUPS_Create_Local_Printer) {}
std::vector<Group*> Response_CUPS_Create_Local_Printer::GetKnownGroups() {
  return {&operation_attributes, &printer_attributes};
}
std::vector<const Group*> Response_CUPS_Create_Local_Printer::GetKnownGroups()
    const {
  return {&operation_attributes, &printer_attributes};
}
std::vector<Attribute*>
Response_CUPS_Create_Local_Printer::G_printer_attributes::GetKnownAttributes() {
  return {&printer_id, &printer_is_accepting_jobs, &printer_state,
          &printer_state_reasons, &printer_uri_supported};
}
std::vector<const Attribute*>
Response_CUPS_Create_Local_Printer::G_printer_attributes::GetKnownAttributes()
    const {
  return {&printer_id, &printer_is_accepting_jobs, &printer_state,
          &printer_state_reasons, &printer_uri_supported};
}
const std::map<AttrName, AttrDef>
    Response_CUPS_Create_Local_Printer::G_printer_attributes::defs_{
        {AttrName::printer_id,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::printer_is_accepting_jobs,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::printer_state,
         {AttrType::enum_, InternalType::kInteger, false}},
        {AttrName::printer_state_reasons,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::printer_uri_supported,
         {AttrType::uri, InternalType::kString, true}}};
Request_CUPS_Delete_Class::Request_CUPS_Delete_Class()
    : Request(Operation::CUPS_Delete_Class) {}
std::vector<Group*> Request_CUPS_Delete_Class::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Request_CUPS_Delete_Class::GetKnownGroups() const {
  return {&operation_attributes};
}
Response_CUPS_Delete_Class::Response_CUPS_Delete_Class()
    : Response(Operation::CUPS_Delete_Class) {}
std::vector<Group*> Response_CUPS_Delete_Class::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Response_CUPS_Delete_Class::GetKnownGroups() const {
  return {&operation_attributes};
}
Request_CUPS_Delete_Printer::Request_CUPS_Delete_Printer()
    : Request(Operation::CUPS_Delete_Printer) {}
std::vector<Group*> Request_CUPS_Delete_Printer::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Request_CUPS_Delete_Printer::GetKnownGroups() const {
  return {&operation_attributes};
}
Response_CUPS_Delete_Printer::Response_CUPS_Delete_Printer()
    : Response(Operation::CUPS_Delete_Printer) {}
std::vector<Group*> Response_CUPS_Delete_Printer::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Response_CUPS_Delete_Printer::GetKnownGroups() const {
  return {&operation_attributes};
}
Request_CUPS_Get_Classes::Request_CUPS_Get_Classes()
    : Request(Operation::CUPS_Get_Classes) {}
std::vector<Group*> Request_CUPS_Get_Classes::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Request_CUPS_Get_Classes::GetKnownGroups() const {
  return {&operation_attributes};
}
std::vector<Attribute*>
Request_CUPS_Get_Classes::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset, &attributes_natural_language,
          &first_printer_name, &limit,
          &printer_location,   &printer_type,
          &printer_type_mask,  &requested_attributes,
          &requested_user_name};
}
std::vector<const Attribute*>
Request_CUPS_Get_Classes::G_operation_attributes::GetKnownAttributes() const {
  return {&attributes_charset, &attributes_natural_language,
          &first_printer_name, &limit,
          &printer_location,   &printer_type,
          &printer_type_mask,  &requested_attributes,
          &requested_user_name};
}
const std::map<AttrName, AttrDef>
    Request_CUPS_Get_Classes::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::first_printer_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::limit, {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::printer_location,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_type,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::printer_type_mask,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::requested_attributes,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::requested_user_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}}};
Response_CUPS_Get_Classes::Response_CUPS_Get_Classes()
    : Response(Operation::CUPS_Get_Classes) {}
std::vector<Group*> Response_CUPS_Get_Classes::GetKnownGroups() {
  return {&operation_attributes, &printer_attributes};
}
std::vector<const Group*> Response_CUPS_Get_Classes::GetKnownGroups() const {
  return {&operation_attributes, &printer_attributes};
}
std::vector<Attribute*>
Response_CUPS_Get_Classes::G_printer_attributes::GetKnownAttributes() {
  return {&member_names, &member_uris};
}
std::vector<const Attribute*>
Response_CUPS_Get_Classes::G_printer_attributes::GetKnownAttributes() const {
  return {&member_names, &member_uris};
}
const std::map<AttrName, AttrDef>
    Response_CUPS_Get_Classes::G_printer_attributes::defs_{
        {AttrName::member_names,
         {AttrType::name, InternalType::kStringWithLanguage, true}},
        {AttrName::member_uris, {AttrType::uri, InternalType::kString, true}}};
Request_CUPS_Get_Default::Request_CUPS_Get_Default()
    : Request(Operation::CUPS_Get_Default) {}
std::vector<Group*> Request_CUPS_Get_Default::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Request_CUPS_Get_Default::GetKnownGroups() const {
  return {&operation_attributes};
}
std::vector<Attribute*>
Request_CUPS_Get_Default::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset, &attributes_natural_language,
          &requested_attributes};
}
std::vector<const Attribute*>
Request_CUPS_Get_Default::G_operation_attributes::GetKnownAttributes() const {
  return {&attributes_charset, &attributes_natural_language,
          &requested_attributes};
}
const std::map<AttrName, AttrDef>
    Request_CUPS_Get_Default::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::requested_attributes,
         {AttrType::keyword, InternalType::kString, true}}};
Response_CUPS_Get_Default::Response_CUPS_Get_Default()
    : Response(Operation::CUPS_Get_Default) {}
std::vector<Group*> Response_CUPS_Get_Default::GetKnownGroups() {
  return {&operation_attributes, &printer_attributes};
}
std::vector<const Group*> Response_CUPS_Get_Default::GetKnownGroups() const {
  return {&operation_attributes, &printer_attributes};
}
std::vector<Attribute*>
Response_CUPS_Get_Default::G_printer_attributes::GetKnownAttributes() {
  return {&baling_type_supported,
          &baling_when_supported,
          &binding_reference_edge_supported,
          &binding_type_supported,
          &charset_configured,
          &charset_supported,
          &coating_sides_supported,
          &coating_type_supported,
          &color_supported,
          &compression_supported,
          &copies_default,
          &copies_supported,
          &cover_back_default,
          &cover_back_supported,
          &cover_front_default,
          &cover_front_supported,
          &covering_name_supported,
          &document_charset_default,
          &document_charset_supported,
          &document_digital_signature_default,
          &document_digital_signature_supported,
          &document_format_default,
          &document_format_details_default,
          &document_format_details_supported,
          &document_format_supported,
          &document_format_version_default,
          &document_format_version_supported,
          &document_natural_language_default,
          &document_natural_language_supported,
          &document_password_supported,
          &feed_orientation_supported,
          &finishing_template_supported,
          &finishings_col_database,
          &finishings_col_default,
          &finishings_col_ready,
          &finishings_default,
          &finishings_ready,
          &finishings_supported,
          &folding_direction_supported,
          &folding_offset_supported,
          &folding_reference_edge_supported,
          &font_name_requested_default,
          &font_name_requested_supported,
          &font_size_requested_default,
          &font_size_requested_supported,
          &generated_natural_language_supported,
          &identify_actions_default,
          &identify_actions_supported,
          &insert_after_page_number_supported,
          &insert_count_supported,
          &insert_sheet_default,
          &ipp_features_supported,
          &ipp_versions_supported,
          &ippget_event_life,
          &job_account_id_default,
          &job_account_id_supported,
          &job_account_type_default,
          &job_account_type_supported,
          &job_accounting_sheets_default,
          &job_accounting_user_id_default,
          &job_accounting_user_id_supported,
          &job_authorization_uri_supported,
          &job_constraints_supported,
          &job_copies_default,
          &job_copies_supported,
          &job_cover_back_default,
          &job_cover_back_supported,
          &job_cover_front_default,
          &job_cover_front_supported,
          &job_delay_output_until_default,
          &job_delay_output_until_supported,
          &job_delay_output_until_time_supported,
          &job_error_action_default,
          &job_error_action_supported,
          &job_error_sheet_default,
          &job_finishings_col_default,
          &job_finishings_col_ready,
          &job_finishings_default,
          &job_finishings_ready,
          &job_finishings_supported,
          &job_hold_until_default,
          &job_hold_until_supported,
          &job_hold_until_time_supported,
          &job_ids_supported,
          &job_impressions_supported,
          &job_k_octets_supported,
          &job_media_sheets_supported,
          &job_message_to_operator_default,
          &job_message_to_operator_supported,
          &job_pages_per_set_supported,
          &job_password_encryption_supported,
          &job_password_supported,
          &job_phone_number_default,
          &job_phone_number_supported,
          &job_priority_default,
          &job_priority_supported,
          &job_recipient_name_default,
          &job_recipient_name_supported,
          &job_resolvers_supported,
          &job_sheet_message_default,
          &job_sheet_message_supported,
          &job_sheets_col_default,
          &job_sheets_default,
          &job_sheets_supported,
          &job_spooling_supported,
          &jpeg_k_octets_supported,
          &jpeg_x_dimension_supported,
          &jpeg_y_dimension_supported,
          &laminating_sides_supported,
          &laminating_type_supported,
          &max_save_info_supported,
          &max_stitching_locations_supported,
          &media_back_coating_supported,
          &media_bottom_margin_supported,
          &media_col_database,
          &media_col_default,
          &media_col_ready,
          &media_color_supported,
          &media_default,
          &media_front_coating_supported,
          &media_grain_supported,
          &media_hole_count_supported,
          &media_info_supported,
          &media_left_margin_supported,
          &media_order_count_supported,
          &media_pre_printed_supported,
          &media_ready,
          &media_recycled_supported,
          &media_right_margin_supported,
          &media_size_supported,
          &media_source_supported,
          &media_supported,
          &media_thickness_supported,
          &media_tooth_supported,
          &media_top_margin_supported,
          &media_type_supported,
          &media_weight_metric_supported,
          &multiple_document_handling_default,
          &multiple_document_handling_supported,
          &multiple_document_jobs_supported,
          &multiple_operation_time_out,
          &multiple_operation_time_out_action,
          &natural_language_configured,
          &notify_events_default,
          &notify_events_supported,
          &notify_lease_duration_default,
          &notify_lease_duration_supported,
          &notify_pull_method_supported,
          &notify_schemes_supported,
          &number_up_default,
          &number_up_supported,
          &oauth_authorization_server_uri,
          &operations_supported,
          &orientation_requested_default,
          &orientation_requested_supported,
          &output_bin_default,
          &output_bin_supported,
          &output_device_supported,
          &output_device_uuid_supported,
          &page_delivery_default,
          &page_delivery_supported,
          &page_order_received_default,
          &page_order_received_supported,
          &page_ranges_supported,
          &pages_per_subset_supported,
          &parent_printers_supported,
          &pdf_k_octets_supported,
          &pdf_versions_supported,
          &pdl_init_file_default,
          &pdl_init_file_entry_supported,
          &pdl_init_file_location_supported,
          &pdl_init_file_name_subdirectory_supported,
          &pdl_init_file_name_supported,
          &pdl_init_file_supported,
          &pdl_override_supported,
          &preferred_attributes_supported,
          &presentation_direction_number_up_default,
          &presentation_direction_number_up_supported,
          &print_color_mode_default,
          &print_color_mode_supported,
          &print_content_optimize_default,
          &print_content_optimize_supported,
          &print_quality_default,
          &print_quality_supported,
          &print_rendering_intent_default,
          &print_rendering_intent_supported,
          &printer_charge_info,
          &printer_charge_info_uri,
          &printer_contact_col,
          &printer_current_time,
          &printer_device_id,
          &printer_dns_sd_name,
          &printer_driver_installer,
          &printer_geo_location,
          &printer_icc_profiles,
          &printer_icons,
          &printer_info,
          &printer_location,
          &printer_make_and_model,
          &printer_more_info_manufacturer,
          &printer_name,
          &printer_organization,
          &printer_organizational_unit,
          &printer_resolution_default,
          &printer_resolution_supported,
          &printer_static_resource_directory_uri,
          &printer_static_resource_k_octets_supported,
          &printer_strings_languages_supported,
          &printer_strings_uri,
          &printer_xri_supported,
          &proof_print_default,
          &proof_print_supported,
          &punching_hole_diameter_configured,
          &punching_locations_supported,
          &punching_offset_supported,
          &punching_reference_edge_supported,
          &pwg_raster_document_resolution_supported,
          &pwg_raster_document_sheet_back,
          &pwg_raster_document_type_supported,
          &reference_uri_schemes_supported,
          &requesting_user_uri_supported,
          &save_disposition_supported,
          &save_document_format_default,
          &save_document_format_supported,
          &save_location_default,
          &save_location_supported,
          &save_name_subdirectory_supported,
          &save_name_supported,
          &separator_sheets_default,
          &sheet_collate_default,
          &sheet_collate_supported,
          &sides_default,
          &sides_supported,
          &stitching_angle_supported,
          &stitching_locations_supported,
          &stitching_method_supported,
          &stitching_offset_supported,
          &stitching_reference_edge_supported,
          &subordinate_printers_supported,
          &trimming_offset_supported,
          &trimming_reference_edge_supported,
          &trimming_type_supported,
          &trimming_when_supported,
          &uri_authentication_supported,
          &uri_security_supported,
          &which_jobs_supported,
          &x_image_position_default,
          &x_image_position_supported,
          &x_image_shift_default,
          &x_image_shift_supported,
          &x_side1_image_shift_default,
          &x_side1_image_shift_supported,
          &x_side2_image_shift_default,
          &x_side2_image_shift_supported,
          &y_image_position_default,
          &y_image_position_supported,
          &y_image_shift_default,
          &y_image_shift_supported,
          &y_side1_image_shift_default,
          &y_side1_image_shift_supported,
          &y_side2_image_shift_default,
          &y_side2_image_shift_supported};
}
std::vector<const Attribute*>
Response_CUPS_Get_Default::G_printer_attributes::GetKnownAttributes() const {
  return {&baling_type_supported,
          &baling_when_supported,
          &binding_reference_edge_supported,
          &binding_type_supported,
          &charset_configured,
          &charset_supported,
          &coating_sides_supported,
          &coating_type_supported,
          &color_supported,
          &compression_supported,
          &copies_default,
          &copies_supported,
          &cover_back_default,
          &cover_back_supported,
          &cover_front_default,
          &cover_front_supported,
          &covering_name_supported,
          &document_charset_default,
          &document_charset_supported,
          &document_digital_signature_default,
          &document_digital_signature_supported,
          &document_format_default,
          &document_format_details_default,
          &document_format_details_supported,
          &document_format_supported,
          &document_format_version_default,
          &document_format_version_supported,
          &document_natural_language_default,
          &document_natural_language_supported,
          &document_password_supported,
          &feed_orientation_supported,
          &finishing_template_supported,
          &finishings_col_database,
          &finishings_col_default,
          &finishings_col_ready,
          &finishings_default,
          &finishings_ready,
          &finishings_supported,
          &folding_direction_supported,
          &folding_offset_supported,
          &folding_reference_edge_supported,
          &font_name_requested_default,
          &font_name_requested_supported,
          &font_size_requested_default,
          &font_size_requested_supported,
          &generated_natural_language_supported,
          &identify_actions_default,
          &identify_actions_supported,
          &insert_after_page_number_supported,
          &insert_count_supported,
          &insert_sheet_default,
          &ipp_features_supported,
          &ipp_versions_supported,
          &ippget_event_life,
          &job_account_id_default,
          &job_account_id_supported,
          &job_account_type_default,
          &job_account_type_supported,
          &job_accounting_sheets_default,
          &job_accounting_user_id_default,
          &job_accounting_user_id_supported,
          &job_authorization_uri_supported,
          &job_constraints_supported,
          &job_copies_default,
          &job_copies_supported,
          &job_cover_back_default,
          &job_cover_back_supported,
          &job_cover_front_default,
          &job_cover_front_supported,
          &job_delay_output_until_default,
          &job_delay_output_until_supported,
          &job_delay_output_until_time_supported,
          &job_error_action_default,
          &job_error_action_supported,
          &job_error_sheet_default,
          &job_finishings_col_default,
          &job_finishings_col_ready,
          &job_finishings_default,
          &job_finishings_ready,
          &job_finishings_supported,
          &job_hold_until_default,
          &job_hold_until_supported,
          &job_hold_until_time_supported,
          &job_ids_supported,
          &job_impressions_supported,
          &job_k_octets_supported,
          &job_media_sheets_supported,
          &job_message_to_operator_default,
          &job_message_to_operator_supported,
          &job_pages_per_set_supported,
          &job_password_encryption_supported,
          &job_password_supported,
          &job_phone_number_default,
          &job_phone_number_supported,
          &job_priority_default,
          &job_priority_supported,
          &job_recipient_name_default,
          &job_recipient_name_supported,
          &job_resolvers_supported,
          &job_sheet_message_default,
          &job_sheet_message_supported,
          &job_sheets_col_default,
          &job_sheets_default,
          &job_sheets_supported,
          &job_spooling_supported,
          &jpeg_k_octets_supported,
          &jpeg_x_dimension_supported,
          &jpeg_y_dimension_supported,
          &laminating_sides_supported,
          &laminating_type_supported,
          &max_save_info_supported,
          &max_stitching_locations_supported,
          &media_back_coating_supported,
          &media_bottom_margin_supported,
          &media_col_database,
          &media_col_default,
          &media_col_ready,
          &media_color_supported,
          &media_default,
          &media_front_coating_supported,
          &media_grain_supported,
          &media_hole_count_supported,
          &media_info_supported,
          &media_left_margin_supported,
          &media_order_count_supported,
          &media_pre_printed_supported,
          &media_ready,
          &media_recycled_supported,
          &media_right_margin_supported,
          &media_size_supported,
          &media_source_supported,
          &media_supported,
          &media_thickness_supported,
          &media_tooth_supported,
          &media_top_margin_supported,
          &media_type_supported,
          &media_weight_metric_supported,
          &multiple_document_handling_default,
          &multiple_document_handling_supported,
          &multiple_document_jobs_supported,
          &multiple_operation_time_out,
          &multiple_operation_time_out_action,
          &natural_language_configured,
          &notify_events_default,
          &notify_events_supported,
          &notify_lease_duration_default,
          &notify_lease_duration_supported,
          &notify_pull_method_supported,
          &notify_schemes_supported,
          &number_up_default,
          &number_up_supported,
          &oauth_authorization_server_uri,
          &operations_supported,
          &orientation_requested_default,
          &orientation_requested_supported,
          &output_bin_default,
          &output_bin_supported,
          &output_device_supported,
          &output_device_uuid_supported,
          &page_delivery_default,
          &page_delivery_supported,
          &page_order_received_default,
          &page_order_received_supported,
          &page_ranges_supported,
          &pages_per_subset_supported,
          &parent_printers_supported,
          &pdf_k_octets_supported,
          &pdf_versions_supported,
          &pdl_init_file_default,
          &pdl_init_file_entry_supported,
          &pdl_init_file_location_supported,
          &pdl_init_file_name_subdirectory_supported,
          &pdl_init_file_name_supported,
          &pdl_init_file_supported,
          &pdl_override_supported,
          &preferred_attributes_supported,
          &presentation_direction_number_up_default,
          &presentation_direction_number_up_supported,
          &print_color_mode_default,
          &print_color_mode_supported,
          &print_content_optimize_default,
          &print_content_optimize_supported,
          &print_quality_default,
          &print_quality_supported,
          &print_rendering_intent_default,
          &print_rendering_intent_supported,
          &printer_charge_info,
          &printer_charge_info_uri,
          &printer_contact_col,
          &printer_current_time,
          &printer_device_id,
          &printer_dns_sd_name,
          &printer_driver_installer,
          &printer_geo_location,
          &printer_icc_profiles,
          &printer_icons,
          &printer_info,
          &printer_location,
          &printer_make_and_model,
          &printer_more_info_manufacturer,
          &printer_name,
          &printer_organization,
          &printer_organizational_unit,
          &printer_resolution_default,
          &printer_resolution_supported,
          &printer_static_resource_directory_uri,
          &printer_static_resource_k_octets_supported,
          &printer_strings_languages_supported,
          &printer_strings_uri,
          &printer_xri_supported,
          &proof_print_default,
          &proof_print_supported,
          &punching_hole_diameter_configured,
          &punching_locations_supported,
          &punching_offset_supported,
          &punching_reference_edge_supported,
          &pwg_raster_document_resolution_supported,
          &pwg_raster_document_sheet_back,
          &pwg_raster_document_type_supported,
          &reference_uri_schemes_supported,
          &requesting_user_uri_supported,
          &save_disposition_supported,
          &save_document_format_default,
          &save_document_format_supported,
          &save_location_default,
          &save_location_supported,
          &save_name_subdirectory_supported,
          &save_name_supported,
          &separator_sheets_default,
          &sheet_collate_default,
          &sheet_collate_supported,
          &sides_default,
          &sides_supported,
          &stitching_angle_supported,
          &stitching_locations_supported,
          &stitching_method_supported,
          &stitching_offset_supported,
          &stitching_reference_edge_supported,
          &subordinate_printers_supported,
          &trimming_offset_supported,
          &trimming_reference_edge_supported,
          &trimming_type_supported,
          &trimming_when_supported,
          &uri_authentication_supported,
          &uri_security_supported,
          &which_jobs_supported,
          &x_image_position_default,
          &x_image_position_supported,
          &x_image_shift_default,
          &x_image_shift_supported,
          &x_side1_image_shift_default,
          &x_side1_image_shift_supported,
          &x_side2_image_shift_default,
          &x_side2_image_shift_supported,
          &y_image_position_default,
          &y_image_position_supported,
          &y_image_shift_default,
          &y_image_shift_supported,
          &y_side1_image_shift_default,
          &y_side1_image_shift_supported,
          &y_side2_image_shift_default,
          &y_side2_image_shift_supported};
}
const std::map<AttrName, AttrDef>
    Response_CUPS_Get_Default::G_printer_attributes::defs_{
        {AttrName::baling_type_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::baling_when_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::binding_reference_edge_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::binding_type_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::charset_configured,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::charset_supported,
         {AttrType::charset, InternalType::kString, true}},
        {AttrName::coating_sides_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::coating_type_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::color_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::compression_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::copies_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::copies_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::cover_back_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_cover_back_default(); }}},
        {AttrName::cover_back_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::cover_front_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_cover_front_default(); }}},
        {AttrName::cover_front_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::covering_name_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::document_charset_default,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::document_charset_supported,
         {AttrType::charset, InternalType::kString, true}},
        {AttrName::document_digital_signature_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::document_digital_signature_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::document_format_default,
         {AttrType::mimeMediaType, InternalType::kString, false}},
        {AttrName::document_format_details_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* {
            return new C_document_format_details_default();
          }}},
        {AttrName::document_format_details_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::document_format_supported,
         {AttrType::mimeMediaType, InternalType::kString, true}},
        {AttrName::document_format_version_default,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::document_format_version_supported,
         {AttrType::text, InternalType::kStringWithLanguage, true}},
        {AttrName::document_natural_language_default,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::document_natural_language_supported,
         {AttrType::naturalLanguage, InternalType::kString, true}},
        {AttrName::document_password_supported,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::feed_orientation_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::finishing_template_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::finishings_col_database,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_finishings_col_database(); }}},
        {AttrName::finishings_col_default,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_finishings_col_default(); }}},
        {AttrName::finishings_col_ready,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_finishings_col_ready(); }}},
        {AttrName::finishings_default,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::finishings_ready,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::finishings_supported,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::folding_direction_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::folding_offset_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::folding_reference_edge_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::font_name_requested_default,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::font_name_requested_supported,
         {AttrType::name, InternalType::kStringWithLanguage, true}},
        {AttrName::font_size_requested_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::font_size_requested_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::generated_natural_language_supported,
         {AttrType::naturalLanguage, InternalType::kString, true}},
        {AttrName::identify_actions_default,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::identify_actions_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::insert_after_page_number_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::insert_count_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::insert_sheet_default,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_insert_sheet_default(); }}},
        {AttrName::ipp_features_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::ipp_versions_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::ippget_event_life,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_account_id_default,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_account_id_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_account_type_default,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_account_type_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::job_accounting_sheets_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* {
            return new C_job_accounting_sheets_default();
          }}},
        {AttrName::job_accounting_user_id_default,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_accounting_user_id_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_authorization_uri_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_constraints_supported,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_job_constraints_supported(); }}},
        {AttrName::job_copies_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_copies_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::job_cover_back_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_cover_back_default(); }}},
        {AttrName::job_cover_back_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::job_cover_front_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_cover_front_default(); }}},
        {AttrName::job_cover_front_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::job_delay_output_until_default,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_delay_output_until_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::job_delay_output_until_time_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::job_error_action_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::job_error_action_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::job_error_sheet_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_error_sheet_default(); }}},
        {AttrName::job_finishings_col_default,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_job_finishings_col_default(); }}},
        {AttrName::job_finishings_col_ready,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_job_finishings_col_ready(); }}},
        {AttrName::job_finishings_default,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::job_finishings_ready,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::job_finishings_supported,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::job_hold_until_default,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_hold_until_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::job_hold_until_time_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::job_ids_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_impressions_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::job_k_octets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::job_media_sheets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::job_message_to_operator_default,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::job_message_to_operator_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_pages_per_set_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_password_encryption_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::job_password_supported,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_phone_number_default,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::job_phone_number_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_priority_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_priority_supported,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_recipient_name_default,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_recipient_name_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_resolvers_supported,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_job_resolvers_supported(); }}},
        {AttrName::job_sheet_message_default,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::job_sheet_message_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_sheets_col_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_sheets_col_default(); }}},
        {AttrName::job_sheets_default,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_sheets_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::job_spooling_supported,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::jpeg_k_octets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::jpeg_x_dimension_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::jpeg_y_dimension_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::laminating_sides_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::laminating_type_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::max_save_info_supported,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::max_stitching_locations_supported,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_back_coating_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_bottom_margin_supported,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::media_col_database,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_media_col_database(); }}},
        {AttrName::media_col_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col_default(); }}},
        {AttrName::media_col_ready,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_media_col_ready(); }}},
        {AttrName::media_color_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_default,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_front_coating_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_grain_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_hole_count_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::media_info_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::media_left_margin_supported,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::media_order_count_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::media_pre_printed_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_ready,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_recycled_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_right_margin_supported,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::media_size_supported,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_media_size_supported(); }}},
        {AttrName::media_source_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_thickness_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::media_tooth_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_top_margin_supported,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::media_type_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_weight_metric_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::multiple_document_handling_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::multiple_document_handling_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::multiple_document_jobs_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::multiple_operation_time_out,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::multiple_operation_time_out_action,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::natural_language_configured,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::notify_events_default,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::notify_events_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::notify_lease_duration_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::notify_lease_duration_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::notify_pull_method_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::notify_schemes_supported,
         {AttrType::uriScheme, InternalType::kString, true}},
        {AttrName::number_up_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::number_up_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::oauth_authorization_server_uri,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::operations_supported,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::orientation_requested_default,
         {AttrType::enum_, InternalType::kInteger, false}},
        {AttrName::orientation_requested_supported,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::output_bin_default,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::output_bin_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::output_device_supported,
         {AttrType::name, InternalType::kStringWithLanguage, true}},
        {AttrName::output_device_uuid_supported,
         {AttrType::uri, InternalType::kString, true}},
        {AttrName::page_delivery_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::page_delivery_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::page_order_received_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::page_order_received_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::page_ranges_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::pages_per_subset_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::parent_printers_supported,
         {AttrType::uri, InternalType::kString, true}},
        {AttrName::pdf_k_octets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::pdf_versions_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::pdl_init_file_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_pdl_init_file_default(); }}},
        {AttrName::pdl_init_file_entry_supported,
         {AttrType::name, InternalType::kStringWithLanguage, true}},
        {AttrName::pdl_init_file_location_supported,
         {AttrType::uri, InternalType::kString, true}},
        {AttrName::pdl_init_file_name_subdirectory_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::pdl_init_file_name_supported,
         {AttrType::name, InternalType::kStringWithLanguage, true}},
        {AttrName::pdl_init_file_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::pdl_override_supported,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::preferred_attributes_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::presentation_direction_number_up_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::presentation_direction_number_up_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::print_color_mode_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::print_color_mode_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::print_content_optimize_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::print_content_optimize_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::print_quality_default,
         {AttrType::enum_, InternalType::kInteger, false}},
        {AttrName::print_quality_supported,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::print_rendering_intent_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::print_rendering_intent_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::printer_charge_info,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_charge_info_uri,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_contact_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_printer_contact_col(); }}},
        {AttrName::printer_current_time,
         {AttrType::dateTime, InternalType::kDateTime, false}},
        {AttrName::printer_device_id,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_dns_sd_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_driver_installer,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_geo_location,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_icc_profiles,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_printer_icc_profiles(); }}},
        {AttrName::printer_icons, {AttrType::uri, InternalType::kString, true}},
        {AttrName::printer_info,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_location,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_make_and_model,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_more_info_manufacturer,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_organization,
         {AttrType::text, InternalType::kStringWithLanguage, true}},
        {AttrName::printer_organizational_unit,
         {AttrType::text, InternalType::kStringWithLanguage, true}},
        {AttrName::printer_resolution_default,
         {AttrType::resolution, InternalType::kResolution, false}},
        {AttrName::printer_resolution_supported,
         {AttrType::resolution, InternalType::kResolution, false}},
        {AttrName::printer_static_resource_directory_uri,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_static_resource_k_octets_supported,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::printer_strings_languages_supported,
         {AttrType::naturalLanguage, InternalType::kString, true}},
        {AttrName::printer_strings_uri,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_xri_supported,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_printer_xri_supported(); }}},
        {AttrName::proof_print_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_proof_print_default(); }}},
        {AttrName::proof_print_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::punching_hole_diameter_configured,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::punching_locations_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::punching_offset_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::punching_reference_edge_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::pwg_raster_document_resolution_supported,
         {AttrType::resolution, InternalType::kResolution, true}},
        {AttrName::pwg_raster_document_sheet_back,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::pwg_raster_document_type_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::reference_uri_schemes_supported,
         {AttrType::uriScheme, InternalType::kString, true}},
        {AttrName::requesting_user_uri_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::save_disposition_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::save_document_format_default,
         {AttrType::mimeMediaType, InternalType::kString, false}},
        {AttrName::save_document_format_supported,
         {AttrType::mimeMediaType, InternalType::kString, true}},
        {AttrName::save_location_default,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::save_location_supported,
         {AttrType::uri, InternalType::kString, true}},
        {AttrName::save_name_subdirectory_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::save_name_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::separator_sheets_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_separator_sheets_default(); }}},
        {AttrName::sheet_collate_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::sheet_collate_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::sides_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::sides_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::stitching_angle_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::stitching_locations_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::stitching_method_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::stitching_offset_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::stitching_reference_edge_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::subordinate_printers_supported,
         {AttrType::uri, InternalType::kString, true}},
        {AttrName::trimming_offset_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::trimming_reference_edge_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::trimming_type_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::trimming_when_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::uri_authentication_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::uri_security_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::which_jobs_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::x_image_position_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::x_image_position_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::x_image_shift_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::x_image_shift_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::x_side1_image_shift_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::x_side1_image_shift_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::x_side2_image_shift_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::x_side2_image_shift_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::y_image_position_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::y_image_position_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::y_image_shift_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_image_shift_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::y_side1_image_shift_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_side1_image_shift_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::y_side2_image_shift_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_side2_image_shift_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_cover_back_default::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_cover_back_default::GetKnownAttributes() const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_cover_back_default::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_cover_front_default::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_cover_front_default::GetKnownAttributes() const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_cover_front_default::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_document_format_details_default::GetKnownAttributes() {
  return {&document_format,
          &document_format_device_id,
          &document_format_version,
          &document_natural_language,
          &document_source_application_name,
          &document_source_application_version,
          &document_source_os_name,
          &document_source_os_version};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_document_format_details_default::GetKnownAttributes() const {
  return {&document_format,
          &document_format_device_id,
          &document_format_version,
          &document_natural_language,
          &document_source_application_name,
          &document_source_application_version,
          &document_source_os_name,
          &document_source_os_version};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_document_format_details_default::defs_{
        {AttrName::document_format,
         {AttrType::mimeMediaType, InternalType::kString, false}},
        {AttrName::document_format_device_id,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::document_format_version,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::document_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, true}},
        {AttrName::document_source_application_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::document_source_application_version,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::document_source_os_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::document_source_os_version,
         {AttrType::text, InternalType::kStringWithLanguage, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::GetKnownAttributes() {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::GetKnownAttributes() const {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_database::defs_{
        {AttrName::baling,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_baling(); }}},
        {AttrName::binding,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_binding(); }}},
        {AttrName::coating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_coating(); }}},
        {AttrName::covering,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_covering(); }}},
        {AttrName::finishing_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::folding,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_folding(); }}},
        {AttrName::imposition_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::laminating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_laminating(); }}},
        {AttrName::media_sheets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::punching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_punching(); }}},
        {AttrName::stitching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_stitching(); }}},
        {AttrName::trimming,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_trimming(); }}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_baling::GetKnownAttributes() {
  return {&baling_type, &baling_when};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_baling::GetKnownAttributes() const {
  return {&baling_type, &baling_when};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_database::C_baling::defs_{
        {AttrName::baling_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::baling_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_binding::GetKnownAttributes() {
  return {&binding_reference_edge, &binding_type};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_binding::GetKnownAttributes() const {
  return {&binding_reference_edge, &binding_type};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_database::C_binding::defs_{
        {AttrName::binding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::binding_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_coating::GetKnownAttributes() {
  return {&coating_sides, &coating_type};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_coating::GetKnownAttributes() const {
  return {&coating_sides, &coating_type};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_database::C_coating::defs_{
        {AttrName::coating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::coating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_covering::GetKnownAttributes() {
  return {&covering_name};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_covering::GetKnownAttributes() const {
  return {&covering_name};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_database::C_covering::defs_{
        {AttrName::covering_name,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_folding::GetKnownAttributes() {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_folding::GetKnownAttributes() const {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_database::C_folding::defs_{
        {AttrName::folding_direction,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::folding_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::folding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_laminating::GetKnownAttributes() {
  return {&laminating_sides, &laminating_type};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_laminating::GetKnownAttributes() const {
  return {&laminating_sides, &laminating_type};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_database::C_laminating::defs_{
        {AttrName::laminating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::laminating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_media_size::GetKnownAttributes() {
  return {};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_media_size::GetKnownAttributes() const {
  return {};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_database::C_media_size::defs_{};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_punching::GetKnownAttributes() {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_punching::GetKnownAttributes() const {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_database::C_punching::defs_{
        {AttrName::punching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::punching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::punching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_stitching::GetKnownAttributes() {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_stitching::GetKnownAttributes() const {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_database::C_stitching::defs_{
        {AttrName::stitching_angle,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::stitching_method,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::stitching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_trimming::GetKnownAttributes() {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_database::C_trimming::GetKnownAttributes() const {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_database::C_trimming::defs_{
        {AttrName::trimming_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::trimming_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::trimming_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::trimming_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::GetKnownAttributes() {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::GetKnownAttributes() const {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_default::defs_{
        {AttrName::baling,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_baling(); }}},
        {AttrName::binding,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_binding(); }}},
        {AttrName::coating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_coating(); }}},
        {AttrName::covering,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_covering(); }}},
        {AttrName::finishing_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::folding,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_folding(); }}},
        {AttrName::imposition_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::laminating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_laminating(); }}},
        {AttrName::media_sheets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::punching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_punching(); }}},
        {AttrName::stitching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_stitching(); }}},
        {AttrName::trimming,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_trimming(); }}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_baling::GetKnownAttributes() {
  return {&baling_type, &baling_when};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_baling::GetKnownAttributes() const {
  return {&baling_type, &baling_when};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_default::C_baling::defs_{
        {AttrName::baling_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::baling_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_binding::GetKnownAttributes() {
  return {&binding_reference_edge, &binding_type};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_binding::GetKnownAttributes() const {
  return {&binding_reference_edge, &binding_type};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_default::C_binding::defs_{
        {AttrName::binding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::binding_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_coating::GetKnownAttributes() {
  return {&coating_sides, &coating_type};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_coating::GetKnownAttributes() const {
  return {&coating_sides, &coating_type};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_default::C_coating::defs_{
        {AttrName::coating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::coating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_covering::GetKnownAttributes() {
  return {&covering_name};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_covering::GetKnownAttributes() const {
  return {&covering_name};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_default::C_covering::defs_{
        {AttrName::covering_name,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_folding::GetKnownAttributes() {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_folding::GetKnownAttributes() const {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_default::C_folding::defs_{
        {AttrName::folding_direction,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::folding_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::folding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_laminating::GetKnownAttributes() {
  return {&laminating_sides, &laminating_type};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_laminating::GetKnownAttributes() const {
  return {&laminating_sides, &laminating_type};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_default::C_laminating::defs_{
        {AttrName::laminating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::laminating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_media_size::GetKnownAttributes() {
  return {};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_media_size::GetKnownAttributes() const {
  return {};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_default::C_media_size::defs_{};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_punching::GetKnownAttributes() {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_punching::GetKnownAttributes() const {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_default::C_punching::defs_{
        {AttrName::punching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::punching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::punching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_stitching::GetKnownAttributes() {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_stitching::GetKnownAttributes() const {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_default::C_stitching::defs_{
        {AttrName::stitching_angle,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::stitching_method,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::stitching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_trimming::GetKnownAttributes() {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_default::C_trimming::GetKnownAttributes() const {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_default::C_trimming::defs_{
        {AttrName::trimming_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::trimming_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::trimming_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::trimming_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::GetKnownAttributes() {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::GetKnownAttributes() const {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_ready::defs_{
        {AttrName::baling,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_baling(); }}},
        {AttrName::binding,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_binding(); }}},
        {AttrName::coating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_coating(); }}},
        {AttrName::covering,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_covering(); }}},
        {AttrName::finishing_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::folding,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_folding(); }}},
        {AttrName::imposition_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::laminating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_laminating(); }}},
        {AttrName::media_sheets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::punching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_punching(); }}},
        {AttrName::stitching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_stitching(); }}},
        {AttrName::trimming,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_trimming(); }}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_baling::GetKnownAttributes() {
  return {&baling_type, &baling_when};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_baling::GetKnownAttributes() const {
  return {&baling_type, &baling_when};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_ready::C_baling::defs_{
        {AttrName::baling_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::baling_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_binding::GetKnownAttributes() {
  return {&binding_reference_edge, &binding_type};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_binding::GetKnownAttributes() const {
  return {&binding_reference_edge, &binding_type};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_ready::C_binding::defs_{
        {AttrName::binding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::binding_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_coating::GetKnownAttributes() {
  return {&coating_sides, &coating_type};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_coating::GetKnownAttributes() const {
  return {&coating_sides, &coating_type};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_ready::C_coating::defs_{
        {AttrName::coating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::coating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_covering::GetKnownAttributes() {
  return {&covering_name};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_covering::GetKnownAttributes() const {
  return {&covering_name};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_ready::C_covering::defs_{
        {AttrName::covering_name,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_folding::GetKnownAttributes() {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_folding::GetKnownAttributes() const {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_ready::C_folding::defs_{
        {AttrName::folding_direction,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::folding_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::folding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_laminating::GetKnownAttributes() {
  return {&laminating_sides, &laminating_type};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_laminating::GetKnownAttributes() const {
  return {&laminating_sides, &laminating_type};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_ready::C_laminating::defs_{
        {AttrName::laminating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::laminating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_media_size::GetKnownAttributes() {
  return {};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_media_size::GetKnownAttributes() const {
  return {};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_ready::C_media_size::defs_{};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_punching::GetKnownAttributes() {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_punching::GetKnownAttributes() const {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_ready::C_punching::defs_{
        {AttrName::punching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::punching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::punching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_stitching::GetKnownAttributes() {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_stitching::GetKnownAttributes() const {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_ready::C_stitching::defs_{
        {AttrName::stitching_angle,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::stitching_method,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::stitching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_trimming::GetKnownAttributes() {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_finishings_col_ready::C_trimming::GetKnownAttributes() const {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_finishings_col_ready::C_trimming::defs_{
        {AttrName::trimming_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::trimming_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::trimming_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::trimming_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_insert_sheet_default::GetKnownAttributes() {
  return {&insert_after_page_number, &insert_count, &media, &media_col};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_insert_sheet_default::GetKnownAttributes() const {
  return {&insert_after_page_number, &insert_count, &media, &media_col};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_insert_sheet_default::defs_{
        {AttrName::insert_after_page_number,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::insert_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_accounting_sheets_default::GetKnownAttributes() {
  return {&job_accounting_output_bin, &job_accounting_sheets_type, &media,
          &media_col};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_accounting_sheets_default::GetKnownAttributes() const {
  return {&job_accounting_output_bin, &job_accounting_sheets_type, &media,
          &media_col};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_accounting_sheets_default::defs_{
        {AttrName::job_accounting_output_bin,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_accounting_sheets_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_constraints_supported::GetKnownAttributes() {
  return {&resolver_name};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_constraints_supported::GetKnownAttributes() const {
  return {&resolver_name};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_constraints_supported::defs_{
        {AttrName::resolver_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_cover_back_default::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_cover_back_default::GetKnownAttributes() const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_cover_back_default::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_cover_front_default::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_cover_front_default::GetKnownAttributes() const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_cover_front_default::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_error_sheet_default::GetKnownAttributes() {
  return {&job_error_sheet_type, &job_error_sheet_when, &media, &media_col};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_error_sheet_default::GetKnownAttributes() const {
  return {&job_error_sheet_type, &job_error_sheet_when, &media, &media_col};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_error_sheet_default::defs_{
        {AttrName::job_error_sheet_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_error_sheet_when,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::GetKnownAttributes() {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::GetKnownAttributes() const {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_default::defs_{
        {AttrName::baling,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_baling(); }}},
        {AttrName::binding,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_binding(); }}},
        {AttrName::coating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_coating(); }}},
        {AttrName::covering,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_covering(); }}},
        {AttrName::finishing_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::folding,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_folding(); }}},
        {AttrName::imposition_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::laminating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_laminating(); }}},
        {AttrName::media_sheets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::punching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_punching(); }}},
        {AttrName::stitching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_stitching(); }}},
        {AttrName::trimming,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_trimming(); }}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_baling::GetKnownAttributes() {
  return {&baling_type, &baling_when};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_baling::GetKnownAttributes() const {
  return {&baling_type, &baling_when};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_default::C_baling::defs_{
        {AttrName::baling_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::baling_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_binding::GetKnownAttributes() {
  return {&binding_reference_edge, &binding_type};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_binding::GetKnownAttributes() const {
  return {&binding_reference_edge, &binding_type};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_default::C_binding::defs_{
        {AttrName::binding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::binding_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_coating::GetKnownAttributes() {
  return {&coating_sides, &coating_type};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_coating::GetKnownAttributes() const {
  return {&coating_sides, &coating_type};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_default::C_coating::defs_{
        {AttrName::coating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::coating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_covering::GetKnownAttributes() {
  return {&covering_name};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_covering::GetKnownAttributes() const {
  return {&covering_name};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_default::C_covering::defs_{
        {AttrName::covering_name,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_folding::GetKnownAttributes() {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_folding::GetKnownAttributes() const {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_default::C_folding::defs_{
        {AttrName::folding_direction,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::folding_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::folding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_laminating::GetKnownAttributes() {
  return {&laminating_sides, &laminating_type};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_laminating::GetKnownAttributes() const {
  return {&laminating_sides, &laminating_type};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_default::C_laminating::defs_{
        {AttrName::laminating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::laminating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_media_size::GetKnownAttributes() {
  return {};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_media_size::GetKnownAttributes() const {
  return {};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_default::C_media_size::defs_{};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_punching::GetKnownAttributes() {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_punching::GetKnownAttributes() const {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_default::C_punching::defs_{
        {AttrName::punching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::punching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::punching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_stitching::GetKnownAttributes() {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_stitching::GetKnownAttributes() const {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_default::C_stitching::defs_{
        {AttrName::stitching_angle,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::stitching_method,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::stitching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_trimming::GetKnownAttributes() {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_default::C_trimming::GetKnownAttributes() const {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_default::C_trimming::defs_{
        {AttrName::trimming_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::trimming_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::trimming_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::trimming_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::GetKnownAttributes() {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::GetKnownAttributes() const {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_ready::defs_{
        {AttrName::baling,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_baling(); }}},
        {AttrName::binding,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_binding(); }}},
        {AttrName::coating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_coating(); }}},
        {AttrName::covering,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_covering(); }}},
        {AttrName::finishing_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::folding,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_folding(); }}},
        {AttrName::imposition_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::laminating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_laminating(); }}},
        {AttrName::media_sheets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::punching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_punching(); }}},
        {AttrName::stitching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_stitching(); }}},
        {AttrName::trimming,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_trimming(); }}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_baling::GetKnownAttributes() {
  return {&baling_type, &baling_when};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_baling::GetKnownAttributes() const {
  return {&baling_type, &baling_when};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_ready::C_baling::defs_{
        {AttrName::baling_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::baling_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_binding::GetKnownAttributes() {
  return {&binding_reference_edge, &binding_type};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_binding::GetKnownAttributes() const {
  return {&binding_reference_edge, &binding_type};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_ready::C_binding::defs_{
        {AttrName::binding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::binding_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_coating::GetKnownAttributes() {
  return {&coating_sides, &coating_type};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_coating::GetKnownAttributes() const {
  return {&coating_sides, &coating_type};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_ready::C_coating::defs_{
        {AttrName::coating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::coating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_covering::GetKnownAttributes() {
  return {&covering_name};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_covering::GetKnownAttributes() const {
  return {&covering_name};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_ready::C_covering::defs_{
        {AttrName::covering_name,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_folding::GetKnownAttributes() {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_folding::GetKnownAttributes() const {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_ready::C_folding::defs_{
        {AttrName::folding_direction,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::folding_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::folding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_laminating::GetKnownAttributes() {
  return {&laminating_sides, &laminating_type};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_laminating::GetKnownAttributes() const {
  return {&laminating_sides, &laminating_type};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_ready::C_laminating::defs_{
        {AttrName::laminating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::laminating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_media_size::GetKnownAttributes() {
  return {};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_media_size::GetKnownAttributes() const {
  return {};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_ready::C_media_size::defs_{};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_punching::GetKnownAttributes() {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_punching::GetKnownAttributes() const {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_ready::C_punching::defs_{
        {AttrName::punching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::punching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::punching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_stitching::GetKnownAttributes() {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_stitching::GetKnownAttributes() const {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_ready::C_stitching::defs_{
        {AttrName::stitching_angle,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::stitching_method,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::stitching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_trimming::GetKnownAttributes() {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_finishings_col_ready::C_trimming::GetKnownAttributes() const {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_finishings_col_ready::C_trimming::defs_{
        {AttrName::trimming_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::trimming_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::trimming_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::trimming_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_resolvers_supported::GetKnownAttributes() {
  return {&resolver_name};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_resolvers_supported::GetKnownAttributes() const {
  return {&resolver_name};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_resolvers_supported::defs_{
        {AttrName::resolver_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_sheets_col_default::GetKnownAttributes() {
  return {&job_sheets, &media, &media_col};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_job_sheets_col_default::GetKnownAttributes() const {
  return {&job_sheets, &media, &media_col};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_job_sheets_col_default::defs_{
        {AttrName::job_sheets,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_media_col_database::GetKnownAttributes() {
  return {&media_back_coating,  &media_bottom_margin,
          &media_color,         &media_front_coating,
          &media_grain,         &media_hole_count,
          &media_info,          &media_key,
          &media_left_margin,   &media_order_count,
          &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,
          &media_size_name,     &media_source,
          &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,
          &media_weight_metric, &media_source_properties};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_media_col_database::GetKnownAttributes() const {
  return {&media_back_coating,  &media_bottom_margin,
          &media_color,         &media_front_coating,
          &media_grain,         &media_hole_count,
          &media_info,          &media_key,
          &media_left_margin,   &media_order_count,
          &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,
          &media_size_name,     &media_source,
          &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,
          &media_weight_metric, &media_source_properties};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_media_col_database::defs_{
        {AttrName::media_back_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_bottom_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_color,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_front_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_grain,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_hole_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_info,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::media_key,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_left_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_order_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_pre_printed,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_recycled,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_right_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::media_source,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_thickness,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_tooth,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_top_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_weight_metric,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_source_properties,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_source_properties(); }}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_media_col_database::C_media_size::GetKnownAttributes() {
  return {&x_dimension, &y_dimension};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_media_col_database::C_media_size::GetKnownAttributes() const {
  return {&x_dimension, &y_dimension};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_media_col_database::C_media_size::defs_{
        {AttrName::x_dimension,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_dimension,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_media_col_database::C_media_source_properties::GetKnownAttributes() {
  return {&media_source_feed_direction, &media_source_feed_orientation};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_media_col_database::C_media_source_properties::GetKnownAttributes()
        const {
  return {&media_source_feed_direction, &media_source_feed_orientation};
}
const std::map<AttrName, AttrDef>
    Response_CUPS_Get_Default::G_printer_attributes::C_media_col_database::
        C_media_source_properties::defs_{
            {AttrName::media_source_feed_direction,
             {AttrType::keyword, InternalType::kInteger, false}},
            {AttrName::media_source_feed_orientation,
             {AttrType::enum_, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_media_col_default::GetKnownAttributes() {
  return {&media_back_coating,  &media_bottom_margin, &media_color,
          &media_front_coating, &media_grain,         &media_hole_count,
          &media_info,          &media_key,           &media_left_margin,
          &media_order_count,   &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,          &media_size_name,
          &media_source,        &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,          &media_weight_metric};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_media_col_default::GetKnownAttributes() const {
  return {&media_back_coating,  &media_bottom_margin, &media_color,
          &media_front_coating, &media_grain,         &media_hole_count,
          &media_info,          &media_key,           &media_left_margin,
          &media_order_count,   &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,          &media_size_name,
          &media_source,        &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,          &media_weight_metric};
}
const std::map<AttrName, AttrDef>
    Response_CUPS_Get_Default::G_printer_attributes::C_media_col_default::defs_{
        {AttrName::media_back_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_bottom_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_color,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_front_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_grain,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_hole_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_info,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::media_key,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_left_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_order_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_pre_printed,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_recycled,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_right_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::media_source,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_thickness,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_tooth,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_top_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_weight_metric,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_media_col_default::C_media_size::GetKnownAttributes() {
  return {&x_dimension, &y_dimension};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_media_col_default::C_media_size::GetKnownAttributes() const {
  return {&x_dimension, &y_dimension};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_media_col_default::C_media_size::defs_{
        {AttrName::x_dimension,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_dimension,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_media_col_ready::GetKnownAttributes() {
  return {&media_back_coating,  &media_bottom_margin,
          &media_color,         &media_front_coating,
          &media_grain,         &media_hole_count,
          &media_info,          &media_key,
          &media_left_margin,   &media_order_count,
          &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,
          &media_size_name,     &media_source,
          &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,
          &media_weight_metric, &media_source_properties};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_media_col_ready::GetKnownAttributes() const {
  return {&media_back_coating,  &media_bottom_margin,
          &media_color,         &media_front_coating,
          &media_grain,         &media_hole_count,
          &media_info,          &media_key,
          &media_left_margin,   &media_order_count,
          &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,
          &media_size_name,     &media_source,
          &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,
          &media_weight_metric, &media_source_properties};
}
const std::map<AttrName, AttrDef>
    Response_CUPS_Get_Default::G_printer_attributes::C_media_col_ready::defs_{
        {AttrName::media_back_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_bottom_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_color,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_front_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_grain,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_hole_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_info,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::media_key,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_left_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_order_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_pre_printed,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_recycled,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_right_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::media_source,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_thickness,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_tooth,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_top_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_weight_metric,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_source_properties,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_source_properties(); }}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_media_col_ready::C_media_size::GetKnownAttributes() {
  return {&x_dimension, &y_dimension};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_media_col_ready::C_media_size::GetKnownAttributes() const {
  return {&x_dimension, &y_dimension};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_media_col_ready::C_media_size::defs_{
        {AttrName::x_dimension,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_dimension,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_media_col_ready::C_media_source_properties::GetKnownAttributes() {
  return {&media_source_feed_direction, &media_source_feed_orientation};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_media_col_ready::C_media_source_properties::GetKnownAttributes() const {
  return {&media_source_feed_direction, &media_source_feed_orientation};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_media_col_ready::C_media_source_properties::defs_{
        {AttrName::media_source_feed_direction,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media_source_feed_orientation,
         {AttrType::enum_, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_media_size_supported::GetKnownAttributes() {
  return {&x_dimension, &y_dimension};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_media_size_supported::GetKnownAttributes() const {
  return {&x_dimension, &y_dimension};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_media_size_supported::defs_{
        {AttrName::x_dimension,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::y_dimension,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_pdl_init_file_default::GetKnownAttributes() {
  return {&pdl_init_file_entry, &pdl_init_file_location, &pdl_init_file_name};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_pdl_init_file_default::GetKnownAttributes() const {
  return {&pdl_init_file_entry, &pdl_init_file_location, &pdl_init_file_name};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_pdl_init_file_default::defs_{
        {AttrName::pdl_init_file_entry,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::pdl_init_file_location,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::pdl_init_file_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_printer_contact_col::GetKnownAttributes() {
  return {&contact_name, &contact_uri, &contact_vcard};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_printer_contact_col::GetKnownAttributes() const {
  return {&contact_name, &contact_uri, &contact_vcard};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_printer_contact_col::defs_{
        {AttrName::contact_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::contact_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::contact_vcard,
         {AttrType::text, InternalType::kStringWithLanguage, true}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_printer_icc_profiles::GetKnownAttributes() {
  return {&profile_name, &profile_url};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_printer_icc_profiles::GetKnownAttributes() const {
  return {&profile_name, &profile_url};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_printer_icc_profiles::defs_{
        {AttrName::profile_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::profile_url, {AttrType::uri, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_printer_xri_supported::GetKnownAttributes() {
  return {&xri_authentication, &xri_security, &xri_uri};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_printer_xri_supported::GetKnownAttributes() const {
  return {&xri_authentication, &xri_security, &xri_uri};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_printer_xri_supported::defs_{
        {AttrName::xri_authentication,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::xri_security,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::xri_uri, {AttrType::uri, InternalType::kString, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_proof_print_default::GetKnownAttributes() {
  return {&media, &media_col, &proof_print_copies};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_proof_print_default::GetKnownAttributes() const {
  return {&media, &media_col, &proof_print_copies};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_proof_print_default::defs_{
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}},
        {AttrName::proof_print_copies,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_separator_sheets_default::GetKnownAttributes() {
  return {&media, &media_col, &separator_sheets_type};
}
std::vector<const Attribute*> Response_CUPS_Get_Default::G_printer_attributes::
    C_separator_sheets_default::GetKnownAttributes() const {
  return {&media, &media_col, &separator_sheets_type};
}
const std::map<AttrName, AttrDef> Response_CUPS_Get_Default::
    G_printer_attributes::C_separator_sheets_default::defs_{
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}},
        {AttrName::separator_sheets_type,
         {AttrType::keyword, InternalType::kInteger, true}}};
Request_CUPS_Get_Document::Request_CUPS_Get_Document()
    : Request(Operation::CUPS_Get_Document) {}
std::vector<Group*> Request_CUPS_Get_Document::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Request_CUPS_Get_Document::GetKnownGroups() const {
  return {&operation_attributes};
}
std::vector<Attribute*>
Request_CUPS_Get_Document::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset,
          &attributes_natural_language,
          &printer_uri,
          &job_id,
          &job_uri,
          &document_number};
}
std::vector<const Attribute*>
Request_CUPS_Get_Document::G_operation_attributes::GetKnownAttributes() const {
  return {&attributes_charset,
          &attributes_natural_language,
          &printer_uri,
          &job_id,
          &job_uri,
          &document_number};
}
const std::map<AttrName, AttrDef>
    Request_CUPS_Get_Document::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::printer_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::job_id, {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::document_number,
         {AttrType::integer, InternalType::kInteger, false}}};
Response_CUPS_Get_Document::Response_CUPS_Get_Document()
    : Response(Operation::CUPS_Get_Document) {}
std::vector<Group*> Response_CUPS_Get_Document::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Response_CUPS_Get_Document::GetKnownGroups() const {
  return {&operation_attributes};
}
std::vector<Attribute*>
Response_CUPS_Get_Document::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset, &attributes_natural_language,
          &status_message,     &detailed_status_message,
          &document_format,    &document_number,
          &document_name};
}
std::vector<const Attribute*>
Response_CUPS_Get_Document::G_operation_attributes::GetKnownAttributes() const {
  return {&attributes_charset, &attributes_natural_language,
          &status_message,     &detailed_status_message,
          &document_format,    &document_number,
          &document_name};
}
const std::map<AttrName, AttrDef>
    Response_CUPS_Get_Document::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::status_message,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::detailed_status_message,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::document_format,
         {AttrType::mimeMediaType, InternalType::kString, false}},
        {AttrName::document_number,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::document_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}}};
Request_CUPS_Get_Printers::Request_CUPS_Get_Printers()
    : Request(Operation::CUPS_Get_Printers) {}
std::vector<Group*> Request_CUPS_Get_Printers::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Request_CUPS_Get_Printers::GetKnownGroups() const {
  return {&operation_attributes};
}
std::vector<Attribute*>
Request_CUPS_Get_Printers::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset,   &attributes_natural_language,
          &first_printer_name,   &limit,
          &printer_id,           &printer_location,
          &printer_type,         &printer_type_mask,
          &requested_attributes, &requested_user_name};
}
std::vector<const Attribute*>
Request_CUPS_Get_Printers::G_operation_attributes::GetKnownAttributes() const {
  return {&attributes_charset,   &attributes_natural_language,
          &first_printer_name,   &limit,
          &printer_id,           &printer_location,
          &printer_type,         &printer_type_mask,
          &requested_attributes, &requested_user_name};
}
const std::map<AttrName, AttrDef>
    Request_CUPS_Get_Printers::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::first_printer_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::limit, {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::printer_id,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::printer_location,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_type,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::printer_type_mask,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::requested_attributes,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::requested_user_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}}};
Response_CUPS_Get_Printers::Response_CUPS_Get_Printers()
    : Response(Operation::CUPS_Get_Printers) {}
std::vector<Group*> Response_CUPS_Get_Printers::GetKnownGroups() {
  return {&operation_attributes, &printer_attributes};
}
std::vector<const Group*> Response_CUPS_Get_Printers::GetKnownGroups() const {
  return {&operation_attributes, &printer_attributes};
}
Request_CUPS_Move_Job::Request_CUPS_Move_Job()
    : Request(Operation::CUPS_Move_Job) {}
std::vector<Group*> Request_CUPS_Move_Job::GetKnownGroups() {
  return {&operation_attributes, &job_attributes};
}
std::vector<const Group*> Request_CUPS_Move_Job::GetKnownGroups() const {
  return {&operation_attributes, &job_attributes};
}
std::vector<Attribute*>
Request_CUPS_Move_Job::G_job_attributes::GetKnownAttributes() {
  return {&job_printer_uri};
}
std::vector<const Attribute*>
Request_CUPS_Move_Job::G_job_attributes::GetKnownAttributes() const {
  return {&job_printer_uri};
}
const std::map<AttrName, AttrDef>
    Request_CUPS_Move_Job::G_job_attributes::defs_{
        {AttrName::job_printer_uri,
         {AttrType::uri, InternalType::kString, false}}};
Response_CUPS_Move_Job::Response_CUPS_Move_Job()
    : Response(Operation::CUPS_Move_Job) {}
std::vector<Group*> Response_CUPS_Move_Job::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Response_CUPS_Move_Job::GetKnownGroups() const {
  return {&operation_attributes};
}
Request_CUPS_Set_Default::Request_CUPS_Set_Default()
    : Request(Operation::CUPS_Set_Default) {}
std::vector<Group*> Request_CUPS_Set_Default::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Request_CUPS_Set_Default::GetKnownGroups() const {
  return {&operation_attributes};
}
Response_CUPS_Set_Default::Response_CUPS_Set_Default()
    : Response(Operation::CUPS_Set_Default) {}
std::vector<Group*> Response_CUPS_Set_Default::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Response_CUPS_Set_Default::GetKnownGroups() const {
  return {&operation_attributes};
}
Request_Cancel_Job::Request_Cancel_Job() : Request(Operation::Cancel_Job) {}
std::vector<Group*> Request_Cancel_Job::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Request_Cancel_Job::GetKnownGroups() const {
  return {&operation_attributes};
}
std::vector<Attribute*>
Request_Cancel_Job::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset,
          &attributes_natural_language,
          &printer_uri,
          &job_id,
          &job_uri,
          &requesting_user_name,
          &message};
}
std::vector<const Attribute*>
Request_Cancel_Job::G_operation_attributes::GetKnownAttributes() const {
  return {&attributes_charset,
          &attributes_natural_language,
          &printer_uri,
          &job_id,
          &job_uri,
          &requesting_user_name,
          &message};
}
const std::map<AttrName, AttrDef>
    Request_Cancel_Job::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::printer_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::job_id, {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::requesting_user_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::message,
         {AttrType::text, InternalType::kStringWithLanguage, false}}};
Response_Cancel_Job::Response_Cancel_Job() : Response(Operation::Cancel_Job) {}
std::vector<Group*> Response_Cancel_Job::GetKnownGroups() {
  return {&operation_attributes, &unsupported_attributes};
}
std::vector<const Group*> Response_Cancel_Job::GetKnownGroups() const {
  return {&operation_attributes, &unsupported_attributes};
}
Request_Create_Job::Request_Create_Job() : Request(Operation::Create_Job) {}
std::vector<Group*> Request_Create_Job::GetKnownGroups() {
  return {&operation_attributes, &job_attributes};
}
std::vector<const Group*> Request_Create_Job::GetKnownGroups() const {
  return {&operation_attributes, &job_attributes};
}
std::vector<Attribute*>
Request_Create_Job::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset, &attributes_natural_language,
          &printer_uri,        &requesting_user_name,
          &job_name,           &ipp_attribute_fidelity,
          &job_k_octets,       &job_impressions,
          &job_media_sheets};
}
std::vector<const Attribute*>
Request_Create_Job::G_operation_attributes::GetKnownAttributes() const {
  return {&attributes_charset, &attributes_natural_language,
          &printer_uri,        &requesting_user_name,
          &job_name,           &ipp_attribute_fidelity,
          &job_k_octets,       &job_impressions,
          &job_media_sheets};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::printer_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::requesting_user_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::ipp_attribute_fidelity,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_k_octets,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_impressions,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_media_sheets,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*>
Request_Create_Job::G_job_attributes::GetKnownAttributes() {
  return {&copies,
          &cover_back,
          &cover_front,
          &feed_orientation,
          &finishings,
          &finishings_col,
          &font_name_requested,
          &font_size_requested,
          &force_front_side,
          &imposition_template,
          &insert_sheet,
          &job_account_id,
          &job_account_type,
          &job_accounting_sheets,
          &job_accounting_user_id,
          &job_copies,
          &job_cover_back,
          &job_cover_front,
          &job_delay_output_until,
          &job_delay_output_until_time,
          &job_error_action,
          &job_error_sheet,
          &job_finishings,
          &job_finishings_col,
          &job_hold_until,
          &job_hold_until_time,
          &job_message_to_operator,
          &job_pages_per_set,
          &job_phone_number,
          &job_priority,
          &job_recipient_name,
          &job_save_disposition,
          &job_sheet_message,
          &job_sheets,
          &job_sheets_col,
          &media,
          &media_col,
          &media_input_tray_check,
          &multiple_document_handling,
          &number_up,
          &orientation_requested,
          &output_bin,
          &output_device,
          &overrides,
          &page_delivery,
          &page_order_received,
          &page_ranges,
          &pages_per_subset,
          &pdl_init_file,
          &presentation_direction_number_up,
          &print_color_mode,
          &print_content_optimize,
          &print_quality,
          &print_rendering_intent,
          &print_scaling,
          &printer_resolution,
          &proof_print,
          &separator_sheets,
          &sheet_collate,
          &sides,
          &x_image_position,
          &x_image_shift,
          &x_side1_image_shift,
          &x_side2_image_shift,
          &y_image_position,
          &y_image_shift,
          &y_side1_image_shift,
          &y_side2_image_shift};
}
std::vector<const Attribute*>
Request_Create_Job::G_job_attributes::GetKnownAttributes() const {
  return {&copies,
          &cover_back,
          &cover_front,
          &feed_orientation,
          &finishings,
          &finishings_col,
          &font_name_requested,
          &font_size_requested,
          &force_front_side,
          &imposition_template,
          &insert_sheet,
          &job_account_id,
          &job_account_type,
          &job_accounting_sheets,
          &job_accounting_user_id,
          &job_copies,
          &job_cover_back,
          &job_cover_front,
          &job_delay_output_until,
          &job_delay_output_until_time,
          &job_error_action,
          &job_error_sheet,
          &job_finishings,
          &job_finishings_col,
          &job_hold_until,
          &job_hold_until_time,
          &job_message_to_operator,
          &job_pages_per_set,
          &job_phone_number,
          &job_priority,
          &job_recipient_name,
          &job_save_disposition,
          &job_sheet_message,
          &job_sheets,
          &job_sheets_col,
          &media,
          &media_col,
          &media_input_tray_check,
          &multiple_document_handling,
          &number_up,
          &orientation_requested,
          &output_bin,
          &output_device,
          &overrides,
          &page_delivery,
          &page_order_received,
          &page_ranges,
          &pages_per_subset,
          &pdl_init_file,
          &presentation_direction_number_up,
          &print_color_mode,
          &print_content_optimize,
          &print_quality,
          &print_rendering_intent,
          &print_scaling,
          &printer_resolution,
          &proof_print,
          &separator_sheets,
          &sheet_collate,
          &sides,
          &x_image_position,
          &x_image_shift,
          &x_side1_image_shift,
          &x_side2_image_shift,
          &y_image_position,
          &y_image_shift,
          &y_side1_image_shift,
          &y_side2_image_shift};
}
const std::map<AttrName, AttrDef> Request_Create_Job::G_job_attributes::defs_{
    {AttrName::copies, {AttrType::integer, InternalType::kInteger, false}},
    {AttrName::cover_back,
     {AttrType::collection, InternalType::kCollection, false,
      []() -> Collection* { return new C_cover_back(); }}},
    {AttrName::cover_front,
     {AttrType::collection, InternalType::kCollection, false,
      []() -> Collection* { return new C_cover_front(); }}},
    {AttrName::feed_orientation,
     {AttrType::keyword, InternalType::kInteger, false}},
    {AttrName::finishings, {AttrType::enum_, InternalType::kInteger, true}},
    {AttrName::finishings_col,
     {AttrType::collection, InternalType::kCollection, true,
      []() -> Collection* { return new C_finishings_col(); }}},
    {AttrName::font_name_requested,
     {AttrType::name, InternalType::kStringWithLanguage, false}},
    {AttrName::font_size_requested,
     {AttrType::integer, InternalType::kInteger, false}},
    {AttrName::force_front_side,
     {AttrType::integer, InternalType::kInteger, true}},
    {AttrName::imposition_template,
     {AttrType::keyword, InternalType::kString, false}},
    {AttrName::insert_sheet,
     {AttrType::collection, InternalType::kCollection, true,
      []() -> Collection* { return new C_insert_sheet(); }}},
    {AttrName::job_account_id,
     {AttrType::name, InternalType::kStringWithLanguage, false}},
    {AttrName::job_account_type,
     {AttrType::keyword, InternalType::kString, false}},
    {AttrName::job_accounting_sheets,
     {AttrType::collection, InternalType::kCollection, false,
      []() -> Collection* { return new C_job_accounting_sheets(); }}},
    {AttrName::job_accounting_user_id,
     {AttrType::name, InternalType::kStringWithLanguage, false}},
    {AttrName::job_copies, {AttrType::integer, InternalType::kInteger, false}},
    {AttrName::job_cover_back,
     {AttrType::collection, InternalType::kCollection, false,
      []() -> Collection* { return new C_job_cover_back(); }}},
    {AttrName::job_cover_front,
     {AttrType::collection, InternalType::kCollection, false,
      []() -> Collection* { return new C_job_cover_front(); }}},
    {AttrName::job_delay_output_until,
     {AttrType::keyword, InternalType::kString, false}},
    {AttrName::job_delay_output_until_time,
     {AttrType::dateTime, InternalType::kDateTime, false}},
    {AttrName::job_error_action,
     {AttrType::keyword, InternalType::kInteger, false}},
    {AttrName::job_error_sheet,
     {AttrType::collection, InternalType::kCollection, false,
      []() -> Collection* { return new C_job_error_sheet(); }}},
    {AttrName::job_finishings, {AttrType::enum_, InternalType::kInteger, true}},
    {AttrName::job_finishings_col,
     {AttrType::collection, InternalType::kCollection, true,
      []() -> Collection* { return new C_job_finishings_col(); }}},
    {AttrName::job_hold_until,
     {AttrType::keyword, InternalType::kString, false}},
    {AttrName::job_hold_until_time,
     {AttrType::dateTime, InternalType::kDateTime, false}},
    {AttrName::job_message_to_operator,
     {AttrType::text, InternalType::kStringWithLanguage, false}},
    {AttrName::job_pages_per_set,
     {AttrType::integer, InternalType::kInteger, false}},
    {AttrName::job_phone_number, {AttrType::uri, InternalType::kString, false}},
    {AttrName::job_priority,
     {AttrType::integer, InternalType::kInteger, false}},
    {AttrName::job_recipient_name,
     {AttrType::name, InternalType::kStringWithLanguage, false}},
    {AttrName::job_save_disposition,
     {AttrType::collection, InternalType::kCollection, false,
      []() -> Collection* { return new C_job_save_disposition(); }}},
    {AttrName::job_sheet_message,
     {AttrType::text, InternalType::kStringWithLanguage, false}},
    {AttrName::job_sheets, {AttrType::keyword, InternalType::kString, false}},
    {AttrName::job_sheets_col,
     {AttrType::collection, InternalType::kCollection, false,
      []() -> Collection* { return new C_job_sheets_col(); }}},
    {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
    {AttrName::media_col,
     {AttrType::collection, InternalType::kCollection, false,
      []() -> Collection* { return new C_media_col(); }}},
    {AttrName::media_input_tray_check,
     {AttrType::keyword, InternalType::kString, false}},
    {AttrName::multiple_document_handling,
     {AttrType::keyword, InternalType::kInteger, false}},
    {AttrName::number_up, {AttrType::integer, InternalType::kInteger, false}},
    {AttrName::orientation_requested,
     {AttrType::enum_, InternalType::kInteger, false}},
    {AttrName::output_bin, {AttrType::keyword, InternalType::kString, false}},
    {AttrName::output_device,
     {AttrType::name, InternalType::kStringWithLanguage, false}},
    {AttrName::overrides,
     {AttrType::collection, InternalType::kCollection, true,
      []() -> Collection* { return new C_overrides(); }}},
    {AttrName::page_delivery,
     {AttrType::keyword, InternalType::kInteger, false}},
    {AttrName::page_order_received,
     {AttrType::keyword, InternalType::kInteger, false}},
    {AttrName::page_ranges,
     {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
    {AttrName::pages_per_subset,
     {AttrType::integer, InternalType::kInteger, true}},
    {AttrName::pdl_init_file,
     {AttrType::collection, InternalType::kCollection, true,
      []() -> Collection* { return new C_pdl_init_file(); }}},
    {AttrName::presentation_direction_number_up,
     {AttrType::keyword, InternalType::kInteger, false}},
    {AttrName::print_color_mode,
     {AttrType::keyword, InternalType::kInteger, false}},
    {AttrName::print_content_optimize,
     {AttrType::keyword, InternalType::kInteger, false}},
    {AttrName::print_quality, {AttrType::enum_, InternalType::kInteger, false}},
    {AttrName::print_rendering_intent,
     {AttrType::keyword, InternalType::kInteger, false}},
    {AttrName::print_scaling,
     {AttrType::keyword, InternalType::kInteger, false}},
    {AttrName::printer_resolution,
     {AttrType::resolution, InternalType::kResolution, false}},
    {AttrName::proof_print,
     {AttrType::collection, InternalType::kCollection, false,
      []() -> Collection* { return new C_proof_print(); }}},
    {AttrName::separator_sheets,
     {AttrType::collection, InternalType::kCollection, false,
      []() -> Collection* { return new C_separator_sheets(); }}},
    {AttrName::sheet_collate,
     {AttrType::keyword, InternalType::kInteger, false}},
    {AttrName::sides, {AttrType::keyword, InternalType::kInteger, false}},
    {AttrName::x_image_position,
     {AttrType::keyword, InternalType::kInteger, false}},
    {AttrName::x_image_shift,
     {AttrType::integer, InternalType::kInteger, false}},
    {AttrName::x_side1_image_shift,
     {AttrType::integer, InternalType::kInteger, false}},
    {AttrName::x_side2_image_shift,
     {AttrType::integer, InternalType::kInteger, false}},
    {AttrName::y_image_position,
     {AttrType::keyword, InternalType::kInteger, false}},
    {AttrName::y_image_shift,
     {AttrType::integer, InternalType::kInteger, false}},
    {AttrName::y_side1_image_shift,
     {AttrType::integer, InternalType::kInteger, false}},
    {AttrName::y_side2_image_shift,
     {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*>
Request_Create_Job::G_job_attributes::C_cover_back::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*>
Request_Create_Job::G_job_attributes::C_cover_back::GetKnownAttributes() const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_cover_back::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*>
Request_Create_Job::G_job_attributes::C_cover_front::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*>
Request_Create_Job::G_job_attributes::C_cover_front::GetKnownAttributes()
    const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_cover_front::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*>
Request_Create_Job::G_job_attributes::C_finishings_col::GetKnownAttributes() {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
std::vector<const Attribute*>
Request_Create_Job::G_job_attributes::C_finishings_col::GetKnownAttributes()
    const {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_finishings_col::defs_{
        {AttrName::baling,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_baling(); }}},
        {AttrName::binding,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_binding(); }}},
        {AttrName::coating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_coating(); }}},
        {AttrName::covering,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_covering(); }}},
        {AttrName::finishing_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::folding,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_folding(); }}},
        {AttrName::imposition_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::laminating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_laminating(); }}},
        {AttrName::media_sheets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::punching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_punching(); }}},
        {AttrName::stitching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_stitching(); }}},
        {AttrName::trimming,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_trimming(); }}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::C_finishings_col::
    C_baling::GetKnownAttributes() {
  return {&baling_type, &baling_when};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_finishings_col::C_baling::GetKnownAttributes() const {
  return {&baling_type, &baling_when};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_finishings_col::C_baling::defs_{
        {AttrName::baling_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::baling_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::C_finishings_col::
    C_binding::GetKnownAttributes() {
  return {&binding_reference_edge, &binding_type};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_finishings_col::C_binding::GetKnownAttributes() const {
  return {&binding_reference_edge, &binding_type};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_finishings_col::C_binding::defs_{
        {AttrName::binding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::binding_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::C_finishings_col::
    C_coating::GetKnownAttributes() {
  return {&coating_sides, &coating_type};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_finishings_col::C_coating::GetKnownAttributes() const {
  return {&coating_sides, &coating_type};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_finishings_col::C_coating::defs_{
        {AttrName::coating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::coating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::C_finishings_col::
    C_covering::GetKnownAttributes() {
  return {&covering_name};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_finishings_col::C_covering::GetKnownAttributes() const {
  return {&covering_name};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_finishings_col::C_covering::defs_{
        {AttrName::covering_name,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::C_finishings_col::
    C_folding::GetKnownAttributes() {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_finishings_col::C_folding::GetKnownAttributes() const {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_finishings_col::C_folding::defs_{
        {AttrName::folding_direction,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::folding_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::folding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::C_finishings_col::
    C_laminating::GetKnownAttributes() {
  return {&laminating_sides, &laminating_type};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_finishings_col::C_laminating::GetKnownAttributes() const {
  return {&laminating_sides, &laminating_type};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_finishings_col::C_laminating::defs_{
        {AttrName::laminating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::laminating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::C_finishings_col::
    C_media_size::GetKnownAttributes() {
  return {};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_finishings_col::C_media_size::GetKnownAttributes() const {
  return {};
}
const std::map<AttrName, AttrDef> Request_Create_Job::G_job_attributes::
    C_finishings_col::C_media_size::defs_{};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::C_finishings_col::
    C_punching::GetKnownAttributes() {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_finishings_col::C_punching::GetKnownAttributes() const {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_finishings_col::C_punching::defs_{
        {AttrName::punching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::punching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::punching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::C_finishings_col::
    C_stitching::GetKnownAttributes() {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_finishings_col::C_stitching::GetKnownAttributes() const {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_finishings_col::C_stitching::defs_{
        {AttrName::stitching_angle,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::stitching_method,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::stitching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::C_finishings_col::
    C_trimming::GetKnownAttributes() {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_finishings_col::C_trimming::GetKnownAttributes() const {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_finishings_col::C_trimming::defs_{
        {AttrName::trimming_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::trimming_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::trimming_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::trimming_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*>
Request_Create_Job::G_job_attributes::C_insert_sheet::GetKnownAttributes() {
  return {&insert_after_page_number, &insert_count, &media, &media_col};
}
std::vector<const Attribute*>
Request_Create_Job::G_job_attributes::C_insert_sheet::GetKnownAttributes()
    const {
  return {&insert_after_page_number, &insert_count, &media, &media_col};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_insert_sheet::defs_{
        {AttrName::insert_after_page_number,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::insert_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::
    C_job_accounting_sheets::GetKnownAttributes() {
  return {&job_accounting_output_bin, &job_accounting_sheets_type, &media,
          &media_col};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_job_accounting_sheets::GetKnownAttributes() const {
  return {&job_accounting_output_bin, &job_accounting_sheets_type, &media,
          &media_col};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_job_accounting_sheets::defs_{
        {AttrName::job_accounting_output_bin,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_accounting_sheets_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*>
Request_Create_Job::G_job_attributes::C_job_cover_back::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*>
Request_Create_Job::G_job_attributes::C_job_cover_back::GetKnownAttributes()
    const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_job_cover_back::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*>
Request_Create_Job::G_job_attributes::C_job_cover_front::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*>
Request_Create_Job::G_job_attributes::C_job_cover_front::GetKnownAttributes()
    const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_job_cover_front::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*>
Request_Create_Job::G_job_attributes::C_job_error_sheet::GetKnownAttributes() {
  return {&job_error_sheet_type, &job_error_sheet_when, &media, &media_col};
}
std::vector<const Attribute*>
Request_Create_Job::G_job_attributes::C_job_error_sheet::GetKnownAttributes()
    const {
  return {&job_error_sheet_type, &job_error_sheet_when, &media, &media_col};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_job_error_sheet::defs_{
        {AttrName::job_error_sheet_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_error_sheet_when,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::GetKnownAttributes() {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
std::vector<const Attribute*>
Request_Create_Job::G_job_attributes::C_job_finishings_col::GetKnownAttributes()
    const {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_job_finishings_col::defs_{
        {AttrName::baling,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_baling(); }}},
        {AttrName::binding,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_binding(); }}},
        {AttrName::coating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_coating(); }}},
        {AttrName::covering,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_covering(); }}},
        {AttrName::finishing_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::folding,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_folding(); }}},
        {AttrName::imposition_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::laminating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_laminating(); }}},
        {AttrName::media_sheets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::punching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_punching(); }}},
        {AttrName::stitching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_stitching(); }}},
        {AttrName::trimming,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_trimming(); }}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_baling::GetKnownAttributes() {
  return {&baling_type, &baling_when};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_baling::GetKnownAttributes() const {
  return {&baling_type, &baling_when};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_job_finishings_col::C_baling::defs_{
        {AttrName::baling_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::baling_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_binding::GetKnownAttributes() {
  return {&binding_reference_edge, &binding_type};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_binding::GetKnownAttributes() const {
  return {&binding_reference_edge, &binding_type};
}
const std::map<AttrName, AttrDef> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_binding::defs_{
        {AttrName::binding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::binding_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_coating::GetKnownAttributes() {
  return {&coating_sides, &coating_type};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_coating::GetKnownAttributes() const {
  return {&coating_sides, &coating_type};
}
const std::map<AttrName, AttrDef> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_coating::defs_{
        {AttrName::coating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::coating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_covering::GetKnownAttributes() {
  return {&covering_name};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_covering::GetKnownAttributes() const {
  return {&covering_name};
}
const std::map<AttrName, AttrDef> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_covering::defs_{
        {AttrName::covering_name,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_folding::GetKnownAttributes() {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_folding::GetKnownAttributes() const {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
const std::map<AttrName, AttrDef> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_folding::defs_{
        {AttrName::folding_direction,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::folding_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::folding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_laminating::GetKnownAttributes() {
  return {&laminating_sides, &laminating_type};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_laminating::GetKnownAttributes() const {
  return {&laminating_sides, &laminating_type};
}
const std::map<AttrName, AttrDef> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_laminating::defs_{
        {AttrName::laminating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::laminating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_media_size::GetKnownAttributes() {
  return {};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_media_size::GetKnownAttributes() const {
  return {};
}
const std::map<AttrName, AttrDef> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_media_size::defs_{};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_punching::GetKnownAttributes() {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_punching::GetKnownAttributes() const {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
const std::map<AttrName, AttrDef> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_punching::defs_{
        {AttrName::punching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::punching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::punching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_stitching::GetKnownAttributes() {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_stitching::GetKnownAttributes() const {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
const std::map<AttrName, AttrDef> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_stitching::defs_{
        {AttrName::stitching_angle,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::stitching_method,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::stitching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_trimming::GetKnownAttributes() {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_trimming::GetKnownAttributes() const {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
const std::map<AttrName, AttrDef> Request_Create_Job::G_job_attributes::
    C_job_finishings_col::C_trimming::defs_{
        {AttrName::trimming_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::trimming_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::trimming_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::trimming_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::
    C_job_save_disposition::GetKnownAttributes() {
  return {&save_disposition, &save_info};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_job_save_disposition::GetKnownAttributes() const {
  return {&save_disposition, &save_info};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_job_save_disposition::defs_{
        {AttrName::save_disposition,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::save_info,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_save_info(); }}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::
    C_job_save_disposition::C_save_info::GetKnownAttributes() {
  return {&save_document_format, &save_location, &save_name};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_job_save_disposition::C_save_info::GetKnownAttributes() const {
  return {&save_document_format, &save_location, &save_name};
}
const std::map<AttrName, AttrDef> Request_Create_Job::G_job_attributes::
    C_job_save_disposition::C_save_info::defs_{
        {AttrName::save_document_format,
         {AttrType::mimeMediaType, InternalType::kString, false}},
        {AttrName::save_location,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::save_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}}};
std::vector<Attribute*>
Request_Create_Job::G_job_attributes::C_job_sheets_col::GetKnownAttributes() {
  return {&job_sheets, &media, &media_col};
}
std::vector<const Attribute*>
Request_Create_Job::G_job_attributes::C_job_sheets_col::GetKnownAttributes()
    const {
  return {&job_sheets, &media, &media_col};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_job_sheets_col::defs_{
        {AttrName::job_sheets,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*>
Request_Create_Job::G_job_attributes::C_media_col::GetKnownAttributes() {
  return {&media_back_coating,  &media_bottom_margin, &media_color,
          &media_front_coating, &media_grain,         &media_hole_count,
          &media_info,          &media_key,           &media_left_margin,
          &media_order_count,   &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,          &media_size_name,
          &media_source,        &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,          &media_weight_metric};
}
std::vector<const Attribute*>
Request_Create_Job::G_job_attributes::C_media_col::GetKnownAttributes() const {
  return {&media_back_coating,  &media_bottom_margin, &media_color,
          &media_front_coating, &media_grain,         &media_hole_count,
          &media_info,          &media_key,           &media_left_margin,
          &media_order_count,   &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,          &media_size_name,
          &media_source,        &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,          &media_weight_metric};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_media_col::defs_{
        {AttrName::media_back_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_bottom_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_color,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_front_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_grain,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_hole_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_info,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::media_key,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_left_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_order_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_pre_printed,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_recycled,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_right_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::media_source,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_thickness,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_tooth,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_top_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_weight_metric,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*> Request_Create_Job::G_job_attributes::C_media_col::
    C_media_size::GetKnownAttributes() {
  return {&x_dimension, &y_dimension};
}
std::vector<const Attribute*> Request_Create_Job::G_job_attributes::
    C_media_col::C_media_size::GetKnownAttributes() const {
  return {&x_dimension, &y_dimension};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_media_col::C_media_size::defs_{
        {AttrName::x_dimension,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_dimension,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*>
Request_Create_Job::G_job_attributes::C_overrides::GetKnownAttributes() {
  return {&job_account_id,
          &job_account_type,
          &job_accounting_sheets,
          &job_accounting_user_id,
          &job_copies,
          &job_cover_back,
          &job_cover_front,
          &job_delay_output_until,
          &job_delay_output_until_time,
          &job_error_action,
          &job_error_sheet,
          &job_finishings,
          &job_finishings_col,
          &job_hold_until,
          &job_hold_until_time,
          &job_message_to_operator,
          &job_pages_per_set,
          &job_phone_number,
          &job_priority,
          &job_recipient_name,
          &job_save_disposition,
          &job_sheet_message,
          &job_sheets,
          &job_sheets_col,
          &pages_per_subset,
          &output_bin,
          &output_device,
          &multiple_document_handling,
          &y_side1_image_shift,
          &y_side2_image_shift,
          &number_up,
          &orientation_requested,
          &page_delivery,
          &page_order_received,
          &page_ranges,
          &pdl_init_file,
          &print_color_mode,
          &print_content_optimize,
          &print_quality,
          &print_rendering_intent,
          &printer_resolution,
          &presentation_direction_number_up,
          &media,
          &sides,
          &x_image_position,
          &x_image_shift,
          &x_side1_image_shift,
          &x_side2_image_shift,
          &y_image_position,
          &y_image_shift,
          &copies,
          &cover_back,
          &cover_front,
          &imposition_template,
          &insert_sheet,
          &media_col,
          &media_input_tray_check,
          &print_scaling,
          &proof_print,
          &separator_sheets,
          &sheet_collate,
          &feed_orientation,
          &finishings,
          &finishings_col,
          &font_name_requested,
          &font_size_requested,
          &force_front_side,
          &document_copies,
          &document_numbers,
          &pages};
}
std::vector<const Attribute*>
Request_Create_Job::G_job_attributes::C_overrides::GetKnownAttributes() const {
  return {&job_account_id,
          &job_account_type,
          &job_accounting_sheets,
          &job_accounting_user_id,
          &job_copies,
          &job_cover_back,
          &job_cover_front,
          &job_delay_output_until,
          &job_delay_output_until_time,
          &job_error_action,
          &job_error_sheet,
          &job_finishings,
          &job_finishings_col,
          &job_hold_until,
          &job_hold_until_time,
          &job_message_to_operator,
          &job_pages_per_set,
          &job_phone_number,
          &job_priority,
          &job_recipient_name,
          &job_save_disposition,
          &job_sheet_message,
          &job_sheets,
          &job_sheets_col,
          &pages_per_subset,
          &output_bin,
          &output_device,
          &multiple_document_handling,
          &y_side1_image_shift,
          &y_side2_image_shift,
          &number_up,
          &orientation_requested,
          &page_delivery,
          &page_order_received,
          &page_ranges,
          &pdl_init_file,
          &print_color_mode,
          &print_content_optimize,
          &print_quality,
          &print_rendering_intent,
          &printer_resolution,
          &presentation_direction_number_up,
          &media,
          &sides,
          &x_image_position,
          &x_image_shift,
          &x_side1_image_shift,
          &x_side2_image_shift,
          &y_image_position,
          &y_image_shift,
          &copies,
          &cover_back,
          &cover_front,
          &imposition_template,
          &insert_sheet,
          &media_col,
          &media_input_tray_check,
          &print_scaling,
          &proof_print,
          &separator_sheets,
          &sheet_collate,
          &feed_orientation,
          &finishings,
          &finishings_col,
          &font_name_requested,
          &font_size_requested,
          &force_front_side,
          &document_copies,
          &document_numbers,
          &pages};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_overrides::defs_{
        {AttrName::job_account_id,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_account_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_accounting_sheets,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_accounting_sheets(); }}},
        {AttrName::job_accounting_user_id,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_copies,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_cover_back,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_cover_back(); }}},
        {AttrName::job_cover_front,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_cover_front(); }}},
        {AttrName::job_delay_output_until,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_delay_output_until_time,
         {AttrType::dateTime, InternalType::kDateTime, false}},
        {AttrName::job_error_action,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::job_error_sheet,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_error_sheet(); }}},
        {AttrName::job_finishings,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::job_finishings_col,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_job_finishings_col(); }}},
        {AttrName::job_hold_until,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_hold_until_time,
         {AttrType::dateTime, InternalType::kDateTime, false}},
        {AttrName::job_message_to_operator,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::job_pages_per_set,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_phone_number,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::job_priority,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_recipient_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_save_disposition,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_save_disposition(); }}},
        {AttrName::job_sheet_message,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::job_sheets,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_sheets_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_sheets_col(); }}},
        {AttrName::pages_per_subset,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::output_bin,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::output_device,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::multiple_document_handling,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::y_side1_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_side2_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::number_up,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::orientation_requested,
         {AttrType::enum_, InternalType::kInteger, false}},
        {AttrName::page_delivery,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::page_order_received,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::page_ranges,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::pdl_init_file,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_pdl_init_file(); }}},
        {AttrName::print_color_mode,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::print_content_optimize,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::print_quality,
         {AttrType::enum_, InternalType::kInteger, false}},
        {AttrName::print_rendering_intent,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::printer_resolution,
         {AttrType::resolution, InternalType::kResolution, false}},
        {AttrName::presentation_direction_number_up,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::sides, {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::x_image_position,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::x_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::x_side1_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::x_side2_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_image_position,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::y_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::copies, {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::cover_back,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_cover_back(); }}},
        {AttrName::cover_front,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_cover_front(); }}},
        {AttrName::imposition_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::insert_sheet,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_insert_sheet(); }}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}},
        {AttrName::media_input_tray_check,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::print_scaling,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::proof_print,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_proof_print(); }}},
        {AttrName::separator_sheets,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_separator_sheets(); }}},
        {AttrName::sheet_collate,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::feed_orientation,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::finishings, {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::finishings_col,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_finishings_col(); }}},
        {AttrName::font_name_requested,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::font_size_requested,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::force_front_side,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::document_copies,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::document_numbers,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::pages,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}}};
std::vector<Attribute*>
Request_Create_Job::G_job_attributes::C_pdl_init_file::GetKnownAttributes() {
  return {&pdl_init_file_entry, &pdl_init_file_location, &pdl_init_file_name};
}
std::vector<const Attribute*>
Request_Create_Job::G_job_attributes::C_pdl_init_file::GetKnownAttributes()
    const {
  return {&pdl_init_file_entry, &pdl_init_file_location, &pdl_init_file_name};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_pdl_init_file::defs_{
        {AttrName::pdl_init_file_entry,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::pdl_init_file_location,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::pdl_init_file_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}}};
std::vector<Attribute*>
Request_Create_Job::G_job_attributes::C_proof_print::GetKnownAttributes() {
  return {&media, &media_col, &proof_print_copies};
}
std::vector<const Attribute*>
Request_Create_Job::G_job_attributes::C_proof_print::GetKnownAttributes()
    const {
  return {&media, &media_col, &proof_print_copies};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_proof_print::defs_{
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}},
        {AttrName::proof_print_copies,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*>
Request_Create_Job::G_job_attributes::C_separator_sheets::GetKnownAttributes() {
  return {&media, &media_col, &separator_sheets_type};
}
std::vector<const Attribute*>
Request_Create_Job::G_job_attributes::C_separator_sheets::GetKnownAttributes()
    const {
  return {&media, &media_col, &separator_sheets_type};
}
const std::map<AttrName, AttrDef>
    Request_Create_Job::G_job_attributes::C_separator_sheets::defs_{
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}},
        {AttrName::separator_sheets_type,
         {AttrType::keyword, InternalType::kInteger, true}}};
Response_Create_Job::Response_Create_Job() : Response(Operation::Create_Job) {}
std::vector<Group*> Response_Create_Job::GetKnownGroups() {
  return {&operation_attributes, &unsupported_attributes, &job_attributes};
}
std::vector<const Group*> Response_Create_Job::GetKnownGroups() const {
  return {&operation_attributes, &unsupported_attributes, &job_attributes};
}
std::vector<Attribute*>
Response_Create_Job::G_job_attributes::GetKnownAttributes() {
  return {&job_id,
          &job_uri,
          &job_state,
          &job_state_reasons,
          &job_state_message,
          &number_of_intervening_jobs};
}
std::vector<const Attribute*>
Response_Create_Job::G_job_attributes::GetKnownAttributes() const {
  return {&job_id,
          &job_uri,
          &job_state,
          &job_state_reasons,
          &job_state_message,
          &number_of_intervening_jobs};
}
const std::map<AttrName, AttrDef> Response_Create_Job::G_job_attributes::defs_{
    {AttrName::job_id, {AttrType::integer, InternalType::kInteger, false}},
    {AttrName::job_uri, {AttrType::uri, InternalType::kString, false}},
    {AttrName::job_state, {AttrType::enum_, InternalType::kInteger, false}},
    {AttrName::job_state_reasons,
     {AttrType::keyword, InternalType::kInteger, true}},
    {AttrName::job_state_message,
     {AttrType::text, InternalType::kStringWithLanguage, false}},
    {AttrName::number_of_intervening_jobs,
     {AttrType::integer, InternalType::kInteger, false}}};
Request_Get_Job_Attributes::Request_Get_Job_Attributes()
    : Request(Operation::Get_Job_Attributes) {}
std::vector<Group*> Request_Get_Job_Attributes::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Request_Get_Job_Attributes::GetKnownGroups() const {
  return {&operation_attributes};
}
std::vector<Attribute*>
Request_Get_Job_Attributes::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset,
          &attributes_natural_language,
          &printer_uri,
          &job_id,
          &job_uri,
          &requesting_user_name,
          &requested_attributes};
}
std::vector<const Attribute*>
Request_Get_Job_Attributes::G_operation_attributes::GetKnownAttributes() const {
  return {&attributes_charset,
          &attributes_natural_language,
          &printer_uri,
          &job_id,
          &job_uri,
          &requesting_user_name,
          &requested_attributes};
}
const std::map<AttrName, AttrDef>
    Request_Get_Job_Attributes::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::printer_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::job_id, {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::requesting_user_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::requested_attributes,
         {AttrType::keyword, InternalType::kString, true}}};
Response_Get_Job_Attributes::Response_Get_Job_Attributes()
    : Response(Operation::Get_Job_Attributes) {}
std::vector<Group*> Response_Get_Job_Attributes::GetKnownGroups() {
  return {&operation_attributes, &unsupported_attributes, &job_attributes};
}
std::vector<const Group*> Response_Get_Job_Attributes::GetKnownGroups() const {
  return {&operation_attributes, &unsupported_attributes, &job_attributes};
}
std::vector<Attribute*>
Response_Get_Job_Attributes::G_job_attributes::GetKnownAttributes() {
  return {&attributes_charset,
          &attributes_natural_language,
          &copies,
          &copies_actual,
          &cover_back,
          &cover_back_actual,
          &cover_front,
          &cover_front_actual,
          &current_page_order,
          &date_time_at_completed,
          &date_time_at_creation,
          &date_time_at_processing,
          &document_charset_supplied,
          &document_format_details_supplied,
          &document_format_ready,
          &document_format_supplied,
          &document_format_version_supplied,
          &document_message_supplied,
          &document_metadata,
          &document_name_supplied,
          &document_natural_language_supplied,
          &errors_count,
          &feed_orientation,
          &finishings,
          &finishings_col,
          &finishings_col_actual,
          &font_name_requested,
          &font_size_requested,
          &force_front_side,
          &force_front_side_actual,
          &imposition_template,
          &impressions_completed_current_copy,
          &insert_sheet,
          &insert_sheet_actual,
          &job_account_id,
          &job_account_id_actual,
          &job_account_type,
          &job_accounting_sheets,
          &job_accounting_sheets_actual,
          &job_accounting_user_id,
          &job_accounting_user_id_actual,
          &job_attribute_fidelity,
          &job_charge_info,
          &job_collation_type,
          &job_copies,
          &job_copies_actual,
          &job_cover_back,
          &job_cover_back_actual,
          &job_cover_front,
          &job_cover_front_actual,
          &job_delay_output_until,
          &job_delay_output_until_time,
          &job_detailed_status_messages,
          &job_document_access_errors,
          &job_error_action,
          &job_error_sheet,
          &job_error_sheet_actual,
          &job_finishings,
          &job_finishings_col,
          &job_finishings_col_actual,
          &job_hold_until,
          &job_hold_until_time,
          &job_id,
          &job_impressions,
          &job_impressions_completed,
          &job_k_octets,
          &job_k_octets_processed,
          &job_mandatory_attributes,
          &job_media_sheets,
          &job_media_sheets_completed,
          &job_message_from_operator,
          &job_message_to_operator,
          &job_message_to_operator_actual,
          &job_more_info,
          &job_name,
          &job_originating_user_name,
          &job_originating_user_uri,
          &job_pages,
          &job_pages_completed,
          &job_pages_completed_current_copy,
          &job_pages_per_set,
          &job_phone_number,
          &job_printer_up_time,
          &job_printer_uri,
          &job_priority,
          &job_priority_actual,
          &job_recipient_name,
          &job_resource_ids,
          &job_save_disposition,
          &job_save_printer_make_and_model,
          &job_sheet_message,
          &job_sheet_message_actual,
          &job_sheets,
          &job_sheets_col,
          &job_sheets_col_actual,
          &job_state,
          &job_state_message,
          &job_state_reasons,
          &job_uri,
          &job_uuid,
          &media,
          &media_col,
          &media_col_actual,
          &media_input_tray_check,
          &multiple_document_handling,
          &number_of_documents,
          &number_of_intervening_jobs,
          &number_up,
          &number_up_actual,
          &orientation_requested,
          &original_requesting_user_name,
          &output_bin,
          &output_device,
          &output_device_actual,
          &output_device_assigned,
          &output_device_job_state_message,
          &output_device_uuid_assigned,
          &overrides,
          &overrides_actual,
          &page_delivery,
          &page_order_received,
          &page_ranges,
          &page_ranges_actual,
          &pages_per_subset,
          &pdl_init_file,
          &presentation_direction_number_up,
          &print_color_mode,
          &print_content_optimize,
          &print_quality,
          &print_rendering_intent,
          &print_scaling,
          &printer_resolution,
          &printer_resolution_actual,
          &proof_print,
          &separator_sheets,
          &separator_sheets_actual,
          &sheet_collate,
          &sheet_completed_copy_number,
          &sheet_completed_document_number,
          &sides,
          &time_at_completed,
          &time_at_creation,
          &time_at_processing,
          &warnings_count,
          &x_image_position,
          &x_image_shift,
          &x_image_shift_actual,
          &x_side1_image_shift,
          &x_side1_image_shift_actual,
          &x_side2_image_shift,
          &x_side2_image_shift_actual,
          &y_image_position,
          &y_image_shift,
          &y_image_shift_actual,
          &y_side1_image_shift,
          &y_side1_image_shift_actual,
          &y_side2_image_shift,
          &y_side2_image_shift_actual};
}
std::vector<const Attribute*>
Response_Get_Job_Attributes::G_job_attributes::GetKnownAttributes() const {
  return {&attributes_charset,
          &attributes_natural_language,
          &copies,
          &copies_actual,
          &cover_back,
          &cover_back_actual,
          &cover_front,
          &cover_front_actual,
          &current_page_order,
          &date_time_at_completed,
          &date_time_at_creation,
          &date_time_at_processing,
          &document_charset_supplied,
          &document_format_details_supplied,
          &document_format_ready,
          &document_format_supplied,
          &document_format_version_supplied,
          &document_message_supplied,
          &document_metadata,
          &document_name_supplied,
          &document_natural_language_supplied,
          &errors_count,
          &feed_orientation,
          &finishings,
          &finishings_col,
          &finishings_col_actual,
          &font_name_requested,
          &font_size_requested,
          &force_front_side,
          &force_front_side_actual,
          &imposition_template,
          &impressions_completed_current_copy,
          &insert_sheet,
          &insert_sheet_actual,
          &job_account_id,
          &job_account_id_actual,
          &job_account_type,
          &job_accounting_sheets,
          &job_accounting_sheets_actual,
          &job_accounting_user_id,
          &job_accounting_user_id_actual,
          &job_attribute_fidelity,
          &job_charge_info,
          &job_collation_type,
          &job_copies,
          &job_copies_actual,
          &job_cover_back,
          &job_cover_back_actual,
          &job_cover_front,
          &job_cover_front_actual,
          &job_delay_output_until,
          &job_delay_output_until_time,
          &job_detailed_status_messages,
          &job_document_access_errors,
          &job_error_action,
          &job_error_sheet,
          &job_error_sheet_actual,
          &job_finishings,
          &job_finishings_col,
          &job_finishings_col_actual,
          &job_hold_until,
          &job_hold_until_time,
          &job_id,
          &job_impressions,
          &job_impressions_completed,
          &job_k_octets,
          &job_k_octets_processed,
          &job_mandatory_attributes,
          &job_media_sheets,
          &job_media_sheets_completed,
          &job_message_from_operator,
          &job_message_to_operator,
          &job_message_to_operator_actual,
          &job_more_info,
          &job_name,
          &job_originating_user_name,
          &job_originating_user_uri,
          &job_pages,
          &job_pages_completed,
          &job_pages_completed_current_copy,
          &job_pages_per_set,
          &job_phone_number,
          &job_printer_up_time,
          &job_printer_uri,
          &job_priority,
          &job_priority_actual,
          &job_recipient_name,
          &job_resource_ids,
          &job_save_disposition,
          &job_save_printer_make_and_model,
          &job_sheet_message,
          &job_sheet_message_actual,
          &job_sheets,
          &job_sheets_col,
          &job_sheets_col_actual,
          &job_state,
          &job_state_message,
          &job_state_reasons,
          &job_uri,
          &job_uuid,
          &media,
          &media_col,
          &media_col_actual,
          &media_input_tray_check,
          &multiple_document_handling,
          &number_of_documents,
          &number_of_intervening_jobs,
          &number_up,
          &number_up_actual,
          &orientation_requested,
          &original_requesting_user_name,
          &output_bin,
          &output_device,
          &output_device_actual,
          &output_device_assigned,
          &output_device_job_state_message,
          &output_device_uuid_assigned,
          &overrides,
          &overrides_actual,
          &page_delivery,
          &page_order_received,
          &page_ranges,
          &page_ranges_actual,
          &pages_per_subset,
          &pdl_init_file,
          &presentation_direction_number_up,
          &print_color_mode,
          &print_content_optimize,
          &print_quality,
          &print_rendering_intent,
          &print_scaling,
          &printer_resolution,
          &printer_resolution_actual,
          &proof_print,
          &separator_sheets,
          &separator_sheets_actual,
          &sheet_collate,
          &sheet_completed_copy_number,
          &sheet_completed_document_number,
          &sides,
          &time_at_completed,
          &time_at_creation,
          &time_at_processing,
          &warnings_count,
          &x_image_position,
          &x_image_shift,
          &x_image_shift_actual,
          &x_side1_image_shift,
          &x_side1_image_shift_actual,
          &x_side2_image_shift,
          &x_side2_image_shift_actual,
          &y_image_position,
          &y_image_shift,
          &y_image_shift_actual,
          &y_side1_image_shift,
          &y_side1_image_shift_actual,
          &y_side2_image_shift,
          &y_side2_image_shift_actual};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::copies, {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::copies_actual,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::cover_back,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_cover_back(); }}},
        {AttrName::cover_back_actual,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_cover_back_actual(); }}},
        {AttrName::cover_front,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_cover_front(); }}},
        {AttrName::cover_front_actual,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_cover_front_actual(); }}},
        {AttrName::current_page_order,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::date_time_at_completed,
         {AttrType::dateTime, InternalType::kDateTime, false}},
        {AttrName::date_time_at_creation,
         {AttrType::dateTime, InternalType::kDateTime, false}},
        {AttrName::date_time_at_processing,
         {AttrType::dateTime, InternalType::kDateTime, false}},
        {AttrName::document_charset_supplied,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::document_format_details_supplied,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* {
            return new C_document_format_details_supplied();
          }}},
        {AttrName::document_format_ready,
         {AttrType::mimeMediaType, InternalType::kString, true}},
        {AttrName::document_format_supplied,
         {AttrType::mimeMediaType, InternalType::kString, false}},
        {AttrName::document_format_version_supplied,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::document_message_supplied,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::document_metadata,
         {AttrType::octetString, InternalType::kString, true}},
        {AttrName::document_name_supplied,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::document_natural_language_supplied,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::errors_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::feed_orientation,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::finishings, {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::finishings_col,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_finishings_col(); }}},
        {AttrName::finishings_col_actual,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_finishings_col_actual(); }}},
        {AttrName::font_name_requested,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::font_size_requested,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::force_front_side,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::force_front_side_actual,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::imposition_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::impressions_completed_current_copy,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::insert_sheet,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_insert_sheet(); }}},
        {AttrName::insert_sheet_actual,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_insert_sheet_actual(); }}},
        {AttrName::job_account_id,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_account_id_actual,
         {AttrType::name, InternalType::kStringWithLanguage, true}},
        {AttrName::job_account_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_accounting_sheets,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_accounting_sheets(); }}},
        {AttrName::job_accounting_sheets_actual,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* {
            return new C_job_accounting_sheets_actual();
          }}},
        {AttrName::job_accounting_user_id,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_accounting_user_id_actual,
         {AttrType::name, InternalType::kStringWithLanguage, true}},
        {AttrName::job_attribute_fidelity,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_charge_info,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::job_collation_type,
         {AttrType::enum_, InternalType::kInteger, false}},
        {AttrName::job_copies,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_copies_actual,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::job_cover_back,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_cover_back(); }}},
        {AttrName::job_cover_back_actual,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_job_cover_back_actual(); }}},
        {AttrName::job_cover_front,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_cover_front(); }}},
        {AttrName::job_cover_front_actual,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_job_cover_front_actual(); }}},
        {AttrName::job_delay_output_until,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_delay_output_until_time,
         {AttrType::dateTime, InternalType::kDateTime, false}},
        {AttrName::job_detailed_status_messages,
         {AttrType::text, InternalType::kStringWithLanguage, true}},
        {AttrName::job_document_access_errors,
         {AttrType::text, InternalType::kStringWithLanguage, true}},
        {AttrName::job_error_action,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::job_error_sheet,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_error_sheet(); }}},
        {AttrName::job_error_sheet_actual,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_job_error_sheet_actual(); }}},
        {AttrName::job_finishings,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::job_finishings_col,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_job_finishings_col(); }}},
        {AttrName::job_finishings_col_actual,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_job_finishings_col_actual(); }}},
        {AttrName::job_hold_until,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_hold_until_time,
         {AttrType::dateTime, InternalType::kDateTime, false}},
        {AttrName::job_id, {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_impressions,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_impressions_completed,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_k_octets,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_k_octets_processed,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_mandatory_attributes,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::job_media_sheets,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_media_sheets_completed,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_message_from_operator,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::job_message_to_operator,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::job_message_to_operator_actual,
         {AttrType::text, InternalType::kStringWithLanguage, true}},
        {AttrName::job_more_info,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::job_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_originating_user_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_originating_user_uri,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::job_pages,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_pages_completed,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_pages_completed_current_copy,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_pages_per_set,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_phone_number,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::job_printer_up_time,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_printer_uri,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::job_priority,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_priority_actual,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::job_recipient_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_resource_ids,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::job_save_disposition,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_save_disposition(); }}},
        {AttrName::job_save_printer_make_and_model,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::job_sheet_message,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::job_sheet_message_actual,
         {AttrType::text, InternalType::kStringWithLanguage, true}},
        {AttrName::job_sheets,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_sheets_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_sheets_col(); }}},
        {AttrName::job_sheets_col_actual,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_job_sheets_col_actual(); }}},
        {AttrName::job_state, {AttrType::enum_, InternalType::kInteger, false}},
        {AttrName::job_state_message,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::job_state_reasons,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::job_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::job_uuid, {AttrType::uri, InternalType::kString, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}},
        {AttrName::media_col_actual,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_media_col_actual(); }}},
        {AttrName::media_input_tray_check,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::multiple_document_handling,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::number_of_documents,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::number_of_intervening_jobs,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::number_up,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::number_up_actual,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::orientation_requested,
         {AttrType::enum_, InternalType::kInteger, false}},
        {AttrName::original_requesting_user_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::output_bin,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::output_device,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::output_device_actual,
         {AttrType::name, InternalType::kStringWithLanguage, true}},
        {AttrName::output_device_assigned,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::output_device_job_state_message,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::output_device_uuid_assigned,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::overrides,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_overrides(); }}},
        {AttrName::overrides_actual,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_overrides_actual(); }}},
        {AttrName::page_delivery,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::page_order_received,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::page_ranges,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::page_ranges_actual,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::pages_per_subset,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::pdl_init_file,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_pdl_init_file(); }}},
        {AttrName::presentation_direction_number_up,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::print_color_mode,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::print_content_optimize,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::print_quality,
         {AttrType::enum_, InternalType::kInteger, false}},
        {AttrName::print_rendering_intent,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::print_scaling,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::printer_resolution,
         {AttrType::resolution, InternalType::kResolution, false}},
        {AttrName::printer_resolution_actual,
         {AttrType::resolution, InternalType::kResolution, true}},
        {AttrName::proof_print,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_proof_print(); }}},
        {AttrName::separator_sheets,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_separator_sheets(); }}},
        {AttrName::separator_sheets_actual,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_separator_sheets_actual(); }}},
        {AttrName::sheet_collate,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::sheet_completed_copy_number,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::sheet_completed_document_number,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::sides, {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::time_at_completed,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::time_at_creation,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::time_at_processing,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::warnings_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::x_image_position,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::x_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::x_image_shift_actual,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::x_side1_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::x_side1_image_shift_actual,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::x_side2_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::x_side2_image_shift_actual,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::y_image_position,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::y_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_image_shift_actual,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::y_side1_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_side1_image_shift_actual,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::y_side2_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_side2_image_shift_actual,
         {AttrType::integer, InternalType::kInteger, true}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_cover_back::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_cover_back::GetKnownAttributes() const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::C_cover_back::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_cover_back_actual::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_cover_back_actual::GetKnownAttributes() const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::C_cover_back_actual::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_cover_front::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_cover_front::GetKnownAttributes() const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::C_cover_front::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_cover_front_actual::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_cover_front_actual::GetKnownAttributes() const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::C_cover_front_actual::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_document_format_details_supplied::GetKnownAttributes() {
  return {&document_format,
          &document_format_device_id,
          &document_format_version,
          &document_natural_language,
          &document_source_application_name,
          &document_source_application_version,
          &document_source_os_name,
          &document_source_os_version};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_document_format_details_supplied::GetKnownAttributes() const {
  return {&document_format,
          &document_format_device_id,
          &document_format_version,
          &document_natural_language,
          &document_source_application_name,
          &document_source_application_version,
          &document_source_os_name,
          &document_source_os_version};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_document_format_details_supplied::defs_{
        {AttrName::document_format,
         {AttrType::mimeMediaType, InternalType::kString, false}},
        {AttrName::document_format_device_id,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::document_format_version,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::document_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, true}},
        {AttrName::document_source_application_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::document_source_application_version,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::document_source_os_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::document_source_os_version,
         {AttrType::text, InternalType::kStringWithLanguage, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::GetKnownAttributes() {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::GetKnownAttributes() const {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::C_finishings_col::defs_{
        {AttrName::baling,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_baling(); }}},
        {AttrName::binding,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_binding(); }}},
        {AttrName::coating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_coating(); }}},
        {AttrName::covering,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_covering(); }}},
        {AttrName::finishing_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::folding,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_folding(); }}},
        {AttrName::imposition_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::laminating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_laminating(); }}},
        {AttrName::media_sheets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::punching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_punching(); }}},
        {AttrName::stitching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_stitching(); }}},
        {AttrName::trimming,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_trimming(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_baling::GetKnownAttributes() {
  return {&baling_type, &baling_when};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_baling::GetKnownAttributes() const {
  return {&baling_type, &baling_when};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col::C_baling::defs_{
        {AttrName::baling_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::baling_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_binding::GetKnownAttributes() {
  return {&binding_reference_edge, &binding_type};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_binding::GetKnownAttributes() const {
  return {&binding_reference_edge, &binding_type};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col::C_binding::defs_{
        {AttrName::binding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::binding_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_coating::GetKnownAttributes() {
  return {&coating_sides, &coating_type};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_coating::GetKnownAttributes() const {
  return {&coating_sides, &coating_type};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col::C_coating::defs_{
        {AttrName::coating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::coating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_covering::GetKnownAttributes() {
  return {&covering_name};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_covering::GetKnownAttributes() const {
  return {&covering_name};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col::C_covering::defs_{
        {AttrName::covering_name,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_folding::GetKnownAttributes() {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_folding::GetKnownAttributes() const {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col::C_folding::defs_{
        {AttrName::folding_direction,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::folding_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::folding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_laminating::GetKnownAttributes() {
  return {&laminating_sides, &laminating_type};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_laminating::GetKnownAttributes() const {
  return {&laminating_sides, &laminating_type};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col::C_laminating::defs_{
        {AttrName::laminating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::laminating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_media_size::GetKnownAttributes() {
  return {};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_media_size::GetKnownAttributes() const {
  return {};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col::C_media_size::defs_{};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_punching::GetKnownAttributes() {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_punching::GetKnownAttributes() const {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col::C_punching::defs_{
        {AttrName::punching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::punching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::punching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_stitching::GetKnownAttributes() {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_stitching::GetKnownAttributes() const {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col::C_stitching::defs_{
        {AttrName::stitching_angle,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::stitching_method,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::stitching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_trimming::GetKnownAttributes() {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col::C_trimming::GetKnownAttributes() const {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col::C_trimming::defs_{
        {AttrName::trimming_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::trimming_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::trimming_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::trimming_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::GetKnownAttributes() {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::GetKnownAttributes() const {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col_actual::defs_{
        {AttrName::baling,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_baling(); }}},
        {AttrName::binding,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_binding(); }}},
        {AttrName::coating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_coating(); }}},
        {AttrName::covering,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_covering(); }}},
        {AttrName::finishing_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::folding,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_folding(); }}},
        {AttrName::imposition_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::laminating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_laminating(); }}},
        {AttrName::media_sheets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::punching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_punching(); }}},
        {AttrName::stitching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_stitching(); }}},
        {AttrName::trimming,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_trimming(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_baling::GetKnownAttributes() {
  return {&baling_type, &baling_when};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_baling::GetKnownAttributes() const {
  return {&baling_type, &baling_when};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col_actual::C_baling::defs_{
        {AttrName::baling_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::baling_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_binding::GetKnownAttributes() {
  return {&binding_reference_edge, &binding_type};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_binding::GetKnownAttributes() const {
  return {&binding_reference_edge, &binding_type};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col_actual::C_binding::defs_{
        {AttrName::binding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::binding_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_coating::GetKnownAttributes() {
  return {&coating_sides, &coating_type};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_coating::GetKnownAttributes() const {
  return {&coating_sides, &coating_type};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col_actual::C_coating::defs_{
        {AttrName::coating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::coating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_covering::GetKnownAttributes() {
  return {&covering_name};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_covering::GetKnownAttributes() const {
  return {&covering_name};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col_actual::C_covering::defs_{
        {AttrName::covering_name,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_folding::GetKnownAttributes() {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_folding::GetKnownAttributes() const {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col_actual::C_folding::defs_{
        {AttrName::folding_direction,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::folding_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::folding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_laminating::GetKnownAttributes() {
  return {&laminating_sides, &laminating_type};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_laminating::GetKnownAttributes() const {
  return {&laminating_sides, &laminating_type};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col_actual::C_laminating::defs_{
        {AttrName::laminating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::laminating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_media_size::GetKnownAttributes() {
  return {};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_media_size::GetKnownAttributes() const {
  return {};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col_actual::C_media_size::defs_{};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_punching::GetKnownAttributes() {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_punching::GetKnownAttributes() const {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col_actual::C_punching::defs_{
        {AttrName::punching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::punching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::punching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_stitching::GetKnownAttributes() {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_stitching::GetKnownAttributes() const {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col_actual::C_stitching::defs_{
        {AttrName::stitching_angle,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::stitching_method,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::stitching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_trimming::GetKnownAttributes() {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_finishings_col_actual::C_trimming::GetKnownAttributes() const {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_finishings_col_actual::C_trimming::defs_{
        {AttrName::trimming_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::trimming_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::trimming_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::trimming_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_insert_sheet::GetKnownAttributes() {
  return {&insert_after_page_number, &insert_count, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_insert_sheet::GetKnownAttributes() const {
  return {&insert_after_page_number, &insert_count, &media, &media_col};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::C_insert_sheet::defs_{
        {AttrName::insert_after_page_number,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::insert_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_insert_sheet_actual::GetKnownAttributes() {
  return {&insert_after_page_number, &insert_count, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_insert_sheet_actual::GetKnownAttributes() const {
  return {&insert_after_page_number, &insert_count, &media, &media_col};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::C_insert_sheet_actual::defs_{
        {AttrName::insert_after_page_number,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::insert_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_accounting_sheets::GetKnownAttributes() {
  return {&job_accounting_output_bin, &job_accounting_sheets_type, &media,
          &media_col};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_accounting_sheets::GetKnownAttributes() const {
  return {&job_accounting_output_bin, &job_accounting_sheets_type, &media,
          &media_col};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_accounting_sheets::defs_{
        {AttrName::job_accounting_output_bin,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_accounting_sheets_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_accounting_sheets_actual::GetKnownAttributes() {
  return {&job_accounting_output_bin, &job_accounting_sheets_type, &media,
          &media_col};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_accounting_sheets_actual::GetKnownAttributes() const {
  return {&job_accounting_output_bin, &job_accounting_sheets_type, &media,
          &media_col};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_accounting_sheets_actual::defs_{
        {AttrName::job_accounting_output_bin,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_accounting_sheets_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_cover_back::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_cover_back::GetKnownAttributes() const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::C_job_cover_back::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_cover_back_actual::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_cover_back_actual::GetKnownAttributes() const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_cover_back_actual::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_cover_front::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_cover_front::GetKnownAttributes() const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::C_job_cover_front::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_cover_front_actual::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_cover_front_actual::GetKnownAttributes() const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_cover_front_actual::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_error_sheet::GetKnownAttributes() {
  return {&job_error_sheet_type, &job_error_sheet_when, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_error_sheet::GetKnownAttributes() const {
  return {&job_error_sheet_type, &job_error_sheet_when, &media, &media_col};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::C_job_error_sheet::defs_{
        {AttrName::job_error_sheet_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_error_sheet_when,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_error_sheet_actual::GetKnownAttributes() {
  return {&job_error_sheet_type, &job_error_sheet_when, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_error_sheet_actual::GetKnownAttributes() const {
  return {&job_error_sheet_type, &job_error_sheet_when, &media, &media_col};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_error_sheet_actual::defs_{
        {AttrName::job_error_sheet_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_error_sheet_when,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::GetKnownAttributes() {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::GetKnownAttributes() const {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::C_job_finishings_col::defs_{
        {AttrName::baling,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_baling(); }}},
        {AttrName::binding,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_binding(); }}},
        {AttrName::coating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_coating(); }}},
        {AttrName::covering,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_covering(); }}},
        {AttrName::finishing_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::folding,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_folding(); }}},
        {AttrName::imposition_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::laminating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_laminating(); }}},
        {AttrName::media_sheets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::punching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_punching(); }}},
        {AttrName::stitching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_stitching(); }}},
        {AttrName::trimming,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_trimming(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_baling::GetKnownAttributes() {
  return {&baling_type, &baling_when};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_baling::GetKnownAttributes() const {
  return {&baling_type, &baling_when};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_finishings_col::C_baling::defs_{
        {AttrName::baling_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::baling_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_binding::GetKnownAttributes() {
  return {&binding_reference_edge, &binding_type};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_binding::GetKnownAttributes() const {
  return {&binding_reference_edge, &binding_type};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_finishings_col::C_binding::defs_{
        {AttrName::binding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::binding_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_coating::GetKnownAttributes() {
  return {&coating_sides, &coating_type};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_coating::GetKnownAttributes() const {
  return {&coating_sides, &coating_type};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_finishings_col::C_coating::defs_{
        {AttrName::coating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::coating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_covering::GetKnownAttributes() {
  return {&covering_name};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_covering::GetKnownAttributes() const {
  return {&covering_name};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_finishings_col::C_covering::defs_{
        {AttrName::covering_name,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_folding::GetKnownAttributes() {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_folding::GetKnownAttributes() const {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_finishings_col::C_folding::defs_{
        {AttrName::folding_direction,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::folding_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::folding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_laminating::GetKnownAttributes() {
  return {&laminating_sides, &laminating_type};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_laminating::GetKnownAttributes() const {
  return {&laminating_sides, &laminating_type};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_finishings_col::C_laminating::defs_{
        {AttrName::laminating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::laminating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_media_size::GetKnownAttributes() {
  return {};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_media_size::GetKnownAttributes() const {
  return {};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_finishings_col::C_media_size::defs_{};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_punching::GetKnownAttributes() {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_punching::GetKnownAttributes() const {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_finishings_col::C_punching::defs_{
        {AttrName::punching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::punching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::punching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_stitching::GetKnownAttributes() {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_stitching::GetKnownAttributes() const {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_finishings_col::C_stitching::defs_{
        {AttrName::stitching_angle,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::stitching_method,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::stitching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_trimming::GetKnownAttributes() {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col::C_trimming::GetKnownAttributes() const {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_finishings_col::C_trimming::defs_{
        {AttrName::trimming_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::trimming_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::trimming_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::trimming_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col_actual::GetKnownAttributes() {
  return {&media_back_coating,  &media_bottom_margin, &media_color,
          &media_front_coating, &media_grain,         &media_hole_count,
          &media_info,          &media_key,           &media_left_margin,
          &media_order_count,   &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,          &media_size_name,
          &media_source,        &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,          &media_weight_metric};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col_actual::GetKnownAttributes() const {
  return {&media_back_coating,  &media_bottom_margin, &media_color,
          &media_front_coating, &media_grain,         &media_hole_count,
          &media_info,          &media_key,           &media_left_margin,
          &media_order_count,   &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,          &media_size_name,
          &media_source,        &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,          &media_weight_metric};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_finishings_col_actual::defs_{
        {AttrName::media_back_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_bottom_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_color,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_front_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_grain,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_hole_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_info,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::media_key,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_left_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_order_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_pre_printed,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_recycled,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_right_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::media_source,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_thickness,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_tooth,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_top_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_weight_metric,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col_actual::C_media_size::GetKnownAttributes() {
  return {&x_dimension, &y_dimension};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_finishings_col_actual::C_media_size::GetKnownAttributes() const {
  return {&x_dimension, &y_dimension};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_finishings_col_actual::C_media_size::defs_{
        {AttrName::x_dimension,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_dimension,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_save_disposition::GetKnownAttributes() {
  return {&save_disposition, &save_info};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_save_disposition::GetKnownAttributes() const {
  return {&save_disposition, &save_info};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_save_disposition::defs_{
        {AttrName::save_disposition,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::save_info,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_save_info(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_save_disposition::C_save_info::GetKnownAttributes() {
  return {&save_document_format, &save_location, &save_name};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_save_disposition::C_save_info::GetKnownAttributes() const {
  return {&save_document_format, &save_location, &save_name};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_save_disposition::C_save_info::defs_{
        {AttrName::save_document_format,
         {AttrType::mimeMediaType, InternalType::kString, false}},
        {AttrName::save_location,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::save_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_sheets_col::GetKnownAttributes() {
  return {&job_sheets, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_sheets_col::GetKnownAttributes() const {
  return {&job_sheets, &media, &media_col};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::C_job_sheets_col::defs_{
        {AttrName::job_sheets,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_sheets_col_actual::GetKnownAttributes() {
  return {&job_sheets, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_job_sheets_col_actual::GetKnownAttributes() const {
  return {&job_sheets, &media, &media_col};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_job_sheets_col_actual::defs_{
        {AttrName::job_sheets,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_media_col::GetKnownAttributes() {
  return {&media_back_coating,  &media_bottom_margin, &media_color,
          &media_front_coating, &media_grain,         &media_hole_count,
          &media_info,          &media_key,           &media_left_margin,
          &media_order_count,   &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,          &media_size_name,
          &media_source,        &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,          &media_weight_metric};
}
std::vector<const Attribute*>
Response_Get_Job_Attributes::G_job_attributes::C_media_col::GetKnownAttributes()
    const {
  return {&media_back_coating,  &media_bottom_margin, &media_color,
          &media_front_coating, &media_grain,         &media_hole_count,
          &media_info,          &media_key,           &media_left_margin,
          &media_order_count,   &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,          &media_size_name,
          &media_source,        &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,          &media_weight_metric};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::C_media_col::defs_{
        {AttrName::media_back_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_bottom_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_color,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_front_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_grain,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_hole_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_info,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::media_key,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_left_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_order_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_pre_printed,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_recycled,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_right_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::media_source,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_thickness,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_tooth,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_top_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_weight_metric,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_media_col::C_media_size::GetKnownAttributes() {
  return {&x_dimension, &y_dimension};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_media_col::C_media_size::GetKnownAttributes() const {
  return {&x_dimension, &y_dimension};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_media_col::C_media_size::defs_{
        {AttrName::x_dimension,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_dimension,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_media_col_actual::GetKnownAttributes() {
  return {&media_back_coating,  &media_bottom_margin, &media_color,
          &media_front_coating, &media_grain,         &media_hole_count,
          &media_info,          &media_key,           &media_left_margin,
          &media_order_count,   &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,          &media_size_name,
          &media_source,        &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,          &media_weight_metric};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_media_col_actual::GetKnownAttributes() const {
  return {&media_back_coating,  &media_bottom_margin, &media_color,
          &media_front_coating, &media_grain,         &media_hole_count,
          &media_info,          &media_key,           &media_left_margin,
          &media_order_count,   &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,          &media_size_name,
          &media_source,        &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,          &media_weight_metric};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::C_media_col_actual::defs_{
        {AttrName::media_back_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_bottom_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_color,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_front_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_grain,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_hole_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_info,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::media_key,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_left_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_order_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_pre_printed,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_recycled,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_right_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::media_source,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_thickness,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_tooth,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_top_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_weight_metric,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_media_col_actual::C_media_size::GetKnownAttributes() {
  return {&x_dimension, &y_dimension};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_media_col_actual::C_media_size::GetKnownAttributes() const {
  return {&x_dimension, &y_dimension};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_media_col_actual::C_media_size::defs_{
        {AttrName::x_dimension,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_dimension,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_overrides::GetKnownAttributes() {
  return {&job_account_id,
          &job_account_type,
          &job_accounting_sheets,
          &job_accounting_user_id,
          &job_copies,
          &job_cover_back,
          &job_cover_front,
          &job_delay_output_until,
          &job_delay_output_until_time,
          &job_error_action,
          &job_error_sheet,
          &job_finishings,
          &job_finishings_col,
          &job_hold_until,
          &job_hold_until_time,
          &job_message_to_operator,
          &job_pages_per_set,
          &job_phone_number,
          &job_priority,
          &job_recipient_name,
          &job_save_disposition,
          &job_sheet_message,
          &job_sheets,
          &job_sheets_col,
          &pages_per_subset,
          &output_bin,
          &output_device,
          &multiple_document_handling,
          &y_side1_image_shift,
          &y_side2_image_shift,
          &number_up,
          &orientation_requested,
          &page_delivery,
          &page_order_received,
          &page_ranges,
          &pdl_init_file,
          &print_color_mode,
          &print_content_optimize,
          &print_quality,
          &print_rendering_intent,
          &printer_resolution,
          &presentation_direction_number_up,
          &media,
          &sides,
          &x_image_position,
          &x_image_shift,
          &x_side1_image_shift,
          &x_side2_image_shift,
          &y_image_position,
          &y_image_shift,
          &copies,
          &cover_back,
          &cover_front,
          &imposition_template,
          &insert_sheet,
          &media_col,
          &media_input_tray_check,
          &print_scaling,
          &proof_print,
          &separator_sheets,
          &sheet_collate,
          &feed_orientation,
          &finishings,
          &finishings_col,
          &font_name_requested,
          &font_size_requested,
          &force_front_side,
          &document_copies,
          &document_numbers,
          &pages};
}
std::vector<const Attribute*>
Response_Get_Job_Attributes::G_job_attributes::C_overrides::GetKnownAttributes()
    const {
  return {&job_account_id,
          &job_account_type,
          &job_accounting_sheets,
          &job_accounting_user_id,
          &job_copies,
          &job_cover_back,
          &job_cover_front,
          &job_delay_output_until,
          &job_delay_output_until_time,
          &job_error_action,
          &job_error_sheet,
          &job_finishings,
          &job_finishings_col,
          &job_hold_until,
          &job_hold_until_time,
          &job_message_to_operator,
          &job_pages_per_set,
          &job_phone_number,
          &job_priority,
          &job_recipient_name,
          &job_save_disposition,
          &job_sheet_message,
          &job_sheets,
          &job_sheets_col,
          &pages_per_subset,
          &output_bin,
          &output_device,
          &multiple_document_handling,
          &y_side1_image_shift,
          &y_side2_image_shift,
          &number_up,
          &orientation_requested,
          &page_delivery,
          &page_order_received,
          &page_ranges,
          &pdl_init_file,
          &print_color_mode,
          &print_content_optimize,
          &print_quality,
          &print_rendering_intent,
          &printer_resolution,
          &presentation_direction_number_up,
          &media,
          &sides,
          &x_image_position,
          &x_image_shift,
          &x_side1_image_shift,
          &x_side2_image_shift,
          &y_image_position,
          &y_image_shift,
          &copies,
          &cover_back,
          &cover_front,
          &imposition_template,
          &insert_sheet,
          &media_col,
          &media_input_tray_check,
          &print_scaling,
          &proof_print,
          &separator_sheets,
          &sheet_collate,
          &feed_orientation,
          &finishings,
          &finishings_col,
          &font_name_requested,
          &font_size_requested,
          &force_front_side,
          &document_copies,
          &document_numbers,
          &pages};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::C_overrides::defs_{
        {AttrName::job_account_id,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_account_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_accounting_sheets,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_accounting_sheets(); }}},
        {AttrName::job_accounting_user_id,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_copies,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_cover_back,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_cover_back(); }}},
        {AttrName::job_cover_front,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_cover_front(); }}},
        {AttrName::job_delay_output_until,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_delay_output_until_time,
         {AttrType::dateTime, InternalType::kDateTime, false}},
        {AttrName::job_error_action,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::job_error_sheet,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_error_sheet(); }}},
        {AttrName::job_finishings,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::job_finishings_col,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_job_finishings_col(); }}},
        {AttrName::job_hold_until,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_hold_until_time,
         {AttrType::dateTime, InternalType::kDateTime, false}},
        {AttrName::job_message_to_operator,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::job_pages_per_set,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_phone_number,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::job_priority,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_recipient_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_save_disposition,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_save_disposition(); }}},
        {AttrName::job_sheet_message,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::job_sheets,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_sheets_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_sheets_col(); }}},
        {AttrName::pages_per_subset,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::output_bin,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::output_device,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::multiple_document_handling,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::y_side1_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_side2_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::number_up,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::orientation_requested,
         {AttrType::enum_, InternalType::kInteger, false}},
        {AttrName::page_delivery,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::page_order_received,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::page_ranges,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::pdl_init_file,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_pdl_init_file(); }}},
        {AttrName::print_color_mode,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::print_content_optimize,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::print_quality,
         {AttrType::enum_, InternalType::kInteger, false}},
        {AttrName::print_rendering_intent,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::printer_resolution,
         {AttrType::resolution, InternalType::kResolution, false}},
        {AttrName::presentation_direction_number_up,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::sides, {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::x_image_position,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::x_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::x_side1_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::x_side2_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_image_position,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::y_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::copies, {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::cover_back,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_cover_back(); }}},
        {AttrName::cover_front,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_cover_front(); }}},
        {AttrName::imposition_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::insert_sheet,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_insert_sheet(); }}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}},
        {AttrName::media_input_tray_check,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::print_scaling,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::proof_print,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_proof_print(); }}},
        {AttrName::separator_sheets,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_separator_sheets(); }}},
        {AttrName::sheet_collate,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::feed_orientation,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::finishings, {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::finishings_col,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_finishings_col(); }}},
        {AttrName::font_name_requested,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::font_size_requested,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::force_front_side,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::document_copies,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::document_numbers,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::pages,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_overrides_actual::GetKnownAttributes() {
  return {&job_account_id,
          &job_account_type,
          &job_accounting_sheets,
          &job_accounting_user_id,
          &job_copies,
          &job_cover_back,
          &job_cover_front,
          &job_delay_output_until,
          &job_delay_output_until_time,
          &job_error_action,
          &job_error_sheet,
          &job_finishings,
          &job_finishings_col,
          &job_hold_until,
          &job_hold_until_time,
          &job_message_to_operator,
          &job_pages_per_set,
          &job_phone_number,
          &job_priority,
          &job_recipient_name,
          &job_save_disposition,
          &job_sheet_message,
          &job_sheets,
          &job_sheets_col,
          &pages_per_subset,
          &output_bin,
          &output_device,
          &multiple_document_handling,
          &y_side1_image_shift,
          &y_side2_image_shift,
          &number_up,
          &orientation_requested,
          &page_delivery,
          &page_order_received,
          &page_ranges,
          &pdl_init_file,
          &print_color_mode,
          &print_content_optimize,
          &print_quality,
          &print_rendering_intent,
          &printer_resolution,
          &presentation_direction_number_up,
          &media,
          &sides,
          &x_image_position,
          &x_image_shift,
          &x_side1_image_shift,
          &x_side2_image_shift,
          &y_image_position,
          &y_image_shift,
          &copies,
          &cover_back,
          &cover_front,
          &imposition_template,
          &insert_sheet,
          &media_col,
          &media_input_tray_check,
          &print_scaling,
          &proof_print,
          &separator_sheets,
          &sheet_collate,
          &feed_orientation,
          &finishings,
          &finishings_col,
          &font_name_requested,
          &font_size_requested,
          &force_front_side,
          &document_copies,
          &document_numbers,
          &pages};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_overrides_actual::GetKnownAttributes() const {
  return {&job_account_id,
          &job_account_type,
          &job_accounting_sheets,
          &job_accounting_user_id,
          &job_copies,
          &job_cover_back,
          &job_cover_front,
          &job_delay_output_until,
          &job_delay_output_until_time,
          &job_error_action,
          &job_error_sheet,
          &job_finishings,
          &job_finishings_col,
          &job_hold_until,
          &job_hold_until_time,
          &job_message_to_operator,
          &job_pages_per_set,
          &job_phone_number,
          &job_priority,
          &job_recipient_name,
          &job_save_disposition,
          &job_sheet_message,
          &job_sheets,
          &job_sheets_col,
          &pages_per_subset,
          &output_bin,
          &output_device,
          &multiple_document_handling,
          &y_side1_image_shift,
          &y_side2_image_shift,
          &number_up,
          &orientation_requested,
          &page_delivery,
          &page_order_received,
          &page_ranges,
          &pdl_init_file,
          &print_color_mode,
          &print_content_optimize,
          &print_quality,
          &print_rendering_intent,
          &printer_resolution,
          &presentation_direction_number_up,
          &media,
          &sides,
          &x_image_position,
          &x_image_shift,
          &x_side1_image_shift,
          &x_side2_image_shift,
          &y_image_position,
          &y_image_shift,
          &copies,
          &cover_back,
          &cover_front,
          &imposition_template,
          &insert_sheet,
          &media_col,
          &media_input_tray_check,
          &print_scaling,
          &proof_print,
          &separator_sheets,
          &sheet_collate,
          &feed_orientation,
          &finishings,
          &finishings_col,
          &font_name_requested,
          &font_size_requested,
          &force_front_side,
          &document_copies,
          &document_numbers,
          &pages};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::C_overrides_actual::defs_{
        {AttrName::job_account_id,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_account_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_accounting_sheets,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_accounting_sheets(); }}},
        {AttrName::job_accounting_user_id,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_copies,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_cover_back,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_cover_back(); }}},
        {AttrName::job_cover_front,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_cover_front(); }}},
        {AttrName::job_delay_output_until,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_delay_output_until_time,
         {AttrType::dateTime, InternalType::kDateTime, false}},
        {AttrName::job_error_action,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::job_error_sheet,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_error_sheet(); }}},
        {AttrName::job_finishings,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::job_finishings_col,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_job_finishings_col(); }}},
        {AttrName::job_hold_until,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_hold_until_time,
         {AttrType::dateTime, InternalType::kDateTime, false}},
        {AttrName::job_message_to_operator,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::job_pages_per_set,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_phone_number,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::job_priority,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_recipient_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_save_disposition,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_save_disposition(); }}},
        {AttrName::job_sheet_message,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::job_sheets,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_sheets_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_sheets_col(); }}},
        {AttrName::pages_per_subset,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::output_bin,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::output_device,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::multiple_document_handling,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::y_side1_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_side2_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::number_up,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::orientation_requested,
         {AttrType::enum_, InternalType::kInteger, false}},
        {AttrName::page_delivery,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::page_order_received,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::page_ranges,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::pdl_init_file,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_pdl_init_file(); }}},
        {AttrName::print_color_mode,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::print_content_optimize,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::print_quality,
         {AttrType::enum_, InternalType::kInteger, false}},
        {AttrName::print_rendering_intent,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::printer_resolution,
         {AttrType::resolution, InternalType::kResolution, false}},
        {AttrName::presentation_direction_number_up,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::sides, {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::x_image_position,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::x_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::x_side1_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::x_side2_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_image_position,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::y_image_shift,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::copies, {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::cover_back,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_cover_back(); }}},
        {AttrName::cover_front,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_cover_front(); }}},
        {AttrName::imposition_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::insert_sheet,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_insert_sheet(); }}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}},
        {AttrName::media_input_tray_check,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::print_scaling,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::proof_print,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_proof_print(); }}},
        {AttrName::separator_sheets,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_separator_sheets(); }}},
        {AttrName::sheet_collate,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::feed_orientation,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::finishings, {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::finishings_col,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_finishings_col(); }}},
        {AttrName::font_name_requested,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::font_size_requested,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::force_front_side,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::document_copies,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::document_numbers,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::pages,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_pdl_init_file::GetKnownAttributes() {
  return {&pdl_init_file_entry, &pdl_init_file_location, &pdl_init_file_name};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_pdl_init_file::GetKnownAttributes() const {
  return {&pdl_init_file_entry, &pdl_init_file_location, &pdl_init_file_name};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::C_pdl_init_file::defs_{
        {AttrName::pdl_init_file_entry,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::pdl_init_file_location,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::pdl_init_file_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_proof_print::GetKnownAttributes() {
  return {&media, &media_col, &proof_print_copies};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_proof_print::GetKnownAttributes() const {
  return {&media, &media_col, &proof_print_copies};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::C_proof_print::defs_{
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}},
        {AttrName::proof_print_copies,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_separator_sheets::GetKnownAttributes() {
  return {&media, &media_col, &separator_sheets_type};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_separator_sheets::GetKnownAttributes() const {
  return {&media, &media_col, &separator_sheets_type};
}
const std::map<AttrName, AttrDef>
    Response_Get_Job_Attributes::G_job_attributes::C_separator_sheets::defs_{
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}},
        {AttrName::separator_sheets_type,
         {AttrType::keyword, InternalType::kInteger, true}}};
std::vector<Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_separator_sheets_actual::GetKnownAttributes() {
  return {&media, &media_col, &separator_sheets_type};
}
std::vector<const Attribute*> Response_Get_Job_Attributes::G_job_attributes::
    C_separator_sheets_actual::GetKnownAttributes() const {
  return {&media, &media_col, &separator_sheets_type};
}
const std::map<AttrName, AttrDef> Response_Get_Job_Attributes::
    G_job_attributes::C_separator_sheets_actual::defs_{
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}},
        {AttrName::separator_sheets_type,
         {AttrType::keyword, InternalType::kInteger, true}}};
Request_Get_Jobs::Request_Get_Jobs() : Request(Operation::Get_Jobs) {}
std::vector<Group*> Request_Get_Jobs::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Request_Get_Jobs::GetKnownGroups() const {
  return {&operation_attributes};
}
std::vector<Attribute*>
Request_Get_Jobs::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset,
          &attributes_natural_language,
          &printer_uri,
          &requesting_user_name,
          &limit,
          &requested_attributes,
          &which_jobs,
          &my_jobs};
}
std::vector<const Attribute*>
Request_Get_Jobs::G_operation_attributes::GetKnownAttributes() const {
  return {&attributes_charset,
          &attributes_natural_language,
          &printer_uri,
          &requesting_user_name,
          &limit,
          &requested_attributes,
          &which_jobs,
          &my_jobs};
}
const std::map<AttrName, AttrDef>
    Request_Get_Jobs::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::printer_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::requesting_user_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::limit, {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::requested_attributes,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::which_jobs,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::my_jobs,
         {AttrType::boolean, InternalType::kInteger, false}}};
Response_Get_Jobs::Response_Get_Jobs() : Response(Operation::Get_Jobs) {}
std::vector<Group*> Response_Get_Jobs::GetKnownGroups() {
  return {&operation_attributes, &unsupported_attributes, &job_attributes};
}
std::vector<const Group*> Response_Get_Jobs::GetKnownGroups() const {
  return {&operation_attributes, &unsupported_attributes, &job_attributes};
}
Request_Get_Printer_Attributes::Request_Get_Printer_Attributes()
    : Request(Operation::Get_Printer_Attributes) {}
std::vector<Group*> Request_Get_Printer_Attributes::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Request_Get_Printer_Attributes::GetKnownGroups()
    const {
  return {&operation_attributes};
}
std::vector<Attribute*>
Request_Get_Printer_Attributes::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset,   &attributes_natural_language,
          &printer_uri,          &requesting_user_name,
          &requested_attributes, &document_format};
}
std::vector<const Attribute*>
Request_Get_Printer_Attributes::G_operation_attributes::GetKnownAttributes()
    const {
  return {&attributes_charset,   &attributes_natural_language,
          &printer_uri,          &requesting_user_name,
          &requested_attributes, &document_format};
}
const std::map<AttrName, AttrDef>
    Request_Get_Printer_Attributes::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::printer_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::requesting_user_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::requested_attributes,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::document_format,
         {AttrType::mimeMediaType, InternalType::kString, false}}};
Response_Get_Printer_Attributes::Response_Get_Printer_Attributes()
    : Response(Operation::Get_Printer_Attributes) {}
std::vector<Group*> Response_Get_Printer_Attributes::GetKnownGroups() {
  return {&operation_attributes, &unsupported_attributes, &printer_attributes};
}
std::vector<const Group*> Response_Get_Printer_Attributes::GetKnownGroups()
    const {
  return {&operation_attributes, &unsupported_attributes, &printer_attributes};
}
std::vector<Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::GetKnownAttributes() {
  return {&baling_type_supported,
          &baling_when_supported,
          &binding_reference_edge_supported,
          &binding_type_supported,
          &charset_configured,
          &charset_supported,
          &coating_sides_supported,
          &coating_type_supported,
          &color_supported,
          &compression_supported,
          &copies_default,
          &copies_supported,
          &cover_back_default,
          &cover_back_supported,
          &cover_front_default,
          &cover_front_supported,
          &covering_name_supported,
          &device_service_count,
          &device_uuid,
          &document_charset_default,
          &document_charset_supported,
          &document_digital_signature_default,
          &document_digital_signature_supported,
          &document_format_default,
          &document_format_details_default,
          &document_format_details_supported,
          &document_format_supported,
          &document_format_varying_attributes,
          &document_format_version_default,
          &document_format_version_supported,
          &document_natural_language_default,
          &document_natural_language_supported,
          &document_password_supported,
          &feed_orientation_supported,
          &finishing_template_supported,
          &finishings_col_database,
          &finishings_col_default,
          &finishings_col_ready,
          &finishings_default,
          &finishings_ready,
          &finishings_supported,
          &folding_direction_supported,
          &folding_offset_supported,
          &folding_reference_edge_supported,
          &font_name_requested_default,
          &font_name_requested_supported,
          &font_size_requested_default,
          &font_size_requested_supported,
          &generated_natural_language_supported,
          &identify_actions_default,
          &identify_actions_supported,
          &insert_after_page_number_supported,
          &insert_count_supported,
          &insert_sheet_default,
          &ipp_features_supported,
          &ipp_versions_supported,
          &ippget_event_life,
          &job_account_id_default,
          &job_account_id_supported,
          &job_account_type_default,
          &job_account_type_supported,
          &job_accounting_sheets_default,
          &job_accounting_user_id_default,
          &job_accounting_user_id_supported,
          &job_authorization_uri_supported,
          &job_constraints_supported,
          &job_copies_default,
          &job_copies_supported,
          &job_cover_back_default,
          &job_cover_back_supported,
          &job_cover_front_default,
          &job_cover_front_supported,
          &job_delay_output_until_default,
          &job_delay_output_until_supported,
          &job_delay_output_until_time_supported,
          &job_error_action_default,
          &job_error_action_supported,
          &job_error_sheet_default,
          &job_finishings_col_default,
          &job_finishings_col_ready,
          &job_finishings_default,
          &job_finishings_ready,
          &job_finishings_supported,
          &job_hold_until_default,
          &job_hold_until_supported,
          &job_hold_until_time_supported,
          &job_ids_supported,
          &job_impressions_supported,
          &job_k_octets_supported,
          &job_media_sheets_supported,
          &job_message_to_operator_default,
          &job_message_to_operator_supported,
          &job_pages_per_set_supported,
          &job_password_encryption_supported,
          &job_password_supported,
          &job_phone_number_default,
          &job_phone_number_supported,
          &job_priority_default,
          &job_priority_supported,
          &job_recipient_name_default,
          &job_recipient_name_supported,
          &job_resolvers_supported,
          &job_sheet_message_default,
          &job_sheet_message_supported,
          &job_sheets_col_default,
          &job_sheets_default,
          &job_sheets_supported,
          &job_spooling_supported,
          &jpeg_k_octets_supported,
          &jpeg_x_dimension_supported,
          &jpeg_y_dimension_supported,
          &laminating_sides_supported,
          &laminating_type_supported,
          &max_save_info_supported,
          &max_stitching_locations_supported,
          &media_back_coating_supported,
          &media_bottom_margin_supported,
          &media_col_database,
          &media_col_default,
          &media_col_ready,
          &media_color_supported,
          &media_default,
          &media_front_coating_supported,
          &media_grain_supported,
          &media_hole_count_supported,
          &media_info_supported,
          &media_left_margin_supported,
          &media_order_count_supported,
          &media_pre_printed_supported,
          &media_ready,
          &media_recycled_supported,
          &media_right_margin_supported,
          &media_size_supported,
          &media_source_supported,
          &media_supported,
          &media_thickness_supported,
          &media_tooth_supported,
          &media_top_margin_supported,
          &media_type_supported,
          &media_weight_metric_supported,
          &multiple_document_handling_default,
          &multiple_document_handling_supported,
          &multiple_document_jobs_supported,
          &multiple_operation_time_out,
          &multiple_operation_time_out_action,
          &natural_language_configured,
          &notify_events_default,
          &notify_events_supported,
          &notify_lease_duration_default,
          &notify_lease_duration_supported,
          &notify_pull_method_supported,
          &notify_schemes_supported,
          &number_up_default,
          &number_up_supported,
          &oauth_authorization_server_uri,
          &operations_supported,
          &orientation_requested_default,
          &orientation_requested_supported,
          &output_bin_default,
          &output_bin_supported,
          &output_device_supported,
          &output_device_uuid_supported,
          &page_delivery_default,
          &page_delivery_supported,
          &page_order_received_default,
          &page_order_received_supported,
          &page_ranges_supported,
          &pages_per_minute,
          &pages_per_minute_color,
          &pages_per_subset_supported,
          &parent_printers_supported,
          &pdf_k_octets_supported,
          &pdf_versions_supported,
          &pdl_init_file_default,
          &pdl_init_file_entry_supported,
          &pdl_init_file_location_supported,
          &pdl_init_file_name_subdirectory_supported,
          &pdl_init_file_name_supported,
          &pdl_init_file_supported,
          &pdl_override_supported,
          &preferred_attributes_supported,
          &presentation_direction_number_up_default,
          &presentation_direction_number_up_supported,
          &print_color_mode_default,
          &print_color_mode_supported,
          &print_content_optimize_default,
          &print_content_optimize_supported,
          &print_quality_default,
          &print_quality_supported,
          &print_rendering_intent_default,
          &print_rendering_intent_supported,
          &printer_alert,
          &printer_alert_description,
          &printer_charge_info,
          &printer_charge_info_uri,
          &printer_config_change_date_time,
          &printer_config_change_time,
          &printer_config_changes,
          &printer_contact_col,
          &printer_current_time,
          &printer_detailed_status_messages,
          &printer_device_id,
          &printer_dns_sd_name,
          &printer_driver_installer,
          &printer_finisher,
          &printer_finisher_description,
          &printer_finisher_supplies,
          &printer_finisher_supplies_description,
          &printer_geo_location,
          &printer_icc_profiles,
          &printer_icons,
          &printer_id,
          &printer_impressions_completed,
          &printer_info,
          &printer_input_tray,
          &printer_is_accepting_jobs,
          &printer_location,
          &printer_make_and_model,
          &printer_media_sheets_completed,
          &printer_message_date_time,
          &printer_message_from_operator,
          &printer_message_time,
          &printer_more_info,
          &printer_more_info_manufacturer,
          &printer_name,
          &printer_organization,
          &printer_organizational_unit,
          &printer_output_tray,
          &printer_pages_completed,
          &printer_resolution_default,
          &printer_resolution_supported,
          &printer_state,
          &printer_state_change_date_time,
          &printer_state_change_time,
          &printer_state_message,
          &printer_state_reasons,
          &printer_static_resource_directory_uri,
          &printer_static_resource_k_octets_free,
          &printer_static_resource_k_octets_supported,
          &printer_strings_languages_supported,
          &printer_strings_uri,
          &printer_supply,
          &printer_supply_description,
          &printer_supply_info_uri,
          &printer_up_time,
          &printer_uri_supported,
          &printer_uuid,
          &printer_xri_supported,
          &proof_print_default,
          &proof_print_supported,
          &punching_hole_diameter_configured,
          &punching_locations_supported,
          &punching_offset_supported,
          &punching_reference_edge_supported,
          &pwg_raster_document_resolution_supported,
          &pwg_raster_document_sheet_back,
          &pwg_raster_document_type_supported,
          &queued_job_count,
          &reference_uri_schemes_supported,
          &requesting_user_uri_supported,
          &save_disposition_supported,
          &save_document_format_default,
          &save_document_format_supported,
          &save_location_default,
          &save_location_supported,
          &save_name_subdirectory_supported,
          &save_name_supported,
          &separator_sheets_default,
          &sheet_collate_default,
          &sheet_collate_supported,
          &sides_default,
          &sides_supported,
          &stitching_angle_supported,
          &stitching_locations_supported,
          &stitching_method_supported,
          &stitching_offset_supported,
          &stitching_reference_edge_supported,
          &subordinate_printers_supported,
          &trimming_offset_supported,
          &trimming_reference_edge_supported,
          &trimming_type_supported,
          &trimming_when_supported,
          &uri_authentication_supported,
          &uri_security_supported,
          &which_jobs_supported,
          &x_image_position_default,
          &x_image_position_supported,
          &x_image_shift_default,
          &x_image_shift_supported,
          &x_side1_image_shift_default,
          &x_side1_image_shift_supported,
          &x_side2_image_shift_default,
          &x_side2_image_shift_supported,
          &xri_authentication_supported,
          &xri_security_supported,
          &xri_uri_scheme_supported,
          &y_image_position_default,
          &y_image_position_supported,
          &y_image_shift_default,
          &y_image_shift_supported,
          &y_side1_image_shift_default,
          &y_side1_image_shift_supported,
          &y_side2_image_shift_default,
          &y_side2_image_shift_supported};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::GetKnownAttributes()
    const {
  return {&baling_type_supported,
          &baling_when_supported,
          &binding_reference_edge_supported,
          &binding_type_supported,
          &charset_configured,
          &charset_supported,
          &coating_sides_supported,
          &coating_type_supported,
          &color_supported,
          &compression_supported,
          &copies_default,
          &copies_supported,
          &cover_back_default,
          &cover_back_supported,
          &cover_front_default,
          &cover_front_supported,
          &covering_name_supported,
          &device_service_count,
          &device_uuid,
          &document_charset_default,
          &document_charset_supported,
          &document_digital_signature_default,
          &document_digital_signature_supported,
          &document_format_default,
          &document_format_details_default,
          &document_format_details_supported,
          &document_format_supported,
          &document_format_varying_attributes,
          &document_format_version_default,
          &document_format_version_supported,
          &document_natural_language_default,
          &document_natural_language_supported,
          &document_password_supported,
          &feed_orientation_supported,
          &finishing_template_supported,
          &finishings_col_database,
          &finishings_col_default,
          &finishings_col_ready,
          &finishings_default,
          &finishings_ready,
          &finishings_supported,
          &folding_direction_supported,
          &folding_offset_supported,
          &folding_reference_edge_supported,
          &font_name_requested_default,
          &font_name_requested_supported,
          &font_size_requested_default,
          &font_size_requested_supported,
          &generated_natural_language_supported,
          &identify_actions_default,
          &identify_actions_supported,
          &insert_after_page_number_supported,
          &insert_count_supported,
          &insert_sheet_default,
          &ipp_features_supported,
          &ipp_versions_supported,
          &ippget_event_life,
          &job_account_id_default,
          &job_account_id_supported,
          &job_account_type_default,
          &job_account_type_supported,
          &job_accounting_sheets_default,
          &job_accounting_user_id_default,
          &job_accounting_user_id_supported,
          &job_authorization_uri_supported,
          &job_constraints_supported,
          &job_copies_default,
          &job_copies_supported,
          &job_cover_back_default,
          &job_cover_back_supported,
          &job_cover_front_default,
          &job_cover_front_supported,
          &job_delay_output_until_default,
          &job_delay_output_until_supported,
          &job_delay_output_until_time_supported,
          &job_error_action_default,
          &job_error_action_supported,
          &job_error_sheet_default,
          &job_finishings_col_default,
          &job_finishings_col_ready,
          &job_finishings_default,
          &job_finishings_ready,
          &job_finishings_supported,
          &job_hold_until_default,
          &job_hold_until_supported,
          &job_hold_until_time_supported,
          &job_ids_supported,
          &job_impressions_supported,
          &job_k_octets_supported,
          &job_media_sheets_supported,
          &job_message_to_operator_default,
          &job_message_to_operator_supported,
          &job_pages_per_set_supported,
          &job_password_encryption_supported,
          &job_password_supported,
          &job_phone_number_default,
          &job_phone_number_supported,
          &job_priority_default,
          &job_priority_supported,
          &job_recipient_name_default,
          &job_recipient_name_supported,
          &job_resolvers_supported,
          &job_sheet_message_default,
          &job_sheet_message_supported,
          &job_sheets_col_default,
          &job_sheets_default,
          &job_sheets_supported,
          &job_spooling_supported,
          &jpeg_k_octets_supported,
          &jpeg_x_dimension_supported,
          &jpeg_y_dimension_supported,
          &laminating_sides_supported,
          &laminating_type_supported,
          &max_save_info_supported,
          &max_stitching_locations_supported,
          &media_back_coating_supported,
          &media_bottom_margin_supported,
          &media_col_database,
          &media_col_default,
          &media_col_ready,
          &media_color_supported,
          &media_default,
          &media_front_coating_supported,
          &media_grain_supported,
          &media_hole_count_supported,
          &media_info_supported,
          &media_left_margin_supported,
          &media_order_count_supported,
          &media_pre_printed_supported,
          &media_ready,
          &media_recycled_supported,
          &media_right_margin_supported,
          &media_size_supported,
          &media_source_supported,
          &media_supported,
          &media_thickness_supported,
          &media_tooth_supported,
          &media_top_margin_supported,
          &media_type_supported,
          &media_weight_metric_supported,
          &multiple_document_handling_default,
          &multiple_document_handling_supported,
          &multiple_document_jobs_supported,
          &multiple_operation_time_out,
          &multiple_operation_time_out_action,
          &natural_language_configured,
          &notify_events_default,
          &notify_events_supported,
          &notify_lease_duration_default,
          &notify_lease_duration_supported,
          &notify_pull_method_supported,
          &notify_schemes_supported,
          &number_up_default,
          &number_up_supported,
          &oauth_authorization_server_uri,
          &operations_supported,
          &orientation_requested_default,
          &orientation_requested_supported,
          &output_bin_default,
          &output_bin_supported,
          &output_device_supported,
          &output_device_uuid_supported,
          &page_delivery_default,
          &page_delivery_supported,
          &page_order_received_default,
          &page_order_received_supported,
          &page_ranges_supported,
          &pages_per_minute,
          &pages_per_minute_color,
          &pages_per_subset_supported,
          &parent_printers_supported,
          &pdf_k_octets_supported,
          &pdf_versions_supported,
          &pdl_init_file_default,
          &pdl_init_file_entry_supported,
          &pdl_init_file_location_supported,
          &pdl_init_file_name_subdirectory_supported,
          &pdl_init_file_name_supported,
          &pdl_init_file_supported,
          &pdl_override_supported,
          &preferred_attributes_supported,
          &presentation_direction_number_up_default,
          &presentation_direction_number_up_supported,
          &print_color_mode_default,
          &print_color_mode_supported,
          &print_content_optimize_default,
          &print_content_optimize_supported,
          &print_quality_default,
          &print_quality_supported,
          &print_rendering_intent_default,
          &print_rendering_intent_supported,
          &printer_alert,
          &printer_alert_description,
          &printer_charge_info,
          &printer_charge_info_uri,
          &printer_config_change_date_time,
          &printer_config_change_time,
          &printer_config_changes,
          &printer_contact_col,
          &printer_current_time,
          &printer_detailed_status_messages,
          &printer_device_id,
          &printer_dns_sd_name,
          &printer_driver_installer,
          &printer_finisher,
          &printer_finisher_description,
          &printer_finisher_supplies,
          &printer_finisher_supplies_description,
          &printer_geo_location,
          &printer_icc_profiles,
          &printer_icons,
          &printer_id,
          &printer_impressions_completed,
          &printer_info,
          &printer_input_tray,
          &printer_is_accepting_jobs,
          &printer_location,
          &printer_make_and_model,
          &printer_media_sheets_completed,
          &printer_message_date_time,
          &printer_message_from_operator,
          &printer_message_time,
          &printer_more_info,
          &printer_more_info_manufacturer,
          &printer_name,
          &printer_organization,
          &printer_organizational_unit,
          &printer_output_tray,
          &printer_pages_completed,
          &printer_resolution_default,
          &printer_resolution_supported,
          &printer_state,
          &printer_state_change_date_time,
          &printer_state_change_time,
          &printer_state_message,
          &printer_state_reasons,
          &printer_static_resource_directory_uri,
          &printer_static_resource_k_octets_free,
          &printer_static_resource_k_octets_supported,
          &printer_strings_languages_supported,
          &printer_strings_uri,
          &printer_supply,
          &printer_supply_description,
          &printer_supply_info_uri,
          &printer_up_time,
          &printer_uri_supported,
          &printer_uuid,
          &printer_xri_supported,
          &proof_print_default,
          &proof_print_supported,
          &punching_hole_diameter_configured,
          &punching_locations_supported,
          &punching_offset_supported,
          &punching_reference_edge_supported,
          &pwg_raster_document_resolution_supported,
          &pwg_raster_document_sheet_back,
          &pwg_raster_document_type_supported,
          &queued_job_count,
          &reference_uri_schemes_supported,
          &requesting_user_uri_supported,
          &save_disposition_supported,
          &save_document_format_default,
          &save_document_format_supported,
          &save_location_default,
          &save_location_supported,
          &save_name_subdirectory_supported,
          &save_name_supported,
          &separator_sheets_default,
          &sheet_collate_default,
          &sheet_collate_supported,
          &sides_default,
          &sides_supported,
          &stitching_angle_supported,
          &stitching_locations_supported,
          &stitching_method_supported,
          &stitching_offset_supported,
          &stitching_reference_edge_supported,
          &subordinate_printers_supported,
          &trimming_offset_supported,
          &trimming_reference_edge_supported,
          &trimming_type_supported,
          &trimming_when_supported,
          &uri_authentication_supported,
          &uri_security_supported,
          &which_jobs_supported,
          &x_image_position_default,
          &x_image_position_supported,
          &x_image_shift_default,
          &x_image_shift_supported,
          &x_side1_image_shift_default,
          &x_side1_image_shift_supported,
          &x_side2_image_shift_default,
          &x_side2_image_shift_supported,
          &xri_authentication_supported,
          &xri_security_supported,
          &xri_uri_scheme_supported,
          &y_image_position_default,
          &y_image_position_supported,
          &y_image_shift_default,
          &y_image_shift_supported,
          &y_side1_image_shift_default,
          &y_side1_image_shift_supported,
          &y_side2_image_shift_default,
          &y_side2_image_shift_supported};
}
const std::map<AttrName, AttrDef>
    Response_Get_Printer_Attributes::G_printer_attributes::defs_{
        {AttrName::baling_type_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::baling_when_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::binding_reference_edge_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::binding_type_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::charset_configured,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::charset_supported,
         {AttrType::charset, InternalType::kString, true}},
        {AttrName::coating_sides_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::coating_type_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::color_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::compression_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::copies_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::copies_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::cover_back_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_cover_back_default(); }}},
        {AttrName::cover_back_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::cover_front_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_cover_front_default(); }}},
        {AttrName::cover_front_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::covering_name_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::device_service_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::device_uuid, {AttrType::uri, InternalType::kString, false}},
        {AttrName::document_charset_default,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::document_charset_supported,
         {AttrType::charset, InternalType::kString, true}},
        {AttrName::document_digital_signature_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::document_digital_signature_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::document_format_default,
         {AttrType::mimeMediaType, InternalType::kString, false}},
        {AttrName::document_format_details_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* {
            return new C_document_format_details_default();
          }}},
        {AttrName::document_format_details_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::document_format_supported,
         {AttrType::mimeMediaType, InternalType::kString, true}},
        {AttrName::document_format_varying_attributes,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::document_format_version_default,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::document_format_version_supported,
         {AttrType::text, InternalType::kStringWithLanguage, true}},
        {AttrName::document_natural_language_default,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::document_natural_language_supported,
         {AttrType::naturalLanguage, InternalType::kString, true}},
        {AttrName::document_password_supported,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::feed_orientation_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::finishing_template_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::finishings_col_database,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_finishings_col_database(); }}},
        {AttrName::finishings_col_default,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_finishings_col_default(); }}},
        {AttrName::finishings_col_ready,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_finishings_col_ready(); }}},
        {AttrName::finishings_default,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::finishings_ready,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::finishings_supported,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::folding_direction_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::folding_offset_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::folding_reference_edge_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::font_name_requested_default,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::font_name_requested_supported,
         {AttrType::name, InternalType::kStringWithLanguage, true}},
        {AttrName::font_size_requested_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::font_size_requested_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::generated_natural_language_supported,
         {AttrType::naturalLanguage, InternalType::kString, true}},
        {AttrName::identify_actions_default,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::identify_actions_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::insert_after_page_number_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::insert_count_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::insert_sheet_default,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_insert_sheet_default(); }}},
        {AttrName::ipp_features_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::ipp_versions_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::ippget_event_life,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_account_id_default,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_account_id_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_account_type_default,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_account_type_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::job_accounting_sheets_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* {
            return new C_job_accounting_sheets_default();
          }}},
        {AttrName::job_accounting_user_id_default,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_accounting_user_id_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_authorization_uri_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_constraints_supported,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_job_constraints_supported(); }}},
        {AttrName::job_copies_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_copies_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::job_cover_back_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_cover_back_default(); }}},
        {AttrName::job_cover_back_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::job_cover_front_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_cover_front_default(); }}},
        {AttrName::job_cover_front_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::job_delay_output_until_default,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_delay_output_until_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::job_delay_output_until_time_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::job_error_action_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::job_error_action_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::job_error_sheet_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_error_sheet_default(); }}},
        {AttrName::job_finishings_col_default,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_job_finishings_col_default(); }}},
        {AttrName::job_finishings_col_ready,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_job_finishings_col_ready(); }}},
        {AttrName::job_finishings_default,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::job_finishings_ready,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::job_finishings_supported,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::job_hold_until_default,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_hold_until_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::job_hold_until_time_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::job_ids_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_impressions_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::job_k_octets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::job_media_sheets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::job_message_to_operator_default,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::job_message_to_operator_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_pages_per_set_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_password_encryption_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::job_password_supported,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_phone_number_default,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::job_phone_number_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_priority_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_priority_supported,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_recipient_name_default,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_recipient_name_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_resolvers_supported,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_job_resolvers_supported(); }}},
        {AttrName::job_sheet_message_default,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::job_sheet_message_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::job_sheets_col_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_job_sheets_col_default(); }}},
        {AttrName::job_sheets_default,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_sheets_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::job_spooling_supported,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::jpeg_k_octets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::jpeg_x_dimension_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::jpeg_y_dimension_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::laminating_sides_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::laminating_type_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::max_save_info_supported,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::max_stitching_locations_supported,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_back_coating_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_bottom_margin_supported,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::media_col_database,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_media_col_database(); }}},
        {AttrName::media_col_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col_default(); }}},
        {AttrName::media_col_ready,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_media_col_ready(); }}},
        {AttrName::media_color_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_default,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_front_coating_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_grain_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_hole_count_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::media_info_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::media_left_margin_supported,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::media_order_count_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::media_pre_printed_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_ready,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_recycled_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_right_margin_supported,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::media_size_supported,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_media_size_supported(); }}},
        {AttrName::media_source_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_thickness_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::media_tooth_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_top_margin_supported,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::media_type_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::media_weight_metric_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::multiple_document_handling_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::multiple_document_handling_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::multiple_document_jobs_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::multiple_operation_time_out,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::multiple_operation_time_out_action,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::natural_language_configured,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::notify_events_default,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::notify_events_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::notify_lease_duration_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::notify_lease_duration_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::notify_pull_method_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::notify_schemes_supported,
         {AttrType::uriScheme, InternalType::kString, true}},
        {AttrName::number_up_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::number_up_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::oauth_authorization_server_uri,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::operations_supported,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::orientation_requested_default,
         {AttrType::enum_, InternalType::kInteger, false}},
        {AttrName::orientation_requested_supported,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::output_bin_default,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::output_bin_supported,
         {AttrType::keyword, InternalType::kString, true}},
        {AttrName::output_device_supported,
         {AttrType::name, InternalType::kStringWithLanguage, true}},
        {AttrName::output_device_uuid_supported,
         {AttrType::uri, InternalType::kString, true}},
        {AttrName::page_delivery_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::page_delivery_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::page_order_received_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::page_order_received_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::page_ranges_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::pages_per_minute,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::pages_per_minute_color,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::pages_per_subset_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::parent_printers_supported,
         {AttrType::uri, InternalType::kString, true}},
        {AttrName::pdf_k_octets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::pdf_versions_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::pdl_init_file_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_pdl_init_file_default(); }}},
        {AttrName::pdl_init_file_entry_supported,
         {AttrType::name, InternalType::kStringWithLanguage, true}},
        {AttrName::pdl_init_file_location_supported,
         {AttrType::uri, InternalType::kString, true}},
        {AttrName::pdl_init_file_name_subdirectory_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::pdl_init_file_name_supported,
         {AttrType::name, InternalType::kStringWithLanguage, true}},
        {AttrName::pdl_init_file_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::pdl_override_supported,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::preferred_attributes_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::presentation_direction_number_up_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::presentation_direction_number_up_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::print_color_mode_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::print_color_mode_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::print_content_optimize_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::print_content_optimize_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::print_quality_default,
         {AttrType::enum_, InternalType::kInteger, false}},
        {AttrName::print_quality_supported,
         {AttrType::enum_, InternalType::kInteger, true}},
        {AttrName::print_rendering_intent_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::print_rendering_intent_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::printer_alert,
         {AttrType::octetString, InternalType::kString, true}},
        {AttrName::printer_alert_description,
         {AttrType::text, InternalType::kStringWithLanguage, true}},
        {AttrName::printer_charge_info,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_charge_info_uri,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_config_change_date_time,
         {AttrType::dateTime, InternalType::kDateTime, false}},
        {AttrName::printer_config_change_time,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::printer_config_changes,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::printer_contact_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_printer_contact_col(); }}},
        {AttrName::printer_current_time,
         {AttrType::dateTime, InternalType::kDateTime, false}},
        {AttrName::printer_detailed_status_messages,
         {AttrType::text, InternalType::kStringWithLanguage, true}},
        {AttrName::printer_device_id,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_dns_sd_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_driver_installer,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_finisher,
         {AttrType::octetString, InternalType::kString, true}},
        {AttrName::printer_finisher_description,
         {AttrType::text, InternalType::kStringWithLanguage, true}},
        {AttrName::printer_finisher_supplies,
         {AttrType::octetString, InternalType::kString, true}},
        {AttrName::printer_finisher_supplies_description,
         {AttrType::text, InternalType::kStringWithLanguage, true}},
        {AttrName::printer_geo_location,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_icc_profiles,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_printer_icc_profiles(); }}},
        {AttrName::printer_icons, {AttrType::uri, InternalType::kString, true}},
        {AttrName::printer_id,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::printer_impressions_completed,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::printer_info,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_input_tray,
         {AttrType::octetString, InternalType::kString, true}},
        {AttrName::printer_is_accepting_jobs,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::printer_location,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_make_and_model,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_media_sheets_completed,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::printer_message_date_time,
         {AttrType::dateTime, InternalType::kDateTime, false}},
        {AttrName::printer_message_from_operator,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_message_time,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::printer_more_info,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_more_info_manufacturer,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_organization,
         {AttrType::text, InternalType::kStringWithLanguage, true}},
        {AttrName::printer_organizational_unit,
         {AttrType::text, InternalType::kStringWithLanguage, true}},
        {AttrName::printer_output_tray,
         {AttrType::octetString, InternalType::kString, true}},
        {AttrName::printer_pages_completed,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::printer_resolution_default,
         {AttrType::resolution, InternalType::kResolution, false}},
        {AttrName::printer_resolution_supported,
         {AttrType::resolution, InternalType::kResolution, false}},
        {AttrName::printer_state,
         {AttrType::enum_, InternalType::kInteger, false}},
        {AttrName::printer_state_change_date_time,
         {AttrType::dateTime, InternalType::kDateTime, false}},
        {AttrName::printer_state_change_time,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::printer_state_message,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::printer_state_reasons,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::printer_static_resource_directory_uri,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_static_resource_k_octets_free,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::printer_static_resource_k_octets_supported,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::printer_strings_languages_supported,
         {AttrType::naturalLanguage, InternalType::kString, true}},
        {AttrName::printer_strings_uri,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_supply,
         {AttrType::octetString, InternalType::kString, true}},
        {AttrName::printer_supply_description,
         {AttrType::text, InternalType::kStringWithLanguage, true}},
        {AttrName::printer_supply_info_uri,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_up_time,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::printer_uri_supported,
         {AttrType::uri, InternalType::kString, true}},
        {AttrName::printer_uuid, {AttrType::uri, InternalType::kString, false}},
        {AttrName::printer_xri_supported,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_printer_xri_supported(); }}},
        {AttrName::proof_print_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_proof_print_default(); }}},
        {AttrName::proof_print_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::punching_hole_diameter_configured,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::punching_locations_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::punching_offset_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::punching_reference_edge_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::pwg_raster_document_resolution_supported,
         {AttrType::resolution, InternalType::kResolution, true}},
        {AttrName::pwg_raster_document_sheet_back,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::pwg_raster_document_type_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::queued_job_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::reference_uri_schemes_supported,
         {AttrType::uriScheme, InternalType::kString, true}},
        {AttrName::requesting_user_uri_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::save_disposition_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::save_document_format_default,
         {AttrType::mimeMediaType, InternalType::kString, false}},
        {AttrName::save_document_format_supported,
         {AttrType::mimeMediaType, InternalType::kString, true}},
        {AttrName::save_location_default,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::save_location_supported,
         {AttrType::uri, InternalType::kString, true}},
        {AttrName::save_name_subdirectory_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::save_name_supported,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::separator_sheets_default,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_separator_sheets_default(); }}},
        {AttrName::sheet_collate_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::sheet_collate_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::sides_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::sides_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::stitching_angle_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::stitching_locations_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::stitching_method_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::stitching_offset_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::stitching_reference_edge_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::subordinate_printers_supported,
         {AttrType::uri, InternalType::kString, true}},
        {AttrName::trimming_offset_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, true}},
        {AttrName::trimming_reference_edge_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::trimming_type_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::trimming_when_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::uri_authentication_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::uri_security_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::which_jobs_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::x_image_position_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::x_image_position_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::x_image_shift_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::x_image_shift_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::x_side1_image_shift_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::x_side1_image_shift_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::x_side2_image_shift_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::x_side2_image_shift_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::xri_authentication_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::xri_security_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::xri_uri_scheme_supported,
         {AttrType::uriScheme, InternalType::kString, true}},
        {AttrName::y_image_position_default,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::y_image_position_supported,
         {AttrType::keyword, InternalType::kInteger, true}},
        {AttrName::y_image_shift_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_image_shift_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::y_side1_image_shift_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_side1_image_shift_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::y_side2_image_shift_default,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_side2_image_shift_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_cover_back_default::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_cover_back_default::GetKnownAttributes() const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_cover_back_default::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_cover_front_default::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_cover_front_default::GetKnownAttributes() const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_cover_front_default::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_document_format_details_default::GetKnownAttributes() {
  return {&document_format,
          &document_format_device_id,
          &document_format_version,
          &document_natural_language,
          &document_source_application_name,
          &document_source_application_version,
          &document_source_os_name,
          &document_source_os_version};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_document_format_details_default::GetKnownAttributes() const {
  return {&document_format,
          &document_format_device_id,
          &document_format_version,
          &document_natural_language,
          &document_source_application_name,
          &document_source_application_version,
          &document_source_os_name,
          &document_source_os_version};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_document_format_details_default::defs_{
        {AttrName::document_format,
         {AttrType::mimeMediaType, InternalType::kString, false}},
        {AttrName::document_format_device_id,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::document_format_version,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::document_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, true}},
        {AttrName::document_source_application_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::document_source_application_version,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::document_source_os_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::document_source_os_version,
         {AttrType::text, InternalType::kStringWithLanguage, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::GetKnownAttributes() {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_database::GetKnownAttributes()
        const {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_database::defs_{
        {AttrName::baling,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_baling(); }}},
        {AttrName::binding,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_binding(); }}},
        {AttrName::coating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_coating(); }}},
        {AttrName::covering,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_covering(); }}},
        {AttrName::finishing_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::folding,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_folding(); }}},
        {AttrName::imposition_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::laminating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_laminating(); }}},
        {AttrName::media_sheets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::punching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_punching(); }}},
        {AttrName::stitching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_stitching(); }}},
        {AttrName::trimming,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_trimming(); }}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_baling::GetKnownAttributes() {
  return {&baling_type, &baling_when};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_baling::GetKnownAttributes() const {
  return {&baling_type, &baling_when};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_database::C_baling::defs_{
        {AttrName::baling_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::baling_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_binding::GetKnownAttributes() {
  return {&binding_reference_edge, &binding_type};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_binding::GetKnownAttributes() const {
  return {&binding_reference_edge, &binding_type};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_database::C_binding::defs_{
        {AttrName::binding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::binding_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_coating::GetKnownAttributes() {
  return {&coating_sides, &coating_type};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_coating::GetKnownAttributes() const {
  return {&coating_sides, &coating_type};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_database::C_coating::defs_{
        {AttrName::coating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::coating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_covering::GetKnownAttributes() {
  return {&covering_name};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_covering::GetKnownAttributes() const {
  return {&covering_name};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_database::C_covering::defs_{
        {AttrName::covering_name,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_folding::GetKnownAttributes() {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_folding::GetKnownAttributes() const {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_database::C_folding::defs_{
        {AttrName::folding_direction,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::folding_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::folding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_laminating::GetKnownAttributes() {
  return {&laminating_sides, &laminating_type};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_laminating::GetKnownAttributes() const {
  return {&laminating_sides, &laminating_type};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_database::C_laminating::defs_{
        {AttrName::laminating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::laminating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_media_size::GetKnownAttributes() {
  return {};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_media_size::GetKnownAttributes() const {
  return {};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_database::C_media_size::defs_{};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_punching::GetKnownAttributes() {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_punching::GetKnownAttributes() const {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_database::C_punching::defs_{
        {AttrName::punching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::punching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::punching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_stitching::GetKnownAttributes() {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_stitching::GetKnownAttributes() const {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_database::C_stitching::defs_{
        {AttrName::stitching_angle,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::stitching_method,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::stitching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_trimming::GetKnownAttributes() {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_database::C_trimming::GetKnownAttributes() const {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_database::C_trimming::defs_{
        {AttrName::trimming_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::trimming_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::trimming_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::trimming_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::GetKnownAttributes() {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_default::GetKnownAttributes() const {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_default::defs_{
        {AttrName::baling,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_baling(); }}},
        {AttrName::binding,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_binding(); }}},
        {AttrName::coating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_coating(); }}},
        {AttrName::covering,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_covering(); }}},
        {AttrName::finishing_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::folding,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_folding(); }}},
        {AttrName::imposition_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::laminating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_laminating(); }}},
        {AttrName::media_sheets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::punching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_punching(); }}},
        {AttrName::stitching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_stitching(); }}},
        {AttrName::trimming,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_trimming(); }}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_baling::GetKnownAttributes() {
  return {&baling_type, &baling_when};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_baling::GetKnownAttributes() const {
  return {&baling_type, &baling_when};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_default::C_baling::defs_{
        {AttrName::baling_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::baling_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_binding::GetKnownAttributes() {
  return {&binding_reference_edge, &binding_type};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_binding::GetKnownAttributes() const {
  return {&binding_reference_edge, &binding_type};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_default::C_binding::defs_{
        {AttrName::binding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::binding_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_coating::GetKnownAttributes() {
  return {&coating_sides, &coating_type};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_coating::GetKnownAttributes() const {
  return {&coating_sides, &coating_type};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_default::C_coating::defs_{
        {AttrName::coating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::coating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_covering::GetKnownAttributes() {
  return {&covering_name};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_covering::GetKnownAttributes() const {
  return {&covering_name};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_default::C_covering::defs_{
        {AttrName::covering_name,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_folding::GetKnownAttributes() {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_folding::GetKnownAttributes() const {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_default::C_folding::defs_{
        {AttrName::folding_direction,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::folding_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::folding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_laminating::GetKnownAttributes() {
  return {&laminating_sides, &laminating_type};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_laminating::GetKnownAttributes() const {
  return {&laminating_sides, &laminating_type};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_default::C_laminating::defs_{
        {AttrName::laminating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::laminating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_media_size::GetKnownAttributes() {
  return {};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_media_size::GetKnownAttributes() const {
  return {};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_default::C_media_size::defs_{};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_punching::GetKnownAttributes() {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_punching::GetKnownAttributes() const {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_default::C_punching::defs_{
        {AttrName::punching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::punching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::punching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_stitching::GetKnownAttributes() {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_stitching::GetKnownAttributes() const {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_default::C_stitching::defs_{
        {AttrName::stitching_angle,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::stitching_method,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::stitching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_trimming::GetKnownAttributes() {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_default::C_trimming::GetKnownAttributes() const {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_default::C_trimming::defs_{
        {AttrName::trimming_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::trimming_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::trimming_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::trimming_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_ready::GetKnownAttributes() {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_ready::GetKnownAttributes() const {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_ready::defs_{
        {AttrName::baling,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_baling(); }}},
        {AttrName::binding,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_binding(); }}},
        {AttrName::coating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_coating(); }}},
        {AttrName::covering,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_covering(); }}},
        {AttrName::finishing_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::folding,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_folding(); }}},
        {AttrName::imposition_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::laminating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_laminating(); }}},
        {AttrName::media_sheets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::punching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_punching(); }}},
        {AttrName::stitching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_stitching(); }}},
        {AttrName::trimming,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_trimming(); }}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_ready::C_baling::GetKnownAttributes() {
  return {&baling_type, &baling_when};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_ready::C_baling::GetKnownAttributes()
        const {
  return {&baling_type, &baling_when};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_ready::C_baling::defs_{
        {AttrName::baling_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::baling_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_ready::C_binding::GetKnownAttributes() {
  return {&binding_reference_edge, &binding_type};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::C_finishings_col_ready::
    C_binding::GetKnownAttributes() const {
  return {&binding_reference_edge, &binding_type};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_ready::C_binding::defs_{
        {AttrName::binding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::binding_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_ready::C_coating::GetKnownAttributes() {
  return {&coating_sides, &coating_type};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::C_finishings_col_ready::
    C_coating::GetKnownAttributes() const {
  return {&coating_sides, &coating_type};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_ready::C_coating::defs_{
        {AttrName::coating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::coating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_ready::C_covering::GetKnownAttributes() {
  return {&covering_name};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::C_finishings_col_ready::
    C_covering::GetKnownAttributes() const {
  return {&covering_name};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_ready::C_covering::defs_{
        {AttrName::covering_name,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_ready::C_folding::GetKnownAttributes() {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::C_finishings_col_ready::
    C_folding::GetKnownAttributes() const {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_ready::C_folding::defs_{
        {AttrName::folding_direction,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::folding_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::folding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_ready::C_laminating::GetKnownAttributes() {
  return {&laminating_sides, &laminating_type};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::C_finishings_col_ready::
    C_laminating::GetKnownAttributes() const {
  return {&laminating_sides, &laminating_type};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_ready::C_laminating::defs_{
        {AttrName::laminating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::laminating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_ready::C_media_size::GetKnownAttributes() {
  return {};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::C_finishings_col_ready::
    C_media_size::GetKnownAttributes() const {
  return {};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_ready::C_media_size::defs_{};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_ready::C_punching::GetKnownAttributes() {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::C_finishings_col_ready::
    C_punching::GetKnownAttributes() const {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_ready::C_punching::defs_{
        {AttrName::punching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::punching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::punching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_ready::C_stitching::GetKnownAttributes() {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::C_finishings_col_ready::
    C_stitching::GetKnownAttributes() const {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_ready::C_stitching::defs_{
        {AttrName::stitching_angle,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::stitching_method,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::stitching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_finishings_col_ready::C_trimming::GetKnownAttributes() {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::C_finishings_col_ready::
    C_trimming::GetKnownAttributes() const {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_finishings_col_ready::C_trimming::defs_{
        {AttrName::trimming_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::trimming_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::trimming_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::trimming_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_insert_sheet_default::GetKnownAttributes() {
  return {&insert_after_page_number, &insert_count, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_insert_sheet_default::GetKnownAttributes() const {
  return {&insert_after_page_number, &insert_count, &media, &media_col};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_insert_sheet_default::defs_{
        {AttrName::insert_after_page_number,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::insert_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_accounting_sheets_default::GetKnownAttributes() {
  return {&job_accounting_output_bin, &job_accounting_sheets_type, &media,
          &media_col};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_accounting_sheets_default::GetKnownAttributes()
        const {
  return {&job_accounting_output_bin, &job_accounting_sheets_type, &media,
          &media_col};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_accounting_sheets_default::defs_{
        {AttrName::job_accounting_output_bin,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_accounting_sheets_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_constraints_supported::GetKnownAttributes() {
  return {&resolver_name};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_constraints_supported::GetKnownAttributes()
        const {
  return {&resolver_name};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_constraints_supported::defs_{
        {AttrName::resolver_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_cover_back_default::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_cover_back_default::GetKnownAttributes() const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_cover_back_default::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_cover_front_default::GetKnownAttributes() {
  return {&cover_type, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_cover_front_default::GetKnownAttributes()
        const {
  return {&cover_type, &media, &media_col};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_cover_front_default::defs_{
        {AttrName::cover_type,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_error_sheet_default::GetKnownAttributes() {
  return {&job_error_sheet_type, &job_error_sheet_when, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_error_sheet_default::GetKnownAttributes()
        const {
  return {&job_error_sheet_type, &job_error_sheet_when, &media, &media_col};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_error_sheet_default::defs_{
        {AttrName::job_error_sheet_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::job_error_sheet_when,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::GetKnownAttributes() {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_default::GetKnownAttributes()
        const {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_default::defs_{
        {AttrName::baling,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_baling(); }}},
        {AttrName::binding,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_binding(); }}},
        {AttrName::coating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_coating(); }}},
        {AttrName::covering,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_covering(); }}},
        {AttrName::finishing_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::folding,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_folding(); }}},
        {AttrName::imposition_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::laminating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_laminating(); }}},
        {AttrName::media_sheets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::punching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_punching(); }}},
        {AttrName::stitching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_stitching(); }}},
        {AttrName::trimming,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_trimming(); }}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_baling::GetKnownAttributes() {
  return {&baling_type, &baling_when};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_baling::GetKnownAttributes() const {
  return {&baling_type, &baling_when};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_default::C_baling::defs_{
        {AttrName::baling_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::baling_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_binding::GetKnownAttributes() {
  return {&binding_reference_edge, &binding_type};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_binding::GetKnownAttributes() const {
  return {&binding_reference_edge, &binding_type};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_default::C_binding::defs_{
        {AttrName::binding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::binding_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_coating::GetKnownAttributes() {
  return {&coating_sides, &coating_type};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_coating::GetKnownAttributes() const {
  return {&coating_sides, &coating_type};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_default::C_coating::defs_{
        {AttrName::coating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::coating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_covering::GetKnownAttributes() {
  return {&covering_name};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_covering::GetKnownAttributes() const {
  return {&covering_name};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_default::C_covering::defs_{
        {AttrName::covering_name,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_folding::GetKnownAttributes() {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_folding::GetKnownAttributes() const {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_default::C_folding::defs_{
        {AttrName::folding_direction,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::folding_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::folding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_laminating::GetKnownAttributes() {
  return {&laminating_sides, &laminating_type};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_laminating::GetKnownAttributes() const {
  return {&laminating_sides, &laminating_type};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_default::C_laminating::defs_{
        {AttrName::laminating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::laminating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_media_size::GetKnownAttributes() {
  return {};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_media_size::GetKnownAttributes() const {
  return {};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_default::C_media_size::defs_{};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_punching::GetKnownAttributes() {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_punching::GetKnownAttributes() const {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_default::C_punching::defs_{
        {AttrName::punching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::punching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::punching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_stitching::GetKnownAttributes() {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_stitching::GetKnownAttributes() const {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_default::C_stitching::defs_{
        {AttrName::stitching_angle,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::stitching_method,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::stitching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_trimming::GetKnownAttributes() {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_default::C_trimming::GetKnownAttributes() const {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_default::C_trimming::defs_{
        {AttrName::trimming_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::trimming_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::trimming_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::trimming_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::GetKnownAttributes() {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_ready::GetKnownAttributes()
        const {
  return {&baling,
          &binding,
          &coating,
          &covering,
          &finishing_template,
          &folding,
          &imposition_template,
          &laminating,
          &media_sheets_supported,
          &media_size,
          &media_size_name,
          &punching,
          &stitching,
          &trimming};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_ready::defs_{
        {AttrName::baling,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_baling(); }}},
        {AttrName::binding,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_binding(); }}},
        {AttrName::coating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_coating(); }}},
        {AttrName::covering,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_covering(); }}},
        {AttrName::finishing_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::folding,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_folding(); }}},
        {AttrName::imposition_template,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::laminating,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_laminating(); }}},
        {AttrName::media_sheets_supported,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::punching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_punching(); }}},
        {AttrName::stitching,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_stitching(); }}},
        {AttrName::trimming,
         {AttrType::collection, InternalType::kCollection, true,
          []() -> Collection* { return new C_trimming(); }}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_baling::GetKnownAttributes() {
  return {&baling_type, &baling_when};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_baling::GetKnownAttributes() const {
  return {&baling_type, &baling_when};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_ready::C_baling::defs_{
        {AttrName::baling_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::baling_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_binding::GetKnownAttributes() {
  return {&binding_reference_edge, &binding_type};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_binding::GetKnownAttributes() const {
  return {&binding_reference_edge, &binding_type};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_ready::C_binding::defs_{
        {AttrName::binding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::binding_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_coating::GetKnownAttributes() {
  return {&coating_sides, &coating_type};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_coating::GetKnownAttributes() const {
  return {&coating_sides, &coating_type};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_ready::C_coating::defs_{
        {AttrName::coating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::coating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_covering::GetKnownAttributes() {
  return {&covering_name};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_covering::GetKnownAttributes() const {
  return {&covering_name};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_ready::C_covering::defs_{
        {AttrName::covering_name,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_folding::GetKnownAttributes() {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_folding::GetKnownAttributes() const {
  return {&folding_direction, &folding_offset, &folding_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_ready::C_folding::defs_{
        {AttrName::folding_direction,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::folding_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::folding_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_laminating::GetKnownAttributes() {
  return {&laminating_sides, &laminating_type};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_laminating::GetKnownAttributes() const {
  return {&laminating_sides, &laminating_type};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_ready::C_laminating::defs_{
        {AttrName::laminating_sides,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::laminating_type,
         {AttrType::keyword, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_media_size::GetKnownAttributes() {
  return {};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_media_size::GetKnownAttributes() const {
  return {};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_ready::C_media_size::defs_{};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_punching::GetKnownAttributes() {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_punching::GetKnownAttributes() const {
  return {&punching_locations, &punching_offset, &punching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_ready::C_punching::defs_{
        {AttrName::punching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::punching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::punching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_stitching::GetKnownAttributes() {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_stitching::GetKnownAttributes() const {
  return {&stitching_angle, &stitching_locations, &stitching_method,
          &stitching_offset, &stitching_reference_edge};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_ready::C_stitching::defs_{
        {AttrName::stitching_angle,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_locations,
         {AttrType::integer, InternalType::kInteger, true}},
        {AttrName::stitching_method,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::stitching_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::stitching_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_trimming::GetKnownAttributes() {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_finishings_col_ready::C_trimming::GetKnownAttributes() const {
  return {&trimming_offset, &trimming_reference_edge, &trimming_type,
          &trimming_when};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_finishings_col_ready::C_trimming::defs_{
        {AttrName::trimming_offset,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::trimming_reference_edge,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::trimming_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::trimming_when,
         {AttrType::keyword, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_resolvers_supported::GetKnownAttributes() {
  return {&resolver_name};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_resolvers_supported::GetKnownAttributes()
        const {
  return {&resolver_name};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_resolvers_supported::defs_{
        {AttrName::resolver_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_job_sheets_col_default::GetKnownAttributes() {
  return {&job_sheets, &media, &media_col};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_sheets_col_default::GetKnownAttributes() const {
  return {&job_sheets, &media, &media_col};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_job_sheets_col_default::defs_{
        {AttrName::job_sheets,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_media_col_database::GetKnownAttributes() {
  return {&media_back_coating,  &media_bottom_margin,
          &media_color,         &media_front_coating,
          &media_grain,         &media_hole_count,
          &media_info,          &media_key,
          &media_left_margin,   &media_order_count,
          &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,
          &media_size_name,     &media_source,
          &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,
          &media_weight_metric, &media_source_properties};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_media_col_database::GetKnownAttributes() const {
  return {&media_back_coating,  &media_bottom_margin,
          &media_color,         &media_front_coating,
          &media_grain,         &media_hole_count,
          &media_info,          &media_key,
          &media_left_margin,   &media_order_count,
          &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,
          &media_size_name,     &media_source,
          &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,
          &media_weight_metric, &media_source_properties};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_media_col_database::defs_{
        {AttrName::media_back_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_bottom_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_color,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_front_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_grain,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_hole_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_info,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::media_key,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_left_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_order_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_pre_printed,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_recycled,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_right_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::media_source,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_thickness,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_tooth,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_top_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_weight_metric,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_source_properties,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_source_properties(); }}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_media_col_database::C_media_size::GetKnownAttributes() {
  return {&x_dimension, &y_dimension};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::C_media_col_database::
    C_media_size::GetKnownAttributes() const {
  return {&x_dimension, &y_dimension};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_media_col_database::C_media_size::defs_{
        {AttrName::x_dimension,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_dimension,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_media_col_database::C_media_source_properties::GetKnownAttributes() {
  return {&media_source_feed_direction, &media_source_feed_orientation};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::C_media_col_database::
    C_media_source_properties::GetKnownAttributes() const {
  return {&media_source_feed_direction, &media_source_feed_orientation};
}
const std::map<AttrName, AttrDef>
    Response_Get_Printer_Attributes::G_printer_attributes::
        C_media_col_database::C_media_source_properties::defs_{
            {AttrName::media_source_feed_direction,
             {AttrType::keyword, InternalType::kInteger, false}},
            {AttrName::media_source_feed_orientation,
             {AttrType::enum_, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_media_col_default::GetKnownAttributes() {
  return {&media_back_coating,  &media_bottom_margin, &media_color,
          &media_front_coating, &media_grain,         &media_hole_count,
          &media_info,          &media_key,           &media_left_margin,
          &media_order_count,   &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,          &media_size_name,
          &media_source,        &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,          &media_weight_metric};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_media_col_default::GetKnownAttributes() const {
  return {&media_back_coating,  &media_bottom_margin, &media_color,
          &media_front_coating, &media_grain,         &media_hole_count,
          &media_info,          &media_key,           &media_left_margin,
          &media_order_count,   &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,          &media_size_name,
          &media_source,        &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,          &media_weight_metric};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_media_col_default::defs_{
        {AttrName::media_back_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_bottom_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_color,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_front_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_grain,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_hole_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_info,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::media_key,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_left_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_order_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_pre_printed,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_recycled,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_right_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::media_source,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_thickness,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_tooth,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_top_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_weight_metric,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_media_col_default::C_media_size::GetKnownAttributes() {
  return {&x_dimension, &y_dimension};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::C_media_col_default::
    C_media_size::GetKnownAttributes() const {
  return {&x_dimension, &y_dimension};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_media_col_default::C_media_size::defs_{
        {AttrName::x_dimension,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_dimension,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_media_col_ready::GetKnownAttributes() {
  return {&media_back_coating,  &media_bottom_margin,
          &media_color,         &media_front_coating,
          &media_grain,         &media_hole_count,
          &media_info,          &media_key,
          &media_left_margin,   &media_order_count,
          &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,
          &media_size_name,     &media_source,
          &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,
          &media_weight_metric, &media_source_properties};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_media_col_ready::GetKnownAttributes() const {
  return {&media_back_coating,  &media_bottom_margin,
          &media_color,         &media_front_coating,
          &media_grain,         &media_hole_count,
          &media_info,          &media_key,
          &media_left_margin,   &media_order_count,
          &media_pre_printed,   &media_recycled,
          &media_right_margin,  &media_size,
          &media_size_name,     &media_source,
          &media_thickness,     &media_tooth,
          &media_top_margin,    &media_type,
          &media_weight_metric, &media_source_properties};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_media_col_ready::defs_{
        {AttrName::media_back_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_bottom_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_color,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_front_coating,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_grain,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_hole_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_info,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::media_key,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_left_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_order_count,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_pre_printed,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_recycled,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_right_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_size,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_size(); }}},
        {AttrName::media_size_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::media_source,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_thickness,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_tooth,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_top_margin,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_type,
         {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_weight_metric,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::media_source_properties,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_source_properties(); }}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_media_col_ready::C_media_size::GetKnownAttributes() {
  return {&x_dimension, &y_dimension};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_media_col_ready::C_media_size::GetKnownAttributes()
        const {
  return {&x_dimension, &y_dimension};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_media_col_ready::C_media_size::defs_{
        {AttrName::x_dimension,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::y_dimension,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_media_col_ready::C_media_source_properties::GetKnownAttributes() {
  return {&media_source_feed_direction, &media_source_feed_orientation};
}
std::vector<const Attribute*>
Response_Get_Printer_Attributes::G_printer_attributes::C_media_col_ready::
    C_media_source_properties::GetKnownAttributes() const {
  return {&media_source_feed_direction, &media_source_feed_orientation};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_media_col_ready::C_media_source_properties::defs_{
        {AttrName::media_source_feed_direction,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::media_source_feed_orientation,
         {AttrType::enum_, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_media_size_supported::GetKnownAttributes() {
  return {&x_dimension, &y_dimension};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_media_size_supported::GetKnownAttributes() const {
  return {&x_dimension, &y_dimension};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_media_size_supported::defs_{
        {AttrName::x_dimension,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}},
        {AttrName::y_dimension,
         {AttrType::rangeOfInteger, InternalType::kRangeOfInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_pdl_init_file_default::GetKnownAttributes() {
  return {&pdl_init_file_entry, &pdl_init_file_location, &pdl_init_file_name};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_pdl_init_file_default::GetKnownAttributes() const {
  return {&pdl_init_file_entry, &pdl_init_file_location, &pdl_init_file_name};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_pdl_init_file_default::defs_{
        {AttrName::pdl_init_file_entry,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::pdl_init_file_location,
         {AttrType::uri, InternalType::kString, false}},
        {AttrName::pdl_init_file_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_printer_contact_col::GetKnownAttributes() {
  return {&contact_name, &contact_uri, &contact_vcard};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_printer_contact_col::GetKnownAttributes() const {
  return {&contact_name, &contact_uri, &contact_vcard};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_printer_contact_col::defs_{
        {AttrName::contact_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::contact_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::contact_vcard,
         {AttrType::text, InternalType::kStringWithLanguage, true}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_printer_icc_profiles::GetKnownAttributes() {
  return {&profile_name, &profile_url};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_printer_icc_profiles::GetKnownAttributes() const {
  return {&profile_name, &profile_url};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_printer_icc_profiles::defs_{
        {AttrName::profile_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::profile_url, {AttrType::uri, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_printer_xri_supported::GetKnownAttributes() {
  return {&xri_authentication, &xri_security, &xri_uri};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_printer_xri_supported::GetKnownAttributes() const {
  return {&xri_authentication, &xri_security, &xri_uri};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_printer_xri_supported::defs_{
        {AttrName::xri_authentication,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::xri_security,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::xri_uri, {AttrType::uri, InternalType::kString, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_proof_print_default::GetKnownAttributes() {
  return {&media, &media_col, &proof_print_copies};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_proof_print_default::GetKnownAttributes() const {
  return {&media, &media_col, &proof_print_copies};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_proof_print_default::defs_{
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}},
        {AttrName::proof_print_copies,
         {AttrType::integer, InternalType::kInteger, false}}};
std::vector<Attribute*> Response_Get_Printer_Attributes::G_printer_attributes::
    C_separator_sheets_default::GetKnownAttributes() {
  return {&media, &media_col, &separator_sheets_type};
}
std::vector<const Attribute*> Response_Get_Printer_Attributes::
    G_printer_attributes::C_separator_sheets_default::GetKnownAttributes()
        const {
  return {&media, &media_col, &separator_sheets_type};
}
const std::map<AttrName, AttrDef> Response_Get_Printer_Attributes::
    G_printer_attributes::C_separator_sheets_default::defs_{
        {AttrName::media, {AttrType::keyword, InternalType::kString, false}},
        {AttrName::media_col,
         {AttrType::collection, InternalType::kCollection, false,
          []() -> Collection* { return new C_media_col(); }}},
        {AttrName::separator_sheets_type,
         {AttrType::keyword, InternalType::kInteger, true}}};
Request_Hold_Job::Request_Hold_Job() : Request(Operation::Hold_Job) {}
std::vector<Group*> Request_Hold_Job::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Request_Hold_Job::GetKnownGroups() const {
  return {&operation_attributes};
}
std::vector<Attribute*>
Request_Hold_Job::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset,
          &attributes_natural_language,
          &printer_uri,
          &job_id,
          &job_uri,
          &requesting_user_name,
          &message,
          &job_hold_until};
}
std::vector<const Attribute*>
Request_Hold_Job::G_operation_attributes::GetKnownAttributes() const {
  return {&attributes_charset,
          &attributes_natural_language,
          &printer_uri,
          &job_id,
          &job_uri,
          &requesting_user_name,
          &message,
          &job_hold_until};
}
const std::map<AttrName, AttrDef>
    Request_Hold_Job::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::printer_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::job_id, {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::requesting_user_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::message,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::job_hold_until,
         {AttrType::keyword, InternalType::kString, false}}};
Response_Hold_Job::Response_Hold_Job() : Response(Operation::Hold_Job) {}
std::vector<Group*> Response_Hold_Job::GetKnownGroups() {
  return {&operation_attributes, &unsupported_attributes};
}
std::vector<const Group*> Response_Hold_Job::GetKnownGroups() const {
  return {&operation_attributes, &unsupported_attributes};
}
Request_Pause_Printer::Request_Pause_Printer()
    : Request(Operation::Pause_Printer) {}
std::vector<Group*> Request_Pause_Printer::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Request_Pause_Printer::GetKnownGroups() const {
  return {&operation_attributes};
}
std::vector<Attribute*>
Request_Pause_Printer::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset, &attributes_natural_language, &printer_uri,
          &requesting_user_name};
}
std::vector<const Attribute*>
Request_Pause_Printer::G_operation_attributes::GetKnownAttributes() const {
  return {&attributes_charset, &attributes_natural_language, &printer_uri,
          &requesting_user_name};
}
const std::map<AttrName, AttrDef>
    Request_Pause_Printer::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::printer_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::requesting_user_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}}};
Response_Pause_Printer::Response_Pause_Printer()
    : Response(Operation::Pause_Printer) {}
std::vector<Group*> Response_Pause_Printer::GetKnownGroups() {
  return {&operation_attributes, &unsupported_attributes};
}
std::vector<const Group*> Response_Pause_Printer::GetKnownGroups() const {
  return {&operation_attributes, &unsupported_attributes};
}
Request_Print_Job::Request_Print_Job() : Request(Operation::Print_Job) {}
std::vector<Group*> Request_Print_Job::GetKnownGroups() {
  return {&operation_attributes, &job_attributes};
}
std::vector<const Group*> Request_Print_Job::GetKnownGroups() const {
  return {&operation_attributes, &job_attributes};
}
std::vector<Attribute*>
Request_Print_Job::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset, &attributes_natural_language,
          &printer_uri,        &requesting_user_name,
          &job_name,           &ipp_attribute_fidelity,
          &document_name,      &compression,
          &document_format,    &document_natural_language,
          &job_k_octets,       &job_impressions,
          &job_media_sheets};
}
std::vector<const Attribute*>
Request_Print_Job::G_operation_attributes::GetKnownAttributes() const {
  return {&attributes_charset, &attributes_natural_language,
          &printer_uri,        &requesting_user_name,
          &job_name,           &ipp_attribute_fidelity,
          &document_name,      &compression,
          &document_format,    &document_natural_language,
          &job_k_octets,       &job_impressions,
          &job_media_sheets};
}
const std::map<AttrName, AttrDef>
    Request_Print_Job::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::printer_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::requesting_user_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::ipp_attribute_fidelity,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::document_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::compression,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::document_format,
         {AttrType::mimeMediaType, InternalType::kString, false}},
        {AttrName::document_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::job_k_octets,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_impressions,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_media_sheets,
         {AttrType::integer, InternalType::kInteger, false}}};
Response_Print_Job::Response_Print_Job() : Response(Operation::Print_Job) {}
std::vector<Group*> Response_Print_Job::GetKnownGroups() {
  return {&operation_attributes, &unsupported_attributes, &job_attributes};
}
std::vector<const Group*> Response_Print_Job::GetKnownGroups() const {
  return {&operation_attributes, &unsupported_attributes, &job_attributes};
}
Request_Print_URI::Request_Print_URI() : Request(Operation::Print_URI) {}
std::vector<Group*> Request_Print_URI::GetKnownGroups() {
  return {&operation_attributes, &job_attributes};
}
std::vector<const Group*> Request_Print_URI::GetKnownGroups() const {
  return {&operation_attributes, &job_attributes};
}
std::vector<Attribute*>
Request_Print_URI::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset,
          &attributes_natural_language,
          &printer_uri,
          &document_uri,
          &requesting_user_name,
          &job_name,
          &ipp_attribute_fidelity,
          &document_name,
          &compression,
          &document_format,
          &document_natural_language,
          &job_k_octets,
          &job_impressions,
          &job_media_sheets};
}
std::vector<const Attribute*>
Request_Print_URI::G_operation_attributes::GetKnownAttributes() const {
  return {&attributes_charset,
          &attributes_natural_language,
          &printer_uri,
          &document_uri,
          &requesting_user_name,
          &job_name,
          &ipp_attribute_fidelity,
          &document_name,
          &compression,
          &document_format,
          &document_natural_language,
          &job_k_octets,
          &job_impressions,
          &job_media_sheets};
}
const std::map<AttrName, AttrDef>
    Request_Print_URI::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::printer_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::document_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::requesting_user_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::job_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::ipp_attribute_fidelity,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::document_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::compression,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::document_format,
         {AttrType::mimeMediaType, InternalType::kString, false}},
        {AttrName::document_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::job_k_octets,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_impressions,
         {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_media_sheets,
         {AttrType::integer, InternalType::kInteger, false}}};
Response_Print_URI::Response_Print_URI() : Response(Operation::Print_URI) {}
std::vector<Group*> Response_Print_URI::GetKnownGroups() {
  return {&operation_attributes, &unsupported_attributes, &job_attributes};
}
std::vector<const Group*> Response_Print_URI::GetKnownGroups() const {
  return {&operation_attributes, &unsupported_attributes, &job_attributes};
}
std::vector<Attribute*>
Response_Print_URI::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset, &attributes_natural_language, &status_message,
          &detailed_status_message, &document_access_error};
}
std::vector<const Attribute*>
Response_Print_URI::G_operation_attributes::GetKnownAttributes() const {
  return {&attributes_charset, &attributes_natural_language, &status_message,
          &detailed_status_message, &document_access_error};
}
const std::map<AttrName, AttrDef>
    Response_Print_URI::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::status_message,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::detailed_status_message,
         {AttrType::text, InternalType::kStringWithLanguage, false}},
        {AttrName::document_access_error,
         {AttrType::text, InternalType::kStringWithLanguage, false}}};
Request_Release_Job::Request_Release_Job() : Request(Operation::Release_Job) {}
std::vector<Group*> Request_Release_Job::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Request_Release_Job::GetKnownGroups() const {
  return {&operation_attributes};
}
Response_Release_Job::Response_Release_Job()
    : Response(Operation::Release_Job) {}
std::vector<Group*> Response_Release_Job::GetKnownGroups() {
  return {&operation_attributes, &unsupported_attributes};
}
std::vector<const Group*> Response_Release_Job::GetKnownGroups() const {
  return {&operation_attributes, &unsupported_attributes};
}
Request_Resume_Printer::Request_Resume_Printer()
    : Request(Operation::Resume_Printer) {}
std::vector<Group*> Request_Resume_Printer::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Request_Resume_Printer::GetKnownGroups() const {
  return {&operation_attributes};
}
Response_Resume_Printer::Response_Resume_Printer()
    : Response(Operation::Resume_Printer) {}
std::vector<Group*> Response_Resume_Printer::GetKnownGroups() {
  return {&operation_attributes, &unsupported_attributes};
}
std::vector<const Group*> Response_Resume_Printer::GetKnownGroups() const {
  return {&operation_attributes, &unsupported_attributes};
}
Request_Send_Document::Request_Send_Document()
    : Request(Operation::Send_Document) {}
std::vector<Group*> Request_Send_Document::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Request_Send_Document::GetKnownGroups() const {
  return {&operation_attributes};
}
std::vector<Attribute*>
Request_Send_Document::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset,
          &attributes_natural_language,
          &printer_uri,
          &job_id,
          &job_uri,
          &requesting_user_name,
          &document_name,
          &compression,
          &document_format,
          &document_natural_language,
          &last_document};
}
std::vector<const Attribute*>
Request_Send_Document::G_operation_attributes::GetKnownAttributes() const {
  return {&attributes_charset,
          &attributes_natural_language,
          &printer_uri,
          &job_id,
          &job_uri,
          &requesting_user_name,
          &document_name,
          &compression,
          &document_format,
          &document_natural_language,
          &last_document};
}
const std::map<AttrName, AttrDef>
    Request_Send_Document::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::printer_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::job_id, {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::requesting_user_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::document_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::compression,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::document_format,
         {AttrType::mimeMediaType, InternalType::kString, false}},
        {AttrName::document_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::last_document,
         {AttrType::boolean, InternalType::kInteger, false}}};
Response_Send_Document::Response_Send_Document()
    : Response(Operation::Send_Document) {}
std::vector<Group*> Response_Send_Document::GetKnownGroups() {
  return {&operation_attributes, &unsupported_attributes, &job_attributes};
}
std::vector<const Group*> Response_Send_Document::GetKnownGroups() const {
  return {&operation_attributes, &unsupported_attributes, &job_attributes};
}
Request_Send_URI::Request_Send_URI() : Request(Operation::Send_URI) {}
std::vector<Group*> Request_Send_URI::GetKnownGroups() {
  return {&operation_attributes};
}
std::vector<const Group*> Request_Send_URI::GetKnownGroups() const {
  return {&operation_attributes};
}
std::vector<Attribute*>
Request_Send_URI::G_operation_attributes::GetKnownAttributes() {
  return {&attributes_charset,
          &attributes_natural_language,
          &printer_uri,
          &job_id,
          &job_uri,
          &requesting_user_name,
          &document_name,
          &compression,
          &document_format,
          &document_natural_language,
          &last_document,
          &document_uri};
}
std::vector<const Attribute*>
Request_Send_URI::G_operation_attributes::GetKnownAttributes() const {
  return {&attributes_charset,
          &attributes_natural_language,
          &printer_uri,
          &job_id,
          &job_uri,
          &requesting_user_name,
          &document_name,
          &compression,
          &document_format,
          &document_natural_language,
          &last_document,
          &document_uri};
}
const std::map<AttrName, AttrDef>
    Request_Send_URI::G_operation_attributes::defs_{
        {AttrName::attributes_charset,
         {AttrType::charset, InternalType::kString, false}},
        {AttrName::attributes_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::printer_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::job_id, {AttrType::integer, InternalType::kInteger, false}},
        {AttrName::job_uri, {AttrType::uri, InternalType::kString, false}},
        {AttrName::requesting_user_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::document_name,
         {AttrType::name, InternalType::kStringWithLanguage, false}},
        {AttrName::compression,
         {AttrType::keyword, InternalType::kInteger, false}},
        {AttrName::document_format,
         {AttrType::mimeMediaType, InternalType::kString, false}},
        {AttrName::document_natural_language,
         {AttrType::naturalLanguage, InternalType::kString, false}},
        {AttrName::last_document,
         {AttrType::boolean, InternalType::kInteger, false}},
        {AttrName::document_uri,
         {AttrType::uri, InternalType::kString, false}}};
Response_Send_URI::Response_Send_URI() : Response(Operation::Send_URI) {}
std::vector<Group*> Response_Send_URI::GetKnownGroups() {
  return {&operation_attributes, &unsupported_attributes, &job_attributes};
}
std::vector<const Group*> Response_Send_URI::GetKnownGroups() const {
  return {&operation_attributes, &unsupported_attributes, &job_attributes};
}
Request_Validate_Job::Request_Validate_Job()
    : Request(Operation::Validate_Job) {}
std::vector<Group*> Request_Validate_Job::GetKnownGroups() {
  return {&operation_attributes, &job_attributes};
}
std::vector<const Group*> Request_Validate_Job::GetKnownGroups() const {
  return {&operation_attributes, &job_attributes};
}
Response_Validate_Job::Response_Validate_Job()
    : Response(Operation::Validate_Job) {}
std::vector<Group*> Response_Validate_Job::GetKnownGroups() {
  return {&operation_attributes, &unsupported_attributes};
}
std::vector<const Group*> Response_Validate_Job::GetKnownGroups() const {
  return {&operation_attributes, &unsupported_attributes};
}
}  // namespace ipp
