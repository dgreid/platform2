// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBIPP_IPP_OPERATIONS_H_
#define LIBIPP_IPP_OPERATIONS_H_

#include <map>
#include <string>
#include <vector>

#include "ipp_attribute.h"
#include "ipp_base.h"
#include "ipp_collections.h"
#include "ipp_enums.h"
#include "ipp_export.h"
#include "ipp_package.h"

// This file contains definition of classes corresponding to supported IPP
// operations. See ipp.h for more details.
// This file was generated from IPP schema based on the following sources:
// * [rfc8011]
// * [CUPS Implementation of IPP] at https://www.cups.org/doc/spec-ipp.html
// * [IPP registry] at https://www.pwg.org/ipp/ipp-registrations.xml

namespace ipp {
struct IPP_EXPORT Request_CUPS_Add_Modify_Class : public Request {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<std::string> printer_uri{this, AttrName::printer_uri};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  struct G_printer_attributes : public Collection {
    SetOfValues<E_auth_info_required> auth_info_required{
        this, AttrName::auth_info_required};
    SetOfValues<std::string> member_uris{this, AttrName::member_uris};
    SingleValue<bool> printer_is_accepting_jobs{
        this, AttrName::printer_is_accepting_jobs};
    SingleValue<StringWithLanguage> printer_info{this, AttrName::printer_info};
    SingleValue<StringWithLanguage> printer_location{
        this, AttrName::printer_location};
    SingleValue<std::string> printer_more_info{this,
                                               AttrName::printer_more_info};
    SingleValue<E_printer_state> printer_state{this, AttrName::printer_state};
    SingleValue<StringWithLanguage> printer_state_message{
        this, AttrName::printer_state_message};
    SetOfValues<StringWithLanguage> requesting_user_name_allowed{
        this, AttrName::requesting_user_name_allowed};
    SetOfValues<StringWithLanguage> requesting_user_name_denied{
        this, AttrName::requesting_user_name_denied};
    G_printer_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_printer_attributes> printer_attributes{
      GroupTag::printer_attributes};
  Request_CUPS_Add_Modify_Class();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_CUPS_Add_Modify_Class : public Response {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<StringWithLanguage> status_message{this,
                                                   AttrName::status_message};
    SingleValue<StringWithLanguage> detailed_status_message{
        this, AttrName::detailed_status_message};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Response_CUPS_Add_Modify_Class();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_CUPS_Add_Modify_Printer : public Request {
  typedef Request_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  struct G_printer_attributes : public Collection {
    SetOfValues<E_auth_info_required> auth_info_required{
        this, AttrName::auth_info_required};
    SingleValue<E_job_sheets_default> job_sheets_default{
        this, AttrName::job_sheets_default};
    SingleValue<std::string> device_uri{this, AttrName::device_uri};
    SingleValue<bool> printer_is_accepting_jobs{
        this, AttrName::printer_is_accepting_jobs};
    SingleValue<StringWithLanguage> printer_info{this, AttrName::printer_info};
    SingleValue<StringWithLanguage> printer_location{
        this, AttrName::printer_location};
    SingleValue<std::string> printer_more_info{this,
                                               AttrName::printer_more_info};
    SingleValue<E_printer_state> printer_state{this, AttrName::printer_state};
    SingleValue<StringWithLanguage> printer_state_message{
        this, AttrName::printer_state_message};
    SetOfValues<StringWithLanguage> requesting_user_name_allowed{
        this, AttrName::requesting_user_name_allowed};
    SetOfValues<StringWithLanguage> requesting_user_name_denied{
        this, AttrName::requesting_user_name_denied};
    G_printer_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_printer_attributes> printer_attributes{
      GroupTag::printer_attributes};
  Request_CUPS_Add_Modify_Printer();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_CUPS_Add_Modify_Printer : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Response_CUPS_Add_Modify_Printer();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_CUPS_Authenticate_Job : public Request {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<std::string> printer_uri{this, AttrName::printer_uri};
    SingleValue<int> job_id{this, AttrName::job_id};
    SingleValue<std::string> job_uri{this, AttrName::job_uri};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  struct G_job_attributes : public Collection {
    SetOfValues<StringWithLanguage> auth_info{this, AttrName::auth_info};
    SingleValue<E_job_hold_until> job_hold_until{this,
                                                 AttrName::job_hold_until};
    G_job_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_job_attributes> job_attributes{GroupTag::job_attributes};
  Request_CUPS_Authenticate_Job();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_CUPS_Authenticate_Job : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  struct G_unsupported_attributes : public Collection {
    G_unsupported_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_unsupported_attributes> unsupported_attributes{
      GroupTag::unsupported_attributes};
  Response_CUPS_Authenticate_Job();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_CUPS_Create_Local_Printer : public Request {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  struct G_printer_attributes : public Collection {
    SingleValue<StringWithLanguage> printer_name{this, AttrName::printer_name};
    SingleValue<std::string> device_uri{this, AttrName::device_uri};
    SingleValue<StringWithLanguage> printer_device_id{
        this, AttrName::printer_device_id};
    SingleValue<std::string> printer_geo_location{
        this, AttrName::printer_geo_location};
    SingleValue<StringWithLanguage> printer_info{this, AttrName::printer_info};
    SingleValue<StringWithLanguage> printer_location{
        this, AttrName::printer_location};
    G_printer_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_printer_attributes> printer_attributes{
      GroupTag::printer_attributes};
  Request_CUPS_Create_Local_Printer();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_CUPS_Create_Local_Printer : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  struct G_printer_attributes : public Collection {
    SingleValue<int> printer_id{this, AttrName::printer_id};
    SingleValue<bool> printer_is_accepting_jobs{
        this, AttrName::printer_is_accepting_jobs};
    SingleValue<E_printer_state> printer_state{this, AttrName::printer_state};
    SetOfValues<E_printer_state_reasons> printer_state_reasons{
        this, AttrName::printer_state_reasons};
    SetOfValues<std::string> printer_uri_supported{
        this, AttrName::printer_uri_supported};
    G_printer_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_printer_attributes> printer_attributes{
      GroupTag::printer_attributes};
  Response_CUPS_Create_Local_Printer();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_CUPS_Delete_Class : public Request {
  typedef Request_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Request_CUPS_Delete_Class();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_CUPS_Delete_Class : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Response_CUPS_Delete_Class();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_CUPS_Delete_Printer : public Request {
  typedef Request_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Request_CUPS_Delete_Printer();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_CUPS_Delete_Printer : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Response_CUPS_Delete_Printer();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_CUPS_Get_Classes : public Request {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<StringWithLanguage> first_printer_name{
        this, AttrName::first_printer_name};
    SingleValue<int> limit{this, AttrName::limit};
    SingleValue<StringWithLanguage> printer_location{
        this, AttrName::printer_location};
    SingleValue<int> printer_type{this, AttrName::printer_type};
    SingleValue<int> printer_type_mask{this, AttrName::printer_type_mask};
    OpenSetOfValues<E_requested_attributes> requested_attributes{
        this, AttrName::requested_attributes};
    SingleValue<StringWithLanguage> requested_user_name{
        this, AttrName::requested_user_name};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Request_CUPS_Get_Classes();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_CUPS_Get_Classes : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  struct G_printer_attributes : public Collection {
    SetOfValues<StringWithLanguage> member_names{this, AttrName::member_names};
    SetOfValues<std::string> member_uris{this, AttrName::member_uris};
    G_printer_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_printer_attributes> printer_attributes{
      GroupTag::printer_attributes};
  Response_CUPS_Get_Classes();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_CUPS_Get_Default : public Request {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    OpenSetOfValues<E_requested_attributes> requested_attributes{
        this, AttrName::requested_attributes};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Request_CUPS_Get_Default();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_CUPS_Get_Default : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  struct G_printer_attributes : public Collection {
    struct C_cover_back_default : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_cover_back_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_cover_front_default : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_cover_front_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_document_format_details_default : public Collection {
      SingleValue<std::string> document_format{this, AttrName::document_format};
      SingleValue<StringWithLanguage> document_format_device_id{
          this, AttrName::document_format_device_id};
      SingleValue<StringWithLanguage> document_format_version{
          this, AttrName::document_format_version};
      SetOfValues<std::string> document_natural_language{
          this, AttrName::document_natural_language};
      SingleValue<StringWithLanguage> document_source_application_name{
          this, AttrName::document_source_application_name};
      SingleValue<StringWithLanguage> document_source_application_version{
          this, AttrName::document_source_application_version};
      SingleValue<StringWithLanguage> document_source_os_name{
          this, AttrName::document_source_os_name};
      SingleValue<StringWithLanguage> document_source_os_version{
          this, AttrName::document_source_os_version};
      C_document_format_details_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_finishings_col_database : public Collection {
      struct C_baling : public Collection {
        SingleValue<E_baling_type> baling_type{this, AttrName::baling_type};
        SingleValue<E_baling_when> baling_when{this, AttrName::baling_when};
        C_baling() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_binding : public Collection {
        SingleValue<E_binding_reference_edge> binding_reference_edge{
            this, AttrName::binding_reference_edge};
        SingleValue<E_binding_type> binding_type{this, AttrName::binding_type};
        C_binding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_coating : public Collection {
        SingleValue<E_coating_sides> coating_sides{this,
                                                   AttrName::coating_sides};
        SingleValue<E_coating_type> coating_type{this, AttrName::coating_type};
        C_coating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_covering : public Collection {
        SingleValue<E_covering_name> covering_name{this,
                                                   AttrName::covering_name};
        C_covering() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_folding : public Collection {
        SingleValue<E_folding_direction> folding_direction{
            this, AttrName::folding_direction};
        SingleValue<int> folding_offset{this, AttrName::folding_offset};
        SingleValue<E_folding_reference_edge> folding_reference_edge{
            this, AttrName::folding_reference_edge};
        C_folding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_laminating : public Collection {
        SingleValue<E_laminating_sides> laminating_sides{
            this, AttrName::laminating_sides};
        SingleValue<E_laminating_type> laminating_type{
            this, AttrName::laminating_type};
        C_laminating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_media_size : public Collection {
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_punching : public Collection {
        SetOfValues<int> punching_locations{this, AttrName::punching_locations};
        SingleValue<int> punching_offset{this, AttrName::punching_offset};
        SingleValue<E_punching_reference_edge> punching_reference_edge{
            this, AttrName::punching_reference_edge};
        C_punching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_stitching : public Collection {
        SingleValue<int> stitching_angle{this, AttrName::stitching_angle};
        SetOfValues<int> stitching_locations{this,
                                             AttrName::stitching_locations};
        SingleValue<E_stitching_method> stitching_method{
            this, AttrName::stitching_method};
        SingleValue<int> stitching_offset{this, AttrName::stitching_offset};
        SingleValue<E_stitching_reference_edge> stitching_reference_edge{
            this, AttrName::stitching_reference_edge};
        C_stitching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_trimming : public Collection {
        SingleValue<int> trimming_offset{this, AttrName::trimming_offset};
        SingleValue<E_trimming_reference_edge> trimming_reference_edge{
            this, AttrName::trimming_reference_edge};
        SingleValue<E_trimming_type> trimming_type{this,
                                                   AttrName::trimming_type};
        SingleValue<E_trimming_when> trimming_when{this,
                                                   AttrName::trimming_when};
        C_trimming() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleCollection<C_baling> baling{this, AttrName::baling};
      SingleCollection<C_binding> binding{this, AttrName::binding};
      SingleCollection<C_coating> coating{this, AttrName::coating};
      SingleCollection<C_covering> covering{this, AttrName::covering};
      SingleValue<E_finishing_template> finishing_template{
          this, AttrName::finishing_template};
      SetOfCollections<C_folding> folding{this, AttrName::folding};
      SingleValue<E_imposition_template> imposition_template{
          this, AttrName::imposition_template};
      SingleCollection<C_laminating> laminating{this, AttrName::laminating};
      SingleValue<RangeOfInteger> media_sheets_supported{
          this, AttrName::media_sheets_supported};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleCollection<C_punching> punching{this, AttrName::punching};
      SingleCollection<C_stitching> stitching{this, AttrName::stitching};
      SetOfCollections<C_trimming> trimming{this, AttrName::trimming};
      C_finishings_col_database() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_finishings_col_default : public Collection {
      struct C_baling : public Collection {
        SingleValue<E_baling_type> baling_type{this, AttrName::baling_type};
        SingleValue<E_baling_when> baling_when{this, AttrName::baling_when};
        C_baling() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_binding : public Collection {
        SingleValue<E_binding_reference_edge> binding_reference_edge{
            this, AttrName::binding_reference_edge};
        SingleValue<E_binding_type> binding_type{this, AttrName::binding_type};
        C_binding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_coating : public Collection {
        SingleValue<E_coating_sides> coating_sides{this,
                                                   AttrName::coating_sides};
        SingleValue<E_coating_type> coating_type{this, AttrName::coating_type};
        C_coating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_covering : public Collection {
        SingleValue<E_covering_name> covering_name{this,
                                                   AttrName::covering_name};
        C_covering() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_folding : public Collection {
        SingleValue<E_folding_direction> folding_direction{
            this, AttrName::folding_direction};
        SingleValue<int> folding_offset{this, AttrName::folding_offset};
        SingleValue<E_folding_reference_edge> folding_reference_edge{
            this, AttrName::folding_reference_edge};
        C_folding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_laminating : public Collection {
        SingleValue<E_laminating_sides> laminating_sides{
            this, AttrName::laminating_sides};
        SingleValue<E_laminating_type> laminating_type{
            this, AttrName::laminating_type};
        C_laminating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_media_size : public Collection {
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_punching : public Collection {
        SetOfValues<int> punching_locations{this, AttrName::punching_locations};
        SingleValue<int> punching_offset{this, AttrName::punching_offset};
        SingleValue<E_punching_reference_edge> punching_reference_edge{
            this, AttrName::punching_reference_edge};
        C_punching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_stitching : public Collection {
        SingleValue<int> stitching_angle{this, AttrName::stitching_angle};
        SetOfValues<int> stitching_locations{this,
                                             AttrName::stitching_locations};
        SingleValue<E_stitching_method> stitching_method{
            this, AttrName::stitching_method};
        SingleValue<int> stitching_offset{this, AttrName::stitching_offset};
        SingleValue<E_stitching_reference_edge> stitching_reference_edge{
            this, AttrName::stitching_reference_edge};
        C_stitching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_trimming : public Collection {
        SingleValue<int> trimming_offset{this, AttrName::trimming_offset};
        SingleValue<E_trimming_reference_edge> trimming_reference_edge{
            this, AttrName::trimming_reference_edge};
        SingleValue<E_trimming_type> trimming_type{this,
                                                   AttrName::trimming_type};
        SingleValue<E_trimming_when> trimming_when{this,
                                                   AttrName::trimming_when};
        C_trimming() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleCollection<C_baling> baling{this, AttrName::baling};
      SingleCollection<C_binding> binding{this, AttrName::binding};
      SingleCollection<C_coating> coating{this, AttrName::coating};
      SingleCollection<C_covering> covering{this, AttrName::covering};
      SingleValue<E_finishing_template> finishing_template{
          this, AttrName::finishing_template};
      SetOfCollections<C_folding> folding{this, AttrName::folding};
      SingleValue<E_imposition_template> imposition_template{
          this, AttrName::imposition_template};
      SingleCollection<C_laminating> laminating{this, AttrName::laminating};
      SingleValue<RangeOfInteger> media_sheets_supported{
          this, AttrName::media_sheets_supported};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleCollection<C_punching> punching{this, AttrName::punching};
      SingleCollection<C_stitching> stitching{this, AttrName::stitching};
      SetOfCollections<C_trimming> trimming{this, AttrName::trimming};
      C_finishings_col_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_finishings_col_ready : public Collection {
      struct C_baling : public Collection {
        SingleValue<E_baling_type> baling_type{this, AttrName::baling_type};
        SingleValue<E_baling_when> baling_when{this, AttrName::baling_when};
        C_baling() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_binding : public Collection {
        SingleValue<E_binding_reference_edge> binding_reference_edge{
            this, AttrName::binding_reference_edge};
        SingleValue<E_binding_type> binding_type{this, AttrName::binding_type};
        C_binding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_coating : public Collection {
        SingleValue<E_coating_sides> coating_sides{this,
                                                   AttrName::coating_sides};
        SingleValue<E_coating_type> coating_type{this, AttrName::coating_type};
        C_coating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_covering : public Collection {
        SingleValue<E_covering_name> covering_name{this,
                                                   AttrName::covering_name};
        C_covering() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_folding : public Collection {
        SingleValue<E_folding_direction> folding_direction{
            this, AttrName::folding_direction};
        SingleValue<int> folding_offset{this, AttrName::folding_offset};
        SingleValue<E_folding_reference_edge> folding_reference_edge{
            this, AttrName::folding_reference_edge};
        C_folding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_laminating : public Collection {
        SingleValue<E_laminating_sides> laminating_sides{
            this, AttrName::laminating_sides};
        SingleValue<E_laminating_type> laminating_type{
            this, AttrName::laminating_type};
        C_laminating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_media_size : public Collection {
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_punching : public Collection {
        SetOfValues<int> punching_locations{this, AttrName::punching_locations};
        SingleValue<int> punching_offset{this, AttrName::punching_offset};
        SingleValue<E_punching_reference_edge> punching_reference_edge{
            this, AttrName::punching_reference_edge};
        C_punching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_stitching : public Collection {
        SingleValue<int> stitching_angle{this, AttrName::stitching_angle};
        SetOfValues<int> stitching_locations{this,
                                             AttrName::stitching_locations};
        SingleValue<E_stitching_method> stitching_method{
            this, AttrName::stitching_method};
        SingleValue<int> stitching_offset{this, AttrName::stitching_offset};
        SingleValue<E_stitching_reference_edge> stitching_reference_edge{
            this, AttrName::stitching_reference_edge};
        C_stitching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_trimming : public Collection {
        SingleValue<int> trimming_offset{this, AttrName::trimming_offset};
        SingleValue<E_trimming_reference_edge> trimming_reference_edge{
            this, AttrName::trimming_reference_edge};
        SingleValue<E_trimming_type> trimming_type{this,
                                                   AttrName::trimming_type};
        SingleValue<E_trimming_when> trimming_when{this,
                                                   AttrName::trimming_when};
        C_trimming() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleCollection<C_baling> baling{this, AttrName::baling};
      SingleCollection<C_binding> binding{this, AttrName::binding};
      SingleCollection<C_coating> coating{this, AttrName::coating};
      SingleCollection<C_covering> covering{this, AttrName::covering};
      SingleValue<E_finishing_template> finishing_template{
          this, AttrName::finishing_template};
      SetOfCollections<C_folding> folding{this, AttrName::folding};
      SingleValue<E_imposition_template> imposition_template{
          this, AttrName::imposition_template};
      SingleCollection<C_laminating> laminating{this, AttrName::laminating};
      SingleValue<RangeOfInteger> media_sheets_supported{
          this, AttrName::media_sheets_supported};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleCollection<C_punching> punching{this, AttrName::punching};
      SingleCollection<C_stitching> stitching{this, AttrName::stitching};
      SetOfCollections<C_trimming> trimming{this, AttrName::trimming};
      C_finishings_col_ready() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_insert_sheet_default : public Collection {
      SingleValue<int> insert_after_page_number{
          this, AttrName::insert_after_page_number};
      SingleValue<int> insert_count{this, AttrName::insert_count};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_insert_sheet_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_accounting_sheets_default : public Collection {
      SingleValue<E_job_accounting_output_bin> job_accounting_output_bin{
          this, AttrName::job_accounting_output_bin};
      SingleValue<E_job_accounting_sheets_type> job_accounting_sheets_type{
          this, AttrName::job_accounting_sheets_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_accounting_sheets_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_constraints_supported : public Collection {
      SingleValue<StringWithLanguage> resolver_name{this,
                                                    AttrName::resolver_name};
      C_job_constraints_supported() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_cover_back_default : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_cover_back_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_cover_front_default : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_cover_front_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_error_sheet_default : public Collection {
      SingleValue<E_job_error_sheet_type> job_error_sheet_type{
          this, AttrName::job_error_sheet_type};
      SingleValue<E_job_error_sheet_when> job_error_sheet_when{
          this, AttrName::job_error_sheet_when};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_error_sheet_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_finishings_col_default : public Collection {
      struct C_baling : public Collection {
        SingleValue<E_baling_type> baling_type{this, AttrName::baling_type};
        SingleValue<E_baling_when> baling_when{this, AttrName::baling_when};
        C_baling() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_binding : public Collection {
        SingleValue<E_binding_reference_edge> binding_reference_edge{
            this, AttrName::binding_reference_edge};
        SingleValue<E_binding_type> binding_type{this, AttrName::binding_type};
        C_binding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_coating : public Collection {
        SingleValue<E_coating_sides> coating_sides{this,
                                                   AttrName::coating_sides};
        SingleValue<E_coating_type> coating_type{this, AttrName::coating_type};
        C_coating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_covering : public Collection {
        SingleValue<E_covering_name> covering_name{this,
                                                   AttrName::covering_name};
        C_covering() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_folding : public Collection {
        SingleValue<E_folding_direction> folding_direction{
            this, AttrName::folding_direction};
        SingleValue<int> folding_offset{this, AttrName::folding_offset};
        SingleValue<E_folding_reference_edge> folding_reference_edge{
            this, AttrName::folding_reference_edge};
        C_folding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_laminating : public Collection {
        SingleValue<E_laminating_sides> laminating_sides{
            this, AttrName::laminating_sides};
        SingleValue<E_laminating_type> laminating_type{
            this, AttrName::laminating_type};
        C_laminating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_media_size : public Collection {
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_punching : public Collection {
        SetOfValues<int> punching_locations{this, AttrName::punching_locations};
        SingleValue<int> punching_offset{this, AttrName::punching_offset};
        SingleValue<E_punching_reference_edge> punching_reference_edge{
            this, AttrName::punching_reference_edge};
        C_punching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_stitching : public Collection {
        SingleValue<int> stitching_angle{this, AttrName::stitching_angle};
        SetOfValues<int> stitching_locations{this,
                                             AttrName::stitching_locations};
        SingleValue<E_stitching_method> stitching_method{
            this, AttrName::stitching_method};
        SingleValue<int> stitching_offset{this, AttrName::stitching_offset};
        SingleValue<E_stitching_reference_edge> stitching_reference_edge{
            this, AttrName::stitching_reference_edge};
        C_stitching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_trimming : public Collection {
        SingleValue<int> trimming_offset{this, AttrName::trimming_offset};
        SingleValue<E_trimming_reference_edge> trimming_reference_edge{
            this, AttrName::trimming_reference_edge};
        SingleValue<E_trimming_type> trimming_type{this,
                                                   AttrName::trimming_type};
        SingleValue<E_trimming_when> trimming_when{this,
                                                   AttrName::trimming_when};
        C_trimming() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleCollection<C_baling> baling{this, AttrName::baling};
      SingleCollection<C_binding> binding{this, AttrName::binding};
      SingleCollection<C_coating> coating{this, AttrName::coating};
      SingleCollection<C_covering> covering{this, AttrName::covering};
      SingleValue<E_finishing_template> finishing_template{
          this, AttrName::finishing_template};
      SetOfCollections<C_folding> folding{this, AttrName::folding};
      SingleValue<E_imposition_template> imposition_template{
          this, AttrName::imposition_template};
      SingleCollection<C_laminating> laminating{this, AttrName::laminating};
      SingleValue<RangeOfInteger> media_sheets_supported{
          this, AttrName::media_sheets_supported};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleCollection<C_punching> punching{this, AttrName::punching};
      SingleCollection<C_stitching> stitching{this, AttrName::stitching};
      SetOfCollections<C_trimming> trimming{this, AttrName::trimming};
      C_job_finishings_col_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_finishings_col_ready : public Collection {
      struct C_baling : public Collection {
        SingleValue<E_baling_type> baling_type{this, AttrName::baling_type};
        SingleValue<E_baling_when> baling_when{this, AttrName::baling_when};
        C_baling() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_binding : public Collection {
        SingleValue<E_binding_reference_edge> binding_reference_edge{
            this, AttrName::binding_reference_edge};
        SingleValue<E_binding_type> binding_type{this, AttrName::binding_type};
        C_binding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_coating : public Collection {
        SingleValue<E_coating_sides> coating_sides{this,
                                                   AttrName::coating_sides};
        SingleValue<E_coating_type> coating_type{this, AttrName::coating_type};
        C_coating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_covering : public Collection {
        SingleValue<E_covering_name> covering_name{this,
                                                   AttrName::covering_name};
        C_covering() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_folding : public Collection {
        SingleValue<E_folding_direction> folding_direction{
            this, AttrName::folding_direction};
        SingleValue<int> folding_offset{this, AttrName::folding_offset};
        SingleValue<E_folding_reference_edge> folding_reference_edge{
            this, AttrName::folding_reference_edge};
        C_folding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_laminating : public Collection {
        SingleValue<E_laminating_sides> laminating_sides{
            this, AttrName::laminating_sides};
        SingleValue<E_laminating_type> laminating_type{
            this, AttrName::laminating_type};
        C_laminating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_media_size : public Collection {
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_punching : public Collection {
        SetOfValues<int> punching_locations{this, AttrName::punching_locations};
        SingleValue<int> punching_offset{this, AttrName::punching_offset};
        SingleValue<E_punching_reference_edge> punching_reference_edge{
            this, AttrName::punching_reference_edge};
        C_punching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_stitching : public Collection {
        SingleValue<int> stitching_angle{this, AttrName::stitching_angle};
        SetOfValues<int> stitching_locations{this,
                                             AttrName::stitching_locations};
        SingleValue<E_stitching_method> stitching_method{
            this, AttrName::stitching_method};
        SingleValue<int> stitching_offset{this, AttrName::stitching_offset};
        SingleValue<E_stitching_reference_edge> stitching_reference_edge{
            this, AttrName::stitching_reference_edge};
        C_stitching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_trimming : public Collection {
        SingleValue<int> trimming_offset{this, AttrName::trimming_offset};
        SingleValue<E_trimming_reference_edge> trimming_reference_edge{
            this, AttrName::trimming_reference_edge};
        SingleValue<E_trimming_type> trimming_type{this,
                                                   AttrName::trimming_type};
        SingleValue<E_trimming_when> trimming_when{this,
                                                   AttrName::trimming_when};
        C_trimming() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleCollection<C_baling> baling{this, AttrName::baling};
      SingleCollection<C_binding> binding{this, AttrName::binding};
      SingleCollection<C_coating> coating{this, AttrName::coating};
      SingleCollection<C_covering> covering{this, AttrName::covering};
      SingleValue<E_finishing_template> finishing_template{
          this, AttrName::finishing_template};
      SetOfCollections<C_folding> folding{this, AttrName::folding};
      SingleValue<E_imposition_template> imposition_template{
          this, AttrName::imposition_template};
      SingleCollection<C_laminating> laminating{this, AttrName::laminating};
      SingleValue<RangeOfInteger> media_sheets_supported{
          this, AttrName::media_sheets_supported};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleCollection<C_punching> punching{this, AttrName::punching};
      SingleCollection<C_stitching> stitching{this, AttrName::stitching};
      SetOfCollections<C_trimming> trimming{this, AttrName::trimming};
      C_job_finishings_col_ready() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_resolvers_supported : public Collection {
      SingleValue<StringWithLanguage> resolver_name{this,
                                                    AttrName::resolver_name};
      C_job_resolvers_supported() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_sheets_col_default : public Collection {
      SingleValue<E_job_sheets> job_sheets{this, AttrName::job_sheets};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_sheets_col_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_media_col_database : public Collection {
      struct C_media_size : public Collection {
        SingleValue<int> x_dimension{this, AttrName::x_dimension};
        SingleValue<int> y_dimension{this, AttrName::y_dimension};
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_media_source_properties : public Collection {
        SingleValue<E_media_source_feed_direction> media_source_feed_direction{
            this, AttrName::media_source_feed_direction};
        SingleValue<E_media_source_feed_orientation>
            media_source_feed_orientation{
                this, AttrName::media_source_feed_orientation};
        C_media_source_properties() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleValue<E_media_back_coating> media_back_coating{
          this, AttrName::media_back_coating};
      SingleValue<int> media_bottom_margin{this, AttrName::media_bottom_margin};
      SingleValue<E_media_color> media_color{this, AttrName::media_color};
      SingleValue<E_media_front_coating> media_front_coating{
          this, AttrName::media_front_coating};
      SingleValue<E_media_grain> media_grain{this, AttrName::media_grain};
      SingleValue<int> media_hole_count{this, AttrName::media_hole_count};
      SingleValue<StringWithLanguage> media_info{this, AttrName::media_info};
      SingleValue<E_media_key> media_key{this, AttrName::media_key};
      SingleValue<int> media_left_margin{this, AttrName::media_left_margin};
      SingleValue<int> media_order_count{this, AttrName::media_order_count};
      SingleValue<E_media_pre_printed> media_pre_printed{
          this, AttrName::media_pre_printed};
      SingleValue<E_media_recycled> media_recycled{this,
                                                   AttrName::media_recycled};
      SingleValue<int> media_right_margin{this, AttrName::media_right_margin};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleValue<E_media_source> media_source{this, AttrName::media_source};
      SingleValue<int> media_thickness{this, AttrName::media_thickness};
      SingleValue<E_media_tooth> media_tooth{this, AttrName::media_tooth};
      SingleValue<int> media_top_margin{this, AttrName::media_top_margin};
      SingleValue<E_media_type> media_type{this, AttrName::media_type};
      SingleValue<int> media_weight_metric{this, AttrName::media_weight_metric};
      SingleCollection<C_media_source_properties> media_source_properties{
          this, AttrName::media_source_properties};
      C_media_col_database() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_media_col_default : public Collection {
      struct C_media_size : public Collection {
        SingleValue<int> x_dimension{this, AttrName::x_dimension};
        SingleValue<int> y_dimension{this, AttrName::y_dimension};
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleValue<E_media_back_coating> media_back_coating{
          this, AttrName::media_back_coating};
      SingleValue<int> media_bottom_margin{this, AttrName::media_bottom_margin};
      SingleValue<E_media_color> media_color{this, AttrName::media_color};
      SingleValue<E_media_front_coating> media_front_coating{
          this, AttrName::media_front_coating};
      SingleValue<E_media_grain> media_grain{this, AttrName::media_grain};
      SingleValue<int> media_hole_count{this, AttrName::media_hole_count};
      SingleValue<StringWithLanguage> media_info{this, AttrName::media_info};
      SingleValue<E_media_key> media_key{this, AttrName::media_key};
      SingleValue<int> media_left_margin{this, AttrName::media_left_margin};
      SingleValue<int> media_order_count{this, AttrName::media_order_count};
      SingleValue<E_media_pre_printed> media_pre_printed{
          this, AttrName::media_pre_printed};
      SingleValue<E_media_recycled> media_recycled{this,
                                                   AttrName::media_recycled};
      SingleValue<int> media_right_margin{this, AttrName::media_right_margin};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleValue<E_media_source> media_source{this, AttrName::media_source};
      SingleValue<int> media_thickness{this, AttrName::media_thickness};
      SingleValue<E_media_tooth> media_tooth{this, AttrName::media_tooth};
      SingleValue<int> media_top_margin{this, AttrName::media_top_margin};
      SingleValue<E_media_type> media_type{this, AttrName::media_type};
      SingleValue<int> media_weight_metric{this, AttrName::media_weight_metric};
      C_media_col_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_media_col_ready : public Collection {
      struct C_media_size : public Collection {
        SingleValue<int> x_dimension{this, AttrName::x_dimension};
        SingleValue<int> y_dimension{this, AttrName::y_dimension};
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_media_source_properties : public Collection {
        SingleValue<E_media_source_feed_direction> media_source_feed_direction{
            this, AttrName::media_source_feed_direction};
        SingleValue<E_media_source_feed_orientation>
            media_source_feed_orientation{
                this, AttrName::media_source_feed_orientation};
        C_media_source_properties() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleValue<E_media_back_coating> media_back_coating{
          this, AttrName::media_back_coating};
      SingleValue<int> media_bottom_margin{this, AttrName::media_bottom_margin};
      SingleValue<E_media_color> media_color{this, AttrName::media_color};
      SingleValue<E_media_front_coating> media_front_coating{
          this, AttrName::media_front_coating};
      SingleValue<E_media_grain> media_grain{this, AttrName::media_grain};
      SingleValue<int> media_hole_count{this, AttrName::media_hole_count};
      SingleValue<StringWithLanguage> media_info{this, AttrName::media_info};
      SingleValue<E_media_key> media_key{this, AttrName::media_key};
      SingleValue<int> media_left_margin{this, AttrName::media_left_margin};
      SingleValue<int> media_order_count{this, AttrName::media_order_count};
      SingleValue<E_media_pre_printed> media_pre_printed{
          this, AttrName::media_pre_printed};
      SingleValue<E_media_recycled> media_recycled{this,
                                                   AttrName::media_recycled};
      SingleValue<int> media_right_margin{this, AttrName::media_right_margin};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleValue<E_media_source> media_source{this, AttrName::media_source};
      SingleValue<int> media_thickness{this, AttrName::media_thickness};
      SingleValue<E_media_tooth> media_tooth{this, AttrName::media_tooth};
      SingleValue<int> media_top_margin{this, AttrName::media_top_margin};
      SingleValue<E_media_type> media_type{this, AttrName::media_type};
      SingleValue<int> media_weight_metric{this, AttrName::media_weight_metric};
      SingleCollection<C_media_source_properties> media_source_properties{
          this, AttrName::media_source_properties};
      C_media_col_ready() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_media_size_supported : public Collection {
      SingleValue<RangeOfInteger> x_dimension{this, AttrName::x_dimension};
      SingleValue<RangeOfInteger> y_dimension{this, AttrName::y_dimension};
      C_media_size_supported() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_pdl_init_file_default : public Collection {
      SingleValue<StringWithLanguage> pdl_init_file_entry{
          this, AttrName::pdl_init_file_entry};
      SingleValue<std::string> pdl_init_file_location{
          this, AttrName::pdl_init_file_location};
      SingleValue<StringWithLanguage> pdl_init_file_name{
          this, AttrName::pdl_init_file_name};
      C_pdl_init_file_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_printer_contact_col : public Collection {
      SingleValue<StringWithLanguage> contact_name{this,
                                                   AttrName::contact_name};
      SingleValue<std::string> contact_uri{this, AttrName::contact_uri};
      SetOfValues<StringWithLanguage> contact_vcard{this,
                                                    AttrName::contact_vcard};
      C_printer_contact_col() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_printer_icc_profiles : public Collection {
      SingleValue<StringWithLanguage> profile_name{this,
                                                   AttrName::profile_name};
      SingleValue<std::string> profile_url{this, AttrName::profile_url};
      C_printer_icc_profiles() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_printer_xri_supported : public Collection {
      SingleValue<E_xri_authentication> xri_authentication{
          this, AttrName::xri_authentication};
      SingleValue<E_xri_security> xri_security{this, AttrName::xri_security};
      SingleValue<std::string> xri_uri{this, AttrName::xri_uri};
      C_printer_xri_supported() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_proof_print_default : public Collection {
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      SingleValue<int> proof_print_copies{this, AttrName::proof_print_copies};
      C_proof_print_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_separator_sheets_default : public Collection {
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      SetOfValues<E_separator_sheets_type> separator_sheets_type{
          this, AttrName::separator_sheets_type};
      C_separator_sheets_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    OpenSetOfValues<E_baling_type_supported> baling_type_supported{
        this, AttrName::baling_type_supported};
    SetOfValues<E_baling_when_supported> baling_when_supported{
        this, AttrName::baling_when_supported};
    SetOfValues<E_binding_reference_edge_supported>
        binding_reference_edge_supported{
            this, AttrName::binding_reference_edge_supported};
    SetOfValues<E_binding_type_supported> binding_type_supported{
        this, AttrName::binding_type_supported};
    SingleValue<std::string> charset_configured{this,
                                                AttrName::charset_configured};
    SetOfValues<std::string> charset_supported{this,
                                               AttrName::charset_supported};
    SetOfValues<E_coating_sides_supported> coating_sides_supported{
        this, AttrName::coating_sides_supported};
    OpenSetOfValues<E_coating_type_supported> coating_type_supported{
        this, AttrName::coating_type_supported};
    SingleValue<bool> color_supported{this, AttrName::color_supported};
    SetOfValues<E_compression_supported> compression_supported{
        this, AttrName::compression_supported};
    SingleValue<int> copies_default{this, AttrName::copies_default};
    SingleValue<RangeOfInteger> copies_supported{this,
                                                 AttrName::copies_supported};
    SingleCollection<C_cover_back_default> cover_back_default{
        this, AttrName::cover_back_default};
    SetOfValues<E_cover_back_supported> cover_back_supported{
        this, AttrName::cover_back_supported};
    SingleCollection<C_cover_front_default> cover_front_default{
        this, AttrName::cover_front_default};
    SetOfValues<E_cover_front_supported> cover_front_supported{
        this, AttrName::cover_front_supported};
    OpenSetOfValues<E_covering_name_supported> covering_name_supported{
        this, AttrName::covering_name_supported};
    SingleValue<std::string> document_charset_default{
        this, AttrName::document_charset_default};
    SetOfValues<std::string> document_charset_supported{
        this, AttrName::document_charset_supported};
    SingleValue<E_document_digital_signature_default>
        document_digital_signature_default{
            this, AttrName::document_digital_signature_default};
    SetOfValues<E_document_digital_signature_supported>
        document_digital_signature_supported{
            this, AttrName::document_digital_signature_supported};
    SingleValue<std::string> document_format_default{
        this, AttrName::document_format_default};
    SingleCollection<C_document_format_details_default>
        document_format_details_default{
            this, AttrName::document_format_details_default};
    SetOfValues<E_document_format_details_supported>
        document_format_details_supported{
            this, AttrName::document_format_details_supported};
    SetOfValues<std::string> document_format_supported{
        this, AttrName::document_format_supported};
    SingleValue<StringWithLanguage> document_format_version_default{
        this, AttrName::document_format_version_default};
    SetOfValues<StringWithLanguage> document_format_version_supported{
        this, AttrName::document_format_version_supported};
    SingleValue<std::string> document_natural_language_default{
        this, AttrName::document_natural_language_default};
    SetOfValues<std::string> document_natural_language_supported{
        this, AttrName::document_natural_language_supported};
    SingleValue<int> document_password_supported{
        this, AttrName::document_password_supported};
    SetOfValues<E_feed_orientation_supported> feed_orientation_supported{
        this, AttrName::feed_orientation_supported};
    OpenSetOfValues<E_finishing_template_supported>
        finishing_template_supported{this,
                                     AttrName::finishing_template_supported};
    SetOfCollections<C_finishings_col_database> finishings_col_database{
        this, AttrName::finishings_col_database};
    SetOfCollections<C_finishings_col_default> finishings_col_default{
        this, AttrName::finishings_col_default};
    SetOfCollections<C_finishings_col_ready> finishings_col_ready{
        this, AttrName::finishings_col_ready};
    SetOfValues<E_finishings_default> finishings_default{
        this, AttrName::finishings_default};
    SetOfValues<E_finishings_ready> finishings_ready{
        this, AttrName::finishings_ready};
    SetOfValues<E_finishings_supported> finishings_supported{
        this, AttrName::finishings_supported};
    SetOfValues<E_folding_direction_supported> folding_direction_supported{
        this, AttrName::folding_direction_supported};
    SetOfValues<RangeOfInteger> folding_offset_supported{
        this, AttrName::folding_offset_supported};
    SetOfValues<E_folding_reference_edge_supported>
        folding_reference_edge_supported{
            this, AttrName::folding_reference_edge_supported};
    SingleValue<StringWithLanguage> font_name_requested_default{
        this, AttrName::font_name_requested_default};
    SetOfValues<StringWithLanguage> font_name_requested_supported{
        this, AttrName::font_name_requested_supported};
    SingleValue<int> font_size_requested_default{
        this, AttrName::font_size_requested_default};
    SetOfValues<RangeOfInteger> font_size_requested_supported{
        this, AttrName::font_size_requested_supported};
    SetOfValues<std::string> generated_natural_language_supported{
        this, AttrName::generated_natural_language_supported};
    SetOfValues<E_identify_actions_default> identify_actions_default{
        this, AttrName::identify_actions_default};
    SetOfValues<E_identify_actions_supported> identify_actions_supported{
        this, AttrName::identify_actions_supported};
    SingleValue<RangeOfInteger> insert_after_page_number_supported{
        this, AttrName::insert_after_page_number_supported};
    SingleValue<RangeOfInteger> insert_count_supported{
        this, AttrName::insert_count_supported};
    SetOfCollections<C_insert_sheet_default> insert_sheet_default{
        this, AttrName::insert_sheet_default};
    SetOfValues<E_ipp_features_supported> ipp_features_supported{
        this, AttrName::ipp_features_supported};
    SetOfValues<E_ipp_versions_supported> ipp_versions_supported{
        this, AttrName::ipp_versions_supported};
    SingleValue<int> ippget_event_life{this, AttrName::ippget_event_life};
    SingleValue<StringWithLanguage> job_account_id_default{
        this, AttrName::job_account_id_default};
    SingleValue<bool> job_account_id_supported{
        this, AttrName::job_account_id_supported};
    SingleValue<E_job_account_type_default> job_account_type_default{
        this, AttrName::job_account_type_default};
    OpenSetOfValues<E_job_account_type_supported> job_account_type_supported{
        this, AttrName::job_account_type_supported};
    SingleCollection<C_job_accounting_sheets_default>
        job_accounting_sheets_default{this,
                                      AttrName::job_accounting_sheets_default};
    SingleValue<StringWithLanguage> job_accounting_user_id_default{
        this, AttrName::job_accounting_user_id_default};
    SingleValue<bool> job_accounting_user_id_supported{
        this, AttrName::job_accounting_user_id_supported};
    SingleValue<bool> job_authorization_uri_supported{
        this, AttrName::job_authorization_uri_supported};
    SetOfCollections<C_job_constraints_supported> job_constraints_supported{
        this, AttrName::job_constraints_supported};
    SingleValue<int> job_copies_default{this, AttrName::job_copies_default};
    SingleValue<RangeOfInteger> job_copies_supported{
        this, AttrName::job_copies_supported};
    SingleCollection<C_job_cover_back_default> job_cover_back_default{
        this, AttrName::job_cover_back_default};
    SetOfValues<E_job_cover_back_supported> job_cover_back_supported{
        this, AttrName::job_cover_back_supported};
    SingleCollection<C_job_cover_front_default> job_cover_front_default{
        this, AttrName::job_cover_front_default};
    SetOfValues<E_job_cover_front_supported> job_cover_front_supported{
        this, AttrName::job_cover_front_supported};
    SingleValue<E_job_delay_output_until_default>
        job_delay_output_until_default{
            this, AttrName::job_delay_output_until_default};
    OpenSetOfValues<E_job_delay_output_until_supported>
        job_delay_output_until_supported{
            this, AttrName::job_delay_output_until_supported};
    SingleValue<RangeOfInteger> job_delay_output_until_time_supported{
        this, AttrName::job_delay_output_until_time_supported};
    SingleValue<E_job_error_action_default> job_error_action_default{
        this, AttrName::job_error_action_default};
    SetOfValues<E_job_error_action_supported> job_error_action_supported{
        this, AttrName::job_error_action_supported};
    SingleCollection<C_job_error_sheet_default> job_error_sheet_default{
        this, AttrName::job_error_sheet_default};
    SetOfCollections<C_job_finishings_col_default> job_finishings_col_default{
        this, AttrName::job_finishings_col_default};
    SetOfCollections<C_job_finishings_col_ready> job_finishings_col_ready{
        this, AttrName::job_finishings_col_ready};
    SetOfValues<E_job_finishings_default> job_finishings_default{
        this, AttrName::job_finishings_default};
    SetOfValues<E_job_finishings_ready> job_finishings_ready{
        this, AttrName::job_finishings_ready};
    SetOfValues<E_job_finishings_supported> job_finishings_supported{
        this, AttrName::job_finishings_supported};
    SingleValue<E_job_hold_until_default> job_hold_until_default{
        this, AttrName::job_hold_until_default};
    OpenSetOfValues<E_job_hold_until_supported> job_hold_until_supported{
        this, AttrName::job_hold_until_supported};
    SingleValue<RangeOfInteger> job_hold_until_time_supported{
        this, AttrName::job_hold_until_time_supported};
    SingleValue<bool> job_ids_supported{this, AttrName::job_ids_supported};
    SingleValue<RangeOfInteger> job_impressions_supported{
        this, AttrName::job_impressions_supported};
    SingleValue<RangeOfInteger> job_k_octets_supported{
        this, AttrName::job_k_octets_supported};
    SingleValue<RangeOfInteger> job_media_sheets_supported{
        this, AttrName::job_media_sheets_supported};
    SingleValue<StringWithLanguage> job_message_to_operator_default{
        this, AttrName::job_message_to_operator_default};
    SingleValue<bool> job_message_to_operator_supported{
        this, AttrName::job_message_to_operator_supported};
    SingleValue<bool> job_pages_per_set_supported{
        this, AttrName::job_pages_per_set_supported};
    OpenSetOfValues<E_job_password_encryption_supported>
        job_password_encryption_supported{
            this, AttrName::job_password_encryption_supported};
    SingleValue<int> job_password_supported{this,
                                            AttrName::job_password_supported};
    SingleValue<std::string> job_phone_number_default{
        this, AttrName::job_phone_number_default};
    SingleValue<bool> job_phone_number_supported{
        this, AttrName::job_phone_number_supported};
    SingleValue<int> job_priority_default{this, AttrName::job_priority_default};
    SingleValue<int> job_priority_supported{this,
                                            AttrName::job_priority_supported};
    SingleValue<StringWithLanguage> job_recipient_name_default{
        this, AttrName::job_recipient_name_default};
    SingleValue<bool> job_recipient_name_supported{
        this, AttrName::job_recipient_name_supported};
    SetOfCollections<C_job_resolvers_supported> job_resolvers_supported{
        this, AttrName::job_resolvers_supported};
    SingleValue<StringWithLanguage> job_sheet_message_default{
        this, AttrName::job_sheet_message_default};
    SingleValue<bool> job_sheet_message_supported{
        this, AttrName::job_sheet_message_supported};
    SingleCollection<C_job_sheets_col_default> job_sheets_col_default{
        this, AttrName::job_sheets_col_default};
    SingleValue<E_job_sheets_default> job_sheets_default{
        this, AttrName::job_sheets_default};
    OpenSetOfValues<E_job_sheets_supported> job_sheets_supported{
        this, AttrName::job_sheets_supported};
    SingleValue<E_job_spooling_supported> job_spooling_supported{
        this, AttrName::job_spooling_supported};
    SingleValue<RangeOfInteger> jpeg_k_octets_supported{
        this, AttrName::jpeg_k_octets_supported};
    SingleValue<RangeOfInteger> jpeg_x_dimension_supported{
        this, AttrName::jpeg_x_dimension_supported};
    SingleValue<RangeOfInteger> jpeg_y_dimension_supported{
        this, AttrName::jpeg_y_dimension_supported};
    SetOfValues<E_laminating_sides_supported> laminating_sides_supported{
        this, AttrName::laminating_sides_supported};
    OpenSetOfValues<E_laminating_type_supported> laminating_type_supported{
        this, AttrName::laminating_type_supported};
    SingleValue<int> max_save_info_supported{this,
                                             AttrName::max_save_info_supported};
    SingleValue<int> max_stitching_locations_supported{
        this, AttrName::max_stitching_locations_supported};
    OpenSetOfValues<E_media_back_coating_supported>
        media_back_coating_supported{this,
                                     AttrName::media_back_coating_supported};
    SetOfValues<int> media_bottom_margin_supported{
        this, AttrName::media_bottom_margin_supported};
    SetOfCollections<C_media_col_database> media_col_database{
        this, AttrName::media_col_database};
    SingleCollection<C_media_col_default> media_col_default{
        this, AttrName::media_col_default};
    SetOfCollections<C_media_col_ready> media_col_ready{
        this, AttrName::media_col_ready};
    OpenSetOfValues<E_media_color_supported> media_color_supported{
        this, AttrName::media_color_supported};
    SingleValue<E_media_default> media_default{this, AttrName::media_default};
    OpenSetOfValues<E_media_front_coating_supported>
        media_front_coating_supported{this,
                                      AttrName::media_front_coating_supported};
    OpenSetOfValues<E_media_grain_supported> media_grain_supported{
        this, AttrName::media_grain_supported};
    SetOfValues<RangeOfInteger> media_hole_count_supported{
        this, AttrName::media_hole_count_supported};
    SingleValue<bool> media_info_supported{this,
                                           AttrName::media_info_supported};
    SetOfValues<int> media_left_margin_supported{
        this, AttrName::media_left_margin_supported};
    SetOfValues<RangeOfInteger> media_order_count_supported{
        this, AttrName::media_order_count_supported};
    OpenSetOfValues<E_media_pre_printed_supported> media_pre_printed_supported{
        this, AttrName::media_pre_printed_supported};
    OpenSetOfValues<E_media_ready> media_ready{this, AttrName::media_ready};
    OpenSetOfValues<E_media_recycled_supported> media_recycled_supported{
        this, AttrName::media_recycled_supported};
    SetOfValues<int> media_right_margin_supported{
        this, AttrName::media_right_margin_supported};
    SetOfCollections<C_media_size_supported> media_size_supported{
        this, AttrName::media_size_supported};
    OpenSetOfValues<E_media_source_supported> media_source_supported{
        this, AttrName::media_source_supported};
    OpenSetOfValues<E_media_supported> media_supported{
        this, AttrName::media_supported};
    SingleValue<RangeOfInteger> media_thickness_supported{
        this, AttrName::media_thickness_supported};
    OpenSetOfValues<E_media_tooth_supported> media_tooth_supported{
        this, AttrName::media_tooth_supported};
    SetOfValues<int> media_top_margin_supported{
        this, AttrName::media_top_margin_supported};
    OpenSetOfValues<E_media_type_supported> media_type_supported{
        this, AttrName::media_type_supported};
    SetOfValues<RangeOfInteger> media_weight_metric_supported{
        this, AttrName::media_weight_metric_supported};
    SingleValue<E_multiple_document_handling_default>
        multiple_document_handling_default{
            this, AttrName::multiple_document_handling_default};
    SetOfValues<E_multiple_document_handling_supported>
        multiple_document_handling_supported{
            this, AttrName::multiple_document_handling_supported};
    SingleValue<bool> multiple_document_jobs_supported{
        this, AttrName::multiple_document_jobs_supported};
    SingleValue<int> multiple_operation_time_out{
        this, AttrName::multiple_operation_time_out};
    SingleValue<E_multiple_operation_time_out_action>
        multiple_operation_time_out_action{
            this, AttrName::multiple_operation_time_out_action};
    SingleValue<std::string> natural_language_configured{
        this, AttrName::natural_language_configured};
    SetOfValues<E_notify_events_default> notify_events_default{
        this, AttrName::notify_events_default};
    SetOfValues<E_notify_events_supported> notify_events_supported{
        this, AttrName::notify_events_supported};
    SingleValue<int> notify_lease_duration_default{
        this, AttrName::notify_lease_duration_default};
    SetOfValues<RangeOfInteger> notify_lease_duration_supported{
        this, AttrName::notify_lease_duration_supported};
    SetOfValues<E_notify_pull_method_supported> notify_pull_method_supported{
        this, AttrName::notify_pull_method_supported};
    SetOfValues<std::string> notify_schemes_supported{
        this, AttrName::notify_schemes_supported};
    SingleValue<int> number_up_default{this, AttrName::number_up_default};
    SingleValue<RangeOfInteger> number_up_supported{
        this, AttrName::number_up_supported};
    SingleValue<std::string> oauth_authorization_server_uri{
        this, AttrName::oauth_authorization_server_uri};
    SetOfValues<E_operations_supported> operations_supported{
        this, AttrName::operations_supported};
    SingleValue<E_orientation_requested_default> orientation_requested_default{
        this, AttrName::orientation_requested_default};
    SetOfValues<E_orientation_requested_supported>
        orientation_requested_supported{
            this, AttrName::orientation_requested_supported};
    SingleValue<E_output_bin_default> output_bin_default{
        this, AttrName::output_bin_default};
    OpenSetOfValues<E_output_bin_supported> output_bin_supported{
        this, AttrName::output_bin_supported};
    SetOfValues<StringWithLanguage> output_device_supported{
        this, AttrName::output_device_supported};
    SetOfValues<std::string> output_device_uuid_supported{
        this, AttrName::output_device_uuid_supported};
    SingleValue<E_page_delivery_default> page_delivery_default{
        this, AttrName::page_delivery_default};
    SetOfValues<E_page_delivery_supported> page_delivery_supported{
        this, AttrName::page_delivery_supported};
    SingleValue<E_page_order_received_default> page_order_received_default{
        this, AttrName::page_order_received_default};
    SetOfValues<E_page_order_received_supported> page_order_received_supported{
        this, AttrName::page_order_received_supported};
    SingleValue<bool> page_ranges_supported{this,
                                            AttrName::page_ranges_supported};
    SingleValue<bool> pages_per_subset_supported{
        this, AttrName::pages_per_subset_supported};
    SetOfValues<std::string> parent_printers_supported{
        this, AttrName::parent_printers_supported};
    SingleValue<RangeOfInteger> pdf_k_octets_supported{
        this, AttrName::pdf_k_octets_supported};
    SetOfValues<E_pdf_versions_supported> pdf_versions_supported{
        this, AttrName::pdf_versions_supported};
    SingleCollection<C_pdl_init_file_default> pdl_init_file_default{
        this, AttrName::pdl_init_file_default};
    SetOfValues<StringWithLanguage> pdl_init_file_entry_supported{
        this, AttrName::pdl_init_file_entry_supported};
    SetOfValues<std::string> pdl_init_file_location_supported{
        this, AttrName::pdl_init_file_location_supported};
    SingleValue<bool> pdl_init_file_name_subdirectory_supported{
        this, AttrName::pdl_init_file_name_subdirectory_supported};
    SetOfValues<StringWithLanguage> pdl_init_file_name_supported{
        this, AttrName::pdl_init_file_name_supported};
    SetOfValues<E_pdl_init_file_supported> pdl_init_file_supported{
        this, AttrName::pdl_init_file_supported};
    SingleValue<E_pdl_override_supported> pdl_override_supported{
        this, AttrName::pdl_override_supported};
    SingleValue<bool> preferred_attributes_supported{
        this, AttrName::preferred_attributes_supported};
    SingleValue<E_presentation_direction_number_up_default>
        presentation_direction_number_up_default{
            this, AttrName::presentation_direction_number_up_default};
    SetOfValues<E_presentation_direction_number_up_supported>
        presentation_direction_number_up_supported{
            this, AttrName::presentation_direction_number_up_supported};
    SingleValue<E_print_color_mode_default> print_color_mode_default{
        this, AttrName::print_color_mode_default};
    SetOfValues<E_print_color_mode_supported> print_color_mode_supported{
        this, AttrName::print_color_mode_supported};
    SingleValue<E_print_content_optimize_default>
        print_content_optimize_default{
            this, AttrName::print_content_optimize_default};
    SetOfValues<E_print_content_optimize_supported>
        print_content_optimize_supported{
            this, AttrName::print_content_optimize_supported};
    SingleValue<E_print_quality_default> print_quality_default{
        this, AttrName::print_quality_default};
    SetOfValues<E_print_quality_supported> print_quality_supported{
        this, AttrName::print_quality_supported};
    SingleValue<E_print_rendering_intent_default>
        print_rendering_intent_default{
            this, AttrName::print_rendering_intent_default};
    SetOfValues<E_print_rendering_intent_supported>
        print_rendering_intent_supported{
            this, AttrName::print_rendering_intent_supported};
    SingleValue<StringWithLanguage> printer_charge_info{
        this, AttrName::printer_charge_info};
    SingleValue<std::string> printer_charge_info_uri{
        this, AttrName::printer_charge_info_uri};
    SingleCollection<C_printer_contact_col> printer_contact_col{
        this, AttrName::printer_contact_col};
    SingleValue<DateTime> printer_current_time{this,
                                               AttrName::printer_current_time};
    SingleValue<StringWithLanguage> printer_device_id{
        this, AttrName::printer_device_id};
    SingleValue<StringWithLanguage> printer_dns_sd_name{
        this, AttrName::printer_dns_sd_name};
    SingleValue<std::string> printer_driver_installer{
        this, AttrName::printer_driver_installer};
    SingleValue<std::string> printer_geo_location{
        this, AttrName::printer_geo_location};
    SetOfCollections<C_printer_icc_profiles> printer_icc_profiles{
        this, AttrName::printer_icc_profiles};
    SetOfValues<std::string> printer_icons{this, AttrName::printer_icons};
    SingleValue<StringWithLanguage> printer_info{this, AttrName::printer_info};
    SingleValue<StringWithLanguage> printer_location{
        this, AttrName::printer_location};
    SingleValue<StringWithLanguage> printer_make_and_model{
        this, AttrName::printer_make_and_model};
    SingleValue<std::string> printer_more_info_manufacturer{
        this, AttrName::printer_more_info_manufacturer};
    SingleValue<StringWithLanguage> printer_name{this, AttrName::printer_name};
    SetOfValues<StringWithLanguage> printer_organization{
        this, AttrName::printer_organization};
    SetOfValues<StringWithLanguage> printer_organizational_unit{
        this, AttrName::printer_organizational_unit};
    SingleValue<Resolution> printer_resolution_default{
        this, AttrName::printer_resolution_default};
    SingleValue<Resolution> printer_resolution_supported{
        this, AttrName::printer_resolution_supported};
    SingleValue<std::string> printer_static_resource_directory_uri{
        this, AttrName::printer_static_resource_directory_uri};
    SingleValue<int> printer_static_resource_k_octets_supported{
        this, AttrName::printer_static_resource_k_octets_supported};
    SetOfValues<std::string> printer_strings_languages_supported{
        this, AttrName::printer_strings_languages_supported};
    SingleValue<std::string> printer_strings_uri{this,
                                                 AttrName::printer_strings_uri};
    SetOfCollections<C_printer_xri_supported> printer_xri_supported{
        this, AttrName::printer_xri_supported};
    SingleCollection<C_proof_print_default> proof_print_default{
        this, AttrName::proof_print_default};
    SetOfValues<E_proof_print_supported> proof_print_supported{
        this, AttrName::proof_print_supported};
    SingleValue<int> punching_hole_diameter_configured{
        this, AttrName::punching_hole_diameter_configured};
    SetOfValues<RangeOfInteger> punching_locations_supported{
        this, AttrName::punching_locations_supported};
    SetOfValues<RangeOfInteger> punching_offset_supported{
        this, AttrName::punching_offset_supported};
    SetOfValues<E_punching_reference_edge_supported>
        punching_reference_edge_supported{
            this, AttrName::punching_reference_edge_supported};
    SetOfValues<Resolution> pwg_raster_document_resolution_supported{
        this, AttrName::pwg_raster_document_resolution_supported};
    SingleValue<E_pwg_raster_document_sheet_back>
        pwg_raster_document_sheet_back{
            this, AttrName::pwg_raster_document_sheet_back};
    SetOfValues<E_pwg_raster_document_type_supported>
        pwg_raster_document_type_supported{
            this, AttrName::pwg_raster_document_type_supported};
    SetOfValues<std::string> reference_uri_schemes_supported{
        this, AttrName::reference_uri_schemes_supported};
    SingleValue<bool> requesting_user_uri_supported{
        this, AttrName::requesting_user_uri_supported};
    SetOfValues<E_save_disposition_supported> save_disposition_supported{
        this, AttrName::save_disposition_supported};
    SingleValue<std::string> save_document_format_default{
        this, AttrName::save_document_format_default};
    SetOfValues<std::string> save_document_format_supported{
        this, AttrName::save_document_format_supported};
    SingleValue<std::string> save_location_default{
        this, AttrName::save_location_default};
    SetOfValues<std::string> save_location_supported{
        this, AttrName::save_location_supported};
    SingleValue<bool> save_name_subdirectory_supported{
        this, AttrName::save_name_subdirectory_supported};
    SingleValue<bool> save_name_supported{this, AttrName::save_name_supported};
    SingleCollection<C_separator_sheets_default> separator_sheets_default{
        this, AttrName::separator_sheets_default};
    SingleValue<E_sheet_collate_default> sheet_collate_default{
        this, AttrName::sheet_collate_default};
    SetOfValues<E_sheet_collate_supported> sheet_collate_supported{
        this, AttrName::sheet_collate_supported};
    SingleValue<E_sides_default> sides_default{this, AttrName::sides_default};
    SetOfValues<E_sides_supported> sides_supported{this,
                                                   AttrName::sides_supported};
    SetOfValues<RangeOfInteger> stitching_angle_supported{
        this, AttrName::stitching_angle_supported};
    SetOfValues<RangeOfInteger> stitching_locations_supported{
        this, AttrName::stitching_locations_supported};
    SetOfValues<E_stitching_method_supported> stitching_method_supported{
        this, AttrName::stitching_method_supported};
    SetOfValues<RangeOfInteger> stitching_offset_supported{
        this, AttrName::stitching_offset_supported};
    SetOfValues<E_stitching_reference_edge_supported>
        stitching_reference_edge_supported{
            this, AttrName::stitching_reference_edge_supported};
    SetOfValues<std::string> subordinate_printers_supported{
        this, AttrName::subordinate_printers_supported};
    SetOfValues<RangeOfInteger> trimming_offset_supported{
        this, AttrName::trimming_offset_supported};
    SetOfValues<E_trimming_reference_edge_supported>
        trimming_reference_edge_supported{
            this, AttrName::trimming_reference_edge_supported};
    SetOfValues<E_trimming_type_supported> trimming_type_supported{
        this, AttrName::trimming_type_supported};
    SetOfValues<E_trimming_when_supported> trimming_when_supported{
        this, AttrName::trimming_when_supported};
    SetOfValues<E_uri_authentication_supported> uri_authentication_supported{
        this, AttrName::uri_authentication_supported};
    SetOfValues<E_uri_security_supported> uri_security_supported{
        this, AttrName::uri_security_supported};
    SetOfValues<E_which_jobs_supported> which_jobs_supported{
        this, AttrName::which_jobs_supported};
    SingleValue<E_x_image_position_default> x_image_position_default{
        this, AttrName::x_image_position_default};
    SetOfValues<E_x_image_position_supported> x_image_position_supported{
        this, AttrName::x_image_position_supported};
    SingleValue<int> x_image_shift_default{this,
                                           AttrName::x_image_shift_default};
    SingleValue<RangeOfInteger> x_image_shift_supported{
        this, AttrName::x_image_shift_supported};
    SingleValue<int> x_side1_image_shift_default{
        this, AttrName::x_side1_image_shift_default};
    SingleValue<RangeOfInteger> x_side1_image_shift_supported{
        this, AttrName::x_side1_image_shift_supported};
    SingleValue<int> x_side2_image_shift_default{
        this, AttrName::x_side2_image_shift_default};
    SingleValue<RangeOfInteger> x_side2_image_shift_supported{
        this, AttrName::x_side2_image_shift_supported};
    SingleValue<E_y_image_position_default> y_image_position_default{
        this, AttrName::y_image_position_default};
    SetOfValues<E_y_image_position_supported> y_image_position_supported{
        this, AttrName::y_image_position_supported};
    SingleValue<int> y_image_shift_default{this,
                                           AttrName::y_image_shift_default};
    SingleValue<RangeOfInteger> y_image_shift_supported{
        this, AttrName::y_image_shift_supported};
    SingleValue<int> y_side1_image_shift_default{
        this, AttrName::y_side1_image_shift_default};
    SingleValue<RangeOfInteger> y_side1_image_shift_supported{
        this, AttrName::y_side1_image_shift_supported};
    SingleValue<int> y_side2_image_shift_default{
        this, AttrName::y_side2_image_shift_default};
    SingleValue<RangeOfInteger> y_side2_image_shift_supported{
        this, AttrName::y_side2_image_shift_supported};
    G_printer_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_printer_attributes> printer_attributes{
      GroupTag::printer_attributes};
  Response_CUPS_Get_Default();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_CUPS_Get_Document : public Request {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<std::string> printer_uri{this, AttrName::printer_uri};
    SingleValue<int> job_id{this, AttrName::job_id};
    SingleValue<std::string> job_uri{this, AttrName::job_uri};
    SingleValue<int> document_number{this, AttrName::document_number};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Request_CUPS_Get_Document();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_CUPS_Get_Document : public Response {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<StringWithLanguage> status_message{this,
                                                   AttrName::status_message};
    SingleValue<StringWithLanguage> detailed_status_message{
        this, AttrName::detailed_status_message};
    SingleValue<std::string> document_format{this, AttrName::document_format};
    SingleValue<int> document_number{this, AttrName::document_number};
    SingleValue<StringWithLanguage> document_name{this,
                                                  AttrName::document_name};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Response_CUPS_Get_Document();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_CUPS_Get_Printers : public Request {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<StringWithLanguage> first_printer_name{
        this, AttrName::first_printer_name};
    SingleValue<int> limit{this, AttrName::limit};
    SingleValue<int> printer_id{this, AttrName::printer_id};
    SingleValue<StringWithLanguage> printer_location{
        this, AttrName::printer_location};
    SingleValue<int> printer_type{this, AttrName::printer_type};
    SingleValue<int> printer_type_mask{this, AttrName::printer_type_mask};
    OpenSetOfValues<E_requested_attributes> requested_attributes{
        this, AttrName::requested_attributes};
    SingleValue<StringWithLanguage> requested_user_name{
        this, AttrName::requested_user_name};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Request_CUPS_Get_Printers();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_CUPS_Get_Printers : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  typedef Response_CUPS_Get_Default::G_printer_attributes G_printer_attributes;
  SetOfGroups<G_printer_attributes> printer_attributes{
      GroupTag::printer_attributes};
  Response_CUPS_Get_Printers();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_CUPS_Move_Job : public Request {
  typedef Request_CUPS_Authenticate_Job::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  struct G_job_attributes : public Collection {
    SingleValue<std::string> job_printer_uri{this, AttrName::job_printer_uri};
    G_job_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_job_attributes> job_attributes{GroupTag::job_attributes};
  Request_CUPS_Move_Job();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_CUPS_Move_Job : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Response_CUPS_Move_Job();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_CUPS_Set_Default : public Request {
  typedef Request_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Request_CUPS_Set_Default();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_CUPS_Set_Default : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Response_CUPS_Set_Default();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_Cancel_Job : public Request {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<std::string> printer_uri{this, AttrName::printer_uri};
    SingleValue<int> job_id{this, AttrName::job_id};
    SingleValue<std::string> job_uri{this, AttrName::job_uri};
    SingleValue<StringWithLanguage> requesting_user_name{
        this, AttrName::requesting_user_name};
    SingleValue<StringWithLanguage> message{this, AttrName::message};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Request_Cancel_Job();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_Cancel_Job : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  typedef Response_CUPS_Authenticate_Job::G_unsupported_attributes
      G_unsupported_attributes;
  SingleGroup<G_unsupported_attributes> unsupported_attributes{
      GroupTag::unsupported_attributes};
  Response_Cancel_Job();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_Create_Job : public Request {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<std::string> printer_uri{this, AttrName::printer_uri};
    SingleValue<StringWithLanguage> requesting_user_name{
        this, AttrName::requesting_user_name};
    SingleValue<StringWithLanguage> job_name{this, AttrName::job_name};
    SingleValue<bool> ipp_attribute_fidelity{this,
                                             AttrName::ipp_attribute_fidelity};
    SingleValue<int> job_k_octets{this, AttrName::job_k_octets};
    SingleValue<int> job_impressions{this, AttrName::job_impressions};
    SingleValue<int> job_media_sheets{this, AttrName::job_media_sheets};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  struct G_job_attributes : public Collection {
    struct C_cover_back : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_cover_back() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_cover_front : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_cover_front() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_finishings_col : public Collection {
      struct C_baling : public Collection {
        SingleValue<E_baling_type> baling_type{this, AttrName::baling_type};
        SingleValue<E_baling_when> baling_when{this, AttrName::baling_when};
        C_baling() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_binding : public Collection {
        SingleValue<E_binding_reference_edge> binding_reference_edge{
            this, AttrName::binding_reference_edge};
        SingleValue<E_binding_type> binding_type{this, AttrName::binding_type};
        C_binding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_coating : public Collection {
        SingleValue<E_coating_sides> coating_sides{this,
                                                   AttrName::coating_sides};
        SingleValue<E_coating_type> coating_type{this, AttrName::coating_type};
        C_coating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_covering : public Collection {
        SingleValue<E_covering_name> covering_name{this,
                                                   AttrName::covering_name};
        C_covering() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_folding : public Collection {
        SingleValue<E_folding_direction> folding_direction{
            this, AttrName::folding_direction};
        SingleValue<int> folding_offset{this, AttrName::folding_offset};
        SingleValue<E_folding_reference_edge> folding_reference_edge{
            this, AttrName::folding_reference_edge};
        C_folding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_laminating : public Collection {
        SingleValue<E_laminating_sides> laminating_sides{
            this, AttrName::laminating_sides};
        SingleValue<E_laminating_type> laminating_type{
            this, AttrName::laminating_type};
        C_laminating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_media_size : public Collection {
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_punching : public Collection {
        SetOfValues<int> punching_locations{this, AttrName::punching_locations};
        SingleValue<int> punching_offset{this, AttrName::punching_offset};
        SingleValue<E_punching_reference_edge> punching_reference_edge{
            this, AttrName::punching_reference_edge};
        C_punching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_stitching : public Collection {
        SingleValue<int> stitching_angle{this, AttrName::stitching_angle};
        SetOfValues<int> stitching_locations{this,
                                             AttrName::stitching_locations};
        SingleValue<E_stitching_method> stitching_method{
            this, AttrName::stitching_method};
        SingleValue<int> stitching_offset{this, AttrName::stitching_offset};
        SingleValue<E_stitching_reference_edge> stitching_reference_edge{
            this, AttrName::stitching_reference_edge};
        C_stitching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_trimming : public Collection {
        SingleValue<int> trimming_offset{this, AttrName::trimming_offset};
        SingleValue<E_trimming_reference_edge> trimming_reference_edge{
            this, AttrName::trimming_reference_edge};
        SingleValue<E_trimming_type> trimming_type{this,
                                                   AttrName::trimming_type};
        SingleValue<E_trimming_when> trimming_when{this,
                                                   AttrName::trimming_when};
        C_trimming() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleCollection<C_baling> baling{this, AttrName::baling};
      SingleCollection<C_binding> binding{this, AttrName::binding};
      SingleCollection<C_coating> coating{this, AttrName::coating};
      SingleCollection<C_covering> covering{this, AttrName::covering};
      SingleValue<E_finishing_template> finishing_template{
          this, AttrName::finishing_template};
      SetOfCollections<C_folding> folding{this, AttrName::folding};
      SingleValue<E_imposition_template> imposition_template{
          this, AttrName::imposition_template};
      SingleCollection<C_laminating> laminating{this, AttrName::laminating};
      SingleValue<RangeOfInteger> media_sheets_supported{
          this, AttrName::media_sheets_supported};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleCollection<C_punching> punching{this, AttrName::punching};
      SingleCollection<C_stitching> stitching{this, AttrName::stitching};
      SetOfCollections<C_trimming> trimming{this, AttrName::trimming};
      C_finishings_col() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_insert_sheet : public Collection {
      SingleValue<int> insert_after_page_number{
          this, AttrName::insert_after_page_number};
      SingleValue<int> insert_count{this, AttrName::insert_count};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_insert_sheet() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_accounting_sheets : public Collection {
      SingleValue<E_job_accounting_output_bin> job_accounting_output_bin{
          this, AttrName::job_accounting_output_bin};
      SingleValue<E_job_accounting_sheets_type> job_accounting_sheets_type{
          this, AttrName::job_accounting_sheets_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_accounting_sheets() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_cover_back : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_cover_back() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_cover_front : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_cover_front() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_error_sheet : public Collection {
      SingleValue<E_job_error_sheet_type> job_error_sheet_type{
          this, AttrName::job_error_sheet_type};
      SingleValue<E_job_error_sheet_when> job_error_sheet_when{
          this, AttrName::job_error_sheet_when};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_error_sheet() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_finishings_col : public Collection {
      struct C_baling : public Collection {
        SingleValue<E_baling_type> baling_type{this, AttrName::baling_type};
        SingleValue<E_baling_when> baling_when{this, AttrName::baling_when};
        C_baling() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_binding : public Collection {
        SingleValue<E_binding_reference_edge> binding_reference_edge{
            this, AttrName::binding_reference_edge};
        SingleValue<E_binding_type> binding_type{this, AttrName::binding_type};
        C_binding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_coating : public Collection {
        SingleValue<E_coating_sides> coating_sides{this,
                                                   AttrName::coating_sides};
        SingleValue<E_coating_type> coating_type{this, AttrName::coating_type};
        C_coating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_covering : public Collection {
        SingleValue<E_covering_name> covering_name{this,
                                                   AttrName::covering_name};
        C_covering() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_folding : public Collection {
        SingleValue<E_folding_direction> folding_direction{
            this, AttrName::folding_direction};
        SingleValue<int> folding_offset{this, AttrName::folding_offset};
        SingleValue<E_folding_reference_edge> folding_reference_edge{
            this, AttrName::folding_reference_edge};
        C_folding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_laminating : public Collection {
        SingleValue<E_laminating_sides> laminating_sides{
            this, AttrName::laminating_sides};
        SingleValue<E_laminating_type> laminating_type{
            this, AttrName::laminating_type};
        C_laminating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_media_size : public Collection {
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_punching : public Collection {
        SetOfValues<int> punching_locations{this, AttrName::punching_locations};
        SingleValue<int> punching_offset{this, AttrName::punching_offset};
        SingleValue<E_punching_reference_edge> punching_reference_edge{
            this, AttrName::punching_reference_edge};
        C_punching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_stitching : public Collection {
        SingleValue<int> stitching_angle{this, AttrName::stitching_angle};
        SetOfValues<int> stitching_locations{this,
                                             AttrName::stitching_locations};
        SingleValue<E_stitching_method> stitching_method{
            this, AttrName::stitching_method};
        SingleValue<int> stitching_offset{this, AttrName::stitching_offset};
        SingleValue<E_stitching_reference_edge> stitching_reference_edge{
            this, AttrName::stitching_reference_edge};
        C_stitching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_trimming : public Collection {
        SingleValue<int> trimming_offset{this, AttrName::trimming_offset};
        SingleValue<E_trimming_reference_edge> trimming_reference_edge{
            this, AttrName::trimming_reference_edge};
        SingleValue<E_trimming_type> trimming_type{this,
                                                   AttrName::trimming_type};
        SingleValue<E_trimming_when> trimming_when{this,
                                                   AttrName::trimming_when};
        C_trimming() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleCollection<C_baling> baling{this, AttrName::baling};
      SingleCollection<C_binding> binding{this, AttrName::binding};
      SingleCollection<C_coating> coating{this, AttrName::coating};
      SingleCollection<C_covering> covering{this, AttrName::covering};
      SingleValue<E_finishing_template> finishing_template{
          this, AttrName::finishing_template};
      SetOfCollections<C_folding> folding{this, AttrName::folding};
      SingleValue<E_imposition_template> imposition_template{
          this, AttrName::imposition_template};
      SingleCollection<C_laminating> laminating{this, AttrName::laminating};
      SingleValue<RangeOfInteger> media_sheets_supported{
          this, AttrName::media_sheets_supported};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleCollection<C_punching> punching{this, AttrName::punching};
      SingleCollection<C_stitching> stitching{this, AttrName::stitching};
      SetOfCollections<C_trimming> trimming{this, AttrName::trimming};
      C_job_finishings_col() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_save_disposition : public Collection {
      struct C_save_info : public Collection {
        SingleValue<std::string> save_document_format{
            this, AttrName::save_document_format};
        SingleValue<std::string> save_location{this, AttrName::save_location};
        SingleValue<StringWithLanguage> save_name{this, AttrName::save_name};
        C_save_info() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleValue<E_save_disposition> save_disposition{
          this, AttrName::save_disposition};
      SetOfCollections<C_save_info> save_info{this, AttrName::save_info};
      C_job_save_disposition() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_sheets_col : public Collection {
      SingleValue<E_job_sheets> job_sheets{this, AttrName::job_sheets};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_sheets_col() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_media_col : public Collection {
      struct C_media_size : public Collection {
        SingleValue<int> x_dimension{this, AttrName::x_dimension};
        SingleValue<int> y_dimension{this, AttrName::y_dimension};
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleValue<E_media_back_coating> media_back_coating{
          this, AttrName::media_back_coating};
      SingleValue<int> media_bottom_margin{this, AttrName::media_bottom_margin};
      SingleValue<E_media_color> media_color{this, AttrName::media_color};
      SingleValue<E_media_front_coating> media_front_coating{
          this, AttrName::media_front_coating};
      SingleValue<E_media_grain> media_grain{this, AttrName::media_grain};
      SingleValue<int> media_hole_count{this, AttrName::media_hole_count};
      SingleValue<StringWithLanguage> media_info{this, AttrName::media_info};
      SingleValue<E_media_key> media_key{this, AttrName::media_key};
      SingleValue<int> media_left_margin{this, AttrName::media_left_margin};
      SingleValue<int> media_order_count{this, AttrName::media_order_count};
      SingleValue<E_media_pre_printed> media_pre_printed{
          this, AttrName::media_pre_printed};
      SingleValue<E_media_recycled> media_recycled{this,
                                                   AttrName::media_recycled};
      SingleValue<int> media_right_margin{this, AttrName::media_right_margin};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleValue<E_media_source> media_source{this, AttrName::media_source};
      SingleValue<int> media_thickness{this, AttrName::media_thickness};
      SingleValue<E_media_tooth> media_tooth{this, AttrName::media_tooth};
      SingleValue<int> media_top_margin{this, AttrName::media_top_margin};
      SingleValue<E_media_type> media_type{this, AttrName::media_type};
      SingleValue<int> media_weight_metric{this, AttrName::media_weight_metric};
      C_media_col() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_overrides : public Collection {
      SingleValue<StringWithLanguage> job_account_id{this,
                                                     AttrName::job_account_id};
      SingleValue<E_job_account_type> job_account_type{
          this, AttrName::job_account_type};
      SingleCollection<C_job_accounting_sheets> job_accounting_sheets{
          this, AttrName::job_accounting_sheets};
      SingleValue<StringWithLanguage> job_accounting_user_id{
          this, AttrName::job_accounting_user_id};
      SingleValue<int> job_copies{this, AttrName::job_copies};
      SingleCollection<C_job_cover_back> job_cover_back{
          this, AttrName::job_cover_back};
      SingleCollection<C_job_cover_front> job_cover_front{
          this, AttrName::job_cover_front};
      SingleValue<E_job_delay_output_until> job_delay_output_until{
          this, AttrName::job_delay_output_until};
      SingleValue<DateTime> job_delay_output_until_time{
          this, AttrName::job_delay_output_until_time};
      SingleValue<E_job_error_action> job_error_action{
          this, AttrName::job_error_action};
      SingleCollection<C_job_error_sheet> job_error_sheet{
          this, AttrName::job_error_sheet};
      SetOfValues<E_job_finishings> job_finishings{this,
                                                   AttrName::job_finishings};
      SetOfCollections<C_job_finishings_col> job_finishings_col{
          this, AttrName::job_finishings_col};
      SingleValue<E_job_hold_until> job_hold_until{this,
                                                   AttrName::job_hold_until};
      SingleValue<DateTime> job_hold_until_time{this,
                                                AttrName::job_hold_until_time};
      SingleValue<StringWithLanguage> job_message_to_operator{
          this, AttrName::job_message_to_operator};
      SingleValue<int> job_pages_per_set{this, AttrName::job_pages_per_set};
      SingleValue<std::string> job_phone_number{this,
                                                AttrName::job_phone_number};
      SingleValue<int> job_priority{this, AttrName::job_priority};
      SingleValue<StringWithLanguage> job_recipient_name{
          this, AttrName::job_recipient_name};
      SingleCollection<C_job_save_disposition> job_save_disposition{
          this, AttrName::job_save_disposition};
      SingleValue<StringWithLanguage> job_sheet_message{
          this, AttrName::job_sheet_message};
      SingleValue<E_job_sheets> job_sheets{this, AttrName::job_sheets};
      SingleCollection<C_job_sheets_col> job_sheets_col{
          this, AttrName::job_sheets_col};
      SetOfValues<int> pages_per_subset{this, AttrName::pages_per_subset};
      SingleValue<E_output_bin> output_bin{this, AttrName::output_bin};
      SingleValue<StringWithLanguage> output_device{this,
                                                    AttrName::output_device};
      SingleValue<E_multiple_document_handling> multiple_document_handling{
          this, AttrName::multiple_document_handling};
      SingleValue<int> y_side1_image_shift{this, AttrName::y_side1_image_shift};
      SingleValue<int> y_side2_image_shift{this, AttrName::y_side2_image_shift};
      SingleValue<int> number_up{this, AttrName::number_up};
      SingleValue<E_orientation_requested> orientation_requested{
          this, AttrName::orientation_requested};
      SingleValue<E_page_delivery> page_delivery{this, AttrName::page_delivery};
      SingleValue<E_page_order_received> page_order_received{
          this, AttrName::page_order_received};
      SetOfValues<RangeOfInteger> page_ranges{this, AttrName::page_ranges};
      SetOfCollections<C_pdl_init_file> pdl_init_file{this,
                                                      AttrName::pdl_init_file};
      SingleValue<E_print_color_mode> print_color_mode{
          this, AttrName::print_color_mode};
      SingleValue<E_print_content_optimize> print_content_optimize{
          this, AttrName::print_content_optimize};
      SingleValue<E_print_quality> print_quality{this, AttrName::print_quality};
      SingleValue<E_print_rendering_intent> print_rendering_intent{
          this, AttrName::print_rendering_intent};
      SingleValue<Resolution> printer_resolution{this,
                                                 AttrName::printer_resolution};
      SingleValue<E_presentation_direction_number_up>
          presentation_direction_number_up{
              this, AttrName::presentation_direction_number_up};
      SingleValue<E_media> media{this, AttrName::media};
      SingleValue<E_sides> sides{this, AttrName::sides};
      SingleValue<E_x_image_position> x_image_position{
          this, AttrName::x_image_position};
      SingleValue<int> x_image_shift{this, AttrName::x_image_shift};
      SingleValue<int> x_side1_image_shift{this, AttrName::x_side1_image_shift};
      SingleValue<int> x_side2_image_shift{this, AttrName::x_side2_image_shift};
      SingleValue<E_y_image_position> y_image_position{
          this, AttrName::y_image_position};
      SingleValue<int> y_image_shift{this, AttrName::y_image_shift};
      SingleValue<int> copies{this, AttrName::copies};
      SingleCollection<C_cover_back> cover_back{this, AttrName::cover_back};
      SingleCollection<C_cover_front> cover_front{this, AttrName::cover_front};
      SingleValue<E_imposition_template> imposition_template{
          this, AttrName::imposition_template};
      SetOfCollections<C_insert_sheet> insert_sheet{this,
                                                    AttrName::insert_sheet};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      SingleValue<E_media_input_tray_check> media_input_tray_check{
          this, AttrName::media_input_tray_check};
      SingleValue<E_print_scaling> print_scaling{this, AttrName::print_scaling};
      SingleCollection<C_proof_print> proof_print{this, AttrName::proof_print};
      SingleCollection<C_separator_sheets> separator_sheets{
          this, AttrName::separator_sheets};
      SingleValue<E_sheet_collate> sheet_collate{this, AttrName::sheet_collate};
      SingleValue<E_feed_orientation> feed_orientation{
          this, AttrName::feed_orientation};
      SetOfValues<E_finishings> finishings{this, AttrName::finishings};
      SetOfCollections<C_finishings_col> finishings_col{
          this, AttrName::finishings_col};
      SingleValue<StringWithLanguage> font_name_requested{
          this, AttrName::font_name_requested};
      SingleValue<int> font_size_requested{this, AttrName::font_size_requested};
      SetOfValues<int> force_front_side{this, AttrName::force_front_side};
      SetOfValues<RangeOfInteger> document_copies{this,
                                                  AttrName::document_copies};
      SetOfValues<RangeOfInteger> document_numbers{this,
                                                   AttrName::document_numbers};
      SetOfValues<RangeOfInteger> pages{this, AttrName::pages};
      C_overrides() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_pdl_init_file : public Collection {
      SingleValue<StringWithLanguage> pdl_init_file_entry{
          this, AttrName::pdl_init_file_entry};
      SingleValue<std::string> pdl_init_file_location{
          this, AttrName::pdl_init_file_location};
      SingleValue<StringWithLanguage> pdl_init_file_name{
          this, AttrName::pdl_init_file_name};
      C_pdl_init_file() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_proof_print : public Collection {
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      SingleValue<int> proof_print_copies{this, AttrName::proof_print_copies};
      C_proof_print() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_separator_sheets : public Collection {
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      SetOfValues<E_separator_sheets_type> separator_sheets_type{
          this, AttrName::separator_sheets_type};
      C_separator_sheets() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    SingleValue<int> copies{this, AttrName::copies};
    SingleCollection<C_cover_back> cover_back{this, AttrName::cover_back};
    SingleCollection<C_cover_front> cover_front{this, AttrName::cover_front};
    SingleValue<E_feed_orientation> feed_orientation{
        this, AttrName::feed_orientation};
    SetOfValues<E_finishings> finishings{this, AttrName::finishings};
    SetOfCollections<C_finishings_col> finishings_col{this,
                                                      AttrName::finishings_col};
    SingleValue<StringWithLanguage> font_name_requested{
        this, AttrName::font_name_requested};
    SingleValue<int> font_size_requested{this, AttrName::font_size_requested};
    SetOfValues<int> force_front_side{this, AttrName::force_front_side};
    SingleValue<E_imposition_template> imposition_template{
        this, AttrName::imposition_template};
    SetOfCollections<C_insert_sheet> insert_sheet{this, AttrName::insert_sheet};
    SingleValue<StringWithLanguage> job_account_id{this,
                                                   AttrName::job_account_id};
    SingleValue<E_job_account_type> job_account_type{
        this, AttrName::job_account_type};
    SingleCollection<C_job_accounting_sheets> job_accounting_sheets{
        this, AttrName::job_accounting_sheets};
    SingleValue<StringWithLanguage> job_accounting_user_id{
        this, AttrName::job_accounting_user_id};
    SingleValue<int> job_copies{this, AttrName::job_copies};
    SingleCollection<C_job_cover_back> job_cover_back{this,
                                                      AttrName::job_cover_back};
    SingleCollection<C_job_cover_front> job_cover_front{
        this, AttrName::job_cover_front};
    SingleValue<E_job_delay_output_until> job_delay_output_until{
        this, AttrName::job_delay_output_until};
    SingleValue<DateTime> job_delay_output_until_time{
        this, AttrName::job_delay_output_until_time};
    SingleValue<E_job_error_action> job_error_action{
        this, AttrName::job_error_action};
    SingleCollection<C_job_error_sheet> job_error_sheet{
        this, AttrName::job_error_sheet};
    SetOfValues<E_job_finishings> job_finishings{this,
                                                 AttrName::job_finishings};
    SetOfCollections<C_job_finishings_col> job_finishings_col{
        this, AttrName::job_finishings_col};
    SingleValue<E_job_hold_until> job_hold_until{this,
                                                 AttrName::job_hold_until};
    SingleValue<DateTime> job_hold_until_time{this,
                                              AttrName::job_hold_until_time};
    SingleValue<StringWithLanguage> job_message_to_operator{
        this, AttrName::job_message_to_operator};
    SingleValue<int> job_pages_per_set{this, AttrName::job_pages_per_set};
    SingleValue<std::string> job_phone_number{this, AttrName::job_phone_number};
    SingleValue<int> job_priority{this, AttrName::job_priority};
    SingleValue<StringWithLanguage> job_recipient_name{
        this, AttrName::job_recipient_name};
    SingleCollection<C_job_save_disposition> job_save_disposition{
        this, AttrName::job_save_disposition};
    SingleValue<StringWithLanguage> job_sheet_message{
        this, AttrName::job_sheet_message};
    SingleValue<E_job_sheets> job_sheets{this, AttrName::job_sheets};
    SingleCollection<C_job_sheets_col> job_sheets_col{this,
                                                      AttrName::job_sheets_col};
    SingleValue<E_media> media{this, AttrName::media};
    SingleCollection<C_media_col> media_col{this, AttrName::media_col};
    SingleValue<E_media_input_tray_check> media_input_tray_check{
        this, AttrName::media_input_tray_check};
    SingleValue<E_multiple_document_handling> multiple_document_handling{
        this, AttrName::multiple_document_handling};
    SingleValue<int> number_up{this, AttrName::number_up};
    SingleValue<E_orientation_requested> orientation_requested{
        this, AttrName::orientation_requested};
    SingleValue<E_output_bin> output_bin{this, AttrName::output_bin};
    SingleValue<StringWithLanguage> output_device{this,
                                                  AttrName::output_device};
    SetOfCollections<C_overrides> overrides{this, AttrName::overrides};
    SingleValue<E_page_delivery> page_delivery{this, AttrName::page_delivery};
    SingleValue<E_page_order_received> page_order_received{
        this, AttrName::page_order_received};
    SetOfValues<RangeOfInteger> page_ranges{this, AttrName::page_ranges};
    SetOfValues<int> pages_per_subset{this, AttrName::pages_per_subset};
    SetOfCollections<C_pdl_init_file> pdl_init_file{this,
                                                    AttrName::pdl_init_file};
    SingleValue<E_presentation_direction_number_up>
        presentation_direction_number_up{
            this, AttrName::presentation_direction_number_up};
    SingleValue<E_print_color_mode> print_color_mode{
        this, AttrName::print_color_mode};
    SingleValue<E_print_content_optimize> print_content_optimize{
        this, AttrName::print_content_optimize};
    SingleValue<E_print_quality> print_quality{this, AttrName::print_quality};
    SingleValue<E_print_rendering_intent> print_rendering_intent{
        this, AttrName::print_rendering_intent};
    SingleValue<E_print_scaling> print_scaling{this, AttrName::print_scaling};
    SingleValue<Resolution> printer_resolution{this,
                                               AttrName::printer_resolution};
    SingleCollection<C_proof_print> proof_print{this, AttrName::proof_print};
    SingleCollection<C_separator_sheets> separator_sheets{
        this, AttrName::separator_sheets};
    SingleValue<E_sheet_collate> sheet_collate{this, AttrName::sheet_collate};
    SingleValue<E_sides> sides{this, AttrName::sides};
    SingleValue<E_x_image_position> x_image_position{
        this, AttrName::x_image_position};
    SingleValue<int> x_image_shift{this, AttrName::x_image_shift};
    SingleValue<int> x_side1_image_shift{this, AttrName::x_side1_image_shift};
    SingleValue<int> x_side2_image_shift{this, AttrName::x_side2_image_shift};
    SingleValue<E_y_image_position> y_image_position{
        this, AttrName::y_image_position};
    SingleValue<int> y_image_shift{this, AttrName::y_image_shift};
    SingleValue<int> y_side1_image_shift{this, AttrName::y_side1_image_shift};
    SingleValue<int> y_side2_image_shift{this, AttrName::y_side2_image_shift};
    G_job_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_job_attributes> job_attributes{GroupTag::job_attributes};
  Request_Create_Job();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_Create_Job : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  typedef Response_CUPS_Authenticate_Job::G_unsupported_attributes
      G_unsupported_attributes;
  SingleGroup<G_unsupported_attributes> unsupported_attributes{
      GroupTag::unsupported_attributes};
  struct G_job_attributes : public Collection {
    SingleValue<int> job_id{this, AttrName::job_id};
    SingleValue<std::string> job_uri{this, AttrName::job_uri};
    SingleValue<E_job_state> job_state{this, AttrName::job_state};
    SetOfValues<E_job_state_reasons> job_state_reasons{
        this, AttrName::job_state_reasons};
    SingleValue<StringWithLanguage> job_state_message{
        this, AttrName::job_state_message};
    SingleValue<int> number_of_intervening_jobs{
        this, AttrName::number_of_intervening_jobs};
    G_job_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_job_attributes> job_attributes{GroupTag::job_attributes};
  Response_Create_Job();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_Get_Job_Attributes : public Request {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<std::string> printer_uri{this, AttrName::printer_uri};
    SingleValue<int> job_id{this, AttrName::job_id};
    SingleValue<std::string> job_uri{this, AttrName::job_uri};
    SingleValue<StringWithLanguage> requesting_user_name{
        this, AttrName::requesting_user_name};
    OpenSetOfValues<E_requested_attributes> requested_attributes{
        this, AttrName::requested_attributes};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Request_Get_Job_Attributes();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_Get_Job_Attributes : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  typedef Response_CUPS_Authenticate_Job::G_unsupported_attributes
      G_unsupported_attributes;
  SingleGroup<G_unsupported_attributes> unsupported_attributes{
      GroupTag::unsupported_attributes};
  struct G_job_attributes : public Collection {
    struct C_cover_back : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_cover_back() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_cover_back_actual : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_cover_back_actual() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_cover_front : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_cover_front() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_cover_front_actual : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_cover_front_actual() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_document_format_details_supplied : public Collection {
      SingleValue<std::string> document_format{this, AttrName::document_format};
      SingleValue<StringWithLanguage> document_format_device_id{
          this, AttrName::document_format_device_id};
      SingleValue<StringWithLanguage> document_format_version{
          this, AttrName::document_format_version};
      SetOfValues<std::string> document_natural_language{
          this, AttrName::document_natural_language};
      SingleValue<StringWithLanguage> document_source_application_name{
          this, AttrName::document_source_application_name};
      SingleValue<StringWithLanguage> document_source_application_version{
          this, AttrName::document_source_application_version};
      SingleValue<StringWithLanguage> document_source_os_name{
          this, AttrName::document_source_os_name};
      SingleValue<StringWithLanguage> document_source_os_version{
          this, AttrName::document_source_os_version};
      C_document_format_details_supplied() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_finishings_col : public Collection {
      struct C_baling : public Collection {
        SingleValue<E_baling_type> baling_type{this, AttrName::baling_type};
        SingleValue<E_baling_when> baling_when{this, AttrName::baling_when};
        C_baling() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_binding : public Collection {
        SingleValue<E_binding_reference_edge> binding_reference_edge{
            this, AttrName::binding_reference_edge};
        SingleValue<E_binding_type> binding_type{this, AttrName::binding_type};
        C_binding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_coating : public Collection {
        SingleValue<E_coating_sides> coating_sides{this,
                                                   AttrName::coating_sides};
        SingleValue<E_coating_type> coating_type{this, AttrName::coating_type};
        C_coating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_covering : public Collection {
        SingleValue<E_covering_name> covering_name{this,
                                                   AttrName::covering_name};
        C_covering() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_folding : public Collection {
        SingleValue<E_folding_direction> folding_direction{
            this, AttrName::folding_direction};
        SingleValue<int> folding_offset{this, AttrName::folding_offset};
        SingleValue<E_folding_reference_edge> folding_reference_edge{
            this, AttrName::folding_reference_edge};
        C_folding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_laminating : public Collection {
        SingleValue<E_laminating_sides> laminating_sides{
            this, AttrName::laminating_sides};
        SingleValue<E_laminating_type> laminating_type{
            this, AttrName::laminating_type};
        C_laminating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_media_size : public Collection {
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_punching : public Collection {
        SetOfValues<int> punching_locations{this, AttrName::punching_locations};
        SingleValue<int> punching_offset{this, AttrName::punching_offset};
        SingleValue<E_punching_reference_edge> punching_reference_edge{
            this, AttrName::punching_reference_edge};
        C_punching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_stitching : public Collection {
        SingleValue<int> stitching_angle{this, AttrName::stitching_angle};
        SetOfValues<int> stitching_locations{this,
                                             AttrName::stitching_locations};
        SingleValue<E_stitching_method> stitching_method{
            this, AttrName::stitching_method};
        SingleValue<int> stitching_offset{this, AttrName::stitching_offset};
        SingleValue<E_stitching_reference_edge> stitching_reference_edge{
            this, AttrName::stitching_reference_edge};
        C_stitching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_trimming : public Collection {
        SingleValue<int> trimming_offset{this, AttrName::trimming_offset};
        SingleValue<E_trimming_reference_edge> trimming_reference_edge{
            this, AttrName::trimming_reference_edge};
        SingleValue<E_trimming_type> trimming_type{this,
                                                   AttrName::trimming_type};
        SingleValue<E_trimming_when> trimming_when{this,
                                                   AttrName::trimming_when};
        C_trimming() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleCollection<C_baling> baling{this, AttrName::baling};
      SingleCollection<C_binding> binding{this, AttrName::binding};
      SingleCollection<C_coating> coating{this, AttrName::coating};
      SingleCollection<C_covering> covering{this, AttrName::covering};
      SingleValue<E_finishing_template> finishing_template{
          this, AttrName::finishing_template};
      SetOfCollections<C_folding> folding{this, AttrName::folding};
      SingleValue<E_imposition_template> imposition_template{
          this, AttrName::imposition_template};
      SingleCollection<C_laminating> laminating{this, AttrName::laminating};
      SingleValue<RangeOfInteger> media_sheets_supported{
          this, AttrName::media_sheets_supported};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleCollection<C_punching> punching{this, AttrName::punching};
      SingleCollection<C_stitching> stitching{this, AttrName::stitching};
      SetOfCollections<C_trimming> trimming{this, AttrName::trimming};
      C_finishings_col() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_finishings_col_actual : public Collection {
      struct C_baling : public Collection {
        SingleValue<E_baling_type> baling_type{this, AttrName::baling_type};
        SingleValue<E_baling_when> baling_when{this, AttrName::baling_when};
        C_baling() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_binding : public Collection {
        SingleValue<E_binding_reference_edge> binding_reference_edge{
            this, AttrName::binding_reference_edge};
        SingleValue<E_binding_type> binding_type{this, AttrName::binding_type};
        C_binding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_coating : public Collection {
        SingleValue<E_coating_sides> coating_sides{this,
                                                   AttrName::coating_sides};
        SingleValue<E_coating_type> coating_type{this, AttrName::coating_type};
        C_coating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_covering : public Collection {
        SingleValue<E_covering_name> covering_name{this,
                                                   AttrName::covering_name};
        C_covering() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_folding : public Collection {
        SingleValue<E_folding_direction> folding_direction{
            this, AttrName::folding_direction};
        SingleValue<int> folding_offset{this, AttrName::folding_offset};
        SingleValue<E_folding_reference_edge> folding_reference_edge{
            this, AttrName::folding_reference_edge};
        C_folding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_laminating : public Collection {
        SingleValue<E_laminating_sides> laminating_sides{
            this, AttrName::laminating_sides};
        SingleValue<E_laminating_type> laminating_type{
            this, AttrName::laminating_type};
        C_laminating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_media_size : public Collection {
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_punching : public Collection {
        SetOfValues<int> punching_locations{this, AttrName::punching_locations};
        SingleValue<int> punching_offset{this, AttrName::punching_offset};
        SingleValue<E_punching_reference_edge> punching_reference_edge{
            this, AttrName::punching_reference_edge};
        C_punching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_stitching : public Collection {
        SingleValue<int> stitching_angle{this, AttrName::stitching_angle};
        SetOfValues<int> stitching_locations{this,
                                             AttrName::stitching_locations};
        SingleValue<E_stitching_method> stitching_method{
            this, AttrName::stitching_method};
        SingleValue<int> stitching_offset{this, AttrName::stitching_offset};
        SingleValue<E_stitching_reference_edge> stitching_reference_edge{
            this, AttrName::stitching_reference_edge};
        C_stitching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_trimming : public Collection {
        SingleValue<int> trimming_offset{this, AttrName::trimming_offset};
        SingleValue<E_trimming_reference_edge> trimming_reference_edge{
            this, AttrName::trimming_reference_edge};
        SingleValue<E_trimming_type> trimming_type{this,
                                                   AttrName::trimming_type};
        SingleValue<E_trimming_when> trimming_when{this,
                                                   AttrName::trimming_when};
        C_trimming() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleCollection<C_baling> baling{this, AttrName::baling};
      SingleCollection<C_binding> binding{this, AttrName::binding};
      SingleCollection<C_coating> coating{this, AttrName::coating};
      SingleCollection<C_covering> covering{this, AttrName::covering};
      SingleValue<E_finishing_template> finishing_template{
          this, AttrName::finishing_template};
      SetOfCollections<C_folding> folding{this, AttrName::folding};
      SingleValue<E_imposition_template> imposition_template{
          this, AttrName::imposition_template};
      SingleCollection<C_laminating> laminating{this, AttrName::laminating};
      SingleValue<RangeOfInteger> media_sheets_supported{
          this, AttrName::media_sheets_supported};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleCollection<C_punching> punching{this, AttrName::punching};
      SingleCollection<C_stitching> stitching{this, AttrName::stitching};
      SetOfCollections<C_trimming> trimming{this, AttrName::trimming};
      C_finishings_col_actual() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_insert_sheet : public Collection {
      SingleValue<int> insert_after_page_number{
          this, AttrName::insert_after_page_number};
      SingleValue<int> insert_count{this, AttrName::insert_count};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_insert_sheet() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_insert_sheet_actual : public Collection {
      SingleValue<int> insert_after_page_number{
          this, AttrName::insert_after_page_number};
      SingleValue<int> insert_count{this, AttrName::insert_count};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_insert_sheet_actual() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_accounting_sheets : public Collection {
      SingleValue<E_job_accounting_output_bin> job_accounting_output_bin{
          this, AttrName::job_accounting_output_bin};
      SingleValue<E_job_accounting_sheets_type> job_accounting_sheets_type{
          this, AttrName::job_accounting_sheets_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_accounting_sheets() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_accounting_sheets_actual : public Collection {
      SingleValue<E_job_accounting_output_bin> job_accounting_output_bin{
          this, AttrName::job_accounting_output_bin};
      SingleValue<E_job_accounting_sheets_type> job_accounting_sheets_type{
          this, AttrName::job_accounting_sheets_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_accounting_sheets_actual() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_cover_back : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_cover_back() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_cover_back_actual : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_cover_back_actual() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_cover_front : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_cover_front() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_cover_front_actual : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_cover_front_actual() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_error_sheet : public Collection {
      SingleValue<E_job_error_sheet_type> job_error_sheet_type{
          this, AttrName::job_error_sheet_type};
      SingleValue<E_job_error_sheet_when> job_error_sheet_when{
          this, AttrName::job_error_sheet_when};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_error_sheet() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_error_sheet_actual : public Collection {
      SingleValue<E_job_error_sheet_type> job_error_sheet_type{
          this, AttrName::job_error_sheet_type};
      SingleValue<E_job_error_sheet_when> job_error_sheet_when{
          this, AttrName::job_error_sheet_when};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_error_sheet_actual() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_finishings_col : public Collection {
      struct C_baling : public Collection {
        SingleValue<E_baling_type> baling_type{this, AttrName::baling_type};
        SingleValue<E_baling_when> baling_when{this, AttrName::baling_when};
        C_baling() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_binding : public Collection {
        SingleValue<E_binding_reference_edge> binding_reference_edge{
            this, AttrName::binding_reference_edge};
        SingleValue<E_binding_type> binding_type{this, AttrName::binding_type};
        C_binding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_coating : public Collection {
        SingleValue<E_coating_sides> coating_sides{this,
                                                   AttrName::coating_sides};
        SingleValue<E_coating_type> coating_type{this, AttrName::coating_type};
        C_coating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_covering : public Collection {
        SingleValue<E_covering_name> covering_name{this,
                                                   AttrName::covering_name};
        C_covering() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_folding : public Collection {
        SingleValue<E_folding_direction> folding_direction{
            this, AttrName::folding_direction};
        SingleValue<int> folding_offset{this, AttrName::folding_offset};
        SingleValue<E_folding_reference_edge> folding_reference_edge{
            this, AttrName::folding_reference_edge};
        C_folding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_laminating : public Collection {
        SingleValue<E_laminating_sides> laminating_sides{
            this, AttrName::laminating_sides};
        SingleValue<E_laminating_type> laminating_type{
            this, AttrName::laminating_type};
        C_laminating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_media_size : public Collection {
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_punching : public Collection {
        SetOfValues<int> punching_locations{this, AttrName::punching_locations};
        SingleValue<int> punching_offset{this, AttrName::punching_offset};
        SingleValue<E_punching_reference_edge> punching_reference_edge{
            this, AttrName::punching_reference_edge};
        C_punching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_stitching : public Collection {
        SingleValue<int> stitching_angle{this, AttrName::stitching_angle};
        SetOfValues<int> stitching_locations{this,
                                             AttrName::stitching_locations};
        SingleValue<E_stitching_method> stitching_method{
            this, AttrName::stitching_method};
        SingleValue<int> stitching_offset{this, AttrName::stitching_offset};
        SingleValue<E_stitching_reference_edge> stitching_reference_edge{
            this, AttrName::stitching_reference_edge};
        C_stitching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_trimming : public Collection {
        SingleValue<int> trimming_offset{this, AttrName::trimming_offset};
        SingleValue<E_trimming_reference_edge> trimming_reference_edge{
            this, AttrName::trimming_reference_edge};
        SingleValue<E_trimming_type> trimming_type{this,
                                                   AttrName::trimming_type};
        SingleValue<E_trimming_when> trimming_when{this,
                                                   AttrName::trimming_when};
        C_trimming() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleCollection<C_baling> baling{this, AttrName::baling};
      SingleCollection<C_binding> binding{this, AttrName::binding};
      SingleCollection<C_coating> coating{this, AttrName::coating};
      SingleCollection<C_covering> covering{this, AttrName::covering};
      SingleValue<E_finishing_template> finishing_template{
          this, AttrName::finishing_template};
      SetOfCollections<C_folding> folding{this, AttrName::folding};
      SingleValue<E_imposition_template> imposition_template{
          this, AttrName::imposition_template};
      SingleCollection<C_laminating> laminating{this, AttrName::laminating};
      SingleValue<RangeOfInteger> media_sheets_supported{
          this, AttrName::media_sheets_supported};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleCollection<C_punching> punching{this, AttrName::punching};
      SingleCollection<C_stitching> stitching{this, AttrName::stitching};
      SetOfCollections<C_trimming> trimming{this, AttrName::trimming};
      C_job_finishings_col() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_finishings_col_actual : public Collection {
      struct C_media_size : public Collection {
        SingleValue<int> x_dimension{this, AttrName::x_dimension};
        SingleValue<int> y_dimension{this, AttrName::y_dimension};
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleValue<E_media_back_coating> media_back_coating{
          this, AttrName::media_back_coating};
      SingleValue<int> media_bottom_margin{this, AttrName::media_bottom_margin};
      SingleValue<E_media_color> media_color{this, AttrName::media_color};
      SingleValue<E_media_front_coating> media_front_coating{
          this, AttrName::media_front_coating};
      SingleValue<E_media_grain> media_grain{this, AttrName::media_grain};
      SingleValue<int> media_hole_count{this, AttrName::media_hole_count};
      SingleValue<StringWithLanguage> media_info{this, AttrName::media_info};
      SingleValue<E_media_key> media_key{this, AttrName::media_key};
      SingleValue<int> media_left_margin{this, AttrName::media_left_margin};
      SingleValue<int> media_order_count{this, AttrName::media_order_count};
      SingleValue<E_media_pre_printed> media_pre_printed{
          this, AttrName::media_pre_printed};
      SingleValue<E_media_recycled> media_recycled{this,
                                                   AttrName::media_recycled};
      SingleValue<int> media_right_margin{this, AttrName::media_right_margin};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleValue<E_media_source> media_source{this, AttrName::media_source};
      SingleValue<int> media_thickness{this, AttrName::media_thickness};
      SingleValue<E_media_tooth> media_tooth{this, AttrName::media_tooth};
      SingleValue<int> media_top_margin{this, AttrName::media_top_margin};
      SingleValue<E_media_type> media_type{this, AttrName::media_type};
      SingleValue<int> media_weight_metric{this, AttrName::media_weight_metric};
      C_job_finishings_col_actual() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_save_disposition : public Collection {
      struct C_save_info : public Collection {
        SingleValue<std::string> save_document_format{
            this, AttrName::save_document_format};
        SingleValue<std::string> save_location{this, AttrName::save_location};
        SingleValue<StringWithLanguage> save_name{this, AttrName::save_name};
        C_save_info() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleValue<E_save_disposition> save_disposition{
          this, AttrName::save_disposition};
      SetOfCollections<C_save_info> save_info{this, AttrName::save_info};
      C_job_save_disposition() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_sheets_col : public Collection {
      SingleValue<E_job_sheets> job_sheets{this, AttrName::job_sheets};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_sheets_col() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_sheets_col_actual : public Collection {
      SingleValue<E_job_sheets> job_sheets{this, AttrName::job_sheets};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_sheets_col_actual() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_media_col : public Collection {
      struct C_media_size : public Collection {
        SingleValue<int> x_dimension{this, AttrName::x_dimension};
        SingleValue<int> y_dimension{this, AttrName::y_dimension};
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleValue<E_media_back_coating> media_back_coating{
          this, AttrName::media_back_coating};
      SingleValue<int> media_bottom_margin{this, AttrName::media_bottom_margin};
      SingleValue<E_media_color> media_color{this, AttrName::media_color};
      SingleValue<E_media_front_coating> media_front_coating{
          this, AttrName::media_front_coating};
      SingleValue<E_media_grain> media_grain{this, AttrName::media_grain};
      SingleValue<int> media_hole_count{this, AttrName::media_hole_count};
      SingleValue<StringWithLanguage> media_info{this, AttrName::media_info};
      SingleValue<E_media_key> media_key{this, AttrName::media_key};
      SingleValue<int> media_left_margin{this, AttrName::media_left_margin};
      SingleValue<int> media_order_count{this, AttrName::media_order_count};
      SingleValue<E_media_pre_printed> media_pre_printed{
          this, AttrName::media_pre_printed};
      SingleValue<E_media_recycled> media_recycled{this,
                                                   AttrName::media_recycled};
      SingleValue<int> media_right_margin{this, AttrName::media_right_margin};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleValue<E_media_source> media_source{this, AttrName::media_source};
      SingleValue<int> media_thickness{this, AttrName::media_thickness};
      SingleValue<E_media_tooth> media_tooth{this, AttrName::media_tooth};
      SingleValue<int> media_top_margin{this, AttrName::media_top_margin};
      SingleValue<E_media_type> media_type{this, AttrName::media_type};
      SingleValue<int> media_weight_metric{this, AttrName::media_weight_metric};
      C_media_col() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_media_col_actual : public Collection {
      struct C_media_size : public Collection {
        SingleValue<int> x_dimension{this, AttrName::x_dimension};
        SingleValue<int> y_dimension{this, AttrName::y_dimension};
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleValue<E_media_back_coating> media_back_coating{
          this, AttrName::media_back_coating};
      SingleValue<int> media_bottom_margin{this, AttrName::media_bottom_margin};
      SingleValue<E_media_color> media_color{this, AttrName::media_color};
      SingleValue<E_media_front_coating> media_front_coating{
          this, AttrName::media_front_coating};
      SingleValue<E_media_grain> media_grain{this, AttrName::media_grain};
      SingleValue<int> media_hole_count{this, AttrName::media_hole_count};
      SingleValue<StringWithLanguage> media_info{this, AttrName::media_info};
      SingleValue<E_media_key> media_key{this, AttrName::media_key};
      SingleValue<int> media_left_margin{this, AttrName::media_left_margin};
      SingleValue<int> media_order_count{this, AttrName::media_order_count};
      SingleValue<E_media_pre_printed> media_pre_printed{
          this, AttrName::media_pre_printed};
      SingleValue<E_media_recycled> media_recycled{this,
                                                   AttrName::media_recycled};
      SingleValue<int> media_right_margin{this, AttrName::media_right_margin};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleValue<E_media_source> media_source{this, AttrName::media_source};
      SingleValue<int> media_thickness{this, AttrName::media_thickness};
      SingleValue<E_media_tooth> media_tooth{this, AttrName::media_tooth};
      SingleValue<int> media_top_margin{this, AttrName::media_top_margin};
      SingleValue<E_media_type> media_type{this, AttrName::media_type};
      SingleValue<int> media_weight_metric{this, AttrName::media_weight_metric};
      C_media_col_actual() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_overrides : public Collection {
      SingleValue<StringWithLanguage> job_account_id{this,
                                                     AttrName::job_account_id};
      SingleValue<E_job_account_type> job_account_type{
          this, AttrName::job_account_type};
      SingleCollection<C_job_accounting_sheets> job_accounting_sheets{
          this, AttrName::job_accounting_sheets};
      SingleValue<StringWithLanguage> job_accounting_user_id{
          this, AttrName::job_accounting_user_id};
      SingleValue<int> job_copies{this, AttrName::job_copies};
      SingleCollection<C_job_cover_back> job_cover_back{
          this, AttrName::job_cover_back};
      SingleCollection<C_job_cover_front> job_cover_front{
          this, AttrName::job_cover_front};
      SingleValue<E_job_delay_output_until> job_delay_output_until{
          this, AttrName::job_delay_output_until};
      SingleValue<DateTime> job_delay_output_until_time{
          this, AttrName::job_delay_output_until_time};
      SingleValue<E_job_error_action> job_error_action{
          this, AttrName::job_error_action};
      SingleCollection<C_job_error_sheet> job_error_sheet{
          this, AttrName::job_error_sheet};
      SetOfValues<E_job_finishings> job_finishings{this,
                                                   AttrName::job_finishings};
      SetOfCollections<C_job_finishings_col> job_finishings_col{
          this, AttrName::job_finishings_col};
      SingleValue<E_job_hold_until> job_hold_until{this,
                                                   AttrName::job_hold_until};
      SingleValue<DateTime> job_hold_until_time{this,
                                                AttrName::job_hold_until_time};
      SingleValue<StringWithLanguage> job_message_to_operator{
          this, AttrName::job_message_to_operator};
      SingleValue<int> job_pages_per_set{this, AttrName::job_pages_per_set};
      SingleValue<std::string> job_phone_number{this,
                                                AttrName::job_phone_number};
      SingleValue<int> job_priority{this, AttrName::job_priority};
      SingleValue<StringWithLanguage> job_recipient_name{
          this, AttrName::job_recipient_name};
      SingleCollection<C_job_save_disposition> job_save_disposition{
          this, AttrName::job_save_disposition};
      SingleValue<StringWithLanguage> job_sheet_message{
          this, AttrName::job_sheet_message};
      SingleValue<E_job_sheets> job_sheets{this, AttrName::job_sheets};
      SingleCollection<C_job_sheets_col> job_sheets_col{
          this, AttrName::job_sheets_col};
      SetOfValues<int> pages_per_subset{this, AttrName::pages_per_subset};
      SingleValue<E_output_bin> output_bin{this, AttrName::output_bin};
      SingleValue<StringWithLanguage> output_device{this,
                                                    AttrName::output_device};
      SingleValue<E_multiple_document_handling> multiple_document_handling{
          this, AttrName::multiple_document_handling};
      SingleValue<int> y_side1_image_shift{this, AttrName::y_side1_image_shift};
      SingleValue<int> y_side2_image_shift{this, AttrName::y_side2_image_shift};
      SingleValue<int> number_up{this, AttrName::number_up};
      SingleValue<E_orientation_requested> orientation_requested{
          this, AttrName::orientation_requested};
      SingleValue<E_page_delivery> page_delivery{this, AttrName::page_delivery};
      SingleValue<E_page_order_received> page_order_received{
          this, AttrName::page_order_received};
      SetOfValues<RangeOfInteger> page_ranges{this, AttrName::page_ranges};
      SetOfCollections<C_pdl_init_file> pdl_init_file{this,
                                                      AttrName::pdl_init_file};
      SingleValue<E_print_color_mode> print_color_mode{
          this, AttrName::print_color_mode};
      SingleValue<E_print_content_optimize> print_content_optimize{
          this, AttrName::print_content_optimize};
      SingleValue<E_print_quality> print_quality{this, AttrName::print_quality};
      SingleValue<E_print_rendering_intent> print_rendering_intent{
          this, AttrName::print_rendering_intent};
      SingleValue<Resolution> printer_resolution{this,
                                                 AttrName::printer_resolution};
      SingleValue<E_presentation_direction_number_up>
          presentation_direction_number_up{
              this, AttrName::presentation_direction_number_up};
      SingleValue<E_media> media{this, AttrName::media};
      SingleValue<E_sides> sides{this, AttrName::sides};
      SingleValue<E_x_image_position> x_image_position{
          this, AttrName::x_image_position};
      SingleValue<int> x_image_shift{this, AttrName::x_image_shift};
      SingleValue<int> x_side1_image_shift{this, AttrName::x_side1_image_shift};
      SingleValue<int> x_side2_image_shift{this, AttrName::x_side2_image_shift};
      SingleValue<E_y_image_position> y_image_position{
          this, AttrName::y_image_position};
      SingleValue<int> y_image_shift{this, AttrName::y_image_shift};
      SingleValue<int> copies{this, AttrName::copies};
      SingleCollection<C_cover_back> cover_back{this, AttrName::cover_back};
      SingleCollection<C_cover_front> cover_front{this, AttrName::cover_front};
      SingleValue<E_imposition_template> imposition_template{
          this, AttrName::imposition_template};
      SetOfCollections<C_insert_sheet> insert_sheet{this,
                                                    AttrName::insert_sheet};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      SingleValue<E_media_input_tray_check> media_input_tray_check{
          this, AttrName::media_input_tray_check};
      SingleValue<E_print_scaling> print_scaling{this, AttrName::print_scaling};
      SingleCollection<C_proof_print> proof_print{this, AttrName::proof_print};
      SingleCollection<C_separator_sheets> separator_sheets{
          this, AttrName::separator_sheets};
      SingleValue<E_sheet_collate> sheet_collate{this, AttrName::sheet_collate};
      SingleValue<E_feed_orientation> feed_orientation{
          this, AttrName::feed_orientation};
      SetOfValues<E_finishings> finishings{this, AttrName::finishings};
      SetOfCollections<C_finishings_col> finishings_col{
          this, AttrName::finishings_col};
      SingleValue<StringWithLanguage> font_name_requested{
          this, AttrName::font_name_requested};
      SingleValue<int> font_size_requested{this, AttrName::font_size_requested};
      SetOfValues<int> force_front_side{this, AttrName::force_front_side};
      SetOfValues<RangeOfInteger> document_copies{this,
                                                  AttrName::document_copies};
      SetOfValues<RangeOfInteger> document_numbers{this,
                                                   AttrName::document_numbers};
      SetOfValues<RangeOfInteger> pages{this, AttrName::pages};
      C_overrides() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_overrides_actual : public Collection {
      SingleValue<StringWithLanguage> job_account_id{this,
                                                     AttrName::job_account_id};
      SingleValue<E_job_account_type> job_account_type{
          this, AttrName::job_account_type};
      SingleCollection<C_job_accounting_sheets> job_accounting_sheets{
          this, AttrName::job_accounting_sheets};
      SingleValue<StringWithLanguage> job_accounting_user_id{
          this, AttrName::job_accounting_user_id};
      SingleValue<int> job_copies{this, AttrName::job_copies};
      SingleCollection<C_job_cover_back> job_cover_back{
          this, AttrName::job_cover_back};
      SingleCollection<C_job_cover_front> job_cover_front{
          this, AttrName::job_cover_front};
      SingleValue<E_job_delay_output_until> job_delay_output_until{
          this, AttrName::job_delay_output_until};
      SingleValue<DateTime> job_delay_output_until_time{
          this, AttrName::job_delay_output_until_time};
      SingleValue<E_job_error_action> job_error_action{
          this, AttrName::job_error_action};
      SingleCollection<C_job_error_sheet> job_error_sheet{
          this, AttrName::job_error_sheet};
      SetOfValues<E_job_finishings> job_finishings{this,
                                                   AttrName::job_finishings};
      SetOfCollections<C_job_finishings_col> job_finishings_col{
          this, AttrName::job_finishings_col};
      SingleValue<E_job_hold_until> job_hold_until{this,
                                                   AttrName::job_hold_until};
      SingleValue<DateTime> job_hold_until_time{this,
                                                AttrName::job_hold_until_time};
      SingleValue<StringWithLanguage> job_message_to_operator{
          this, AttrName::job_message_to_operator};
      SingleValue<int> job_pages_per_set{this, AttrName::job_pages_per_set};
      SingleValue<std::string> job_phone_number{this,
                                                AttrName::job_phone_number};
      SingleValue<int> job_priority{this, AttrName::job_priority};
      SingleValue<StringWithLanguage> job_recipient_name{
          this, AttrName::job_recipient_name};
      SingleCollection<C_job_save_disposition> job_save_disposition{
          this, AttrName::job_save_disposition};
      SingleValue<StringWithLanguage> job_sheet_message{
          this, AttrName::job_sheet_message};
      SingleValue<E_job_sheets> job_sheets{this, AttrName::job_sheets};
      SingleCollection<C_job_sheets_col> job_sheets_col{
          this, AttrName::job_sheets_col};
      SetOfValues<int> pages_per_subset{this, AttrName::pages_per_subset};
      SingleValue<E_output_bin> output_bin{this, AttrName::output_bin};
      SingleValue<StringWithLanguage> output_device{this,
                                                    AttrName::output_device};
      SingleValue<E_multiple_document_handling> multiple_document_handling{
          this, AttrName::multiple_document_handling};
      SingleValue<int> y_side1_image_shift{this, AttrName::y_side1_image_shift};
      SingleValue<int> y_side2_image_shift{this, AttrName::y_side2_image_shift};
      SingleValue<int> number_up{this, AttrName::number_up};
      SingleValue<E_orientation_requested> orientation_requested{
          this, AttrName::orientation_requested};
      SingleValue<E_page_delivery> page_delivery{this, AttrName::page_delivery};
      SingleValue<E_page_order_received> page_order_received{
          this, AttrName::page_order_received};
      SetOfValues<RangeOfInteger> page_ranges{this, AttrName::page_ranges};
      SetOfCollections<C_pdl_init_file> pdl_init_file{this,
                                                      AttrName::pdl_init_file};
      SingleValue<E_print_color_mode> print_color_mode{
          this, AttrName::print_color_mode};
      SingleValue<E_print_content_optimize> print_content_optimize{
          this, AttrName::print_content_optimize};
      SingleValue<E_print_quality> print_quality{this, AttrName::print_quality};
      SingleValue<E_print_rendering_intent> print_rendering_intent{
          this, AttrName::print_rendering_intent};
      SingleValue<Resolution> printer_resolution{this,
                                                 AttrName::printer_resolution};
      SingleValue<E_presentation_direction_number_up>
          presentation_direction_number_up{
              this, AttrName::presentation_direction_number_up};
      SingleValue<E_media> media{this, AttrName::media};
      SingleValue<E_sides> sides{this, AttrName::sides};
      SingleValue<E_x_image_position> x_image_position{
          this, AttrName::x_image_position};
      SingleValue<int> x_image_shift{this, AttrName::x_image_shift};
      SingleValue<int> x_side1_image_shift{this, AttrName::x_side1_image_shift};
      SingleValue<int> x_side2_image_shift{this, AttrName::x_side2_image_shift};
      SingleValue<E_y_image_position> y_image_position{
          this, AttrName::y_image_position};
      SingleValue<int> y_image_shift{this, AttrName::y_image_shift};
      SingleValue<int> copies{this, AttrName::copies};
      SingleCollection<C_cover_back> cover_back{this, AttrName::cover_back};
      SingleCollection<C_cover_front> cover_front{this, AttrName::cover_front};
      SingleValue<E_imposition_template> imposition_template{
          this, AttrName::imposition_template};
      SetOfCollections<C_insert_sheet> insert_sheet{this,
                                                    AttrName::insert_sheet};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      SingleValue<E_media_input_tray_check> media_input_tray_check{
          this, AttrName::media_input_tray_check};
      SingleValue<E_print_scaling> print_scaling{this, AttrName::print_scaling};
      SingleCollection<C_proof_print> proof_print{this, AttrName::proof_print};
      SingleCollection<C_separator_sheets> separator_sheets{
          this, AttrName::separator_sheets};
      SingleValue<E_sheet_collate> sheet_collate{this, AttrName::sheet_collate};
      SingleValue<E_feed_orientation> feed_orientation{
          this, AttrName::feed_orientation};
      SetOfValues<E_finishings> finishings{this, AttrName::finishings};
      SetOfCollections<C_finishings_col> finishings_col{
          this, AttrName::finishings_col};
      SingleValue<StringWithLanguage> font_name_requested{
          this, AttrName::font_name_requested};
      SingleValue<int> font_size_requested{this, AttrName::font_size_requested};
      SetOfValues<int> force_front_side{this, AttrName::force_front_side};
      SetOfValues<RangeOfInteger> document_copies{this,
                                                  AttrName::document_copies};
      SetOfValues<RangeOfInteger> document_numbers{this,
                                                   AttrName::document_numbers};
      SetOfValues<RangeOfInteger> pages{this, AttrName::pages};
      C_overrides_actual() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_pdl_init_file : public Collection {
      SingleValue<StringWithLanguage> pdl_init_file_entry{
          this, AttrName::pdl_init_file_entry};
      SingleValue<std::string> pdl_init_file_location{
          this, AttrName::pdl_init_file_location};
      SingleValue<StringWithLanguage> pdl_init_file_name{
          this, AttrName::pdl_init_file_name};
      C_pdl_init_file() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_proof_print : public Collection {
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      SingleValue<int> proof_print_copies{this, AttrName::proof_print_copies};
      C_proof_print() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_separator_sheets : public Collection {
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      SetOfValues<E_separator_sheets_type> separator_sheets_type{
          this, AttrName::separator_sheets_type};
      C_separator_sheets() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_separator_sheets_actual : public Collection {
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      SetOfValues<E_separator_sheets_type> separator_sheets_type{
          this, AttrName::separator_sheets_type};
      C_separator_sheets_actual() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<int> copies{this, AttrName::copies};
    SetOfValues<int> copies_actual{this, AttrName::copies_actual};
    SingleCollection<C_cover_back> cover_back{this, AttrName::cover_back};
    SetOfCollections<C_cover_back_actual> cover_back_actual{
        this, AttrName::cover_back_actual};
    SingleCollection<C_cover_front> cover_front{this, AttrName::cover_front};
    SetOfCollections<C_cover_front_actual> cover_front_actual{
        this, AttrName::cover_front_actual};
    SingleValue<E_current_page_order> current_page_order{
        this, AttrName::current_page_order};
    SingleValue<DateTime> date_time_at_completed{
        this, AttrName::date_time_at_completed};
    SingleValue<DateTime> date_time_at_creation{
        this, AttrName::date_time_at_creation};
    SingleValue<DateTime> date_time_at_processing{
        this, AttrName::date_time_at_processing};
    SingleValue<std::string> document_charset_supplied{
        this, AttrName::document_charset_supplied};
    SetOfCollections<C_document_format_details_supplied>
        document_format_details_supplied{
            this, AttrName::document_format_details_supplied};
    SetOfValues<std::string> document_format_ready{
        this, AttrName::document_format_ready};
    SingleValue<std::string> document_format_supplied{
        this, AttrName::document_format_supplied};
    SingleValue<StringWithLanguage> document_format_version_supplied{
        this, AttrName::document_format_version_supplied};
    SingleValue<StringWithLanguage> document_message_supplied{
        this, AttrName::document_message_supplied};
    SetOfValues<std::string> document_metadata{this,
                                               AttrName::document_metadata};
    SingleValue<StringWithLanguage> document_name_supplied{
        this, AttrName::document_name_supplied};
    SingleValue<std::string> document_natural_language_supplied{
        this, AttrName::document_natural_language_supplied};
    SingleValue<int> errors_count{this, AttrName::errors_count};
    SingleValue<E_feed_orientation> feed_orientation{
        this, AttrName::feed_orientation};
    SetOfValues<E_finishings> finishings{this, AttrName::finishings};
    SetOfCollections<C_finishings_col> finishings_col{this,
                                                      AttrName::finishings_col};
    SetOfCollections<C_finishings_col_actual> finishings_col_actual{
        this, AttrName::finishings_col_actual};
    SingleValue<StringWithLanguage> font_name_requested{
        this, AttrName::font_name_requested};
    SingleValue<int> font_size_requested{this, AttrName::font_size_requested};
    SetOfValues<int> force_front_side{this, AttrName::force_front_side};
    SetOfValues<int> force_front_side_actual{this,
                                             AttrName::force_front_side_actual};
    SingleValue<E_imposition_template> imposition_template{
        this, AttrName::imposition_template};
    SingleValue<int> impressions_completed_current_copy{
        this, AttrName::impressions_completed_current_copy};
    SetOfCollections<C_insert_sheet> insert_sheet{this, AttrName::insert_sheet};
    SetOfCollections<C_insert_sheet_actual> insert_sheet_actual{
        this, AttrName::insert_sheet_actual};
    SingleValue<StringWithLanguage> job_account_id{this,
                                                   AttrName::job_account_id};
    SetOfValues<StringWithLanguage> job_account_id_actual{
        this, AttrName::job_account_id_actual};
    SingleValue<E_job_account_type> job_account_type{
        this, AttrName::job_account_type};
    SingleCollection<C_job_accounting_sheets> job_accounting_sheets{
        this, AttrName::job_accounting_sheets};
    SetOfCollections<C_job_accounting_sheets_actual>
        job_accounting_sheets_actual{this,
                                     AttrName::job_accounting_sheets_actual};
    SingleValue<StringWithLanguage> job_accounting_user_id{
        this, AttrName::job_accounting_user_id};
    SetOfValues<StringWithLanguage> job_accounting_user_id_actual{
        this, AttrName::job_accounting_user_id_actual};
    SingleValue<bool> job_attribute_fidelity{this,
                                             AttrName::job_attribute_fidelity};
    SingleValue<StringWithLanguage> job_charge_info{this,
                                                    AttrName::job_charge_info};
    SingleValue<E_job_collation_type> job_collation_type{
        this, AttrName::job_collation_type};
    SingleValue<int> job_copies{this, AttrName::job_copies};
    SetOfValues<int> job_copies_actual{this, AttrName::job_copies_actual};
    SingleCollection<C_job_cover_back> job_cover_back{this,
                                                      AttrName::job_cover_back};
    SetOfCollections<C_job_cover_back_actual> job_cover_back_actual{
        this, AttrName::job_cover_back_actual};
    SingleCollection<C_job_cover_front> job_cover_front{
        this, AttrName::job_cover_front};
    SetOfCollections<C_job_cover_front_actual> job_cover_front_actual{
        this, AttrName::job_cover_front_actual};
    SingleValue<E_job_delay_output_until> job_delay_output_until{
        this, AttrName::job_delay_output_until};
    SingleValue<DateTime> job_delay_output_until_time{
        this, AttrName::job_delay_output_until_time};
    SetOfValues<StringWithLanguage> job_detailed_status_messages{
        this, AttrName::job_detailed_status_messages};
    SetOfValues<StringWithLanguage> job_document_access_errors{
        this, AttrName::job_document_access_errors};
    SingleValue<E_job_error_action> job_error_action{
        this, AttrName::job_error_action};
    SingleCollection<C_job_error_sheet> job_error_sheet{
        this, AttrName::job_error_sheet};
    SetOfCollections<C_job_error_sheet_actual> job_error_sheet_actual{
        this, AttrName::job_error_sheet_actual};
    SetOfValues<E_job_finishings> job_finishings{this,
                                                 AttrName::job_finishings};
    SetOfCollections<C_job_finishings_col> job_finishings_col{
        this, AttrName::job_finishings_col};
    SetOfCollections<C_job_finishings_col_actual> job_finishings_col_actual{
        this, AttrName::job_finishings_col_actual};
    SingleValue<E_job_hold_until> job_hold_until{this,
                                                 AttrName::job_hold_until};
    SingleValue<DateTime> job_hold_until_time{this,
                                              AttrName::job_hold_until_time};
    SingleValue<int> job_id{this, AttrName::job_id};
    SingleValue<int> job_impressions{this, AttrName::job_impressions};
    SingleValue<int> job_impressions_completed{
        this, AttrName::job_impressions_completed};
    SingleValue<int> job_k_octets{this, AttrName::job_k_octets};
    SingleValue<int> job_k_octets_processed{this,
                                            AttrName::job_k_octets_processed};
    SetOfValues<E_job_mandatory_attributes> job_mandatory_attributes{
        this, AttrName::job_mandatory_attributes};
    SingleValue<int> job_media_sheets{this, AttrName::job_media_sheets};
    SingleValue<int> job_media_sheets_completed{
        this, AttrName::job_media_sheets_completed};
    SingleValue<StringWithLanguage> job_message_from_operator{
        this, AttrName::job_message_from_operator};
    SingleValue<StringWithLanguage> job_message_to_operator{
        this, AttrName::job_message_to_operator};
    SetOfValues<StringWithLanguage> job_message_to_operator_actual{
        this, AttrName::job_message_to_operator_actual};
    SingleValue<std::string> job_more_info{this, AttrName::job_more_info};
    SingleValue<StringWithLanguage> job_name{this, AttrName::job_name};
    SingleValue<StringWithLanguage> job_originating_user_name{
        this, AttrName::job_originating_user_name};
    SingleValue<std::string> job_originating_user_uri{
        this, AttrName::job_originating_user_uri};
    SingleValue<int> job_pages{this, AttrName::job_pages};
    SingleValue<int> job_pages_completed{this, AttrName::job_pages_completed};
    SingleValue<int> job_pages_completed_current_copy{
        this, AttrName::job_pages_completed_current_copy};
    SingleValue<int> job_pages_per_set{this, AttrName::job_pages_per_set};
    SingleValue<std::string> job_phone_number{this, AttrName::job_phone_number};
    SingleValue<int> job_printer_up_time{this, AttrName::job_printer_up_time};
    SingleValue<std::string> job_printer_uri{this, AttrName::job_printer_uri};
    SingleValue<int> job_priority{this, AttrName::job_priority};
    SetOfValues<int> job_priority_actual{this, AttrName::job_priority_actual};
    SingleValue<StringWithLanguage> job_recipient_name{
        this, AttrName::job_recipient_name};
    SetOfValues<int> job_resource_ids{this, AttrName::job_resource_ids};
    SingleCollection<C_job_save_disposition> job_save_disposition{
        this, AttrName::job_save_disposition};
    SingleValue<StringWithLanguage> job_save_printer_make_and_model{
        this, AttrName::job_save_printer_make_and_model};
    SingleValue<StringWithLanguage> job_sheet_message{
        this, AttrName::job_sheet_message};
    SetOfValues<StringWithLanguage> job_sheet_message_actual{
        this, AttrName::job_sheet_message_actual};
    SingleValue<E_job_sheets> job_sheets{this, AttrName::job_sheets};
    SingleCollection<C_job_sheets_col> job_sheets_col{this,
                                                      AttrName::job_sheets_col};
    SetOfCollections<C_job_sheets_col_actual> job_sheets_col_actual{
        this, AttrName::job_sheets_col_actual};
    SingleValue<E_job_state> job_state{this, AttrName::job_state};
    SingleValue<StringWithLanguage> job_state_message{
        this, AttrName::job_state_message};
    SetOfValues<E_job_state_reasons> job_state_reasons{
        this, AttrName::job_state_reasons};
    SingleValue<std::string> job_uri{this, AttrName::job_uri};
    SingleValue<std::string> job_uuid{this, AttrName::job_uuid};
    SingleValue<E_media> media{this, AttrName::media};
    SingleCollection<C_media_col> media_col{this, AttrName::media_col};
    SetOfCollections<C_media_col_actual> media_col_actual{
        this, AttrName::media_col_actual};
    SingleValue<E_media_input_tray_check> media_input_tray_check{
        this, AttrName::media_input_tray_check};
    SingleValue<E_multiple_document_handling> multiple_document_handling{
        this, AttrName::multiple_document_handling};
    SingleValue<int> number_of_documents{this, AttrName::number_of_documents};
    SingleValue<int> number_of_intervening_jobs{
        this, AttrName::number_of_intervening_jobs};
    SingleValue<int> number_up{this, AttrName::number_up};
    SetOfValues<int> number_up_actual{this, AttrName::number_up_actual};
    SingleValue<E_orientation_requested> orientation_requested{
        this, AttrName::orientation_requested};
    SingleValue<StringWithLanguage> original_requesting_user_name{
        this, AttrName::original_requesting_user_name};
    SingleValue<E_output_bin> output_bin{this, AttrName::output_bin};
    SingleValue<StringWithLanguage> output_device{this,
                                                  AttrName::output_device};
    SetOfValues<StringWithLanguage> output_device_actual{
        this, AttrName::output_device_actual};
    SingleValue<StringWithLanguage> output_device_assigned{
        this, AttrName::output_device_assigned};
    SingleValue<StringWithLanguage> output_device_job_state_message{
        this, AttrName::output_device_job_state_message};
    SingleValue<std::string> output_device_uuid_assigned{
        this, AttrName::output_device_uuid_assigned};
    SetOfCollections<C_overrides> overrides{this, AttrName::overrides};
    SetOfCollections<C_overrides_actual> overrides_actual{
        this, AttrName::overrides_actual};
    SingleValue<E_page_delivery> page_delivery{this, AttrName::page_delivery};
    SingleValue<E_page_order_received> page_order_received{
        this, AttrName::page_order_received};
    SetOfValues<RangeOfInteger> page_ranges{this, AttrName::page_ranges};
    SetOfValues<RangeOfInteger> page_ranges_actual{
        this, AttrName::page_ranges_actual};
    SetOfValues<int> pages_per_subset{this, AttrName::pages_per_subset};
    SetOfCollections<C_pdl_init_file> pdl_init_file{this,
                                                    AttrName::pdl_init_file};
    SingleValue<E_presentation_direction_number_up>
        presentation_direction_number_up{
            this, AttrName::presentation_direction_number_up};
    SingleValue<E_print_color_mode> print_color_mode{
        this, AttrName::print_color_mode};
    SingleValue<E_print_content_optimize> print_content_optimize{
        this, AttrName::print_content_optimize};
    SingleValue<E_print_quality> print_quality{this, AttrName::print_quality};
    SingleValue<E_print_rendering_intent> print_rendering_intent{
        this, AttrName::print_rendering_intent};
    SingleValue<E_print_scaling> print_scaling{this, AttrName::print_scaling};
    SingleValue<Resolution> printer_resolution{this,
                                               AttrName::printer_resolution};
    SetOfValues<Resolution> printer_resolution_actual{
        this, AttrName::printer_resolution_actual};
    SingleCollection<C_proof_print> proof_print{this, AttrName::proof_print};
    SingleCollection<C_separator_sheets> separator_sheets{
        this, AttrName::separator_sheets};
    SetOfCollections<C_separator_sheets_actual> separator_sheets_actual{
        this, AttrName::separator_sheets_actual};
    SingleValue<E_sheet_collate> sheet_collate{this, AttrName::sheet_collate};
    SingleValue<int> sheet_completed_copy_number{
        this, AttrName::sheet_completed_copy_number};
    SingleValue<int> sheet_completed_document_number{
        this, AttrName::sheet_completed_document_number};
    SingleValue<E_sides> sides{this, AttrName::sides};
    SingleValue<int> time_at_completed{this, AttrName::time_at_completed};
    SingleValue<int> time_at_creation{this, AttrName::time_at_creation};
    SingleValue<int> time_at_processing{this, AttrName::time_at_processing};
    SingleValue<int> warnings_count{this, AttrName::warnings_count};
    SingleValue<E_x_image_position> x_image_position{
        this, AttrName::x_image_position};
    SingleValue<int> x_image_shift{this, AttrName::x_image_shift};
    SetOfValues<int> x_image_shift_actual{this, AttrName::x_image_shift_actual};
    SingleValue<int> x_side1_image_shift{this, AttrName::x_side1_image_shift};
    SetOfValues<int> x_side1_image_shift_actual{
        this, AttrName::x_side1_image_shift_actual};
    SingleValue<int> x_side2_image_shift{this, AttrName::x_side2_image_shift};
    SetOfValues<int> x_side2_image_shift_actual{
        this, AttrName::x_side2_image_shift_actual};
    SingleValue<E_y_image_position> y_image_position{
        this, AttrName::y_image_position};
    SingleValue<int> y_image_shift{this, AttrName::y_image_shift};
    SetOfValues<int> y_image_shift_actual{this, AttrName::y_image_shift_actual};
    SingleValue<int> y_side1_image_shift{this, AttrName::y_side1_image_shift};
    SetOfValues<int> y_side1_image_shift_actual{
        this, AttrName::y_side1_image_shift_actual};
    SingleValue<int> y_side2_image_shift{this, AttrName::y_side2_image_shift};
    SetOfValues<int> y_side2_image_shift_actual{
        this, AttrName::y_side2_image_shift_actual};
    G_job_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_job_attributes> job_attributes{GroupTag::job_attributes};
  Response_Get_Job_Attributes();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_Get_Jobs : public Request {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<std::string> printer_uri{this, AttrName::printer_uri};
    SingleValue<StringWithLanguage> requesting_user_name{
        this, AttrName::requesting_user_name};
    SingleValue<int> limit{this, AttrName::limit};
    OpenSetOfValues<E_requested_attributes> requested_attributes{
        this, AttrName::requested_attributes};
    SingleValue<E_which_jobs> which_jobs{this, AttrName::which_jobs};
    SingleValue<bool> my_jobs{this, AttrName::my_jobs};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Request_Get_Jobs();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_Get_Jobs : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  typedef Response_CUPS_Authenticate_Job::G_unsupported_attributes
      G_unsupported_attributes;
  SingleGroup<G_unsupported_attributes> unsupported_attributes{
      GroupTag::unsupported_attributes};
  typedef Response_Get_Job_Attributes::G_job_attributes G_job_attributes;
  SetOfGroups<G_job_attributes> job_attributes{GroupTag::job_attributes};
  Response_Get_Jobs();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_Get_Printer_Attributes : public Request {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<std::string> printer_uri{this, AttrName::printer_uri};
    SingleValue<StringWithLanguage> requesting_user_name{
        this, AttrName::requesting_user_name};
    OpenSetOfValues<E_requested_attributes> requested_attributes{
        this, AttrName::requested_attributes};
    SingleValue<std::string> document_format{this, AttrName::document_format};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Request_Get_Printer_Attributes();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_Get_Printer_Attributes : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  typedef Response_CUPS_Authenticate_Job::G_unsupported_attributes
      G_unsupported_attributes;
  SingleGroup<G_unsupported_attributes> unsupported_attributes{
      GroupTag::unsupported_attributes};
  struct G_printer_attributes : public Collection {
    struct C_cover_back_default : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_cover_back_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_cover_front_default : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_cover_front_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_document_format_details_default : public Collection {
      SingleValue<std::string> document_format{this, AttrName::document_format};
      SingleValue<StringWithLanguage> document_format_device_id{
          this, AttrName::document_format_device_id};
      SingleValue<StringWithLanguage> document_format_version{
          this, AttrName::document_format_version};
      SetOfValues<std::string> document_natural_language{
          this, AttrName::document_natural_language};
      SingleValue<StringWithLanguage> document_source_application_name{
          this, AttrName::document_source_application_name};
      SingleValue<StringWithLanguage> document_source_application_version{
          this, AttrName::document_source_application_version};
      SingleValue<StringWithLanguage> document_source_os_name{
          this, AttrName::document_source_os_name};
      SingleValue<StringWithLanguage> document_source_os_version{
          this, AttrName::document_source_os_version};
      C_document_format_details_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_finishings_col_database : public Collection {
      struct C_baling : public Collection {
        SingleValue<E_baling_type> baling_type{this, AttrName::baling_type};
        SingleValue<E_baling_when> baling_when{this, AttrName::baling_when};
        C_baling() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_binding : public Collection {
        SingleValue<E_binding_reference_edge> binding_reference_edge{
            this, AttrName::binding_reference_edge};
        SingleValue<E_binding_type> binding_type{this, AttrName::binding_type};
        C_binding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_coating : public Collection {
        SingleValue<E_coating_sides> coating_sides{this,
                                                   AttrName::coating_sides};
        SingleValue<E_coating_type> coating_type{this, AttrName::coating_type};
        C_coating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_covering : public Collection {
        SingleValue<E_covering_name> covering_name{this,
                                                   AttrName::covering_name};
        C_covering() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_folding : public Collection {
        SingleValue<E_folding_direction> folding_direction{
            this, AttrName::folding_direction};
        SingleValue<int> folding_offset{this, AttrName::folding_offset};
        SingleValue<E_folding_reference_edge> folding_reference_edge{
            this, AttrName::folding_reference_edge};
        C_folding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_laminating : public Collection {
        SingleValue<E_laminating_sides> laminating_sides{
            this, AttrName::laminating_sides};
        SingleValue<E_laminating_type> laminating_type{
            this, AttrName::laminating_type};
        C_laminating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_media_size : public Collection {
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_punching : public Collection {
        SetOfValues<int> punching_locations{this, AttrName::punching_locations};
        SingleValue<int> punching_offset{this, AttrName::punching_offset};
        SingleValue<E_punching_reference_edge> punching_reference_edge{
            this, AttrName::punching_reference_edge};
        C_punching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_stitching : public Collection {
        SingleValue<int> stitching_angle{this, AttrName::stitching_angle};
        SetOfValues<int> stitching_locations{this,
                                             AttrName::stitching_locations};
        SingleValue<E_stitching_method> stitching_method{
            this, AttrName::stitching_method};
        SingleValue<int> stitching_offset{this, AttrName::stitching_offset};
        SingleValue<E_stitching_reference_edge> stitching_reference_edge{
            this, AttrName::stitching_reference_edge};
        C_stitching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_trimming : public Collection {
        SingleValue<int> trimming_offset{this, AttrName::trimming_offset};
        SingleValue<E_trimming_reference_edge> trimming_reference_edge{
            this, AttrName::trimming_reference_edge};
        SingleValue<E_trimming_type> trimming_type{this,
                                                   AttrName::trimming_type};
        SingleValue<E_trimming_when> trimming_when{this,
                                                   AttrName::trimming_when};
        C_trimming() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleCollection<C_baling> baling{this, AttrName::baling};
      SingleCollection<C_binding> binding{this, AttrName::binding};
      SingleCollection<C_coating> coating{this, AttrName::coating};
      SingleCollection<C_covering> covering{this, AttrName::covering};
      SingleValue<E_finishing_template> finishing_template{
          this, AttrName::finishing_template};
      SetOfCollections<C_folding> folding{this, AttrName::folding};
      SingleValue<E_imposition_template> imposition_template{
          this, AttrName::imposition_template};
      SingleCollection<C_laminating> laminating{this, AttrName::laminating};
      SingleValue<RangeOfInteger> media_sheets_supported{
          this, AttrName::media_sheets_supported};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleCollection<C_punching> punching{this, AttrName::punching};
      SingleCollection<C_stitching> stitching{this, AttrName::stitching};
      SetOfCollections<C_trimming> trimming{this, AttrName::trimming};
      C_finishings_col_database() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_finishings_col_default : public Collection {
      struct C_baling : public Collection {
        SingleValue<E_baling_type> baling_type{this, AttrName::baling_type};
        SingleValue<E_baling_when> baling_when{this, AttrName::baling_when};
        C_baling() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_binding : public Collection {
        SingleValue<E_binding_reference_edge> binding_reference_edge{
            this, AttrName::binding_reference_edge};
        SingleValue<E_binding_type> binding_type{this, AttrName::binding_type};
        C_binding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_coating : public Collection {
        SingleValue<E_coating_sides> coating_sides{this,
                                                   AttrName::coating_sides};
        SingleValue<E_coating_type> coating_type{this, AttrName::coating_type};
        C_coating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_covering : public Collection {
        SingleValue<E_covering_name> covering_name{this,
                                                   AttrName::covering_name};
        C_covering() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_folding : public Collection {
        SingleValue<E_folding_direction> folding_direction{
            this, AttrName::folding_direction};
        SingleValue<int> folding_offset{this, AttrName::folding_offset};
        SingleValue<E_folding_reference_edge> folding_reference_edge{
            this, AttrName::folding_reference_edge};
        C_folding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_laminating : public Collection {
        SingleValue<E_laminating_sides> laminating_sides{
            this, AttrName::laminating_sides};
        SingleValue<E_laminating_type> laminating_type{
            this, AttrName::laminating_type};
        C_laminating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_media_size : public Collection {
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_punching : public Collection {
        SetOfValues<int> punching_locations{this, AttrName::punching_locations};
        SingleValue<int> punching_offset{this, AttrName::punching_offset};
        SingleValue<E_punching_reference_edge> punching_reference_edge{
            this, AttrName::punching_reference_edge};
        C_punching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_stitching : public Collection {
        SingleValue<int> stitching_angle{this, AttrName::stitching_angle};
        SetOfValues<int> stitching_locations{this,
                                             AttrName::stitching_locations};
        SingleValue<E_stitching_method> stitching_method{
            this, AttrName::stitching_method};
        SingleValue<int> stitching_offset{this, AttrName::stitching_offset};
        SingleValue<E_stitching_reference_edge> stitching_reference_edge{
            this, AttrName::stitching_reference_edge};
        C_stitching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_trimming : public Collection {
        SingleValue<int> trimming_offset{this, AttrName::trimming_offset};
        SingleValue<E_trimming_reference_edge> trimming_reference_edge{
            this, AttrName::trimming_reference_edge};
        SingleValue<E_trimming_type> trimming_type{this,
                                                   AttrName::trimming_type};
        SingleValue<E_trimming_when> trimming_when{this,
                                                   AttrName::trimming_when};
        C_trimming() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleCollection<C_baling> baling{this, AttrName::baling};
      SingleCollection<C_binding> binding{this, AttrName::binding};
      SingleCollection<C_coating> coating{this, AttrName::coating};
      SingleCollection<C_covering> covering{this, AttrName::covering};
      SingleValue<E_finishing_template> finishing_template{
          this, AttrName::finishing_template};
      SetOfCollections<C_folding> folding{this, AttrName::folding};
      SingleValue<E_imposition_template> imposition_template{
          this, AttrName::imposition_template};
      SingleCollection<C_laminating> laminating{this, AttrName::laminating};
      SingleValue<RangeOfInteger> media_sheets_supported{
          this, AttrName::media_sheets_supported};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleCollection<C_punching> punching{this, AttrName::punching};
      SingleCollection<C_stitching> stitching{this, AttrName::stitching};
      SetOfCollections<C_trimming> trimming{this, AttrName::trimming};
      C_finishings_col_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_finishings_col_ready : public Collection {
      struct C_baling : public Collection {
        SingleValue<E_baling_type> baling_type{this, AttrName::baling_type};
        SingleValue<E_baling_when> baling_when{this, AttrName::baling_when};
        C_baling() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_binding : public Collection {
        SingleValue<E_binding_reference_edge> binding_reference_edge{
            this, AttrName::binding_reference_edge};
        SingleValue<E_binding_type> binding_type{this, AttrName::binding_type};
        C_binding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_coating : public Collection {
        SingleValue<E_coating_sides> coating_sides{this,
                                                   AttrName::coating_sides};
        SingleValue<E_coating_type> coating_type{this, AttrName::coating_type};
        C_coating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_covering : public Collection {
        SingleValue<E_covering_name> covering_name{this,
                                                   AttrName::covering_name};
        C_covering() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_folding : public Collection {
        SingleValue<E_folding_direction> folding_direction{
            this, AttrName::folding_direction};
        SingleValue<int> folding_offset{this, AttrName::folding_offset};
        SingleValue<E_folding_reference_edge> folding_reference_edge{
            this, AttrName::folding_reference_edge};
        C_folding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_laminating : public Collection {
        SingleValue<E_laminating_sides> laminating_sides{
            this, AttrName::laminating_sides};
        SingleValue<E_laminating_type> laminating_type{
            this, AttrName::laminating_type};
        C_laminating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_media_size : public Collection {
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_punching : public Collection {
        SetOfValues<int> punching_locations{this, AttrName::punching_locations};
        SingleValue<int> punching_offset{this, AttrName::punching_offset};
        SingleValue<E_punching_reference_edge> punching_reference_edge{
            this, AttrName::punching_reference_edge};
        C_punching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_stitching : public Collection {
        SingleValue<int> stitching_angle{this, AttrName::stitching_angle};
        SetOfValues<int> stitching_locations{this,
                                             AttrName::stitching_locations};
        SingleValue<E_stitching_method> stitching_method{
            this, AttrName::stitching_method};
        SingleValue<int> stitching_offset{this, AttrName::stitching_offset};
        SingleValue<E_stitching_reference_edge> stitching_reference_edge{
            this, AttrName::stitching_reference_edge};
        C_stitching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_trimming : public Collection {
        SingleValue<int> trimming_offset{this, AttrName::trimming_offset};
        SingleValue<E_trimming_reference_edge> trimming_reference_edge{
            this, AttrName::trimming_reference_edge};
        SingleValue<E_trimming_type> trimming_type{this,
                                                   AttrName::trimming_type};
        SingleValue<E_trimming_when> trimming_when{this,
                                                   AttrName::trimming_when};
        C_trimming() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleCollection<C_baling> baling{this, AttrName::baling};
      SingleCollection<C_binding> binding{this, AttrName::binding};
      SingleCollection<C_coating> coating{this, AttrName::coating};
      SingleCollection<C_covering> covering{this, AttrName::covering};
      SingleValue<E_finishing_template> finishing_template{
          this, AttrName::finishing_template};
      SetOfCollections<C_folding> folding{this, AttrName::folding};
      SingleValue<E_imposition_template> imposition_template{
          this, AttrName::imposition_template};
      SingleCollection<C_laminating> laminating{this, AttrName::laminating};
      SingleValue<RangeOfInteger> media_sheets_supported{
          this, AttrName::media_sheets_supported};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleCollection<C_punching> punching{this, AttrName::punching};
      SingleCollection<C_stitching> stitching{this, AttrName::stitching};
      SetOfCollections<C_trimming> trimming{this, AttrName::trimming};
      C_finishings_col_ready() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_insert_sheet_default : public Collection {
      SingleValue<int> insert_after_page_number{
          this, AttrName::insert_after_page_number};
      SingleValue<int> insert_count{this, AttrName::insert_count};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_insert_sheet_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_accounting_sheets_default : public Collection {
      SingleValue<E_job_accounting_output_bin> job_accounting_output_bin{
          this, AttrName::job_accounting_output_bin};
      SingleValue<E_job_accounting_sheets_type> job_accounting_sheets_type{
          this, AttrName::job_accounting_sheets_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_accounting_sheets_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_constraints_supported : public Collection {
      SingleValue<StringWithLanguage> resolver_name{this,
                                                    AttrName::resolver_name};
      C_job_constraints_supported() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_cover_back_default : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_cover_back_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_cover_front_default : public Collection {
      SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_cover_front_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_error_sheet_default : public Collection {
      SingleValue<E_job_error_sheet_type> job_error_sheet_type{
          this, AttrName::job_error_sheet_type};
      SingleValue<E_job_error_sheet_when> job_error_sheet_when{
          this, AttrName::job_error_sheet_when};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_error_sheet_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_finishings_col_default : public Collection {
      struct C_baling : public Collection {
        SingleValue<E_baling_type> baling_type{this, AttrName::baling_type};
        SingleValue<E_baling_when> baling_when{this, AttrName::baling_when};
        C_baling() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_binding : public Collection {
        SingleValue<E_binding_reference_edge> binding_reference_edge{
            this, AttrName::binding_reference_edge};
        SingleValue<E_binding_type> binding_type{this, AttrName::binding_type};
        C_binding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_coating : public Collection {
        SingleValue<E_coating_sides> coating_sides{this,
                                                   AttrName::coating_sides};
        SingleValue<E_coating_type> coating_type{this, AttrName::coating_type};
        C_coating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_covering : public Collection {
        SingleValue<E_covering_name> covering_name{this,
                                                   AttrName::covering_name};
        C_covering() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_folding : public Collection {
        SingleValue<E_folding_direction> folding_direction{
            this, AttrName::folding_direction};
        SingleValue<int> folding_offset{this, AttrName::folding_offset};
        SingleValue<E_folding_reference_edge> folding_reference_edge{
            this, AttrName::folding_reference_edge};
        C_folding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_laminating : public Collection {
        SingleValue<E_laminating_sides> laminating_sides{
            this, AttrName::laminating_sides};
        SingleValue<E_laminating_type> laminating_type{
            this, AttrName::laminating_type};
        C_laminating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_media_size : public Collection {
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_punching : public Collection {
        SetOfValues<int> punching_locations{this, AttrName::punching_locations};
        SingleValue<int> punching_offset{this, AttrName::punching_offset};
        SingleValue<E_punching_reference_edge> punching_reference_edge{
            this, AttrName::punching_reference_edge};
        C_punching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_stitching : public Collection {
        SingleValue<int> stitching_angle{this, AttrName::stitching_angle};
        SetOfValues<int> stitching_locations{this,
                                             AttrName::stitching_locations};
        SingleValue<E_stitching_method> stitching_method{
            this, AttrName::stitching_method};
        SingleValue<int> stitching_offset{this, AttrName::stitching_offset};
        SingleValue<E_stitching_reference_edge> stitching_reference_edge{
            this, AttrName::stitching_reference_edge};
        C_stitching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_trimming : public Collection {
        SingleValue<int> trimming_offset{this, AttrName::trimming_offset};
        SingleValue<E_trimming_reference_edge> trimming_reference_edge{
            this, AttrName::trimming_reference_edge};
        SingleValue<E_trimming_type> trimming_type{this,
                                                   AttrName::trimming_type};
        SingleValue<E_trimming_when> trimming_when{this,
                                                   AttrName::trimming_when};
        C_trimming() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleCollection<C_baling> baling{this, AttrName::baling};
      SingleCollection<C_binding> binding{this, AttrName::binding};
      SingleCollection<C_coating> coating{this, AttrName::coating};
      SingleCollection<C_covering> covering{this, AttrName::covering};
      SingleValue<E_finishing_template> finishing_template{
          this, AttrName::finishing_template};
      SetOfCollections<C_folding> folding{this, AttrName::folding};
      SingleValue<E_imposition_template> imposition_template{
          this, AttrName::imposition_template};
      SingleCollection<C_laminating> laminating{this, AttrName::laminating};
      SingleValue<RangeOfInteger> media_sheets_supported{
          this, AttrName::media_sheets_supported};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleCollection<C_punching> punching{this, AttrName::punching};
      SingleCollection<C_stitching> stitching{this, AttrName::stitching};
      SetOfCollections<C_trimming> trimming{this, AttrName::trimming};
      C_job_finishings_col_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_finishings_col_ready : public Collection {
      struct C_baling : public Collection {
        SingleValue<E_baling_type> baling_type{this, AttrName::baling_type};
        SingleValue<E_baling_when> baling_when{this, AttrName::baling_when};
        C_baling() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_binding : public Collection {
        SingleValue<E_binding_reference_edge> binding_reference_edge{
            this, AttrName::binding_reference_edge};
        SingleValue<E_binding_type> binding_type{this, AttrName::binding_type};
        C_binding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_coating : public Collection {
        SingleValue<E_coating_sides> coating_sides{this,
                                                   AttrName::coating_sides};
        SingleValue<E_coating_type> coating_type{this, AttrName::coating_type};
        C_coating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_covering : public Collection {
        SingleValue<E_covering_name> covering_name{this,
                                                   AttrName::covering_name};
        C_covering() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_folding : public Collection {
        SingleValue<E_folding_direction> folding_direction{
            this, AttrName::folding_direction};
        SingleValue<int> folding_offset{this, AttrName::folding_offset};
        SingleValue<E_folding_reference_edge> folding_reference_edge{
            this, AttrName::folding_reference_edge};
        C_folding() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_laminating : public Collection {
        SingleValue<E_laminating_sides> laminating_sides{
            this, AttrName::laminating_sides};
        SingleValue<E_laminating_type> laminating_type{
            this, AttrName::laminating_type};
        C_laminating() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_media_size : public Collection {
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_punching : public Collection {
        SetOfValues<int> punching_locations{this, AttrName::punching_locations};
        SingleValue<int> punching_offset{this, AttrName::punching_offset};
        SingleValue<E_punching_reference_edge> punching_reference_edge{
            this, AttrName::punching_reference_edge};
        C_punching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_stitching : public Collection {
        SingleValue<int> stitching_angle{this, AttrName::stitching_angle};
        SetOfValues<int> stitching_locations{this,
                                             AttrName::stitching_locations};
        SingleValue<E_stitching_method> stitching_method{
            this, AttrName::stitching_method};
        SingleValue<int> stitching_offset{this, AttrName::stitching_offset};
        SingleValue<E_stitching_reference_edge> stitching_reference_edge{
            this, AttrName::stitching_reference_edge};
        C_stitching() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_trimming : public Collection {
        SingleValue<int> trimming_offset{this, AttrName::trimming_offset};
        SingleValue<E_trimming_reference_edge> trimming_reference_edge{
            this, AttrName::trimming_reference_edge};
        SingleValue<E_trimming_type> trimming_type{this,
                                                   AttrName::trimming_type};
        SingleValue<E_trimming_when> trimming_when{this,
                                                   AttrName::trimming_when};
        C_trimming() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleCollection<C_baling> baling{this, AttrName::baling};
      SingleCollection<C_binding> binding{this, AttrName::binding};
      SingleCollection<C_coating> coating{this, AttrName::coating};
      SingleCollection<C_covering> covering{this, AttrName::covering};
      SingleValue<E_finishing_template> finishing_template{
          this, AttrName::finishing_template};
      SetOfCollections<C_folding> folding{this, AttrName::folding};
      SingleValue<E_imposition_template> imposition_template{
          this, AttrName::imposition_template};
      SingleCollection<C_laminating> laminating{this, AttrName::laminating};
      SingleValue<RangeOfInteger> media_sheets_supported{
          this, AttrName::media_sheets_supported};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleCollection<C_punching> punching{this, AttrName::punching};
      SingleCollection<C_stitching> stitching{this, AttrName::stitching};
      SetOfCollections<C_trimming> trimming{this, AttrName::trimming};
      C_job_finishings_col_ready() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_resolvers_supported : public Collection {
      SingleValue<StringWithLanguage> resolver_name{this,
                                                    AttrName::resolver_name};
      C_job_resolvers_supported() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_job_sheets_col_default : public Collection {
      SingleValue<E_job_sheets> job_sheets{this, AttrName::job_sheets};
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      C_job_sheets_col_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_media_col_database : public Collection {
      struct C_media_size : public Collection {
        SingleValue<int> x_dimension{this, AttrName::x_dimension};
        SingleValue<int> y_dimension{this, AttrName::y_dimension};
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_media_source_properties : public Collection {
        SingleValue<E_media_source_feed_direction> media_source_feed_direction{
            this, AttrName::media_source_feed_direction};
        SingleValue<E_media_source_feed_orientation>
            media_source_feed_orientation{
                this, AttrName::media_source_feed_orientation};
        C_media_source_properties() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleValue<E_media_back_coating> media_back_coating{
          this, AttrName::media_back_coating};
      SingleValue<int> media_bottom_margin{this, AttrName::media_bottom_margin};
      SingleValue<E_media_color> media_color{this, AttrName::media_color};
      SingleValue<E_media_front_coating> media_front_coating{
          this, AttrName::media_front_coating};
      SingleValue<E_media_grain> media_grain{this, AttrName::media_grain};
      SingleValue<int> media_hole_count{this, AttrName::media_hole_count};
      SingleValue<StringWithLanguage> media_info{this, AttrName::media_info};
      SingleValue<E_media_key> media_key{this, AttrName::media_key};
      SingleValue<int> media_left_margin{this, AttrName::media_left_margin};
      SingleValue<int> media_order_count{this, AttrName::media_order_count};
      SingleValue<E_media_pre_printed> media_pre_printed{
          this, AttrName::media_pre_printed};
      SingleValue<E_media_recycled> media_recycled{this,
                                                   AttrName::media_recycled};
      SingleValue<int> media_right_margin{this, AttrName::media_right_margin};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleValue<E_media_source> media_source{this, AttrName::media_source};
      SingleValue<int> media_thickness{this, AttrName::media_thickness};
      SingleValue<E_media_tooth> media_tooth{this, AttrName::media_tooth};
      SingleValue<int> media_top_margin{this, AttrName::media_top_margin};
      SingleValue<E_media_type> media_type{this, AttrName::media_type};
      SingleValue<int> media_weight_metric{this, AttrName::media_weight_metric};
      SingleCollection<C_media_source_properties> media_source_properties{
          this, AttrName::media_source_properties};
      C_media_col_database() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_media_col_default : public Collection {
      struct C_media_size : public Collection {
        SingleValue<int> x_dimension{this, AttrName::x_dimension};
        SingleValue<int> y_dimension{this, AttrName::y_dimension};
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleValue<E_media_back_coating> media_back_coating{
          this, AttrName::media_back_coating};
      SingleValue<int> media_bottom_margin{this, AttrName::media_bottom_margin};
      SingleValue<E_media_color> media_color{this, AttrName::media_color};
      SingleValue<E_media_front_coating> media_front_coating{
          this, AttrName::media_front_coating};
      SingleValue<E_media_grain> media_grain{this, AttrName::media_grain};
      SingleValue<int> media_hole_count{this, AttrName::media_hole_count};
      SingleValue<StringWithLanguage> media_info{this, AttrName::media_info};
      SingleValue<E_media_key> media_key{this, AttrName::media_key};
      SingleValue<int> media_left_margin{this, AttrName::media_left_margin};
      SingleValue<int> media_order_count{this, AttrName::media_order_count};
      SingleValue<E_media_pre_printed> media_pre_printed{
          this, AttrName::media_pre_printed};
      SingleValue<E_media_recycled> media_recycled{this,
                                                   AttrName::media_recycled};
      SingleValue<int> media_right_margin{this, AttrName::media_right_margin};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleValue<E_media_source> media_source{this, AttrName::media_source};
      SingleValue<int> media_thickness{this, AttrName::media_thickness};
      SingleValue<E_media_tooth> media_tooth{this, AttrName::media_tooth};
      SingleValue<int> media_top_margin{this, AttrName::media_top_margin};
      SingleValue<E_media_type> media_type{this, AttrName::media_type};
      SingleValue<int> media_weight_metric{this, AttrName::media_weight_metric};
      C_media_col_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_media_col_ready : public Collection {
      struct C_media_size : public Collection {
        SingleValue<int> x_dimension{this, AttrName::x_dimension};
        SingleValue<int> y_dimension{this, AttrName::y_dimension};
        C_media_size() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      struct C_media_source_properties : public Collection {
        SingleValue<E_media_source_feed_direction> media_source_feed_direction{
            this, AttrName::media_source_feed_direction};
        SingleValue<E_media_source_feed_orientation>
            media_source_feed_orientation{
                this, AttrName::media_source_feed_orientation};
        C_media_source_properties() : Collection(&defs_) {}
        std::vector<Attribute*> GetKnownAttributes() override;
        std::vector<const Attribute*> GetKnownAttributes() const override;
        static const std::map<AttrName, AttrDef> defs_;
      };
      SingleValue<E_media_back_coating> media_back_coating{
          this, AttrName::media_back_coating};
      SingleValue<int> media_bottom_margin{this, AttrName::media_bottom_margin};
      SingleValue<E_media_color> media_color{this, AttrName::media_color};
      SingleValue<E_media_front_coating> media_front_coating{
          this, AttrName::media_front_coating};
      SingleValue<E_media_grain> media_grain{this, AttrName::media_grain};
      SingleValue<int> media_hole_count{this, AttrName::media_hole_count};
      SingleValue<StringWithLanguage> media_info{this, AttrName::media_info};
      SingleValue<E_media_key> media_key{this, AttrName::media_key};
      SingleValue<int> media_left_margin{this, AttrName::media_left_margin};
      SingleValue<int> media_order_count{this, AttrName::media_order_count};
      SingleValue<E_media_pre_printed> media_pre_printed{
          this, AttrName::media_pre_printed};
      SingleValue<E_media_recycled> media_recycled{this,
                                                   AttrName::media_recycled};
      SingleValue<int> media_right_margin{this, AttrName::media_right_margin};
      SingleCollection<C_media_size> media_size{this, AttrName::media_size};
      SingleValue<StringWithLanguage> media_size_name{
          this, AttrName::media_size_name};
      SingleValue<E_media_source> media_source{this, AttrName::media_source};
      SingleValue<int> media_thickness{this, AttrName::media_thickness};
      SingleValue<E_media_tooth> media_tooth{this, AttrName::media_tooth};
      SingleValue<int> media_top_margin{this, AttrName::media_top_margin};
      SingleValue<E_media_type> media_type{this, AttrName::media_type};
      SingleValue<int> media_weight_metric{this, AttrName::media_weight_metric};
      SingleCollection<C_media_source_properties> media_source_properties{
          this, AttrName::media_source_properties};
      C_media_col_ready() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_media_size_supported : public Collection {
      SingleValue<RangeOfInteger> x_dimension{this, AttrName::x_dimension};
      SingleValue<RangeOfInteger> y_dimension{this, AttrName::y_dimension};
      C_media_size_supported() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_pdl_init_file_default : public Collection {
      SingleValue<StringWithLanguage> pdl_init_file_entry{
          this, AttrName::pdl_init_file_entry};
      SingleValue<std::string> pdl_init_file_location{
          this, AttrName::pdl_init_file_location};
      SingleValue<StringWithLanguage> pdl_init_file_name{
          this, AttrName::pdl_init_file_name};
      C_pdl_init_file_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_printer_contact_col : public Collection {
      SingleValue<StringWithLanguage> contact_name{this,
                                                   AttrName::contact_name};
      SingleValue<std::string> contact_uri{this, AttrName::contact_uri};
      SetOfValues<StringWithLanguage> contact_vcard{this,
                                                    AttrName::contact_vcard};
      C_printer_contact_col() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_printer_icc_profiles : public Collection {
      SingleValue<StringWithLanguage> profile_name{this,
                                                   AttrName::profile_name};
      SingleValue<std::string> profile_url{this, AttrName::profile_url};
      C_printer_icc_profiles() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_printer_xri_supported : public Collection {
      SingleValue<E_xri_authentication> xri_authentication{
          this, AttrName::xri_authentication};
      SingleValue<E_xri_security> xri_security{this, AttrName::xri_security};
      SingleValue<std::string> xri_uri{this, AttrName::xri_uri};
      C_printer_xri_supported() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_proof_print_default : public Collection {
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      SingleValue<int> proof_print_copies{this, AttrName::proof_print_copies};
      C_proof_print_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    struct C_separator_sheets_default : public Collection {
      SingleValue<E_media> media{this, AttrName::media};
      SingleCollection<C_media_col> media_col{this, AttrName::media_col};
      SetOfValues<E_separator_sheets_type> separator_sheets_type{
          this, AttrName::separator_sheets_type};
      C_separator_sheets_default() : Collection(&defs_) {}
      std::vector<Attribute*> GetKnownAttributes() override;
      std::vector<const Attribute*> GetKnownAttributes() const override;
      static const std::map<AttrName, AttrDef> defs_;
    };
    OpenSetOfValues<E_baling_type_supported> baling_type_supported{
        this, AttrName::baling_type_supported};
    SetOfValues<E_baling_when_supported> baling_when_supported{
        this, AttrName::baling_when_supported};
    SetOfValues<E_binding_reference_edge_supported>
        binding_reference_edge_supported{
            this, AttrName::binding_reference_edge_supported};
    SetOfValues<E_binding_type_supported> binding_type_supported{
        this, AttrName::binding_type_supported};
    SingleValue<std::string> charset_configured{this,
                                                AttrName::charset_configured};
    SetOfValues<std::string> charset_supported{this,
                                               AttrName::charset_supported};
    SetOfValues<E_coating_sides_supported> coating_sides_supported{
        this, AttrName::coating_sides_supported};
    OpenSetOfValues<E_coating_type_supported> coating_type_supported{
        this, AttrName::coating_type_supported};
    SingleValue<bool> color_supported{this, AttrName::color_supported};
    SetOfValues<E_compression_supported> compression_supported{
        this, AttrName::compression_supported};
    SingleValue<int> copies_default{this, AttrName::copies_default};
    SingleValue<RangeOfInteger> copies_supported{this,
                                                 AttrName::copies_supported};
    SingleCollection<C_cover_back_default> cover_back_default{
        this, AttrName::cover_back_default};
    SetOfValues<E_cover_back_supported> cover_back_supported{
        this, AttrName::cover_back_supported};
    SingleCollection<C_cover_front_default> cover_front_default{
        this, AttrName::cover_front_default};
    SetOfValues<E_cover_front_supported> cover_front_supported{
        this, AttrName::cover_front_supported};
    OpenSetOfValues<E_covering_name_supported> covering_name_supported{
        this, AttrName::covering_name_supported};
    SingleValue<int> device_service_count{this, AttrName::device_service_count};
    SingleValue<std::string> device_uuid{this, AttrName::device_uuid};
    SingleValue<std::string> document_charset_default{
        this, AttrName::document_charset_default};
    SetOfValues<std::string> document_charset_supported{
        this, AttrName::document_charset_supported};
    SingleValue<E_document_digital_signature_default>
        document_digital_signature_default{
            this, AttrName::document_digital_signature_default};
    SetOfValues<E_document_digital_signature_supported>
        document_digital_signature_supported{
            this, AttrName::document_digital_signature_supported};
    SingleValue<std::string> document_format_default{
        this, AttrName::document_format_default};
    SingleCollection<C_document_format_details_default>
        document_format_details_default{
            this, AttrName::document_format_details_default};
    SetOfValues<E_document_format_details_supported>
        document_format_details_supported{
            this, AttrName::document_format_details_supported};
    SetOfValues<std::string> document_format_supported{
        this, AttrName::document_format_supported};
    SetOfValues<E_document_format_varying_attributes>
        document_format_varying_attributes{
            this, AttrName::document_format_varying_attributes};
    SingleValue<StringWithLanguage> document_format_version_default{
        this, AttrName::document_format_version_default};
    SetOfValues<StringWithLanguage> document_format_version_supported{
        this, AttrName::document_format_version_supported};
    SingleValue<std::string> document_natural_language_default{
        this, AttrName::document_natural_language_default};
    SetOfValues<std::string> document_natural_language_supported{
        this, AttrName::document_natural_language_supported};
    SingleValue<int> document_password_supported{
        this, AttrName::document_password_supported};
    SetOfValues<E_feed_orientation_supported> feed_orientation_supported{
        this, AttrName::feed_orientation_supported};
    OpenSetOfValues<E_finishing_template_supported>
        finishing_template_supported{this,
                                     AttrName::finishing_template_supported};
    SetOfCollections<C_finishings_col_database> finishings_col_database{
        this, AttrName::finishings_col_database};
    SetOfCollections<C_finishings_col_default> finishings_col_default{
        this, AttrName::finishings_col_default};
    SetOfCollections<C_finishings_col_ready> finishings_col_ready{
        this, AttrName::finishings_col_ready};
    SetOfValues<E_finishings_default> finishings_default{
        this, AttrName::finishings_default};
    SetOfValues<E_finishings_ready> finishings_ready{
        this, AttrName::finishings_ready};
    SetOfValues<E_finishings_supported> finishings_supported{
        this, AttrName::finishings_supported};
    SetOfValues<E_folding_direction_supported> folding_direction_supported{
        this, AttrName::folding_direction_supported};
    SetOfValues<RangeOfInteger> folding_offset_supported{
        this, AttrName::folding_offset_supported};
    SetOfValues<E_folding_reference_edge_supported>
        folding_reference_edge_supported{
            this, AttrName::folding_reference_edge_supported};
    SingleValue<StringWithLanguage> font_name_requested_default{
        this, AttrName::font_name_requested_default};
    SetOfValues<StringWithLanguage> font_name_requested_supported{
        this, AttrName::font_name_requested_supported};
    SingleValue<int> font_size_requested_default{
        this, AttrName::font_size_requested_default};
    SetOfValues<RangeOfInteger> font_size_requested_supported{
        this, AttrName::font_size_requested_supported};
    SetOfValues<std::string> generated_natural_language_supported{
        this, AttrName::generated_natural_language_supported};
    SetOfValues<E_identify_actions_default> identify_actions_default{
        this, AttrName::identify_actions_default};
    SetOfValues<E_identify_actions_supported> identify_actions_supported{
        this, AttrName::identify_actions_supported};
    SingleValue<RangeOfInteger> insert_after_page_number_supported{
        this, AttrName::insert_after_page_number_supported};
    SingleValue<RangeOfInteger> insert_count_supported{
        this, AttrName::insert_count_supported};
    SetOfCollections<C_insert_sheet_default> insert_sheet_default{
        this, AttrName::insert_sheet_default};
    SetOfValues<E_ipp_features_supported> ipp_features_supported{
        this, AttrName::ipp_features_supported};
    SetOfValues<E_ipp_versions_supported> ipp_versions_supported{
        this, AttrName::ipp_versions_supported};
    SingleValue<int> ippget_event_life{this, AttrName::ippget_event_life};
    SingleValue<StringWithLanguage> job_account_id_default{
        this, AttrName::job_account_id_default};
    SingleValue<bool> job_account_id_supported{
        this, AttrName::job_account_id_supported};
    SingleValue<E_job_account_type_default> job_account_type_default{
        this, AttrName::job_account_type_default};
    OpenSetOfValues<E_job_account_type_supported> job_account_type_supported{
        this, AttrName::job_account_type_supported};
    SingleCollection<C_job_accounting_sheets_default>
        job_accounting_sheets_default{this,
                                      AttrName::job_accounting_sheets_default};
    SingleValue<StringWithLanguage> job_accounting_user_id_default{
        this, AttrName::job_accounting_user_id_default};
    SingleValue<bool> job_accounting_user_id_supported{
        this, AttrName::job_accounting_user_id_supported};
    SingleValue<bool> job_authorization_uri_supported{
        this, AttrName::job_authorization_uri_supported};
    SetOfCollections<C_job_constraints_supported> job_constraints_supported{
        this, AttrName::job_constraints_supported};
    SingleValue<int> job_copies_default{this, AttrName::job_copies_default};
    SingleValue<RangeOfInteger> job_copies_supported{
        this, AttrName::job_copies_supported};
    SingleCollection<C_job_cover_back_default> job_cover_back_default{
        this, AttrName::job_cover_back_default};
    SetOfValues<E_job_cover_back_supported> job_cover_back_supported{
        this, AttrName::job_cover_back_supported};
    SingleCollection<C_job_cover_front_default> job_cover_front_default{
        this, AttrName::job_cover_front_default};
    SetOfValues<E_job_cover_front_supported> job_cover_front_supported{
        this, AttrName::job_cover_front_supported};
    SingleValue<E_job_delay_output_until_default>
        job_delay_output_until_default{
            this, AttrName::job_delay_output_until_default};
    OpenSetOfValues<E_job_delay_output_until_supported>
        job_delay_output_until_supported{
            this, AttrName::job_delay_output_until_supported};
    SingleValue<RangeOfInteger> job_delay_output_until_time_supported{
        this, AttrName::job_delay_output_until_time_supported};
    SingleValue<E_job_error_action_default> job_error_action_default{
        this, AttrName::job_error_action_default};
    SetOfValues<E_job_error_action_supported> job_error_action_supported{
        this, AttrName::job_error_action_supported};
    SingleCollection<C_job_error_sheet_default> job_error_sheet_default{
        this, AttrName::job_error_sheet_default};
    SetOfCollections<C_job_finishings_col_default> job_finishings_col_default{
        this, AttrName::job_finishings_col_default};
    SetOfCollections<C_job_finishings_col_ready> job_finishings_col_ready{
        this, AttrName::job_finishings_col_ready};
    SetOfValues<E_job_finishings_default> job_finishings_default{
        this, AttrName::job_finishings_default};
    SetOfValues<E_job_finishings_ready> job_finishings_ready{
        this, AttrName::job_finishings_ready};
    SetOfValues<E_job_finishings_supported> job_finishings_supported{
        this, AttrName::job_finishings_supported};
    SingleValue<E_job_hold_until_default> job_hold_until_default{
        this, AttrName::job_hold_until_default};
    OpenSetOfValues<E_job_hold_until_supported> job_hold_until_supported{
        this, AttrName::job_hold_until_supported};
    SingleValue<RangeOfInteger> job_hold_until_time_supported{
        this, AttrName::job_hold_until_time_supported};
    SingleValue<bool> job_ids_supported{this, AttrName::job_ids_supported};
    SingleValue<RangeOfInteger> job_impressions_supported{
        this, AttrName::job_impressions_supported};
    SingleValue<RangeOfInteger> job_k_octets_supported{
        this, AttrName::job_k_octets_supported};
    SingleValue<RangeOfInteger> job_media_sheets_supported{
        this, AttrName::job_media_sheets_supported};
    SingleValue<StringWithLanguage> job_message_to_operator_default{
        this, AttrName::job_message_to_operator_default};
    SingleValue<bool> job_message_to_operator_supported{
        this, AttrName::job_message_to_operator_supported};
    SingleValue<bool> job_pages_per_set_supported{
        this, AttrName::job_pages_per_set_supported};
    OpenSetOfValues<E_job_password_encryption_supported>
        job_password_encryption_supported{
            this, AttrName::job_password_encryption_supported};
    SingleValue<int> job_password_supported{this,
                                            AttrName::job_password_supported};
    SingleValue<std::string> job_phone_number_default{
        this, AttrName::job_phone_number_default};
    SingleValue<bool> job_phone_number_supported{
        this, AttrName::job_phone_number_supported};
    SingleValue<int> job_priority_default{this, AttrName::job_priority_default};
    SingleValue<int> job_priority_supported{this,
                                            AttrName::job_priority_supported};
    SingleValue<StringWithLanguage> job_recipient_name_default{
        this, AttrName::job_recipient_name_default};
    SingleValue<bool> job_recipient_name_supported{
        this, AttrName::job_recipient_name_supported};
    SetOfCollections<C_job_resolvers_supported> job_resolvers_supported{
        this, AttrName::job_resolvers_supported};
    SingleValue<StringWithLanguage> job_sheet_message_default{
        this, AttrName::job_sheet_message_default};
    SingleValue<bool> job_sheet_message_supported{
        this, AttrName::job_sheet_message_supported};
    SingleCollection<C_job_sheets_col_default> job_sheets_col_default{
        this, AttrName::job_sheets_col_default};
    SingleValue<E_job_sheets_default> job_sheets_default{
        this, AttrName::job_sheets_default};
    OpenSetOfValues<E_job_sheets_supported> job_sheets_supported{
        this, AttrName::job_sheets_supported};
    SingleValue<E_job_spooling_supported> job_spooling_supported{
        this, AttrName::job_spooling_supported};
    SingleValue<RangeOfInteger> jpeg_k_octets_supported{
        this, AttrName::jpeg_k_octets_supported};
    SingleValue<RangeOfInteger> jpeg_x_dimension_supported{
        this, AttrName::jpeg_x_dimension_supported};
    SingleValue<RangeOfInteger> jpeg_y_dimension_supported{
        this, AttrName::jpeg_y_dimension_supported};
    SetOfValues<E_laminating_sides_supported> laminating_sides_supported{
        this, AttrName::laminating_sides_supported};
    OpenSetOfValues<E_laminating_type_supported> laminating_type_supported{
        this, AttrName::laminating_type_supported};
    SingleValue<int> max_save_info_supported{this,
                                             AttrName::max_save_info_supported};
    SingleValue<int> max_stitching_locations_supported{
        this, AttrName::max_stitching_locations_supported};
    OpenSetOfValues<E_media_back_coating_supported>
        media_back_coating_supported{this,
                                     AttrName::media_back_coating_supported};
    SetOfValues<int> media_bottom_margin_supported{
        this, AttrName::media_bottom_margin_supported};
    SetOfCollections<C_media_col_database> media_col_database{
        this, AttrName::media_col_database};
    SingleCollection<C_media_col_default> media_col_default{
        this, AttrName::media_col_default};
    SetOfCollections<C_media_col_ready> media_col_ready{
        this, AttrName::media_col_ready};
    OpenSetOfValues<E_media_color_supported> media_color_supported{
        this, AttrName::media_color_supported};
    SingleValue<E_media_default> media_default{this, AttrName::media_default};
    OpenSetOfValues<E_media_front_coating_supported>
        media_front_coating_supported{this,
                                      AttrName::media_front_coating_supported};
    OpenSetOfValues<E_media_grain_supported> media_grain_supported{
        this, AttrName::media_grain_supported};
    SetOfValues<RangeOfInteger> media_hole_count_supported{
        this, AttrName::media_hole_count_supported};
    SingleValue<bool> media_info_supported{this,
                                           AttrName::media_info_supported};
    SetOfValues<int> media_left_margin_supported{
        this, AttrName::media_left_margin_supported};
    SetOfValues<RangeOfInteger> media_order_count_supported{
        this, AttrName::media_order_count_supported};
    OpenSetOfValues<E_media_pre_printed_supported> media_pre_printed_supported{
        this, AttrName::media_pre_printed_supported};
    OpenSetOfValues<E_media_ready> media_ready{this, AttrName::media_ready};
    OpenSetOfValues<E_media_recycled_supported> media_recycled_supported{
        this, AttrName::media_recycled_supported};
    SetOfValues<int> media_right_margin_supported{
        this, AttrName::media_right_margin_supported};
    SetOfCollections<C_media_size_supported> media_size_supported{
        this, AttrName::media_size_supported};
    OpenSetOfValues<E_media_source_supported> media_source_supported{
        this, AttrName::media_source_supported};
    OpenSetOfValues<E_media_supported> media_supported{
        this, AttrName::media_supported};
    SingleValue<RangeOfInteger> media_thickness_supported{
        this, AttrName::media_thickness_supported};
    OpenSetOfValues<E_media_tooth_supported> media_tooth_supported{
        this, AttrName::media_tooth_supported};
    SetOfValues<int> media_top_margin_supported{
        this, AttrName::media_top_margin_supported};
    OpenSetOfValues<E_media_type_supported> media_type_supported{
        this, AttrName::media_type_supported};
    SetOfValues<RangeOfInteger> media_weight_metric_supported{
        this, AttrName::media_weight_metric_supported};
    SingleValue<E_multiple_document_handling_default>
        multiple_document_handling_default{
            this, AttrName::multiple_document_handling_default};
    SetOfValues<E_multiple_document_handling_supported>
        multiple_document_handling_supported{
            this, AttrName::multiple_document_handling_supported};
    SingleValue<bool> multiple_document_jobs_supported{
        this, AttrName::multiple_document_jobs_supported};
    SingleValue<int> multiple_operation_time_out{
        this, AttrName::multiple_operation_time_out};
    SingleValue<E_multiple_operation_time_out_action>
        multiple_operation_time_out_action{
            this, AttrName::multiple_operation_time_out_action};
    SingleValue<std::string> natural_language_configured{
        this, AttrName::natural_language_configured};
    SetOfValues<E_notify_events_default> notify_events_default{
        this, AttrName::notify_events_default};
    SetOfValues<E_notify_events_supported> notify_events_supported{
        this, AttrName::notify_events_supported};
    SingleValue<int> notify_lease_duration_default{
        this, AttrName::notify_lease_duration_default};
    SetOfValues<RangeOfInteger> notify_lease_duration_supported{
        this, AttrName::notify_lease_duration_supported};
    SetOfValues<E_notify_pull_method_supported> notify_pull_method_supported{
        this, AttrName::notify_pull_method_supported};
    SetOfValues<std::string> notify_schemes_supported{
        this, AttrName::notify_schemes_supported};
    SingleValue<int> number_up_default{this, AttrName::number_up_default};
    SingleValue<RangeOfInteger> number_up_supported{
        this, AttrName::number_up_supported};
    SingleValue<std::string> oauth_authorization_server_uri{
        this, AttrName::oauth_authorization_server_uri};
    SetOfValues<E_operations_supported> operations_supported{
        this, AttrName::operations_supported};
    SingleValue<E_orientation_requested_default> orientation_requested_default{
        this, AttrName::orientation_requested_default};
    SetOfValues<E_orientation_requested_supported>
        orientation_requested_supported{
            this, AttrName::orientation_requested_supported};
    SingleValue<E_output_bin_default> output_bin_default{
        this, AttrName::output_bin_default};
    OpenSetOfValues<E_output_bin_supported> output_bin_supported{
        this, AttrName::output_bin_supported};
    SetOfValues<StringWithLanguage> output_device_supported{
        this, AttrName::output_device_supported};
    SetOfValues<std::string> output_device_uuid_supported{
        this, AttrName::output_device_uuid_supported};
    SingleValue<E_page_delivery_default> page_delivery_default{
        this, AttrName::page_delivery_default};
    SetOfValues<E_page_delivery_supported> page_delivery_supported{
        this, AttrName::page_delivery_supported};
    SingleValue<E_page_order_received_default> page_order_received_default{
        this, AttrName::page_order_received_default};
    SetOfValues<E_page_order_received_supported> page_order_received_supported{
        this, AttrName::page_order_received_supported};
    SingleValue<bool> page_ranges_supported{this,
                                            AttrName::page_ranges_supported};
    SingleValue<int> pages_per_minute{this, AttrName::pages_per_minute};
    SingleValue<int> pages_per_minute_color{this,
                                            AttrName::pages_per_minute_color};
    SingleValue<bool> pages_per_subset_supported{
        this, AttrName::pages_per_subset_supported};
    SetOfValues<std::string> parent_printers_supported{
        this, AttrName::parent_printers_supported};
    SingleValue<RangeOfInteger> pdf_k_octets_supported{
        this, AttrName::pdf_k_octets_supported};
    SetOfValues<E_pdf_versions_supported> pdf_versions_supported{
        this, AttrName::pdf_versions_supported};
    SingleCollection<C_pdl_init_file_default> pdl_init_file_default{
        this, AttrName::pdl_init_file_default};
    SetOfValues<StringWithLanguage> pdl_init_file_entry_supported{
        this, AttrName::pdl_init_file_entry_supported};
    SetOfValues<std::string> pdl_init_file_location_supported{
        this, AttrName::pdl_init_file_location_supported};
    SingleValue<bool> pdl_init_file_name_subdirectory_supported{
        this, AttrName::pdl_init_file_name_subdirectory_supported};
    SetOfValues<StringWithLanguage> pdl_init_file_name_supported{
        this, AttrName::pdl_init_file_name_supported};
    SetOfValues<E_pdl_init_file_supported> pdl_init_file_supported{
        this, AttrName::pdl_init_file_supported};
    SingleValue<E_pdl_override_supported> pdl_override_supported{
        this, AttrName::pdl_override_supported};
    SingleValue<bool> preferred_attributes_supported{
        this, AttrName::preferred_attributes_supported};
    SingleValue<E_presentation_direction_number_up_default>
        presentation_direction_number_up_default{
            this, AttrName::presentation_direction_number_up_default};
    SetOfValues<E_presentation_direction_number_up_supported>
        presentation_direction_number_up_supported{
            this, AttrName::presentation_direction_number_up_supported};
    SingleValue<E_print_color_mode_default> print_color_mode_default{
        this, AttrName::print_color_mode_default};
    SetOfValues<E_print_color_mode_supported> print_color_mode_supported{
        this, AttrName::print_color_mode_supported};
    SingleValue<E_print_content_optimize_default>
        print_content_optimize_default{
            this, AttrName::print_content_optimize_default};
    SetOfValues<E_print_content_optimize_supported>
        print_content_optimize_supported{
            this, AttrName::print_content_optimize_supported};
    SingleValue<E_print_quality_default> print_quality_default{
        this, AttrName::print_quality_default};
    SetOfValues<E_print_quality_supported> print_quality_supported{
        this, AttrName::print_quality_supported};
    SingleValue<E_print_rendering_intent_default>
        print_rendering_intent_default{
            this, AttrName::print_rendering_intent_default};
    SetOfValues<E_print_rendering_intent_supported>
        print_rendering_intent_supported{
            this, AttrName::print_rendering_intent_supported};
    SetOfValues<std::string> printer_alert{this, AttrName::printer_alert};
    SetOfValues<StringWithLanguage> printer_alert_description{
        this, AttrName::printer_alert_description};
    SingleValue<StringWithLanguage> printer_charge_info{
        this, AttrName::printer_charge_info};
    SingleValue<std::string> printer_charge_info_uri{
        this, AttrName::printer_charge_info_uri};
    SingleValue<DateTime> printer_config_change_date_time{
        this, AttrName::printer_config_change_date_time};
    SingleValue<int> printer_config_change_time{
        this, AttrName::printer_config_change_time};
    SingleValue<int> printer_config_changes{this,
                                            AttrName::printer_config_changes};
    SingleCollection<C_printer_contact_col> printer_contact_col{
        this, AttrName::printer_contact_col};
    SingleValue<DateTime> printer_current_time{this,
                                               AttrName::printer_current_time};
    SetOfValues<StringWithLanguage> printer_detailed_status_messages{
        this, AttrName::printer_detailed_status_messages};
    SingleValue<StringWithLanguage> printer_device_id{
        this, AttrName::printer_device_id};
    SingleValue<StringWithLanguage> printer_dns_sd_name{
        this, AttrName::printer_dns_sd_name};
    SingleValue<std::string> printer_driver_installer{
        this, AttrName::printer_driver_installer};
    SetOfValues<std::string> printer_finisher{this, AttrName::printer_finisher};
    SetOfValues<StringWithLanguage> printer_finisher_description{
        this, AttrName::printer_finisher_description};
    SetOfValues<std::string> printer_finisher_supplies{
        this, AttrName::printer_finisher_supplies};
    SetOfValues<StringWithLanguage> printer_finisher_supplies_description{
        this, AttrName::printer_finisher_supplies_description};
    SingleValue<std::string> printer_geo_location{
        this, AttrName::printer_geo_location};
    SetOfCollections<C_printer_icc_profiles> printer_icc_profiles{
        this, AttrName::printer_icc_profiles};
    SetOfValues<std::string> printer_icons{this, AttrName::printer_icons};
    SingleValue<int> printer_id{this, AttrName::printer_id};
    SingleValue<int> printer_impressions_completed{
        this, AttrName::printer_impressions_completed};
    SingleValue<StringWithLanguage> printer_info{this, AttrName::printer_info};
    SetOfValues<std::string> printer_input_tray{this,
                                                AttrName::printer_input_tray};
    SingleValue<bool> printer_is_accepting_jobs{
        this, AttrName::printer_is_accepting_jobs};
    SingleValue<StringWithLanguage> printer_location{
        this, AttrName::printer_location};
    SingleValue<StringWithLanguage> printer_make_and_model{
        this, AttrName::printer_make_and_model};
    SingleValue<int> printer_media_sheets_completed{
        this, AttrName::printer_media_sheets_completed};
    SingleValue<DateTime> printer_message_date_time{
        this, AttrName::printer_message_date_time};
    SingleValue<StringWithLanguage> printer_message_from_operator{
        this, AttrName::printer_message_from_operator};
    SingleValue<int> printer_message_time{this, AttrName::printer_message_time};
    SingleValue<std::string> printer_more_info{this,
                                               AttrName::printer_more_info};
    SingleValue<std::string> printer_more_info_manufacturer{
        this, AttrName::printer_more_info_manufacturer};
    SingleValue<StringWithLanguage> printer_name{this, AttrName::printer_name};
    SetOfValues<StringWithLanguage> printer_organization{
        this, AttrName::printer_organization};
    SetOfValues<StringWithLanguage> printer_organizational_unit{
        this, AttrName::printer_organizational_unit};
    SetOfValues<std::string> printer_output_tray{this,
                                                 AttrName::printer_output_tray};
    SingleValue<int> printer_pages_completed{this,
                                             AttrName::printer_pages_completed};
    SingleValue<Resolution> printer_resolution_default{
        this, AttrName::printer_resolution_default};
    SingleValue<Resolution> printer_resolution_supported{
        this, AttrName::printer_resolution_supported};
    SingleValue<E_printer_state> printer_state{this, AttrName::printer_state};
    SingleValue<DateTime> printer_state_change_date_time{
        this, AttrName::printer_state_change_date_time};
    SingleValue<int> printer_state_change_time{
        this, AttrName::printer_state_change_time};
    SingleValue<StringWithLanguage> printer_state_message{
        this, AttrName::printer_state_message};
    SetOfValues<E_printer_state_reasons> printer_state_reasons{
        this, AttrName::printer_state_reasons};
    SingleValue<std::string> printer_static_resource_directory_uri{
        this, AttrName::printer_static_resource_directory_uri};
    SingleValue<int> printer_static_resource_k_octets_free{
        this, AttrName::printer_static_resource_k_octets_free};
    SingleValue<int> printer_static_resource_k_octets_supported{
        this, AttrName::printer_static_resource_k_octets_supported};
    SetOfValues<std::string> printer_strings_languages_supported{
        this, AttrName::printer_strings_languages_supported};
    SingleValue<std::string> printer_strings_uri{this,
                                                 AttrName::printer_strings_uri};
    SetOfValues<std::string> printer_supply{this, AttrName::printer_supply};
    SetOfValues<StringWithLanguage> printer_supply_description{
        this, AttrName::printer_supply_description};
    SingleValue<std::string> printer_supply_info_uri{
        this, AttrName::printer_supply_info_uri};
    SingleValue<int> printer_up_time{this, AttrName::printer_up_time};
    SetOfValues<std::string> printer_uri_supported{
        this, AttrName::printer_uri_supported};
    SingleValue<std::string> printer_uuid{this, AttrName::printer_uuid};
    SetOfCollections<C_printer_xri_supported> printer_xri_supported{
        this, AttrName::printer_xri_supported};
    SingleCollection<C_proof_print_default> proof_print_default{
        this, AttrName::proof_print_default};
    SetOfValues<E_proof_print_supported> proof_print_supported{
        this, AttrName::proof_print_supported};
    SingleValue<int> punching_hole_diameter_configured{
        this, AttrName::punching_hole_diameter_configured};
    SetOfValues<RangeOfInteger> punching_locations_supported{
        this, AttrName::punching_locations_supported};
    SetOfValues<RangeOfInteger> punching_offset_supported{
        this, AttrName::punching_offset_supported};
    SetOfValues<E_punching_reference_edge_supported>
        punching_reference_edge_supported{
            this, AttrName::punching_reference_edge_supported};
    SetOfValues<Resolution> pwg_raster_document_resolution_supported{
        this, AttrName::pwg_raster_document_resolution_supported};
    SingleValue<E_pwg_raster_document_sheet_back>
        pwg_raster_document_sheet_back{
            this, AttrName::pwg_raster_document_sheet_back};
    SetOfValues<E_pwg_raster_document_type_supported>
        pwg_raster_document_type_supported{
            this, AttrName::pwg_raster_document_type_supported};
    SingleValue<int> queued_job_count{this, AttrName::queued_job_count};
    SetOfValues<std::string> reference_uri_schemes_supported{
        this, AttrName::reference_uri_schemes_supported};
    SingleValue<bool> requesting_user_uri_supported{
        this, AttrName::requesting_user_uri_supported};
    SetOfValues<E_save_disposition_supported> save_disposition_supported{
        this, AttrName::save_disposition_supported};
    SingleValue<std::string> save_document_format_default{
        this, AttrName::save_document_format_default};
    SetOfValues<std::string> save_document_format_supported{
        this, AttrName::save_document_format_supported};
    SingleValue<std::string> save_location_default{
        this, AttrName::save_location_default};
    SetOfValues<std::string> save_location_supported{
        this, AttrName::save_location_supported};
    SingleValue<bool> save_name_subdirectory_supported{
        this, AttrName::save_name_subdirectory_supported};
    SingleValue<bool> save_name_supported{this, AttrName::save_name_supported};
    SingleCollection<C_separator_sheets_default> separator_sheets_default{
        this, AttrName::separator_sheets_default};
    SingleValue<E_sheet_collate_default> sheet_collate_default{
        this, AttrName::sheet_collate_default};
    SetOfValues<E_sheet_collate_supported> sheet_collate_supported{
        this, AttrName::sheet_collate_supported};
    SingleValue<E_sides_default> sides_default{this, AttrName::sides_default};
    SetOfValues<E_sides_supported> sides_supported{this,
                                                   AttrName::sides_supported};
    SetOfValues<RangeOfInteger> stitching_angle_supported{
        this, AttrName::stitching_angle_supported};
    SetOfValues<RangeOfInteger> stitching_locations_supported{
        this, AttrName::stitching_locations_supported};
    SetOfValues<E_stitching_method_supported> stitching_method_supported{
        this, AttrName::stitching_method_supported};
    SetOfValues<RangeOfInteger> stitching_offset_supported{
        this, AttrName::stitching_offset_supported};
    SetOfValues<E_stitching_reference_edge_supported>
        stitching_reference_edge_supported{
            this, AttrName::stitching_reference_edge_supported};
    SetOfValues<std::string> subordinate_printers_supported{
        this, AttrName::subordinate_printers_supported};
    SetOfValues<RangeOfInteger> trimming_offset_supported{
        this, AttrName::trimming_offset_supported};
    SetOfValues<E_trimming_reference_edge_supported>
        trimming_reference_edge_supported{
            this, AttrName::trimming_reference_edge_supported};
    SetOfValues<E_trimming_type_supported> trimming_type_supported{
        this, AttrName::trimming_type_supported};
    SetOfValues<E_trimming_when_supported> trimming_when_supported{
        this, AttrName::trimming_when_supported};
    SetOfValues<E_uri_authentication_supported> uri_authentication_supported{
        this, AttrName::uri_authentication_supported};
    SetOfValues<E_uri_security_supported> uri_security_supported{
        this, AttrName::uri_security_supported};
    SetOfValues<E_which_jobs_supported> which_jobs_supported{
        this, AttrName::which_jobs_supported};
    SingleValue<E_x_image_position_default> x_image_position_default{
        this, AttrName::x_image_position_default};
    SetOfValues<E_x_image_position_supported> x_image_position_supported{
        this, AttrName::x_image_position_supported};
    SingleValue<int> x_image_shift_default{this,
                                           AttrName::x_image_shift_default};
    SingleValue<RangeOfInteger> x_image_shift_supported{
        this, AttrName::x_image_shift_supported};
    SingleValue<int> x_side1_image_shift_default{
        this, AttrName::x_side1_image_shift_default};
    SingleValue<RangeOfInteger> x_side1_image_shift_supported{
        this, AttrName::x_side1_image_shift_supported};
    SingleValue<int> x_side2_image_shift_default{
        this, AttrName::x_side2_image_shift_default};
    SingleValue<RangeOfInteger> x_side2_image_shift_supported{
        this, AttrName::x_side2_image_shift_supported};
    SetOfValues<E_xri_authentication_supported> xri_authentication_supported{
        this, AttrName::xri_authentication_supported};
    SetOfValues<E_xri_security_supported> xri_security_supported{
        this, AttrName::xri_security_supported};
    SetOfValues<std::string> xri_uri_scheme_supported{
        this, AttrName::xri_uri_scheme_supported};
    SingleValue<E_y_image_position_default> y_image_position_default{
        this, AttrName::y_image_position_default};
    SetOfValues<E_y_image_position_supported> y_image_position_supported{
        this, AttrName::y_image_position_supported};
    SingleValue<int> y_image_shift_default{this,
                                           AttrName::y_image_shift_default};
    SingleValue<RangeOfInteger> y_image_shift_supported{
        this, AttrName::y_image_shift_supported};
    SingleValue<int> y_side1_image_shift_default{
        this, AttrName::y_side1_image_shift_default};
    SingleValue<RangeOfInteger> y_side1_image_shift_supported{
        this, AttrName::y_side1_image_shift_supported};
    SingleValue<int> y_side2_image_shift_default{
        this, AttrName::y_side2_image_shift_default};
    SingleValue<RangeOfInteger> y_side2_image_shift_supported{
        this, AttrName::y_side2_image_shift_supported};
    G_printer_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_printer_attributes> printer_attributes{
      GroupTag::printer_attributes};
  Response_Get_Printer_Attributes();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_Hold_Job : public Request {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<std::string> printer_uri{this, AttrName::printer_uri};
    SingleValue<int> job_id{this, AttrName::job_id};
    SingleValue<std::string> job_uri{this, AttrName::job_uri};
    SingleValue<StringWithLanguage> requesting_user_name{
        this, AttrName::requesting_user_name};
    SingleValue<StringWithLanguage> message{this, AttrName::message};
    SingleValue<E_job_hold_until> job_hold_until{this,
                                                 AttrName::job_hold_until};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Request_Hold_Job();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_Hold_Job : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  typedef Response_CUPS_Authenticate_Job::G_unsupported_attributes
      G_unsupported_attributes;
  SingleGroup<G_unsupported_attributes> unsupported_attributes{
      GroupTag::unsupported_attributes};
  Response_Hold_Job();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_Pause_Printer : public Request {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<std::string> printer_uri{this, AttrName::printer_uri};
    SingleValue<StringWithLanguage> requesting_user_name{
        this, AttrName::requesting_user_name};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Request_Pause_Printer();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_Pause_Printer : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  typedef Response_CUPS_Authenticate_Job::G_unsupported_attributes
      G_unsupported_attributes;
  SingleGroup<G_unsupported_attributes> unsupported_attributes{
      GroupTag::unsupported_attributes};
  Response_Pause_Printer();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_Print_Job : public Request {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<std::string> printer_uri{this, AttrName::printer_uri};
    SingleValue<StringWithLanguage> requesting_user_name{
        this, AttrName::requesting_user_name};
    SingleValue<StringWithLanguage> job_name{this, AttrName::job_name};
    SingleValue<bool> ipp_attribute_fidelity{this,
                                             AttrName::ipp_attribute_fidelity};
    SingleValue<StringWithLanguage> document_name{this,
                                                  AttrName::document_name};
    SingleValue<E_compression> compression{this, AttrName::compression};
    SingleValue<std::string> document_format{this, AttrName::document_format};
    SingleValue<std::string> document_natural_language{
        this, AttrName::document_natural_language};
    SingleValue<int> job_k_octets{this, AttrName::job_k_octets};
    SingleValue<int> job_impressions{this, AttrName::job_impressions};
    SingleValue<int> job_media_sheets{this, AttrName::job_media_sheets};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  typedef Request_Create_Job::G_job_attributes G_job_attributes;
  SingleGroup<G_job_attributes> job_attributes{GroupTag::job_attributes};
  Request_Print_Job();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_Print_Job : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  typedef Response_CUPS_Authenticate_Job::G_unsupported_attributes
      G_unsupported_attributes;
  SingleGroup<G_unsupported_attributes> unsupported_attributes{
      GroupTag::unsupported_attributes};
  typedef Response_Create_Job::G_job_attributes G_job_attributes;
  SingleGroup<G_job_attributes> job_attributes{GroupTag::job_attributes};
  Response_Print_Job();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_Print_URI : public Request {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<std::string> printer_uri{this, AttrName::printer_uri};
    SingleValue<std::string> document_uri{this, AttrName::document_uri};
    SingleValue<StringWithLanguage> requesting_user_name{
        this, AttrName::requesting_user_name};
    SingleValue<StringWithLanguage> job_name{this, AttrName::job_name};
    SingleValue<bool> ipp_attribute_fidelity{this,
                                             AttrName::ipp_attribute_fidelity};
    SingleValue<StringWithLanguage> document_name{this,
                                                  AttrName::document_name};
    SingleValue<E_compression> compression{this, AttrName::compression};
    SingleValue<std::string> document_format{this, AttrName::document_format};
    SingleValue<std::string> document_natural_language{
        this, AttrName::document_natural_language};
    SingleValue<int> job_k_octets{this, AttrName::job_k_octets};
    SingleValue<int> job_impressions{this, AttrName::job_impressions};
    SingleValue<int> job_media_sheets{this, AttrName::job_media_sheets};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  typedef Request_Create_Job::G_job_attributes G_job_attributes;
  SingleGroup<G_job_attributes> job_attributes{GroupTag::job_attributes};
  Request_Print_URI();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_Print_URI : public Response {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<StringWithLanguage> status_message{this,
                                                   AttrName::status_message};
    SingleValue<StringWithLanguage> detailed_status_message{
        this, AttrName::detailed_status_message};
    SingleValue<StringWithLanguage> document_access_error{
        this, AttrName::document_access_error};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  typedef Response_CUPS_Authenticate_Job::G_unsupported_attributes
      G_unsupported_attributes;
  SingleGroup<G_unsupported_attributes> unsupported_attributes{
      GroupTag::unsupported_attributes};
  typedef Response_Create_Job::G_job_attributes G_job_attributes;
  SingleGroup<G_job_attributes> job_attributes{GroupTag::job_attributes};
  Response_Print_URI();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_Release_Job : public Request {
  typedef Request_Cancel_Job::G_operation_attributes G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Request_Release_Job();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_Release_Job : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  typedef Response_CUPS_Authenticate_Job::G_unsupported_attributes
      G_unsupported_attributes;
  SingleGroup<G_unsupported_attributes> unsupported_attributes{
      GroupTag::unsupported_attributes};
  Response_Release_Job();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_Resume_Printer : public Request {
  typedef Request_Pause_Printer::G_operation_attributes G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Request_Resume_Printer();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_Resume_Printer : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  typedef Response_CUPS_Authenticate_Job::G_unsupported_attributes
      G_unsupported_attributes;
  SingleGroup<G_unsupported_attributes> unsupported_attributes{
      GroupTag::unsupported_attributes};
  Response_Resume_Printer();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_Send_Document : public Request {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<std::string> printer_uri{this, AttrName::printer_uri};
    SingleValue<int> job_id{this, AttrName::job_id};
    SingleValue<std::string> job_uri{this, AttrName::job_uri};
    SingleValue<StringWithLanguage> requesting_user_name{
        this, AttrName::requesting_user_name};
    SingleValue<StringWithLanguage> document_name{this,
                                                  AttrName::document_name};
    SingleValue<E_compression> compression{this, AttrName::compression};
    SingleValue<std::string> document_format{this, AttrName::document_format};
    SingleValue<std::string> document_natural_language{
        this, AttrName::document_natural_language};
    SingleValue<bool> last_document{this, AttrName::last_document};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Request_Send_Document();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_Send_Document : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  typedef Response_CUPS_Authenticate_Job::G_unsupported_attributes
      G_unsupported_attributes;
  SingleGroup<G_unsupported_attributes> unsupported_attributes{
      GroupTag::unsupported_attributes};
  typedef Response_Create_Job::G_job_attributes G_job_attributes;
  SingleGroup<G_job_attributes> job_attributes{GroupTag::job_attributes};
  Response_Send_Document();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_Send_URI : public Request {
  struct G_operation_attributes : public Collection {
    SingleValue<std::string> attributes_charset{this,
                                                AttrName::attributes_charset};
    SingleValue<std::string> attributes_natural_language{
        this, AttrName::attributes_natural_language};
    SingleValue<std::string> printer_uri{this, AttrName::printer_uri};
    SingleValue<int> job_id{this, AttrName::job_id};
    SingleValue<std::string> job_uri{this, AttrName::job_uri};
    SingleValue<StringWithLanguage> requesting_user_name{
        this, AttrName::requesting_user_name};
    SingleValue<StringWithLanguage> document_name{this,
                                                  AttrName::document_name};
    SingleValue<E_compression> compression{this, AttrName::compression};
    SingleValue<std::string> document_format{this, AttrName::document_format};
    SingleValue<std::string> document_natural_language{
        this, AttrName::document_natural_language};
    SingleValue<bool> last_document{this, AttrName::last_document};
    SingleValue<std::string> document_uri{this, AttrName::document_uri};
    G_operation_attributes() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  Request_Send_URI();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_Send_URI : public Response {
  typedef Response_Print_URI::G_operation_attributes G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  typedef Response_CUPS_Authenticate_Job::G_unsupported_attributes
      G_unsupported_attributes;
  SingleGroup<G_unsupported_attributes> unsupported_attributes{
      GroupTag::unsupported_attributes};
  typedef Response_Create_Job::G_job_attributes G_job_attributes;
  SingleGroup<G_job_attributes> job_attributes{GroupTag::job_attributes};
  Response_Send_URI();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Request_Validate_Job : public Request {
  typedef Request_Print_Job::G_operation_attributes G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  typedef Request_Create_Job::G_job_attributes G_job_attributes;
  SingleGroup<G_job_attributes> job_attributes{GroupTag::job_attributes};
  Request_Validate_Job();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
struct IPP_EXPORT Response_Validate_Job : public Response {
  typedef Response_CUPS_Add_Modify_Class::G_operation_attributes
      G_operation_attributes;
  SingleGroup<G_operation_attributes> operation_attributes{
      GroupTag::operation_attributes};
  typedef Response_CUPS_Authenticate_Job::G_unsupported_attributes
      G_unsupported_attributes;
  SingleGroup<G_unsupported_attributes> unsupported_attributes{
      GroupTag::unsupported_attributes};
  Response_Validate_Job();
  std::vector<Group*> GetKnownGroups() override;
  std::vector<const Group*> GetKnownGroups() const override;
};
}  // namespace ipp

#endif  //  LIBIPP_IPP_OPERATIONS_H_
