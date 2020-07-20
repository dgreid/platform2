/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hardware_verifier/verifier_impl.h"

#include <map>
#include <set>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/optional.h>

namespace hardware_verifier {

namespace {

constexpr auto kGenericComponentName = "generic";
constexpr auto kNoMatchComponentName = "NO_MATCH";

void AddFoundComponentInfo(
    HwVerificationReport* hw_verification_report,
    const runtime_probe::ProbeRequest_SupportCategory& component_category,
    const std::string& comp_name,
    const QualificationStatus status) {
  auto* found_comp_info = hw_verification_report->add_found_component_infos();
  found_comp_info->set_component_category(component_category);
  found_comp_info->set_component_uuid(comp_name);
  found_comp_info->set_qualification_status(status);
  if (status != QualificationStatus::QUALIFIED) {
    hw_verification_report->set_is_compliant(false);
  }
}

}  // namespace

VerifierImpl::VerifierImpl() {
  using CppType = google::protobuf::FieldDescriptor::CppType;
  constexpr int kCppTypeMsg = CppType::CPPTYPE_MESSAGE;
  constexpr int kCppTypeStr = CppType::CPPTYPE_STRING;

  // Resolve |comp_category_infos_| in the constructor.
  const auto* category_enum_desc =
      runtime_probe::ProbeRequest_SupportCategory_descriptor();
  comp_category_infos_.resize(category_enum_desc->value_count());

  const auto* probe_result_desc = runtime_probe::ProbeResult::descriptor();
  const auto* generic_device_info_desc =
      HwVerificationReport_GenericDeviceInfo::descriptor();
  for (int i = 0; i < category_enum_desc->value_count(); ++i) {
    auto* comp_category_info = &comp_category_infos_[i];
    const auto& comp_category_name = category_enum_desc->value(i)->name();

    comp_category_info->enum_value = category_enum_desc->value(i)->number();
    comp_category_info->enum_name = comp_category_name;

    const auto* field_desc =
        probe_result_desc->FindFieldByName(comp_category_name);
    DCHECK(field_desc && field_desc->cpp_type() == kCppTypeMsg &&
           field_desc->is_repeated())
        << "Field (" << comp_category_name << ") must be a repeated field for "
        << "the HW components in |runtime_probe::ProbeResult|.";
    comp_category_info->probe_result_comp_field = field_desc;

    const auto* probe_result_comp_desc = field_desc->message_type();
    field_desc = probe_result_comp_desc->FindFieldByName("name");
    DCHECK(field_desc && field_desc->cpp_type() == kCppTypeStr &&
           field_desc->is_optional())
        << "Field (" << comp_category_name
        << ") should contain a string of the name of the component.";
    comp_category_info->probe_result_comp_name_field = field_desc;

    field_desc = probe_result_comp_desc->FindFieldByName("values");
    DCHECK(field_desc && field_desc->cpp_type() == kCppTypeMsg &&
           field_desc->is_optional())
        << "Field (" << comp_category_name
        << ") should contain a message field for the component values.";
    comp_category_info->probe_result_comp_values_field = field_desc;

    field_desc = generic_device_info_desc->FindFieldByName(comp_category_name);
    if (field_desc) {
      DCHECK(field_desc->cpp_type() == kCppTypeMsg && field_desc->is_repeated())
          << "|hardware_verifier::HwVerificationReport_GenericDeviceInfo| "
          << "should contain a repeated field for the generic ("
          << comp_category_name << ") components.";
    } else {
      VLOG(1) << "(" << comp_category_name << ") field is not found in "
              << "|hardware_verifier::HwVerificationReport_GenericDeviceInfo|, "
              << "will ignore the generic component of that category.";
    }
    comp_category_info->report_comp_values_field = field_desc;
  }
}

base::Optional<HwVerificationReport> VerifierImpl::Verify(
    const runtime_probe::ProbeResult& probe_result,
    const HwVerificationSpec& hw_verification_spec) const {
  // A dictionary of 'expected_component_category => seen'.
  std::map<int, bool> seen_comp;
  // Collect the categories of generic components we found.
  std::set<int> seen_generic_comp;

  // A dictionary which maps (component_category, component_uuid) to its
  // qualification status.
  std::map<int, std::map<std::string, QualificationStatus>> qual_status_dict;
  for (const auto& comp_info : hw_verification_spec.component_infos()) {
    const auto& category = comp_info.component_category();
    const auto& uuid = comp_info.component_uuid();
    const auto& qualification_status = comp_info.qualification_status();
    const auto& insert_result =
        qual_status_dict[static_cast<int>(category)].emplace(
            uuid, qualification_status);
    if (!insert_result.second) {
      LOG(ERROR)
          << "The verification spec contains duplicated component infos.";
      return base::nullopt;
    }

    // We expect to see this component in probe result.
    seen_comp[category] = false;
  }

  // A dictionary which maps component_category to the field names in the
  // allowlist.
  std::map<int, std::set<std::string>> generic_comp_value_allowlists;
  for (const auto& spec_info :
       hw_verification_spec.generic_component_value_allowlists()) {
    const auto& insert_result = generic_comp_value_allowlists.emplace(
        spec_info.component_category(),
        std::set<std::string>(spec_info.field_names().cbegin(),
                              spec_info.field_names().cend()));
    if (!insert_result.second) {
      LOG(ERROR) << "Duplicated allowlist tables for category (num="
                 << spec_info.component_category() << ") are detected in the "
                 << "verification spec.";
      return base::nullopt;
    }
  }

  HwVerificationReport hw_verification_report;
  hw_verification_report.set_is_compliant(true);
  auto* generic_device_info =
      hw_verification_report.mutable_generic_device_info();
  const auto* generic_device_info_refl = generic_device_info->GetReflection();

  const auto* probe_result_refl = probe_result.GetReflection();
  for (const auto& comp_category_info : comp_category_infos_) {
    const auto& comp_name_to_qual_status =
        qual_status_dict[comp_category_info.enum_value];

    // the default allowlist is empty.
    const auto& generic_comp_value_allowlist =
        generic_comp_value_allowlists[comp_category_info.enum_value];

    const auto& num_comps = probe_result_refl->FieldSize(
        probe_result, comp_category_info.probe_result_comp_field);
    for (int i = 0; i < num_comps; ++i) {
      const auto& comp = probe_result_refl->GetRepeatedMessage(
          probe_result, comp_category_info.probe_result_comp_field, i);
      const auto* comp_refl = comp.GetReflection();
      const auto& comp_name = comp_refl->GetString(
          comp, comp_category_info.probe_result_comp_name_field);

      // If the component name is "generic", add it to |generic_device_info|
      // in the report.
      if (comp_name == kGenericComponentName) {
        seen_generic_comp.insert(comp_category_info.enum_value);
        if (!comp_category_info.report_comp_values_field) {
          VLOG(1) << "Ignore the generic component of ("
                  << comp_category_info.enum_name << ") category.";
        } else {
          // Duplicate the original values and filter the fields by the
          // allowlist.
          auto* dup_comp_values = generic_device_info_refl->AddMessage(
              generic_device_info, comp_category_info.report_comp_values_field);
          dup_comp_values->CopyFrom(comp_refl->GetMessage(
              comp, comp_category_info.probe_result_comp_values_field));

          const auto* dup_comp_values_refl = dup_comp_values->GetReflection();
          const auto* dup_comp_values_desc = dup_comp_values->GetDescriptor();
          for (int j = 0; j < dup_comp_values_desc->field_count(); ++j) {
            const auto* field = dup_comp_values_desc->field(j);
            if (!generic_comp_value_allowlist.count(field->name())) {
              dup_comp_values_refl->ClearField(dup_comp_values, field);
            }
          }
        }
        continue;
      }

      // If the component name is not "generic", do the regular qualification
      // status check.
      const auto& qual_status_it = comp_name_to_qual_status.find(comp_name);
      if (qual_status_it == comp_name_to_qual_status.end()) {
        LOG(ERROR) << "The probe result contains unregonizable components "
                   << "(category=" << comp_category_info.enum_name
                   << ", uuid=" << comp_name << ").";
        return base::nullopt;
      }
      // TODO(b147654337): How about components that are "missing", that is:
      //   - It is expected on the system (according to SKU or MODEL).
      //   - We cannot find this in generic nor non-generic components.
      AddFoundComponentInfo(
          &hw_verification_report,
          static_cast<runtime_probe::ProbeRequest_SupportCategory>(
              comp_category_info.enum_value),
          comp_name, qual_status_it->second);
      seen_comp[comp_category_info.enum_value] = true;
    }
  }

  for (const auto& it : seen_comp) {
    // We have found a generic component in this category, but this doesn't have
    // any qualification status.
    if (!it.second && seen_generic_comp.count(it.first)) {
      AddFoundComponentInfo(
          &hw_verification_report,
          static_cast<runtime_probe::ProbeRequest_SupportCategory>(it.first),
          kNoMatchComponentName, QualificationStatus::NO_MATCH);
    }
  }

  // TODO(yhong): Implement the SKU specific checks.
  return hw_verification_report;
}

}  // namespace hardware_verifier
