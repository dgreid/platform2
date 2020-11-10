// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_set>

#include <base/strings/string_util.h>
#include <components/policy/core/common/registry_dict.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "authpolicy/policy/device_policy_encoder.h"
#include "authpolicy/policy/policy_encoder_test_base.h"
#include "bindings/chrome_device_policy.pb.h"
#include "bindings/policy_constants.h"

namespace em = enterprise_management;

namespace policy {

namespace {

// Converts a repeated string field to a vector.
std::vector<std::string> ToVector(
    const google::protobuf::RepeatedPtrField<std::string>& repeated_field) {
  return std::vector<std::string>(repeated_field.begin(), repeated_field.end());
}

// Converts a repeated int field to a vector.
std::vector<int> ToVector(
    const google::protobuf::RepeatedField<int>& repeated_field) {
  return std::vector<int>(repeated_field.begin(), repeated_field.end());
}

}  // namespace

// Checks whether all device policies are properly encoded from RegistryDict
// into em::ChromeDeviceSettingsProto. Makes sure no device policy is missing.
class DevicePolicyEncoderTest
    : public PolicyEncoderTestBase<em::ChromeDeviceSettingsProto> {
 public:
  DevicePolicyEncoderTest() {}
  DevicePolicyEncoderTest(const DevicePolicyEncoderTest&) = delete;
  DevicePolicyEncoderTest& operator=(const DevicePolicyEncoderTest&) = delete;
  ~DevicePolicyEncoderTest() override {}

 protected:
  void EncodeDict(em::ChromeDeviceSettingsProto* policy,
                  const RegistryDict* dict) override {
    DevicePolicyEncoder encoder(dict, POLICY_LEVEL_MANDATORY);
    *policy = em::ChromeDeviceSettingsProto();
    encoder.EncodePolicy(policy);
  }

  void MarkHandled(const char* key) override {
    handled_policy_keys_.insert(key);
  }

  // Returns a vector of all policy keys that were not encoded.
  std::vector<std::string> GetUnhandledPolicyKeys() const {
    std::vector<std::string> unhandled_policy_keys;
    for (const char** key = kDevicePolicyKeys; *key; ++key) {
      if (handled_policy_keys_.find(*key) == handled_policy_keys_.end())
        unhandled_policy_keys.push_back(*key);
    }
    return unhandled_policy_keys;
  }

 private:
  // Keeps track of handled device policies. Used to detect device policies that
  // device_policy_encoder forgets to encode.
  std::unordered_set<std::string> handled_policy_keys_;
};

TEST_F(DevicePolicyEncoderTest, TestEncoding) {
  // Note that kStringList can't be constexpr, so we put them all here.
  constexpr bool kBool = true;
  constexpr int kInt = 123;
  constexpr int kScreenMagnifierTypeInRangeInt = 1;
  constexpr int kScreenMagnifierTypeOutOfRangeInt = 10;
  constexpr int kDeviceChromeVariationsInRangeInt = 1;
  constexpr int kDeviceChromeVariationsOutOfRangeInt = 12;
  constexpr int kDeviceCrostiniArcAdbSideloadingAllowedOutOfRangeInt = 13;
  const std::string kString = "val1";
  const std::vector<std::string> kStringList = {"val1", "val2", "val3"};

  em::ChromeDeviceSettingsProto policy;

  //
  // Login policies.
  //

  EncodeBoolean(&policy, key::kDeviceGuestModeEnabled, kBool);
  EXPECT_EQ(kBool, policy.guest_mode_enabled().guest_mode_enabled());

  EncodeBoolean(&policy, key::kDeviceRebootOnShutdown, kBool);
  EXPECT_EQ(kBool, policy.reboot_on_shutdown().reboot_on_shutdown());

  EncodeBoolean(&policy, key::kDeviceShowUserNamesOnSignin, kBool);
  EXPECT_EQ(kBool, policy.show_user_names().show_user_names());

  EncodeBoolean(&policy, key::kDeviceAllowNewUsers, kBool);
  EXPECT_EQ(kBool, policy.allow_new_users().allow_new_users());

  EncodeStringList(&policy, key::kDeviceUserWhitelist, kStringList);
  EXPECT_EQ(kStringList, ToVector(policy.user_whitelist().user_whitelist()));
  // Old policy copied to new name.
  EXPECT_EQ(kStringList, ToVector(policy.user_allowlist().user_allowlist()));

  EncodeStringList(&policy, key::kDeviceUserAllowlist, kStringList);
  EXPECT_EQ(kStringList, ToVector(policy.user_allowlist().user_allowlist()));

  EncodeBoolean(&policy, key::kDeviceEphemeralUsersEnabled, kBool);
  EXPECT_EQ(kBool, policy.ephemeral_users_enabled().ephemeral_users_enabled());

  EncodeBoolean(&policy, key::kDeviceAllowBluetooth, kBool);
  EXPECT_EQ(kBool, policy.allow_bluetooth().allow_bluetooth());

  EncodeStringList(&policy, key::kDeviceLoginScreenExtensions, kStringList);
  EXPECT_EQ(kStringList, ToVector(policy.device_login_screen_extensions()
                                      .device_login_screen_extensions()));

  EncodeString(&policy, key::kDeviceLoginScreenDomainAutoComplete, kString);
  EXPECT_EQ(kString, policy.login_screen_domain_auto_complete()
                         .login_screen_domain_auto_complete());

  EncodeStringList(&policy, key::kDeviceLoginScreenLocales, kStringList);
  EXPECT_EQ(kStringList,
            ToVector(policy.login_screen_locales().login_screen_locales()));

  EncodeStringList(&policy, key::kDeviceLoginScreenInputMethods, kStringList);
  EXPECT_EQ(
      kStringList,
      ToVector(
          policy.login_screen_input_methods().login_screen_input_methods()));

  EncodeStringList(&policy, key::kDeviceLoginScreenAutoSelectCertificateForUrls,
                   kStringList);
  EXPECT_EQ(
      kStringList,
      ToVector(policy.device_login_screen_auto_select_certificate_for_urls()
                   .login_screen_auto_select_certificate_rules()));

  EncodeInteger(&policy, key::kDeviceRebootOnUserSignout,
                em::DeviceRebootOnUserSignoutProto_RebootOnSignoutMode_ALWAYS);
  EXPECT_EQ(em::DeviceRebootOnUserSignoutProto_RebootOnSignoutMode_ALWAYS,
            policy.device_reboot_on_user_signout().reboot_on_signout_mode());

  EncodeBoolean(&policy, key::kDevicePowerwashAllowed, kBool);
  EXPECT_EQ(kBool,
            policy.device_powerwash_allowed().device_powerwash_allowed());

  EncodeBoolean(&policy, key::kManagedGuestSessionPrivacyWarningsEnabled,
                kBool);
  EXPECT_EQ(kBool, policy.managed_guest_session_privacy_warnings().enabled());

  //
  // Network policies.
  //

  EncodeBoolean(&policy, key::kDeviceDataRoamingEnabled, kBool);
  EXPECT_EQ(kBool, policy.data_roaming_enabled().data_roaming_enabled());

  EncodeBoolean(&policy, key::kDeviceWiFiFastTransitionEnabled, kBool);
  EXPECT_EQ(kBool, policy.device_wifi_fast_transition_enabled()
                       .device_wifi_fast_transition_enabled());

  EncodeString(&policy, key::kDeviceOpenNetworkConfiguration, kString);
  EXPECT_EQ(kString,
            policy.open_network_configuration().open_network_configuration());

  EncodeString(&policy, key::kDeviceHostnameTemplate, kString);
  EXPECT_EQ(kString, policy.network_hostname().device_hostname_template());

  // The encoder of this policy converts ints to
  // DeviceKerberosEncryptionTypes::Types enums.
  EncodeInteger(&policy, key::kDeviceKerberosEncryptionTypes,
                em::DeviceKerberosEncryptionTypesProto::ENC_TYPES_ALL);
  EXPECT_EQ(em::DeviceKerberosEncryptionTypesProto::ENC_TYPES_ALL,
            policy.device_kerberos_encryption_types().types());

  //
  // Auto update policies.
  //

  EncodeString(&policy, key::kChromeOsReleaseChannel, kString);
  EXPECT_EQ(kString, policy.release_channel().release_channel());

  EncodeBoolean(&policy, key::kChromeOsReleaseChannelDelegated, kBool);
  EXPECT_EQ(kBool, policy.release_channel().release_channel_delegated());

  EncodeString(&policy, key::kDeviceReleaseLtsTag, kString);
  EXPECT_EQ(kString, policy.release_channel().release_lts_tag());

  EncodeBoolean(&policy, key::kDeviceAutoUpdateDisabled, kBool);
  EXPECT_EQ(kBool, policy.auto_update_settings().update_disabled());

  EncodeString(&policy, key::kDeviceTargetVersionPrefix, kString);
  EXPECT_EQ(kString, policy.auto_update_settings().target_version_prefix());

  EncodeString(&policy, key::kDeviceQuickFixBuildToken, kString);
  EXPECT_EQ(kString,
            policy.auto_update_settings().device_quick_fix_build_token());

  // The encoder of this policy converts ints to RollbackToTargetVersion enums.
  EncodeInteger(&policy, key::kDeviceRollbackToTargetVersion,
                em::AutoUpdateSettingsProto::ROLLBACK_AND_POWERWASH);
  EXPECT_EQ(em::AutoUpdateSettingsProto::ROLLBACK_AND_POWERWASH,
            policy.auto_update_settings().rollback_to_target_version());

  EncodeInteger(&policy, key::kDeviceRollbackAllowedMilestones, kInt);
  EXPECT_EQ(kInt, policy.auto_update_settings().rollback_allowed_milestones());

  EncodeInteger(&policy, key::kDeviceUpdateScatterFactor, kInt);
  EXPECT_EQ(kInt, policy.auto_update_settings().scatter_factor_in_seconds());

  // The encoder of this policy converts connection type strings to enums.
  std::vector<std::string> str_types;
  std::vector<int> enum_types;
  for (size_t n = 0; n < kConnectionTypesSize; ++n) {
    str_types.push_back(kConnectionTypes[n].first);
    enum_types.push_back(kConnectionTypes[n].second);
  }
  EncodeStringList(&policy, key::kDeviceUpdateAllowedConnectionTypes,
                   str_types);
  EXPECT_EQ(enum_types,
            ToVector(policy.auto_update_settings().allowed_connection_types()));

  EncodeBoolean(&policy, key::kDeviceUpdateHttpDownloadsEnabled, kBool);
  EXPECT_EQ(kBool, policy.auto_update_settings().http_downloads_enabled());

  EncodeBoolean(&policy, key::kRebootAfterUpdate, kBool);
  EXPECT_EQ(kBool, policy.auto_update_settings().reboot_after_update());

  EncodeBoolean(&policy, key::kDeviceAutoUpdateP2PEnabled, kBool);
  EXPECT_EQ(kBool, policy.auto_update_settings().p2p_enabled());

  EncodeString(&policy, key::kDeviceAutoUpdateTimeRestrictions, kString);
  EXPECT_EQ(kString, policy.auto_update_settings().disallowed_time_intervals());

  EncodeString(&policy, key::kDeviceUpdateStagingSchedule, kString);
  EXPECT_EQ(kString, policy.auto_update_settings().staging_schedule());

  EncodeString(&policy, key::kDeviceLoginScreenWebUsbAllowDevicesForUrls,
               kString);
  EXPECT_EQ(kString, policy.device_login_screen_webusb_allow_devices_for_urls()
                         .device_login_screen_webusb_allow_devices_for_urls());

  EncodeInteger(&policy, key::kDeviceChannelDowngradeBehavior,
                em::AutoUpdateSettingsProto::ROLLBACK);
  EXPECT_EQ(em::AutoUpdateSettingsProto::ROLLBACK,
            policy.auto_update_settings().channel_downgrade_behavior());

  //
  // Accessibility policies.
  //

  EncodeBoolean(&policy, key::kDeviceLoginScreenDefaultLargeCursorEnabled,
                kBool);
  EXPECT_EQ(kBool, policy.accessibility_settings()
                       .login_screen_default_large_cursor_enabled());

  EncodeBoolean(&policy, key::kDeviceLoginScreenLargeCursorEnabled, kBool);
  EXPECT_EQ(
      kBool,
      policy.accessibility_settings().login_screen_large_cursor_enabled());

  EncodeBoolean(&policy, key::kDeviceLoginScreenAutoclickEnabled, kBool);
  EXPECT_EQ(kBool,
            policy.accessibility_settings().login_screen_autoclick_enabled());

  EncodeBoolean(&policy, key::kDeviceLoginScreenCaretHighlightEnabled, kBool);
  EXPECT_EQ(
      kBool,
      policy.accessibility_settings().login_screen_caret_highlight_enabled());

  EncodeBoolean(&policy, key::kDeviceLoginScreenCursorHighlightEnabled, kBool);
  EXPECT_EQ(
      kBool,
      policy.accessibility_settings().login_screen_cursor_highlight_enabled());

  EncodeBoolean(&policy, key::kDeviceLoginScreenDictationEnabled, kBool);
  EXPECT_EQ(kBool,
            policy.accessibility_settings().login_screen_dictation_enabled());

  EncodeBoolean(&policy, key::kDeviceLoginScreenHighContrastEnabled, kBool);
  EXPECT_EQ(
      kBool,
      policy.accessibility_settings().login_screen_high_contrast_enabled());

  EncodeBoolean(&policy, key::kDeviceLoginScreenMonoAudioEnabled, kBool);
  EXPECT_EQ(kBool,
            policy.accessibility_settings().login_screen_mono_audio_enabled());

  EncodeBoolean(&policy, key::kDeviceLoginScreenSelectToSpeakEnabled, kBool);
  EXPECT_EQ(
      kBool,
      policy.accessibility_settings().login_screen_select_to_speak_enabled());

  EncodeBoolean(&policy, key::kDeviceLoginScreenSpokenFeedbackEnabled, kBool);
  EXPECT_EQ(
      kBool,
      policy.accessibility_settings().login_screen_spoken_feedback_enabled());

  EncodeBoolean(&policy, key::kDeviceLoginScreenStickyKeysEnabled, kBool);
  EXPECT_EQ(kBool,
            policy.accessibility_settings().login_screen_sticky_keys_enabled());

  EncodeBoolean(&policy, key::kDeviceLoginScreenVirtualKeyboardEnabled, kBool);
  EXPECT_EQ(
      kBool,
      policy.accessibility_settings().login_screen_virtual_keyboard_enabled());

  EncodeInteger(&policy, key::kDeviceLoginScreenScreenMagnifierType,
                kScreenMagnifierTypeOutOfRangeInt);
  EXPECT_FALSE(
      policy.accessibility_settings().has_login_screen_screen_magnifier_type());

  EncodeInteger(&policy, key::kDeviceLoginScreenScreenMagnifierType,
                kScreenMagnifierTypeInRangeInt);
  EXPECT_EQ(
      kScreenMagnifierTypeInRangeInt,
      policy.accessibility_settings().login_screen_screen_magnifier_type());

  EncodeBoolean(&policy, key::kDeviceLoginScreenDefaultSpokenFeedbackEnabled,
                kBool);
  EXPECT_EQ(kBool, policy.accessibility_settings()
                       .login_screen_default_spoken_feedback_enabled());

  EncodeBoolean(&policy, key::kDeviceLoginScreenDefaultHighContrastEnabled,
                kBool);
  EXPECT_EQ(kBool, policy.accessibility_settings()
                       .login_screen_default_high_contrast_enabled());

  EncodeInteger(&policy, key::kDeviceChromeVariations,
                kDeviceChromeVariationsOutOfRangeInt);
  EXPECT_FALSE(policy.has_device_chrome_variations_type());

  EncodeInteger(&policy, key::kDeviceChromeVariations,
                kDeviceChromeVariationsInRangeInt);
  EXPECT_EQ(kDeviceChromeVariationsInRangeInt,
            policy.device_chrome_variations_type().value());

  EncodeBoolean(&policy, key::kDeviceLoginScreenPrivacyScreenEnabled, kBool);
  EXPECT_EQ(kBool,
            policy.device_login_screen_privacy_screen_enabled().enabled());

  EncodeBoolean(&policy, key::kDeviceShowNumericKeyboardForPassword, kBool);
  EXPECT_EQ(kBool, policy.device_show_numeric_keyboard_for_password().value());

  EncodeStringList(&policy, key::kDeviceWebBasedAttestationAllowedUrls,
                   kStringList);
  EXPECT_EQ(kStringList,
            ToVector(policy.device_web_based_attestation_allowed_urls()
                         .value()
                         .entries()));

  // The encoder of this policy converts ints to ScreenMagnifierType enums.
  EncodeInteger(&policy, key::kDeviceLoginScreenDefaultScreenMagnifierType,
                em::AccessibilitySettingsProto::SCREEN_MAGNIFIER_TYPE_FULL);
  EXPECT_EQ(em::AccessibilitySettingsProto::SCREEN_MAGNIFIER_TYPE_FULL,
            policy.accessibility_settings()
                .login_screen_default_screen_magnifier_type());

  EncodeBoolean(&policy, key::kDeviceLoginScreenDefaultVirtualKeyboardEnabled,
                kBool);
  EXPECT_EQ(kBool, policy.accessibility_settings()
                       .login_screen_default_virtual_keyboard_enabled());

  //
  // Generic policies.
  //

  EncodeInteger(&policy, key::kDevicePolicyRefreshRate, kInt);
  EXPECT_EQ(kInt,
            policy.device_policy_refresh_rate().device_policy_refresh_rate());

  EncodeBoolean(&policy, key::kDeviceMetricsReportingEnabled, kBool);
  EXPECT_EQ(kBool, policy.metrics_enabled().metrics_enabled());

  EncodeString(&policy, key::kSystemTimezone, kString);
  EXPECT_EQ(kString, policy.system_timezone().timezone());

  EncodeString(&policy, key::kRequiredClientCertificateForDevice, kString);
  EXPECT_EQ(policy.required_client_certificate_for_device()
                .required_client_certificate_for_device(),
            kString);

  EncodeString(&policy, key::kSystemProxySettings, kString);
  EXPECT_FALSE(policy.has_system_proxy_settings());

  // The encoder of this policy converts ints to
  // DeviceCrostiniArcAdbSideloadingAllowedProto::AllowanceMode enums.
  EncodeInteger(&policy, key::kDeviceCrostiniArcAdbSideloadingAllowed,
                em::DeviceCrostiniArcAdbSideloadingAllowedProto::DISALLOW);
  EXPECT_EQ(em::DeviceCrostiniArcAdbSideloadingAllowedProto::DISALLOW,
            policy.device_crostini_arc_adb_sideloading_allowed().mode());

  EncodeInteger(&policy, key::kDeviceCrostiniArcAdbSideloadingAllowed,
                kDeviceCrostiniArcAdbSideloadingAllowedOutOfRangeInt);
  EXPECT_FALSE(policy.has_device_crostini_arc_adb_sideloading_allowed());

  // TODO(crbug.com/1092593) The following policy is going to be supported for
  // chrome_os, but its not now. However, it needs to be encoded temporarily to
  // pass the tests.
  MarkHandled(key::kDeviceSamlLoginAuthenticationType);

  EncodeString(&policy, key::kSystemProxySettings,
               R"!!!(
               {
                 "system_proxy_username": "test_user",
                 "system_services_password": "1234",
                 "system_proxy_enabled": true,
               })!!!");
  EXPECT_TRUE(policy.has_system_proxy_settings());

  EncodeString(&policy, key::kDeviceMinimumVersion, kString);
  EXPECT_EQ(policy.device_minimum_version().value(), kString);

  EncodeString(&policy, key::kDeviceMinimumVersionAueMessage, kString);
  EXPECT_EQ(policy.device_minimum_version_aue_message().value(), kString);

  // The encoder of this policy converts ints to AutomaticTimezoneDetectionType
  // enums.
  EncodeInteger(&policy, key::kSystemTimezoneAutomaticDetection,
                em::SystemTimezoneProto::IP_ONLY);
  EXPECT_EQ(em::SystemTimezoneProto::IP_ONLY,
            policy.system_timezone().timezone_detection_type());

  EncodeBoolean(&policy, key::kSystemUse24HourClock, kBool);
  EXPECT_EQ(kBool, policy.use_24hour_clock().use_24hour_clock());

  EncodeBoolean(&policy, key::kDeviceAllowRedeemChromeOsRegistrationOffers,
                kBool);
  EXPECT_EQ(kBool, policy.allow_redeem_offers().allow_redeem_offers());

  EncodeString(&policy, key::kDeviceVariationsRestrictParameter, kString);
  EXPECT_EQ(kString, policy.variations_parameter().parameter());

  EncodeString(&policy, key::kDeviceLoginScreenPowerManagement, kString);
  EXPECT_EQ(
      kString,
      policy.login_screen_power_management().login_screen_power_management());

  // The encoder of this policy converts ints to Rotation enums.
  EncodeInteger(&policy, key::kDisplayRotationDefault,
                em::DisplayRotationDefaultProto::ROTATE_180);
  EXPECT_EQ(em::DisplayRotationDefaultProto::ROTATE_180,
            policy.display_rotation_default().display_rotation_default());

  EncodeString(&policy, key::kDeviceDisplayResolution, kString);
  EXPECT_EQ(kString,
            policy.device_display_resolution().device_display_resolution());

  // The encoder of this policy converts a JSON string to separate values.
  EncodeStringList(&policy, key::kUsbDetachableWhitelist,
                   {"{\"vendor_id\":123, \"product_id\":234}",
                    "{\"vendor_id\":345, \"product_id\":456}"});
  const auto& whitelist_proto = policy.usb_detachable_whitelist();
  const auto& copied_allowlist_proto = policy.usb_detachable_allowlist();
  EXPECT_EQ(123, whitelist_proto.id().Get(0).vendor_id());
  EXPECT_EQ(234, whitelist_proto.id().Get(0).product_id());
  EXPECT_EQ(345, whitelist_proto.id().Get(1).vendor_id());
  EXPECT_EQ(456, whitelist_proto.id().Get(1).product_id());
  // Whitelist values should have been copied to the allowlist proto
  EXPECT_EQ(123, copied_allowlist_proto.id().Get(0).vendor_id());
  EXPECT_EQ(234, copied_allowlist_proto.id().Get(0).product_id());
  EXPECT_EQ(345, copied_allowlist_proto.id().Get(1).vendor_id());
  EXPECT_EQ(456, copied_allowlist_proto.id().Get(1).product_id());

  EncodeStringList(&policy, key::kUsbDetachableAllowlist,
                   {"{\"vendor_id\":1234, \"product_id\":2345}",
                    "{\"vendor_id\":3456, \"product_id\":4567}"});
  const auto& allowlist_proto = policy.usb_detachable_allowlist();
  EXPECT_EQ(1234, allowlist_proto.id().Get(0).vendor_id());
  EXPECT_EQ(2345, allowlist_proto.id().Get(0).product_id());
  EXPECT_EQ(3456, allowlist_proto.id().Get(1).vendor_id());
  EXPECT_EQ(4567, allowlist_proto.id().Get(1).product_id());
  EXPECT_FALSE(policy.has_usb_detachable_whitelist());

  EncodeBoolean(&policy, key::kDeviceQuirksDownloadEnabled, kBool);
  EXPECT_EQ(kBool, policy.quirks_download_enabled().quirks_download_enabled());

  EncodeString(&policy, key::kDeviceWallpaperImage, kString);
  EXPECT_EQ(kString, policy.device_wallpaper_image().device_wallpaper_image());

  EncodeString(&policy, key::kDeviceOffHours,
               R"!!!(
               {
                 "intervals":
                 [
                   {
                     "start": {
                       "day_of_week": "MONDAY",
                       "time": 12840000
                     },
                     "end": {
                       "day_of_week": "MONDAY",
                       "time": 21720000
                     }
                   },
                   {
                     "start": {
                       "day_of_week": "FRIDAY",
                       "time": 38640000
                     },
                     "end": {
                       "day_of_week": "FRIDAY",
                       "time": 57600000
                     }
                   }
                 ],
                 "timezone": "GMT",
                 "ignored_policy_proto_tags": [3, 8]
               })!!!");
  const auto& device_off_hours_proto = policy.device_off_hours();
  EXPECT_EQ(2, device_off_hours_proto.intervals_size());
  {
    const auto& interval1 = device_off_hours_proto.intervals().Get(0);
    const auto& interval2 = device_off_hours_proto.intervals().Get(1);
    EXPECT_EQ(em::WeeklyTimeProto::MONDAY, interval1.start().day_of_week());
    EXPECT_EQ(em::WeeklyTimeProto::MONDAY, interval1.end().day_of_week());
    EXPECT_EQ(12840000, interval1.start().time());
    EXPECT_EQ(21720000, interval1.end().time());
    EXPECT_EQ(em::WeeklyTimeProto::FRIDAY, interval2.start().day_of_week());
    EXPECT_EQ(em::WeeklyTimeProto::FRIDAY, interval2.end().day_of_week());
    EXPECT_EQ(38640000, interval2.start().time());
    EXPECT_EQ(57600000, interval2.end().time());
  }
  EXPECT_EQ("GMT", device_off_hours_proto.timezone());
  EXPECT_EQ(2, device_off_hours_proto.ignored_policy_proto_tags_size());
  EXPECT_EQ(3, device_off_hours_proto.ignored_policy_proto_tags().Get(0));
  EXPECT_EQ(8, device_off_hours_proto.ignored_policy_proto_tags().Get(1));

  EncodeString(&policy, key::kCastReceiverName, kString);
  EXPECT_EQ(kString, policy.cast_receiver_name().name());

  EncodeString(&policy, key::kDevicePrinters, kString);
  EXPECT_EQ(kString, policy.device_printers().external_policy());

  // Old policy copied to new name.
  EncodeString(&policy, key::kDeviceNativePrinters, kString);
  EXPECT_EQ(kString, policy.native_device_printers().external_policy());
  // Old policy copied to new name.
  EXPECT_EQ(kString, policy.device_printers().external_policy());

  // The encoder of this policy converts ints to AccessMode enums.
  EncodeInteger(&policy, key::kDevicePrintersAccessMode,
                em::DevicePrintersAccessModeProto::ACCESS_MODE_ALLOWLIST);
  EXPECT_EQ(em::DevicePrintersAccessModeProto::ACCESS_MODE_ALLOWLIST,
            policy.device_printers_access_mode().access_mode());

  // Old policy copied to new name.
  EncodeInteger(&policy, key::kDeviceNativePrintersAccessMode,
                em::DeviceNativePrintersAccessModeProto::ACCESS_MODE_WHITELIST);
  EXPECT_EQ(em::DeviceNativePrintersAccessModeProto::ACCESS_MODE_WHITELIST,
            policy.native_device_printers_access_mode().access_mode());
  // Old policy copied to new name.
  EXPECT_EQ(em::DevicePrintersAccessModeProto::ACCESS_MODE_ALLOWLIST,
            policy.device_printers_access_mode().access_mode());

  EncodeStringList(&policy, key::kDevicePrintersAllowlist, kStringList);
  EXPECT_EQ(kStringList,
            ToVector(policy.device_printers_allowlist().allowlist()));

  EncodeStringList(&policy, key::kDeviceNativePrintersWhitelist, kStringList);
  EXPECT_EQ(kStringList,
            ToVector(policy.native_device_printers_whitelist().whitelist()));
  // Old policy copied to new name.
  EXPECT_EQ(kStringList,
            ToVector(policy.device_printers_allowlist().allowlist()));

  EncodeStringList(&policy, key::kDevicePrintersBlocklist, kStringList);
  EXPECT_EQ(kStringList,
            ToVector(policy.device_printers_blocklist().blocklist()));

  EncodeStringList(&policy, key::kDeviceNativePrintersBlacklist, kStringList);
  EXPECT_EQ(kStringList,
            ToVector(policy.native_device_printers_blacklist().blacklist()));
  // Old policy copied to new name.
  EXPECT_EQ(kStringList,
            ToVector(policy.device_printers_blocklist().blocklist()));

  EncodeString(&policy, key::kDeviceExternalPrintServers, kString);
  EXPECT_EQ(kString, policy.external_print_servers().external_policy());

  EncodeStringList(&policy, key::kDeviceExternalPrintServersAllowlist,
                   kStringList);
  EXPECT_EQ(kStringList,
            ToVector(policy.external_print_servers_allowlist().allowlist()));

  EncodeString(&policy, key::kTPMFirmwareUpdateSettings,
               "{\"allow-user-initiated-powerwash\":true,"
               " \"allow-user-initiated-preserve-device-state\":true}");
  EXPECT_EQ(
      true,
      policy.tpm_firmware_update_settings().allow_user_initiated_powerwash());
  EXPECT_EQ(true, policy.tpm_firmware_update_settings()
                      .allow_user_initiated_preserve_device_state());

  EncodeBoolean(&policy, key::kUnaffiliatedArcAllowed, kBool);
  EXPECT_EQ(kBool,
            policy.unaffiliated_arc_allowed().unaffiliated_arc_allowed());

  EncodeBoolean(&policy, key::kPluginVmAllowed, kBool);
  EXPECT_EQ(kBool, policy.plugin_vm_allowed().plugin_vm_allowed());
  EncodeString(&policy, key::kPluginVmLicenseKey, kString);
  EXPECT_EQ(kString, policy.plugin_vm_license_key().plugin_vm_license_key());

  EncodeBoolean(&policy, key::kDeviceWilcoDtcAllowed, kBool);
  EXPECT_EQ(kBool,
            policy.device_wilco_dtc_allowed().device_wilco_dtc_allowed());

  EncodeBoolean(&policy, key::kDeviceBootOnAcEnabled, kBool);
  EXPECT_EQ(kBool, policy.device_boot_on_ac().enabled());

  EncodeInteger(&policy, key::kDevicePowerPeakShiftBatteryThreshold, kInt);
  EXPECT_EQ(kInt, policy.device_power_peak_shift().battery_threshold());
  EncodeBoolean(&policy, key::kDevicePowerPeakShiftEnabled, kBool);
  EXPECT_EQ(kBool, policy.device_power_peak_shift().enabled());
  EncodeString(&policy, key::kDevicePowerPeakShiftDayConfig, kString);
  EXPECT_EQ(kString, policy.device_power_peak_shift().day_configs());

  EncodeBoolean(&policy, key::kDeviceWiFiAllowed, kBool);
  EXPECT_EQ(kBool, policy.device_wifi_allowed().device_wifi_allowed());

  EncodeString(&policy, key::kDeviceWilcoDtcConfiguration, kString);
  EXPECT_EQ(
      kString,
      policy.device_wilco_dtc_configuration().device_wilco_dtc_configuration());

  // The encoder of this policy converts ints to
  // DeviceDockMacAddressSourceProto::Source enums.
  EncodeInteger(&policy, key::kDeviceDockMacAddressSource,
                em::DeviceDockMacAddressSourceProto::DOCK_NIC_MAC_ADDRESS);
  EXPECT_EQ(em::DeviceDockMacAddressSourceProto::DOCK_NIC_MAC_ADDRESS,
            policy.device_dock_mac_address_source().source());

  EncodeBoolean(&policy, key::kDeviceAdvancedBatteryChargeModeEnabled, kBool);
  EXPECT_EQ(kBool, policy.device_advanced_battery_charge_mode().enabled());

  EncodeString(&policy, key::kDeviceAdvancedBatteryChargeModeDayConfig,
               kString);
  EXPECT_EQ(kString,
            policy.device_advanced_battery_charge_mode().day_configs());

  // The encoder of this policy converts ints to
  // DeviceBatteryChargeMode::BatteryChardeMode enums.
  EncodeInteger(&policy, key::kDeviceBatteryChargeMode,
                em::DeviceBatteryChargeModeProto::CUSTOM);
  EXPECT_EQ(em::DeviceBatteryChargeModeProto::CUSTOM,
            policy.device_battery_charge_mode().battery_charge_mode());

  EncodeInteger(&policy, key::kDeviceBatteryChargeCustomStartCharging, kInt);
  EXPECT_EQ(kInt, policy.device_battery_charge_mode().custom_charge_start());

  EncodeInteger(&policy, key::kDeviceBatteryChargeCustomStopCharging, kInt);
  EXPECT_EQ(kInt, policy.device_battery_charge_mode().custom_charge_stop());

  EncodeBoolean(&policy, key::kDeviceUsbPowerShareEnabled, kBool);
  EXPECT_EQ(kBool, policy.device_usb_power_share().enabled());

  // The encoder of this policy converts ints to
  // DeviceUserPolicyLoopbackProcessingModeProto::Mode enums.
  EncodeInteger(
      &policy, key::kDeviceUserPolicyLoopbackProcessingMode,
      em::DeviceUserPolicyLoopbackProcessingModeProto::USER_POLICY_MODE_MERGE);
  EXPECT_EQ(
      em::DeviceUserPolicyLoopbackProcessingModeProto::USER_POLICY_MODE_MERGE,
      policy.device_user_policy_loopback_processing_mode().mode());

  EncodeBoolean(&policy, key::kVirtualMachinesAllowed, kBool);
  EXPECT_EQ(kBool,
            policy.virtual_machines_allowed().virtual_machines_allowed());

  EncodeInteger(&policy, key::kDeviceMachinePasswordChangeRate, kInt);
  EXPECT_EQ(kInt, policy.device_machine_password_change_rate().rate_days());

  EncodeInteger(&policy, key::kDeviceGpoCacheLifetime, kInt);
  EXPECT_EQ(kInt, policy.device_gpo_cache_lifetime().lifetime_hours());

  EncodeInteger(&policy, key::kDeviceAuthDataCacheLifetime, kInt);
  EXPECT_EQ(kInt, policy.device_auth_data_cache_lifetime().lifetime_hours());

  EncodeBoolean(&policy, key::kDeviceUnaffiliatedCrostiniAllowed, kBool);
  EXPECT_EQ(kBool, policy.device_unaffiliated_crostini_allowed()
                       .device_unaffiliated_crostini_allowed());

  EncodeBoolean(&policy, key::kDeviceShowLowDiskSpaceNotification, kBool);
  EXPECT_EQ(kBool, policy.device_show_low_disk_space_notification()
                       .device_show_low_disk_space_notification());

  //
  // Check whether all device policies have been handled.
  //

  std::vector<std::string> unhandled_policy_keys = GetUnhandledPolicyKeys();
  EXPECT_TRUE(unhandled_policy_keys.empty())
      << "Unhandled policy detected.\n"
      << "Please handle the following policies in "
      << "device_policy_encoder.cc and device_policy_encoder_unittest.cc:\n"
      << "  " << base::JoinString(unhandled_policy_keys, "\n  ");
}

}  // namespace policy
