// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBIPP_IPP_COLLECTIONS_H_
#define LIBIPP_IPP_COLLECTIONS_H_

#include <map>
#include <string>
#include <vector>

#include "ipp_attribute.h"
#include "ipp_enums.h"
#include "ipp_export.h"
#include "ipp_package.h"

// This file contains definition of classes corresponding to supported IPP
// collection attributes. See ipp.h for more details.
// This file was generated from IPP schema based on the following sources:
// * [rfc8011]
// * [CUPS Implementation of IPP] at https://www.cups.org/doc/spec-ipp.html
// * [IPP registry] at https://www.pwg.org/ipp/ipp-registrations.xml

namespace ipp {
struct IPP_EXPORT C_job_constraints_supported : public Collection {
  SingleValue<StringWithLanguage> resolver_name{this, AttrName::resolver_name};
  C_job_constraints_supported() : Collection(&defs_) {}
  std::vector<Attribute*> GetKnownAttributes() override;
  std::vector<const Attribute*> GetKnownAttributes() const override;
  static const std::map<AttrName, AttrDef> defs_;
};
struct IPP_EXPORT C_job_finishings_col_actual : public Collection {
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
  SingleValue<E_media_recycled> media_recycled{this, AttrName::media_recycled};
  SingleValue<int> media_right_margin{this, AttrName::media_right_margin};
  SingleCollection<C_media_size> media_size{this, AttrName::media_size};
  SingleValue<StringWithLanguage> media_size_name{this,
                                                  AttrName::media_size_name};
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
typedef C_job_constraints_supported C_job_resolvers_supported;
struct IPP_EXPORT C_job_save_disposition : public Collection {
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
  SingleValue<E_save_disposition> save_disposition{this,
                                                   AttrName::save_disposition};
  SetOfCollections<C_save_info> save_info{this, AttrName::save_info};
  C_job_save_disposition() : Collection(&defs_) {}
  std::vector<Attribute*> GetKnownAttributes() override;
  std::vector<const Attribute*> GetKnownAttributes() const override;
  static const std::map<AttrName, AttrDef> defs_;
};
typedef C_job_finishings_col_actual C_media_col;
typedef C_job_finishings_col_actual C_media_col_actual;
struct IPP_EXPORT C_media_col_database : public Collection {
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
    SingleValue<E_media_source_feed_orientation> media_source_feed_orientation{
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
  SingleValue<E_media_recycled> media_recycled{this, AttrName::media_recycled};
  SingleValue<int> media_right_margin{this, AttrName::media_right_margin};
  SingleCollection<C_media_size> media_size{this, AttrName::media_size};
  SingleValue<StringWithLanguage> media_size_name{this,
                                                  AttrName::media_size_name};
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
typedef C_job_finishings_col_actual C_media_col_default;
typedef C_media_col_database C_media_col_ready;
struct IPP_EXPORT C_media_size_supported : public Collection {
  SingleValue<RangeOfInteger> x_dimension{this, AttrName::x_dimension};
  SingleValue<RangeOfInteger> y_dimension{this, AttrName::y_dimension};
  C_media_size_supported() : Collection(&defs_) {}
  std::vector<Attribute*> GetKnownAttributes() override;
  std::vector<const Attribute*> GetKnownAttributes() const override;
  static const std::map<AttrName, AttrDef> defs_;
};
struct IPP_EXPORT C_pdl_init_file : public Collection {
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
typedef C_pdl_init_file C_pdl_init_file_default;
struct IPP_EXPORT C_printer_contact_col : public Collection {
  SingleValue<StringWithLanguage> contact_name{this, AttrName::contact_name};
  SingleValue<std::string> contact_uri{this, AttrName::contact_uri};
  SetOfValues<StringWithLanguage> contact_vcard{this, AttrName::contact_vcard};
  C_printer_contact_col() : Collection(&defs_) {}
  std::vector<Attribute*> GetKnownAttributes() override;
  std::vector<const Attribute*> GetKnownAttributes() const override;
  static const std::map<AttrName, AttrDef> defs_;
};
struct IPP_EXPORT C_printer_icc_profiles : public Collection {
  SingleValue<StringWithLanguage> profile_name{this, AttrName::profile_name};
  SingleValue<std::string> profile_url{this, AttrName::profile_url};
  C_printer_icc_profiles() : Collection(&defs_) {}
  std::vector<Attribute*> GetKnownAttributes() override;
  std::vector<const Attribute*> GetKnownAttributes() const override;
  static const std::map<AttrName, AttrDef> defs_;
};
struct IPP_EXPORT C_printer_xri_supported : public Collection {
  SingleValue<E_xri_authentication> xri_authentication{
      this, AttrName::xri_authentication};
  SingleValue<E_xri_security> xri_security{this, AttrName::xri_security};
  SingleValue<std::string> xri_uri{this, AttrName::xri_uri};
  C_printer_xri_supported() : Collection(&defs_) {}
  std::vector<Attribute*> GetKnownAttributes() override;
  std::vector<const Attribute*> GetKnownAttributes() const override;
  static const std::map<AttrName, AttrDef> defs_;
};
struct IPP_EXPORT C_proof_print : public Collection {
  SingleValue<E_media> media{this, AttrName::media};
  SingleCollection<C_media_col> media_col{this, AttrName::media_col};
  SingleValue<int> proof_print_copies{this, AttrName::proof_print_copies};
  C_proof_print() : Collection(&defs_) {}
  std::vector<Attribute*> GetKnownAttributes() override;
  std::vector<const Attribute*> GetKnownAttributes() const override;
  static const std::map<AttrName, AttrDef> defs_;
};
typedef C_proof_print C_proof_print_default;
struct IPP_EXPORT C_separator_sheets : public Collection {
  SingleValue<E_media> media{this, AttrName::media};
  SingleCollection<C_media_col> media_col{this, AttrName::media_col};
  SetOfValues<E_separator_sheets_type> separator_sheets_type{
      this, AttrName::separator_sheets_type};
  C_separator_sheets() : Collection(&defs_) {}
  std::vector<Attribute*> GetKnownAttributes() override;
  std::vector<const Attribute*> GetKnownAttributes() const override;
  static const std::map<AttrName, AttrDef> defs_;
};
typedef C_separator_sheets C_separator_sheets_actual;
typedef C_separator_sheets C_separator_sheets_default;
struct IPP_EXPORT C_cover_back : public Collection {
  SingleValue<E_cover_type> cover_type{this, AttrName::cover_type};
  SingleValue<E_media> media{this, AttrName::media};
  SingleCollection<C_media_col> media_col{this, AttrName::media_col};
  C_cover_back() : Collection(&defs_) {}
  std::vector<Attribute*> GetKnownAttributes() override;
  std::vector<const Attribute*> GetKnownAttributes() const override;
  static const std::map<AttrName, AttrDef> defs_;
};
typedef C_cover_back C_cover_back_actual;
typedef C_cover_back C_cover_back_default;
typedef C_cover_back C_cover_front;
typedef C_cover_back C_cover_front_actual;
typedef C_cover_back C_cover_front_default;
struct IPP_EXPORT C_document_format_details_default : public Collection {
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
typedef C_document_format_details_default C_document_format_details_supplied;
struct IPP_EXPORT C_finishings_col : public Collection {
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
    SingleValue<E_coating_sides> coating_sides{this, AttrName::coating_sides};
    SingleValue<E_coating_type> coating_type{this, AttrName::coating_type};
    C_coating() : Collection(&defs_) {}
    std::vector<Attribute*> GetKnownAttributes() override;
    std::vector<const Attribute*> GetKnownAttributes() const override;
    static const std::map<AttrName, AttrDef> defs_;
  };
  struct C_covering : public Collection {
    SingleValue<E_covering_name> covering_name{this, AttrName::covering_name};
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
    SingleValue<E_laminating_type> laminating_type{this,
                                                   AttrName::laminating_type};
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
    SetOfValues<int> stitching_locations{this, AttrName::stitching_locations};
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
    SingleValue<E_trimming_type> trimming_type{this, AttrName::trimming_type};
    SingleValue<E_trimming_when> trimming_when{this, AttrName::trimming_when};
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
  SingleValue<StringWithLanguage> media_size_name{this,
                                                  AttrName::media_size_name};
  SingleCollection<C_punching> punching{this, AttrName::punching};
  SingleCollection<C_stitching> stitching{this, AttrName::stitching};
  SetOfCollections<C_trimming> trimming{this, AttrName::trimming};
  C_finishings_col() : Collection(&defs_) {}
  std::vector<Attribute*> GetKnownAttributes() override;
  std::vector<const Attribute*> GetKnownAttributes() const override;
  static const std::map<AttrName, AttrDef> defs_;
};
typedef C_finishings_col C_finishings_col_actual;
typedef C_finishings_col C_finishings_col_database;
typedef C_finishings_col C_finishings_col_default;
typedef C_finishings_col C_finishings_col_ready;
struct IPP_EXPORT C_insert_sheet : public Collection {
  SingleValue<int> insert_after_page_number{this,
                                            AttrName::insert_after_page_number};
  SingleValue<int> insert_count{this, AttrName::insert_count};
  SingleValue<E_media> media{this, AttrName::media};
  SingleCollection<C_media_col> media_col{this, AttrName::media_col};
  C_insert_sheet() : Collection(&defs_) {}
  std::vector<Attribute*> GetKnownAttributes() override;
  std::vector<const Attribute*> GetKnownAttributes() const override;
  static const std::map<AttrName, AttrDef> defs_;
};
typedef C_insert_sheet C_insert_sheet_actual;
typedef C_insert_sheet C_insert_sheet_default;
struct IPP_EXPORT C_job_accounting_sheets : public Collection {
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
typedef C_job_accounting_sheets C_job_accounting_sheets_actual;
typedef C_job_accounting_sheets C_job_accounting_sheets_default;
typedef C_cover_back C_job_cover_back;
typedef C_cover_back C_job_cover_back_actual;
typedef C_cover_back C_job_cover_back_default;
typedef C_cover_back C_job_cover_front;
typedef C_cover_back C_job_cover_front_actual;
typedef C_cover_back C_job_cover_front_default;
struct IPP_EXPORT C_job_error_sheet : public Collection {
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
typedef C_job_error_sheet C_job_error_sheet_actual;
typedef C_job_error_sheet C_job_error_sheet_default;
typedef C_finishings_col C_job_finishings_col;
typedef C_finishings_col C_job_finishings_col_default;
typedef C_finishings_col C_job_finishings_col_ready;
struct IPP_EXPORT C_job_sheets_col : public Collection {
  SingleValue<E_job_sheets> job_sheets{this, AttrName::job_sheets};
  SingleValue<E_media> media{this, AttrName::media};
  SingleCollection<C_media_col> media_col{this, AttrName::media_col};
  C_job_sheets_col() : Collection(&defs_) {}
  std::vector<Attribute*> GetKnownAttributes() override;
  std::vector<const Attribute*> GetKnownAttributes() const override;
  static const std::map<AttrName, AttrDef> defs_;
};
typedef C_job_sheets_col C_job_sheets_col_actual;
typedef C_job_sheets_col C_job_sheets_col_default;
struct IPP_EXPORT C_overrides : public Collection {
  SingleValue<StringWithLanguage> job_account_id{this,
                                                 AttrName::job_account_id};
  SingleValue<E_job_account_type> job_account_type{this,
                                                   AttrName::job_account_type};
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
  SingleValue<E_job_error_action> job_error_action{this,
                                                   AttrName::job_error_action};
  SingleCollection<C_job_error_sheet> job_error_sheet{
      this, AttrName::job_error_sheet};
  SetOfValues<E_job_finishings> job_finishings{this, AttrName::job_finishings};
  SetOfCollections<C_job_finishings_col> job_finishings_col{
      this, AttrName::job_finishings_col};
  SingleValue<E_job_hold_until> job_hold_until{this, AttrName::job_hold_until};
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
  SetOfValues<int> pages_per_subset{this, AttrName::pages_per_subset};
  SingleValue<E_output_bin> output_bin{this, AttrName::output_bin};
  SingleValue<StringWithLanguage> output_device{this, AttrName::output_device};
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
  SingleValue<E_print_color_mode> print_color_mode{this,
                                                   AttrName::print_color_mode};
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
  SingleValue<E_x_image_position> x_image_position{this,
                                                   AttrName::x_image_position};
  SingleValue<int> x_image_shift{this, AttrName::x_image_shift};
  SingleValue<int> x_side1_image_shift{this, AttrName::x_side1_image_shift};
  SingleValue<int> x_side2_image_shift{this, AttrName::x_side2_image_shift};
  SingleValue<E_y_image_position> y_image_position{this,
                                                   AttrName::y_image_position};
  SingleValue<int> y_image_shift{this, AttrName::y_image_shift};
  SingleValue<int> copies{this, AttrName::copies};
  SingleCollection<C_cover_back> cover_back{this, AttrName::cover_back};
  SingleCollection<C_cover_front> cover_front{this, AttrName::cover_front};
  SingleValue<E_imposition_template> imposition_template{
      this, AttrName::imposition_template};
  SetOfCollections<C_insert_sheet> insert_sheet{this, AttrName::insert_sheet};
  SingleCollection<C_media_col> media_col{this, AttrName::media_col};
  SingleValue<E_media_input_tray_check> media_input_tray_check{
      this, AttrName::media_input_tray_check};
  SingleValue<E_print_scaling> print_scaling{this, AttrName::print_scaling};
  SingleCollection<C_proof_print> proof_print{this, AttrName::proof_print};
  SingleCollection<C_separator_sheets> separator_sheets{
      this, AttrName::separator_sheets};
  SingleValue<E_sheet_collate> sheet_collate{this, AttrName::sheet_collate};
  SingleValue<E_feed_orientation> feed_orientation{this,
                                                   AttrName::feed_orientation};
  SetOfValues<E_finishings> finishings{this, AttrName::finishings};
  SetOfCollections<C_finishings_col> finishings_col{this,
                                                    AttrName::finishings_col};
  SingleValue<StringWithLanguage> font_name_requested{
      this, AttrName::font_name_requested};
  SingleValue<int> font_size_requested{this, AttrName::font_size_requested};
  SetOfValues<int> force_front_side{this, AttrName::force_front_side};
  SetOfValues<RangeOfInteger> document_copies{this, AttrName::document_copies};
  SetOfValues<RangeOfInteger> document_numbers{this,
                                               AttrName::document_numbers};
  SetOfValues<RangeOfInteger> pages{this, AttrName::pages};
  C_overrides() : Collection(&defs_) {}
  std::vector<Attribute*> GetKnownAttributes() override;
  std::vector<const Attribute*> GetKnownAttributes() const override;
  static const std::map<AttrName, AttrDef> defs_;
};
typedef C_overrides C_overrides_actual;
}  // namespace ipp

#endif  //  LIBIPP_IPP_COLLECTIONS_H_
