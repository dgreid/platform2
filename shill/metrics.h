// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_METRICS_H_
#define SHILL_METRICS_H_

#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <metrics/cumulative_metrics.h>
#include <metrics/metrics_library.h>
#include <metrics/timer.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "shill/default_service_observer.h"
#include "shill/portal_detector.h"
#include "shill/power_manager.h"
#include "shill/refptr_types.h"
#include "shill/service.h"

#if !defined(DISABLE_WIFI)
#include "shill/net/ieee80211.h"
#include "shill/wifi/wake_on_wifi.h"
#endif  // DISABLE_WIFI

namespace shill {

class Metrics : public DefaultServiceObserver {
 public:
  enum WiFiChannel {
    kWiFiChannelUndef = 0,
    kWiFiChannel2412 = 1,
    kWiFiChannelMin24 = kWiFiChannel2412,
    kWiFiChannel2417 = 2,
    kWiFiChannel2422 = 3,
    kWiFiChannel2427 = 4,
    kWiFiChannel2432 = 5,
    kWiFiChannel2437 = 6,
    kWiFiChannel2442 = 7,
    kWiFiChannel2447 = 8,
    kWiFiChannel2452 = 9,
    kWiFiChannel2457 = 10,
    kWiFiChannel2462 = 11,
    kWiFiChannel2467 = 12,
    kWiFiChannel2472 = 13,
    kWiFiChannel2484 = 14,
    kWiFiChannelMax24 = kWiFiChannel2484,

    kWiFiChannel5180 = 15,
    kWiFiChannelMin5 = kWiFiChannel5180,
    kWiFiChannel5200 = 16,
    kWiFiChannel5220 = 17,
    kWiFiChannel5240 = 18,
    kWiFiChannel5260 = 19,
    kWiFiChannel5280 = 20,
    kWiFiChannel5300 = 21,
    kWiFiChannel5320 = 22,

    kWiFiChannel5500 = 23,
    kWiFiChannel5520 = 24,
    kWiFiChannel5540 = 25,
    kWiFiChannel5560 = 26,
    kWiFiChannel5580 = 27,
    kWiFiChannel5600 = 28,
    kWiFiChannel5620 = 29,
    kWiFiChannel5640 = 30,
    kWiFiChannel5660 = 31,
    kWiFiChannel5680 = 32,
    kWiFiChannel5700 = 33,

    kWiFiChannel5745 = 34,
    kWiFiChannel5765 = 35,
    kWiFiChannel5785 = 36,
    kWiFiChannel5805 = 37,
    kWiFiChannel5825 = 38,

    kWiFiChannel5170 = 39,
    kWiFiChannel5190 = 40,
    kWiFiChannel5210 = 41,
    kWiFiChannel5230 = 42,
    kWiFiChannelMax5 = kWiFiChannel5230,

    /* NB: ignore old 11b bands 2312..2372 and 2512..2532 */
    /* NB: ignore regulated bands 4920..4980 and 5020..5160 */
    kWiFiChannelMax
  };

  enum WiFiFrequencyRange {
    kWiFiFrequencyRangeUndef = 0,
    kWiFiFrequencyRange24 = 1,
    kWiFiFrequencyRange5 = 2,

    kWiFiFrequencyRangeMax
  };

  enum WiFiNetworkPhyMode {
    kWiFiNetworkPhyModeUndef = 0,    // Unknown/undefined
    kWiFiNetworkPhyMode11a = 1,      // 802.11a
    kWiFiNetworkPhyMode11b = 2,      // 802.11b
    kWiFiNetworkPhyMode11g = 3,      // 802.11g
    kWiFiNetworkPhyMode11n = 4,      // 802.11n
    kWiFiNetworkPhyModeHalf = 5,     // PSB Half-width
    kWiFiNetworkPhyModeQuarter = 6,  // PSB Quarter-width
    kWiFiNetworkPhyMode11ac = 7,     // 802.11ac

    kWiFiNetworkPhyModeMax
  };

  enum EapOuterProtocol {
    kEapOuterProtocolUnknown = 0,
    kEapOuterProtocolLeap = 1,
    kEapOuterProtocolPeap = 2,
    kEapOuterProtocolTls = 3,
    kEapOuterProtocolTtls = 4,

    kEapOuterProtocolMax
  };

  enum EapInnerProtocol {
    kEapInnerProtocolUnknown = 0,
    kEapInnerProtocolNone = 1,
    kEapInnerProtocolPeapMd5 = 2,
    kEapInnerProtocolPeapMschapv2 = 3,
    kEapInnerProtocolTtlsEapMd5 = 4,
    kEapInnerProtocolTtlsEapMschapv2 = 5,
    kEapInnerProtocolTtlsMschapv2 = 6,
    kEapInnerProtocolTtlsMschap = 7,
    kEapInnerProtocolTtlsPap = 8,
    kEapInnerProtocolTtlsChap = 9,

    kEapInnerProtocolMax
  };

  enum WiFiSecurity {
    kWiFiSecurityUnknown = 0,
    kWiFiSecurityNone = 1,
    kWiFiSecurityWep = 2,
    kWiFiSecurityWpa = 3,
    kWiFiSecurityRsn = 4,
    kWiFiSecurity8021x = 5,
    kWiFiSecurityPsk = 6,
    kWiFiSecurityWpa3 = 7,

    kWiFiSecurityMax
  };

  enum PortalResult {
    kPortalResultSuccess = 0,
    kPortalResultDNSFailure = 1,
    kPortalResultDNSTimeout = 2,
    kPortalResultConnectionFailure = 3,
    kPortalResultConnectionTimeout = 4,
    kPortalResultHTTPFailure = 5,
    kPortalResultHTTPTimeout = 6,
    kPortalResultContentFailure = 7,
    kPortalResultContentTimeout = 8,
    kPortalResultUnknown = 9,
    kPortalResultContentRedirect = 10,

    kPortalResultMax
  };

  enum LinkMonitorFailure {
    kLinkMonitorMacAddressNotFound = 0,
    kLinkMonitorClientStartFailure = 1,
    kLinkMonitorTransmitFailure = 2,
    kLinkMonitorFailureThresholdReached = 3,

    kLinkMonitorFailureMax
  };

  enum NeighborLinkMonitorFailure {
    kNeighborLinkMonitorFailureUnknown = 0,
    kNeighborIPv4GatewayFailure = 1,
    kNeighborIPv4DNSServerFailure = 2,
    kNeighborIPv4GatewayAndDNSServerFailure = 3,
    kNeighborIPv6GatewayFailure = 4,
    kNeighborIPv6DNSServerFailure = 5,
    kNeighborIPv6GatewayAndDNSServerFailure = 6,

    kNeighborLinkMonitorFailureMax
  };

  enum WiFiApChannelSwitch {
    kWiFiApChannelSwitchUndef = 0,
    kWiFiApChannelSwitch24To24 = 1,
    kWiFiApChannelSwitch24To5 = 2,
    kWiFiApChannelSwitch5To24 = 3,
    kWiFiApChannelSwitch5To5 = 4,

    kWiFiApChannelSwitchMax
  };

  enum WiFiAp80211kSupport {
    kWiFiAp80211kNone = 0,
    kWiFiAp80211kNeighborList = 1,

    kWiFiAp80211kMax
  };

  enum WiFiAp80211rSupport {
    kWiFiAp80211rNone = 0,
    kWiFiAp80211rOTA = 1,
    kWiFiAp80211rOTDS = 2,

    kWiFiAp80211rMax
  };

  enum WiFiAp80211vDMSSupport {
    kWiFiAp80211vNoDMS = 0,
    kWiFiAp80211vDMS = 1,

    kWiFiAp80211vDMSMax
  };

  enum WiFiAp80211vBSSMaxIdlePeriodSupport {
    kWiFiAp80211vNoBSSMaxIdlePeriod = 0,
    kWiFiAp80211vBSSMaxIdlePeriod = 1,

    kWiFiAp80211vBSSMaxIdlePeriodMax
  };

  enum WiFiAp80211vBSSTransitionSupport {
    kWiFiAp80211vNoBSSTransition = 0,
    kWiFiAp80211vBSSTransition = 1,

    kWiFiAp80211vBSSTransitionMax
  };

  enum WiFiRoamComplete {
    kWiFiRoamSuccess = 0,
    kWiFiRoamFailure = 1,

    kWiFiRoamCompleteMax
  };

  enum WiFiReasonType {
    kReasonCodeTypeByAp,
    kReasonCodeTypeByClient,
    kReasonCodeTypeByUser,
    kReasonCodeTypeConsideredDead,
    kReasonCodeTypeMax
  };

  enum WiFiDisconnectByWhom { kDisconnectedByAp, kDisconnectedNotByAp };

  enum WiFiScanResult {
    kScanResultProgressiveConnected,
    kScanResultProgressiveErrorAndFullFoundNothing,
    kScanResultProgressiveErrorButFullConnected,
    kScanResultProgressiveAndFullFoundNothing,
    kScanResultProgressiveAndFullConnected,
    kScanResultFullScanFoundNothing,
    kScanResultFullScanConnected,
    kScanResultInternalError,
    kScanResultMax
  };

  enum TerminationActionResult {
    kTerminationActionResultSuccess,
    kTerminationActionResultFailure,
    kTerminationActionResultMax
  };

  enum SuspendActionResult {
    kSuspendActionResultSuccess,
    kSuspendActionResultFailure,
    kSuspendActionResultMax
  };

  enum DarkResumeActionResult {
    kDarkResumeActionResultSuccess,
    kDarkResumeActionResultFailure,
    kDarkResumeActionResultMax
  };

  enum DarkResumeUnmatchedScanResultReceived {
    kDarkResumeUnmatchedScanResultsReceivedFalse = 0,
    kDarkResumeUnmatchedScanResultsReceivedTrue = 1,
    kDarkResumeUnmatchedScanResultsReceivedMax
  };

  enum VerifyWakeOnWiFiSettingsResult {
    kVerifyWakeOnWiFiSettingsResultSuccess,
    kVerifyWakeOnWiFiSettingsResultFailure,
    kVerifyWakeOnWiFiSettingsResultMax
  };

  enum WiFiConnectionStatusAfterWake {
    kWiFiConnectionStatusAfterWakeWoWOnConnected = 0,
    kWiFiConnectionStatusAfterWakeWoWOnDisconnected = 1,
    kWiFiConnectionStatusAfterWakeWoWOffConnected = 2,
    kWiFiConnectionStatusAfterWakeWoWOffDisconnected = 3,
    kWiFiConnectionStatusAfterWakeMax
  };

  enum Cellular3GPPRegistrationDelayedDrop {
    kCellular3GPPRegistrationDelayedDropPosted = 0,
    kCellular3GPPRegistrationDelayedDropCanceled = 1,
    kCellular3GPPRegistrationDelayedDropMax
  };

  enum CellularDropTechnology {
    kCellularDropTechnology1Xrtt = 0,
    kCellularDropTechnologyEdge = 1,
    kCellularDropTechnologyEvdo = 2,
    kCellularDropTechnologyGprs = 3,
    kCellularDropTechnologyGsm = 4,
    kCellularDropTechnologyHspa = 5,
    kCellularDropTechnologyHspaPlus = 6,
    kCellularDropTechnologyLte = 7,
    kCellularDropTechnologyUmts = 8,
    kCellularDropTechnologyUnknown = 9,
    kCellularDropTechnologyMax
  };

  enum CellularOutOfCreditsReason {
    kCellularOutOfCreditsReasonConnectDisconnectLoop = 0,
    kCellularOutOfCreditsReasonTxCongested = 1,
    kCellularOutOfCreditsReasonElongatedTimeWait = 2,
    kCellularOutOfCreditsReasonMax
  };

  enum CorruptedProfile { kCorruptedProfile = 1, kCorruptedProfileMax };

  enum ConnectionDiagnosticsIssue {
    kConnectionDiagnosticsIssueIPCollision = 0,
    kConnectionDiagnosticsIssueRouting = 1,
    kConnectionDiagnosticsIssueHTTPBrokenPortal = 2,
    kConnectionDiagnosticsIssueDNSServerMisconfig = 3,
    kConnectionDiagnosticsIssueDNSServerNoResponse = 4,
    kConnectionDiagnosticsIssueNoDNSServersConfigured = 5,
    kConnectionDiagnosticsIssueDNSServersInvalid = 6,
    kConnectionDiagnosticsIssueNone = 7,
    kConnectionDiagnosticsIssueCaptivePortal = 8,
    kConnectionDiagnosticsIssueGatewayUpstream = 9,
    kConnectionDiagnosticsIssueGatewayNotResponding = 10,
    kConnectionDiagnosticsIssueServerNotResponding = 11,
    kConnectionDiagnosticsIssueGatewayArpFailed = 12,
    kConnectionDiagnosticsIssueServerArpFailed = 13,
    kConnectionDiagnosticsIssueInternalError = 14,
    kConnectionDiagnosticsIssueGatewayNoNeighborEntry = 15,
    kConnectionDiagnosticsIssueServerNoNeighborEntry = 16,
    kConnectionDiagnosticsIssueGatewayNeighborEntryNotConnected = 17,
    kConnectionDiagnosticsIssueServerNeighborEntryNotConnected = 18,
    kConnectionDiagnosticsIssuePlaceholder1 = 19,
    kConnectionDiagnosticsIssuePlaceholder2 = 20,
    kConnectionDiagnosticsIssuePlaceholder3 = 21,
    kConnectionDiagnosticsIssuePlaceholder4 = 22,
    kConnectionDiagnosticsIssueMax
  };

  enum PortalDetectionMultiProbeResult {
    kPortalDetectionMultiProbeResultUndefined = 0,
    kPortalDetectionMultiProbeResultHTTPSBlockedHTTPBlocked = 1,
    kPortalDetectionMultiProbeResultHTTPSBlockedHTTPRedirected = 2,
    kPortalDetectionMultiProbeResultHTTPSBlockedHTTPUnblocked = 3,
    kPortalDetectionMultiProbeResultHTTPSUnblockedHTTPBlocked = 4,
    kPortalDetectionMultiProbeResultHTTPSUnblockedHTTPRedirected = 5,
    kPortalDetectionMultiProbeResultHTTPSUnblockedHTTPUnblocked = 6,
    kPortalDetectionMultiProbeResultMax
  };

  enum VpnDriver {
    kVpnDriverOpenVpn = 0,
    kVpnDriverL2tpIpsec = 1,
    kVpnDriverThirdParty = 2,
    kVpnDriverArc = 3,
    kVpnDriverMax
  };

  enum VpnRemoteAuthenticationType {
    kVpnRemoteAuthenticationTypeOpenVpnDefault = 0,
    kVpnRemoteAuthenticationTypeOpenVpnCertificate = 1,
    kVpnRemoteAuthenticationTypeL2tpIpsecDefault = 2,
    kVpnRemoteAuthenticationTypeL2tpIpsecCertificate = 3,
    kVpnRemoteAuthenticationTypeL2tpIpsecPsk = 4,
    kVpnRemoteAuthenticationTypeMax
  };

  enum VpnUserAuthenticationType {
    kVpnUserAuthenticationTypeOpenVpnNone = 0,
    kVpnUserAuthenticationTypeOpenVpnCertificate = 1,
    kVpnUserAuthenticationTypeOpenVpnUsernamePassword = 2,
    kVpnUserAuthenticationTypeOpenVpnUsernamePasswordOtp = 3,
    kVpnUserAuthenticationTypeOpenVpnUsernameToken = 7,
    kVpnUserAuthenticationTypeL2tpIpsecNone = 4,
    kVpnUserAuthenticationTypeL2tpIpsecCertificate = 5,
    kVpnUserAuthenticationTypeL2tpIpsecUsernamePassword = 6,
    kVpnUserAuthenticationTypeMax
  };

  enum UserInitiatedEvent {
    kUserInitiatedEventWifiScan = 0,
    kUserInitiatedEventReserved,
    kUserInitiatedEventMax
  };

  // Result of a connection initiated by Service::UserInitiatedConnect.
  enum UserInitiatedConnectionResult {
    kUserInitiatedConnectionResultSuccess = 0,
    kUserInitiatedConnectionResultFailure = 1,
    kUserInitiatedConnectionResultAborted = 2,
    kUserInitiatedConnectionResultMax
  };

  enum DNSTestResult {
    kFallbackDNSTestResultSuccess = 0,
    kFallbackDNSTestResultFailure,
    kFallbackDNSTestResultMax
  };

  // Network problem detected by traffic monitor.
  enum NetworkProblem {
    kNetworkProblemCongestedTCPTxQueue = 0,
    kNetworkProblemDNSFailure,
    kNetworkProblemMax
  };

  // Device's connection status.
  enum ConnectionStatus {
    kConnectionStatusOffline = 0,
    kConnectionStatusConnected = 1,
    kConnectionStatusOnline = 2,
    kConnectionStatusMax
  };

  // Reason when a connection initiated by Service::UserInitiatedConnect fails.
  enum UserInitiatedConnectionFailureReason {
    kUserInitiatedConnectionFailureReasonBadPassphrase = 1,
    kUserInitiatedConnectionFailureReasonBadWEPKey = 2,
    kUserInitiatedConnectionFailureReasonConnect = 3,
    kUserInitiatedConnectionFailureReasonDHCP = 4,
    kUserInitiatedConnectionFailureReasonDNSLookup = 5,
    kUserInitiatedConnectionFailureReasonEAPAuthentication = 6,
    kUserInitiatedConnectionFailureReasonEAPLocalTLS = 7,
    kUserInitiatedConnectionFailureReasonEAPRemoteTLS = 8,
    kUserInitiatedConnectionFailureReasonOutOfRange = 9,
    kUserInitiatedConnectionFailureReasonPinMissing = 10,
    kUserInitiatedConnectionFailureReasonUnknown = 11,
    kUserInitiatedConnectionFailureReasonNone = 12,
    kUserInitiatedConnectionFailureReasonNotAssociated = 13,
    kUserInitiatedConnectionFailureReasonNotAuthenticated = 14,
    kUserInitiatedConnectionFailureReasonTooManySTAs = 15,
    kUserInitiatedConnectionFailureReasonMax
  };

  enum DhcpClientStatus {
    kDhcpClientStatusArpGateway = 0,
    kDhcpClientStatusArpSelf = 1,
    kDhcpClientStatusBound = 2,
    kDhcpClientStatusDiscover = 3,
    kDhcpClientStatusIgnoreAdditionalOffer = 4,
    kDhcpClientStatusIgnoreFailedOffer = 5,
    kDhcpClientStatusIgnoreInvalidOffer = 6,
    kDhcpClientStatusIgnoreNonOffer = 7,
    kDhcpClientStatusInform = 8,
    kDhcpClientStatusInit = 9,
    kDhcpClientStatusNakDefer = 10,
    kDhcpClientStatusRebind = 11,
    kDhcpClientStatusReboot = 12,
    kDhcpClientStatusRelease = 13,
    kDhcpClientStatusRenew = 14,
    kDhcpClientStatusRequest = 15,
    kDhcpClientStatusMax
  };

  enum NetworkConnectionIPType {
    kNetworkConnectionIPTypeIPv4 = 0,
    kNetworkConnectionIPTypeIPv6 = 1,
    kNetworkConnectionIPTypeMax
  };

  enum IPv6ConnectivityStatus {
    kIPv6ConnectivityStatusNo = 0,
    kIPv6ConnectivityStatusYes = 1,
    kIPv6ConnectivityStatusMax
  };

  enum DevicePresenceStatus {
    kDevicePresenceStatusNo = 0,
    kDevicePresenceStatusYes = 1,
    kDevicePresenceStatusMax
  };

  enum DeviceTechnologyType {
    kDeviceTechnologyTypeUnknown = 0,
    kDeviceTechnologyTypeEthernet = 1,
    kDeviceTechnologyTypeWifi = 2,
    // deprecated: kDeviceTechnologyTypeWimax = 3,
    kDeviceTechnologyTypeCellular = 4,
    kDeviceTechnologyTypeMax
  };

  // These correspond to entries in Chrome's tools/metrics/histograms/enums.xml.
  // Please do not remove entries (append 'Deprecated' instead), and update the
  // enums.xml file when entries are added.
  enum NetworkServiceError {
    kNetworkServiceErrorNone = 0,
    kNetworkServiceErrorAAA = 1,
    kNetworkServiceErrorActivation = 2,
    kNetworkServiceErrorBadPassphrase = 3,
    kNetworkServiceErrorBadWEPKey = 4,
    kNetworkServiceErrorConnect = 5,
    kNetworkServiceErrorDHCP = 6,
    kNetworkServiceErrorDNSLookup = 7,
    kNetworkServiceErrorEAPAuthentication = 8,
    kNetworkServiceErrorEAPLocalTLS = 9,
    kNetworkServiceErrorEAPRemoteTLS = 10,
    kNetworkServiceErrorHTTPGet = 11,
    kNetworkServiceErrorIPSecCertAuth = 12,
    kNetworkServiceErrorIPSecPSKAuth = 13,
    kNetworkServiceErrorInternal = 14,
    kNetworkServiceErrorNeedEVDO = 15,
    kNetworkServiceErrorNeedHomeNetwork = 16,
    kNetworkServiceErrorOTASP = 17,
    kNetworkServiceErrorOutOfRange = 18,
    kNetworkServiceErrorPPPAuth = 19,
    kNetworkServiceErrorPinMissing = 20,
    kNetworkServiceErrorUnknown = 21,
    kNetworkServiceErrorNotAssociated = 22,
    kNetworkServiceErrorNotAuthenticated = 23,
    kNetworkServiceErrorTooManySTAs = 24,
    kNetworkServiceErrorDisconnect = 25,
    kNetworkServiceErrorMax
  };

  enum WakeOnWiFiFeaturesEnabledState {
    kWakeOnWiFiFeaturesEnabledStateNone = 0,
    kWakeOnWiFiFeaturesEnabledStatePacket = 1,
    kWakeOnWiFiFeaturesEnabledStateDarkConnect = 2,
    kWakeOnWiFiFeaturesEnabledStatePacketDarkConnect = 3,
    kWakeOnWiFiFeaturesEnabledStateMax
  };

  enum WakeOnWiFiThrottled {
    kWakeOnWiFiThrottledFalse = 0,
    kWakeOnWiFiThrottledTrue = 1,
    kWakeOnWiFiThrottledMax
  };

  enum WakeReasonReceivedBeforeOnDarkResume {
    kWakeReasonReceivedBeforeOnDarkResumeFalse = 0,
    kWakeReasonReceivedBeforeOnDarkResumeTrue = 1,
    kWakeReasonReceivedBeforeOnDarkResumeMax
  };

  enum DarkResumeWakeReason {
    kDarkResumeWakeReasonUnsupported = 0,
    kDarkResumeWakeReasonPattern = 1,
    kDarkResumeWakeReasonDisconnect = 2,
    kDarkResumeWakeReasonSSID = 3,
    kDarkResumeWakeReasonMax
  };

  enum DarkResumeScanType {
    kDarkResumeScanTypeActive = 0,
    kDarkResumeScanTypePassive = 1,
    kDarkResumeScanTypeMax
  };

  enum DarkResumeScanRetryResult {
    kDarkResumeScanRetryResultNotConnected = 0,
    kDarkResumeScanRetryResultConnected = 1,
    kDarkResumeScanRetryResultMax
  };

  // Identifiers for indices in cumulative name arrays.  The FRACTION entries
  // must be last.
  enum CumulativeName {
    CHOSEN_ANY = 0,
    CHOSEN_CELLULAR = 1,
    CHOSEN_WIFI = 2,
    CHOSEN_FRACTION_CELLULAR = 3,
    CHOSEN_FRACTION_WIFI = 4,
  };

  // Corresponds to RegulatoryDomain enum values in
  // /chromium/src/tools/metrics/histograms/enums.xml.
  enum RegulatoryDomain {
    kRegDom00 = 1,
    kCountryCodeInvalid = 678,
    kRegDomMaxValue
  };

  enum HS20Support {
    kHS20Unsupported = 0,
    kHS20VersionInvalid = 1,
    kHS20Version1 = 2,
    kHS20Version2 = 3,
    kHS20Version3 = 4,
    kHS20SupportMax
  };

  static const char kMetricDisconnectSuffix[];
  static const int kMetricDisconnectMax;
  static const int kMetricDisconnectMin;
  static const int kMetricDisconnectNumBuckets;
  static const char kMetricSignalAtDisconnectSuffix[];
  static const int kMetricSignalAtDisconnectMin;
  static const int kMetricSignalAtDisconnectMax;
  static const int kMetricSignalAtDisconnectNumBuckets;
  static const char kMetricNetworkChannelSuffix[];
  static const int kMetricNetworkChannelMax;
  static const char kMetricNetworkEapInnerProtocolSuffix[];
  static const int kMetricNetworkEapInnerProtocolMax;
  static const char kMetricNetworkEapOuterProtocolSuffix[];
  static const int kMetricNetworkEapOuterProtocolMax;
  static const char kMetricNetworkPhyModeSuffix[];
  static const int kMetricNetworkPhyModeMax;
  static const char kMetricNetworkSecuritySuffix[];
  static const int kMetricNetworkSecurityMax;
  static const char kMetricNetworkServiceErrorSuffix[];
  static const char kMetricNetworkServiceErrors[];
  static const char kMetricNetworkSignalStrengthSuffix[];
  static const int kMetricNetworkSignalStrengthMin;
  static const int kMetricNetworkSignalStrengthMax;
  static const int kMetricNetworkSignalStrengthNumBuckets;
  // Histogram parameters for next two are the same as for
  // kMetricRememberedWiFiNetworkCount. Must be constexpr, for static
  // checking of format string. Must be defined inline, for constexpr.
  static constexpr char
      kMetricRememberedSystemWiFiNetworkCountBySecurityModeFormat[] =
          "Network.Shill.WiFi.RememberedSystemNetworkCount.%s";
  static constexpr char
      kMetricRememberedUserWiFiNetworkCountBySecurityModeFormat[] =
          "Network.Shill.WiFi.RememberedUserNetworkCount.%s";
  static const char kMetricRememberedWiFiNetworkCount[];
  static const int kMetricRememberedWiFiNetworkCountMin;
  static const int kMetricRememberedWiFiNetworkCountMax;
  static const int kMetricRememberedWiFiNetworkCountNumBuckets;
  static const char kMetricHiddenSSIDNetworkCount[];
  static const char kMetricTimeOnlineSecondsSuffix[];
  static const int kMetricTimeOnlineSecondsMax;
  static const int kMetricTimeOnlineSecondsMin;
  static const int kMetricTimeOnlineSecondsNumBuckets;
  static const char kMetricTimeResumeToReadyMillisecondsSuffix[];
  static const char kMetricTimeToConfigMillisecondsSuffix[];
  static const char kMetricTimeToConnectMillisecondsSuffix[];
  static const int kMetricTimeToConnectMillisecondsMax;
  static const int kMetricTimeToConnectMillisecondsMin;
  static const int kMetricTimeToConnectMillisecondsNumBuckets;
  static const char kMetricTimeToScanAndConnectMillisecondsSuffix[];
  static const char kMetricsCumulativeDirectory[];
  static const int kMetricsCumulativeTimeOnlineBucketCount;
  static const char kMetricTimeToDropSeconds[];
  static const int kMetricTimeToDropSecondsMax;
  static const int kMetricTimeToDropSecondsMin;
  static const char kMetricTimeToDisableMillisecondsSuffix[];
  static const int kMetricTimeToDisableMillisecondsMax;
  static const int kMetricTimeToDisableMillisecondsMin;
  static const int kMetricTimeToDisableMillisecondsNumBuckets;
  static const char kMetricTimeToEnableMillisecondsSuffix[];
  static const int kMetricTimeToEnableMillisecondsMax;
  static const int kMetricTimeToEnableMillisecondsMin;
  static const int kMetricTimeToEnableMillisecondsNumBuckets;
  static const char kMetricTimeToInitializeMillisecondsSuffix[];
  static const int kMetricTimeToInitializeMillisecondsMax;
  static const int kMetricTimeToInitializeMillisecondsMin;
  static const int kMetricTimeToInitializeMillisecondsNumBuckets;
  static const char kMetricTimeToJoinMillisecondsSuffix[];
  static const char kMetricTimeToOnlineMillisecondsSuffix[];
  static const char kMetricTimeToPortalMillisecondsSuffix[];
  static const char kMetricTimeToRedirectFoundMillisecondsSuffix[];
  static const char kMetricTimeToScanMillisecondsSuffix[];
  static const int kMetricTimeToScanMillisecondsMax;
  static const int kMetricTimeToScanMillisecondsMin;
  static const int kMetricTimeToScanMillisecondsNumBuckets;
  static const int kTimerHistogramMillisecondsMax;
  static const int kTimerHistogramMillisecondsMin;
  static const int kTimerHistogramNumBuckets;

  // The 4 histograms below track the time spent in suspended
  // state for each of the 4 scenarios in WiFiConnectionStatusAfterWake
  // We consider it normal that wifi disconnects after a resume after
  // a long time spent in suspend, but not after a short time.
  // See bug chromium:614790.
  static const char kMetricSuspendDurationWoWOnConnected[];
  static const char kMetricSuspendDurationWoWOnDisconnected[];
  static const char kMetricSuspendDurationWoWOffConnected[];
  static const char kMetricSuspendDurationWoWOffDisconnected[];
  static const int kSuspendDurationMin;
  static const int kSuspendDurationMax;
  static const int kSuspendDurationNumBuckets;

  // The total number of portal detections attempted between the Connected
  // state and the Online state.  This includes both failure/timeout attempts
  // and the final successful attempt.
  static const char kMetricPortalAttemptsToOnlineSuffix[];
  static const int kMetricPortalAttemptsToOnlineMax;
  static const int kMetricPortalAttemptsToOnlineMin;
  static const int kMetricPortalAttemptsToOnlineNumBuckets;

  // The result of the portal detection.
  static const char kMetricPortalResultSuffix[];

  static const char kMetricScanResult[];
  static const char kMetricWiFiScanTimeInEbusyMilliseconds[];

  static const char kMetricPowerManagerKey[];

  // LinkMonitor statistics.
  static const char kMetricLinkMonitorFailureSuffix[];
  static const char kMetricLinkMonitorResponseTimeSampleSuffix[];
  static const int kMetricLinkMonitorResponseTimeSampleMin;
  static const int kMetricLinkMonitorResponseTimeSampleMax;
  static const int kMetricLinkMonitorResponseTimeSampleNumBuckets;
  static const char kMetricLinkMonitorSecondsToFailureSuffix[];
  static const int kMetricLinkMonitorSecondsToFailureMin;
  static const int kMetricLinkMonitorSecondsToFailureMax;
  static const int kMetricLinkMonitorSecondsToFailureNumBuckets;
  static const char kMetricLinkMonitorBroadcastErrorsAtFailureSuffix[];
  static const char kMetricLinkMonitorUnicastErrorsAtFailureSuffix[];
  static const int kMetricLinkMonitorErrorCountMin;
  static const int kMetricLinkMonitorErrorCountMax;
  static const int kMetricLinkMonitorErrorCountNumBuckets;

  // patchpanel::NeighborLinkMonitor statistics.
  static const char kMetricNeighborLinkMonitorFailureSuffix[];

  // Signal strength when link becomes unreliable (multiple link monitor
  // failures in short period of time).
  static const char kMetricUnreliableLinkSignalStrengthSuffix[];
  static const int kMetricServiceSignalStrengthMin;
  static const int kMetricServiceSignalStrengthMax;
  static const int kMetricServiceSignalStrengthNumBuckets;

  // AP 802.11r/k/v support statistics.
  static const char kMetricAp80211kSupport[];
  static const char kMetricAp80211rSupport[];
  static const char kMetricAp80211vDMSSupport[];
  static const char kMetricAp80211vBSSMaxIdlePeriodSupport[];
  static const char kMetricAp80211vBSSTransitionSupport[];

  static const char kMetricLinkClientDisconnectReason[];
  static const char kMetricLinkApDisconnectReason[];
  static const char kMetricLinkClientDisconnectType[];
  static const char kMetricLinkApDisconnectType[];

  // 802.11 Status Codes for auth/assoc failures
  static const char kMetricWiFiAssocFailureType[];
  static const char kMetricWiFiAuthFailureType[];

  // Roam time for FT and non-FT key management suites.
  static const char kMetricWifiRoamTimePrefix[];
  static const int kMetricWifiRoamTimeMillisecondsMax;
  static const int kMetricWifiRoamTimeMillisecondsMin;
  static const int kMetricWifiRoamTimeNumBuckets;

  // Roam completions for FT and non-FT key management suites.
  static const char kMetricWifiRoamCompletePrefix[];

  // Session Lengths for FT and non-FT key management suites.
  static const char kMetricWifiSessionLengthPrefix[];
  static const int kMetricWifiSessionLengthMillisecondsMax;
  static const int kMetricWifiSessionLengthMillisecondsMin;
  static const int kMetricWifiSessionLengthNumBuckets;

  // Suffixes for roam histograms.
  static const char kMetricWifiPSKSuffix[];
  static const char kMetricWifiFTPSKSuffix[];
  static const char kMetricWifiEAPSuffix[];
  static const char kMetricWifiFTEAPSuffix[];

  static const char kMetricApChannelSwitch[];

  // Shill termination action statistics.
  static const char kMetricTerminationActionTimeTaken[];
  static const char kMetricTerminationActionResult[];
  static const int kMetricTerminationActionTimeTakenMillisecondsMax;
  static const int kMetricTerminationActionTimeTakenMillisecondsMin;

  // Shill suspend action statistics.
  static const char kMetricSuspendActionTimeTaken[];
  static const char kMetricSuspendActionResult[];
  static const int kMetricSuspendActionTimeTakenMillisecondsMax;
  static const int kMetricSuspendActionTimeTakenMillisecondsMin;

  // Shill dark resume action statistics.
  static const char kMetricDarkResumeActionTimeTaken[];
  static const char kMetricDarkResumeActionResult[];
  static const int kMetricDarkResumeActionTimeTakenMillisecondsMax;
  static const int kMetricDarkResumeActionTimeTakenMillisecondsMin;
  static const char kMetricDarkResumeUnmatchedScanResultReceived[];

  // Shill wake on WiFi feature state statistics.
  static const char kMetricWakeOnWiFiFeaturesEnabledState[];
  // The result of NIC wake on WiFi settings verification.
  static const char kMetricVerifyWakeOnWiFiSettingsResult[];
  static const char kMetricWiFiConnectionStatusAfterWake[];
  // Whether or not wake on WiFi was throttled during the last suspend.
  static const char kMetricWakeOnWiFiThrottled[];
  // Whether or not a wakeup reason was received before WakeOnWiFi::OnDarkResume
  // executes.
  static const char kMetricWakeReasonReceivedBeforeOnDarkResume[];
  static const char kMetricDarkResumeWakeReason[];
  static const char kMetricDarkResumeScanType[];
  static const char kMetricDarkResumeScanRetryResult[];
  static const char kMetricDarkResumeScanNumRetries[];
  static const int kMetricDarkResumeScanNumRetriesMax;
  static const int kMetricDarkResumeScanNumRetriesMin;

  // Cellular specific statistics.
  static const char kMetricCellular3GPPRegistrationDelayedDrop[];
  static const char kMetricCellularAutoConnectTries[];
  static const int kMetricCellularAutoConnectTriesMax;
  static const int kMetricCellularAutoConnectTriesMin;
  static const int kMetricCellularAutoConnectTriesNumBuckets;
  static const char kMetricCellularAutoConnectTotalTime[];
  static const int kMetricCellularAutoConnectTotalTimeMax;
  static const int kMetricCellularAutoConnectTotalTimeMin;
  static const int kMetricCellularAutoConnectTotalTimeNumBuckets;
  static const char kMetricCellularDrop[];
  static const char kMetricCellularDropsPerHour[];
  static const int kMetricCellularDropsPerHourMax;
  static const int kMetricCellularDropsPerHourMin;
  static const int kMetricCellularDropsPerHourNumBuckets;
  static const char kMetricCellularFailure[];
  static const int kMetricCellularConnectionFailure;
  static const int kMetricCellularDisconnectionFailure;
  static const int kMetricCellularMaxFailure;
  static const char kMetricCellularOutOfCreditsReason[];
  static const char kMetricCellularSignalStrengthBeforeDrop[];
  static const int kMetricCellularSignalStrengthBeforeDropMax;
  static const int kMetricCellularSignalStrengthBeforeDropMin;
  static const int kMetricCellularSignalStrengthBeforeDropNumBuckets;

  // Profile statistics.
  static const char kMetricCorruptedProfile[];

  // VPN connection statistics.
  static const char kMetricVpnDriver[];
  static const int kMetricVpnDriverMax;
  static const char kMetricVpnRemoteAuthenticationType[];
  static const int kMetricVpnRemoteAuthenticationTypeMax;
  static const char kMetricVpnUserAuthenticationType[];
  static const int kMetricVpnUserAuthenticationTypeMax;

  // The length in seconds of a lease that has expired while the DHCP
  // client was attempting to renew the lease..
  static const char kMetricExpiredLeaseLengthSecondsSuffix[];
  static const int kMetricExpiredLeaseLengthSecondsMax;
  static const int kMetricExpiredLeaseLengthSecondsMin;
  static const int kMetricExpiredLeaseLengthSecondsNumBuckets;

  // Number of wifi services available when auto-connect is initiated.
  static const char kMetricWifiAutoConnectableServices[];
  static const int kMetricWifiAutoConnectableServicesMax;
  static const int kMetricWifiAutoConnectableServicesMin;
  static const int kMetricWifiAutoConnectableServicesNumBuckets;

  // Number of BSSes available for a wifi service when we attempt to connect
  // to that service.
  static const char kMetricWifiAvailableBSSes[];
  static const int kMetricWifiAvailableBSSesMax;
  static const int kMetricWifiAvailableBSSesMin;
  static const int kMetricWifiAvailableBSSesNumBuckets;

  // Reason that the mac80211 TX queue is stopped.
  static const char kMetricWifiStoppedTxQueueReason[];

  // Maximal queue length amongst all stopped mac80211 TX queues.
  static const char kMetricWifiStoppedTxQueueLength[];
  static const int kMetricWifiStoppedTxQueueLengthMax;
  static const int kMetricWifiStoppedTxQueueLengthMin;
  static const int kMetricWifiStoppedTxQueueLengthNumBuckets;

  // Number of services associated with currently connected network.
  static const char kMetricServicesOnSameNetwork[];
  static const int kMetricServicesOnSameNetworkMax;
  static const int kMetricServicesOnSameNetworkMin;
  static const int kMetricServicesOnSameNetworkNumBuckets;

  // Metric for user-initiated events.
  static const char kMetricUserInitiatedEvents[];

  // Wifi TX bitrate in Mbps.
  static const char kMetricWifiTxBitrate[];
  static const int kMetricWifiTxBitrateMax;
  static const int kMetricWifiTxBitrateMin;
  static const int kMetricWifiTxBitrateNumBuckets;

  // User-initiated wifi connection attempt result.
  static const char kMetricWifiUserInitiatedConnectionResult[];

  // The reason of failed user-initiated wifi connection attempt.
  static const char kMetricWifiUserInitiatedConnectionFailureReason[];

  // Number of attempts made to connect to supplicant before success (max ==
  // failure).
  static const char kMetricWifiSupplicantAttempts[];
  static const int kMetricWifiSupplicantAttemptsMax;
  static const int kMetricWifiSupplicantAttemptsMin;
  static const int kMetricWifiSupplicantAttemptsNumBuckets;

  // DNS test result.
  static const char kMetricFallbackDNSTestResultSuffix[];

  // Network problem detected by traffic monitor
  static const char kMetricNetworkProblemDetectedSuffix[];

  // Device's connection status.
  static const char kMetricDeviceConnectionStatus[];

  // DHCP client status.
  static const char kMetricDhcpClientStatus[];

  // Assigned MTU values, both from DHCP and PPP.
  static const char kMetricDhcpClientMTUValue[];
  static const char kMetricPPPMTUValue[];

  // Network connection IP type.
  static const char kMetricNetworkConnectionIPTypeSuffix[];

  // IPv6 connectivity status.
  static const char kMetricIPv6ConnectivityStatusSuffix[];

  // Device presence.
  static const char kMetricDevicePresenceStatusSuffix[];

  // Device removal event.
  static const char kMetricDeviceRemovedEvent[];

  // Connection diagnostics issue.
  static const char kMetricConnectionDiagnosticsIssue[];

  // Portal detection results.
  static const char kMetricPortalDetectionMultiProbeResult[];

  // Wireless regulatory domain metric.
  static const char kMetricRegulatoryDomain[];

  // Hotspot 2.0 version number metric.
  static const char kMetricHS20Support[];

  Metrics();
  Metrics(const Metrics&) = delete;
  Metrics& operator=(const Metrics&) = delete;

  virtual ~Metrics();

  // Converts the WiFi frequency into the associated UMA channel enumerator.
  static WiFiChannel WiFiFrequencyToChannel(uint16_t frequency);

  // Converts WiFi Channel to the associated frequency range.
  static WiFiFrequencyRange WiFiChannelToFrequencyRange(WiFiChannel channel);

  // Converts a flimflam security string into its UMA security enumerator.
  static WiFiSecurity WiFiSecurityStringToEnum(const std::string& security);

  // Converts a flimflam EAP outer protocol string into its UMA enumerator.
  static EapOuterProtocol EapOuterProtocolStringToEnum(
      const std::string& outer);

  // Converts a flimflam EAP inner protocol string into its UMA enumerator.
  static EapInnerProtocol EapInnerProtocolStringToEnum(
      const std::string& inner);

  // Converts portal detection result to UMA portal result enumerator.
  static PortalResult PortalDetectionResultToEnum(
      const PortalDetector::Result& result);

  // Callback for accumulating per-technology connected time.
  static void AccumulateTimeOnTechnology(
      const Metrics* metrics,
      std::vector<std::string> cumulative_names,
      chromeos_metrics::CumulativeMetrics* cm);

  // Callback for reporting UMA stats on connected time per device per time
  // period.
  static void ReportTimeOnTechnology(
      MetricsLibraryInterface* mli,
      const std::vector<std::string> histogram_names,
      const int min,
      const int max,
      const std::vector<std::string> cumulative_names,
      chromeos_metrics::CumulativeMetrics* cm);

  // Starts this object.  Call this during initialization.
  virtual void Start();

  // Stops this object.  Call this during cleanup.
  virtual void Stop();

  // Registers a service with this object so it can use the timers to track
  // state transition metrics.
  void RegisterService(const Service& service);

  // Deregisters the service from this class.  All state transition timers
  // will be removed.
  void DeregisterService(const Service& service);

  // Tracks the time it takes |service| to go from |start_state| to
  // |stop_state|.  When |stop_state| is reached, the time is sent to UMA.
  virtual void AddServiceStateTransitionTimer(const Service& service,
                                              const std::string& histogram_name,
                                              Service::ConnectState start_state,
                                              Service::ConnectState stop_state);

  // Specializes |metric_suffix| for the specified |technology_id|.
  std::string GetFullMetricName(const char* metric_suffix,
                                Technology technology_id);

  std::string GetSuspendDurationMetricNameFromStatus(
      WiFiConnectionStatusAfterWake status);

  // Implements DefaultServiceObserver.
  void OnDefaultServiceChanged(const ServiceRefPtr& logical_service,
                               bool logical_service_changed,
                               const ServiceRefPtr& physical_service,
                               bool physical_service_changed) override;

  // Notifies this object that |service| state has changed.
  virtual void NotifyServiceStateChanged(const Service& service,
                                         Service::ConnectState new_state);

  // Notifies this object that |service| has been disconnected.
  void NotifyServiceDisconnect(const Service& service);

  // Notifies this object of power at disconnect.
  void NotifySignalAtDisconnect(const Service& service,
                                int16_t signal_strength);

  // Notifies this object of the end of a suspend attempt.
  void NotifySuspendDone();

  // Notifies this object of the current wake on WiFi features enabled
  // represented by the WakeOnWiFiFeaturesEnabledState |state|.
  void NotifyWakeOnWiFiFeaturesEnabledState(
      WakeOnWiFiFeaturesEnabledState state);

  // Notifies this object of the result of NIC wake on WiFi settings
  // verification.
  virtual void NotifyVerifyWakeOnWiFiSettingsResult(
      VerifyWakeOnWiFiSettingsResult result);

  // Notifies this object of whether or not the WiFi device is connected to a
  // service after waking from suspend.
  virtual void NotifyConnectedToServiceAfterWake(
      WiFiConnectionStatusAfterWake status);

  // Notifies this object that termination actions started executing.
  void NotifyTerminationActionsStarted();

  // Notifies this object that termination actions have been completed.
  // |success| is true, if the termination actions completed successfully.
  void NotifyTerminationActionsCompleted(bool success);

  virtual void NotifySuspendDurationAfterWake(
      WiFiConnectionStatusAfterWake status, int seconds_in_suspend);

  // Notifies this object that suspend actions started executing.
  void NotifySuspendActionsStarted();

  // Notifies this object that suspend actions have been completed.
  // |success| is true, if the suspend actions completed successfully.
  void NotifySuspendActionsCompleted(bool success);

  // Notifies this object that dark resume actions started executing.
  void NotifyDarkResumeActionsStarted();

  // Notifies this object that dark resume actions have been completed.
  // |success| is true, if the dark resume actions completed successfully.
  void NotifyDarkResumeActionsCompleted(bool success);

  // Notifies this object that a scan has been initiated by shill while in dark
  // resume.
  virtual void NotifyDarkResumeInitiateScan();

  // Notifies this object that a scan results have been received in dark resume.
  void NotifyDarkResumeScanResultsReceived();

  // Notifies this object of a failure in LinkMonitor.
  void NotifyLinkMonitorFailure(Technology technology,
                                LinkMonitorFailure failure,
                                int seconds_to_failure,
                                int broadcast_error_count,
                                int unicast_error_count);

  // Notifies this object that LinkMonitor has added a response time sample
  // for |connection| with a value of |response_time_milliseconds|.
  void NotifyLinkMonitorResponseTimeSampleAdded(Technology technology,
                                                int response_time_milliseconds);

  // Notifies this object of a failure in patchpanel::NeighborLinkMonitor.
  void NotifyNeighborLinkMonitorFailure(
      Technology technology,
      IPAddress::Family family,
      patchpanel::NeighborReachabilityEventSignal::Role role);

  // Notifies this object that an AP was discovered and of that AP's 802.11k
  // support.
  void NotifyAp80211kSupport(bool neighbor_list_supported);

  // Notifies this object that an AP was discovered and of that AP's 802.11r
  // support.
  void NotifyAp80211rSupport(bool ota_ft_supported, bool otds_ft_supported);

  // Notifies this object that an AP was discovered and of that AP's 802.11v
  // DMS support.
  void NotifyAp80211vDMSSupport(bool dms_supported);

  // Notifies this object that an AP was discovered and of that AP's 802.11v
  // BSS Max Idle Period support.
  void NotifyAp80211vBSSMaxIdlePeriodSupport(
      bool bss_max_idle_period_supported);

  // Notifies this object that an AP was discovered and of that AP's 802.11v
  // BSS Transition support.
  void NotifyAp80211vBSSTransitionSupport(bool bss_transition_supported);

#if !defined(DISABLE_WIFI)
  // Notifies this object of WiFi disconnect.
  virtual void Notify80211Disconnect(WiFiDisconnectByWhom by_whom,
                                     IEEE_80211::WiFiReasonCode reason);
#endif  // DISABLE_WIFI

  // Notifies that WiFi tried to set up supplicant too many times.
  void NotifyWiFiSupplicantAbort();

  // Notifies that WiFi successfully set up supplicant after some number of
  // |attempts|.
  virtual void NotifyWiFiSupplicantSuccess(int attempts);

  // Notifies this object that an AP has switched channels.
  void NotifyApChannelSwitch(uint16_t frequency, uint16_t new_frequency);

  // Registers a device with this object so the device can use the timers to
  // track state transition metrics.
  void RegisterDevice(int interface_index, Technology technology);

  // Checks to see if the device has already been registered.
  bool IsDeviceRegistered(int interface_index, Technology technology);

  // Deregisters the device from this class.  All state transition timers
  // will be removed.
  virtual void DeregisterDevice(int interface_index);

  // Notifies this object that a device has been initialized.
  void NotifyDeviceInitialized(int interface_index);

  // Notifies this object that a device has started the enable process.
  void NotifyDeviceEnableStarted(int interface_index);

  // Notifies this object that a device has completed the enable process.
  void NotifyDeviceEnableFinished(int interface_index);

  // Notifies this object that a device has started the disable process.
  void NotifyDeviceDisableStarted(int interface_index);

  // Notifies this object that a device has completed the disable process.
  void NotifyDeviceDisableFinished(int interface_index);

  // Notifies this object that a device has started the scanning process.
  virtual void NotifyDeviceScanStarted(int interface_index);

  // Notifies this object that a device has completed the scanning process.
  virtual void NotifyDeviceScanFinished(int interface_index);

  // Terminates an underway scan (does nothing if a scan wasn't underway).
  virtual void ResetScanTimer(int interface_index);

  // Notifies this object that a device has started the connect process.
  virtual void NotifyDeviceConnectStarted(int interface_index,
                                          bool is_auto_connecting);

  // Notifies this object that a device has completed the connect process.
  virtual void NotifyDeviceConnectFinished(int interface_index);

  // Resets both the connect_timer and the scan_connect_timer the timer (the
  // latter so that a future connect will not erroneously be associated with
  // the previous scan).
  virtual void ResetConnectTimer(int interface_index);

  // Notifies this object that a cellular device has been dropped by the
  // network.
  void NotifyCellularDeviceDrop(const std::string& network_technology,
                                uint16_t signal_strength);

  // Notifies this object about 3GPP registration drop events.
  virtual void Notify3GPPRegistrationDelayedDropPosted();
  virtual void Notify3GPPRegistrationDelayedDropCanceled();

  // Notifies this object about a cellular connection failure.
  void NotifyCellularDeviceConnectionFailure();

  // Notifies this object about a cellular disconnection failure.
  void NotifyCellularDeviceDisconnectionFailure();

  // Notifies this object that a cellular service has been marked as
  // out-of-credits.
  void NotifyCellularOutOfCredits(Metrics::CellularOutOfCreditsReason reason);

  // Notifies this object about number of wifi services available for auto
  // connect when auto-connect is initiated.
  virtual void NotifyWifiAutoConnectableServices(int num_services);

  // Notifies this object about number of BSSes available for a wifi service
  // when attempt to connect to that service.
  virtual void NotifyWifiAvailableBSSes(int num_services);

  // Notifies this object about number of services associated to the
  // currently connected network.
  virtual void NotifyServicesOnSameNetwork(int num_services);

  // Notifies this object about WIFI TX bitrate in Mbps.
  virtual void NotifyWifiTxBitrate(int bitrate);

  // Notifies this object about the result of user-initiated connection
  // attempt.
  virtual void NotifyUserInitiatedConnectionResult(const std::string& name,
                                                   int result);

  // Notifies this object about the reason of failed user-initiated connection
  // attempt.
  virtual void NotifyUserInitiatedConnectionFailureReason(
      const std::string& name, const Service::ConnectFailure failure);

  // Notifies this object about a corrupted profile.
  virtual void NotifyCorruptedProfile();

  // Notifies this object about user-initiated event.
  virtual void NotifyUserInitiatedEvent(int event);

  // Notifies this object about the result of the fallback DNS test.
  virtual void NotifyFallbackDNSTestResult(Technology technology_id,
                                           int result);

  // Notifies this object about a network problem detected on the currently
  // connected network.
  virtual void NotifyNetworkProblemDetected(Technology technology_id,
                                            int reason);

  // Notifies this object about current connection status (online vs offline).
  virtual void NotifyDeviceConnectionStatus(Metrics::ConnectionStatus status);

  // Notifies this object about the DHCP client status.
  virtual void NotifyDhcpClientStatus(Metrics::DhcpClientStatus status);

  // Notifies this object about the IP type of the current network connection.
  virtual void NotifyNetworkConnectionIPType(Technology technology_id,
                                             NetworkConnectionIPType type);

  // Notifies this object about the IPv6 connectivity status.
  virtual void NotifyIPv6ConnectivityStatus(Technology technology_id,
                                            bool status);

  // Notifies this object about the presence of given technology type device.
  virtual void NotifyDevicePresenceStatus(Technology technology_id,
                                          bool status);

  // Notifies this object about the signal strength when link is unreliable.
  virtual void NotifyUnreliableLinkSignalStrength(Technology technology_id,
                                                  int signal_strength);

  // Sends linear histogram data to UMA.
  virtual bool SendEnumToUMA(const std::string& name, int sample, int max);

  // Send logarithmic histogram data to UMA.
  virtual bool SendToUMA(
      const std::string& name, int sample, int min, int max, int num_buckets);

  // Sends sparse histogram data to UMA.
  virtual bool SendSparseToUMA(const std::string& name, int sample);

  // Notifies this object that wake on WiFi has been disabled because of
  // excessive dark resume wakes.
  virtual void NotifyWakeOnWiFiThrottled();

  // Notifies this object that shill has resumed from a period of suspension
  // where wake on WiFi functionality was enabled on the NIC.
  virtual void NotifySuspendWithWakeOnWiFiEnabledDone();

  // Notifies this object that a wakeup reason has been received.
  virtual void NotifyWakeupReasonReceived();

#if !defined(DISABLE_WIFI)
  // Notifies this object that WakeOnWiFi::OnDarkResume has begun executing,
  // and that the dark resume was caused by |reason|.
  virtual void NotifyWakeOnWiFiOnDarkResume(
      WakeOnWiFi::WakeOnWiFiTrigger reason);
#endif  // DISABLE_WIFI

  // Notifies this object that a scan was started in dark resume. If
  // |is_active_scan| is true, the scan started was an active scan. Otherwise
  // the scan started was a passive scan.
  // Note: Metrics::NotifyDarkResumeInitiateScan is called when shill initiates
  // a scan in dark resume, while Metrics::NotifyScanStartedInDarkResume is
  // called when the kernel notifies shill that a scan (shill-initiated or not)
  // has actually started.
  virtual void NotifyScanStartedInDarkResume(bool is_active_scan);

  // Notifies this object that a dark resume scan retry was launched.
  virtual void NotifyDarkResumeScanRetry();

  // Notifies this object that shill is about to suspend and is executing
  // WakeOnWiFi::BeforeSuspendActions. |is_connected| indicates whether shill
  // was connected before suspending, and |in_dark_resume| indicates whether
  // shill is current in dark resume.
  // Note: this will only be called if wake on WiFi is supported and enabled.
  virtual void NotifyBeforeSuspendActions(bool is_connected,
                                          bool in_dark_resume);

  // Notifies this object that connection diagnostics have been performed, and
  // the connection issue that was diagnosed is |issue|.
  virtual void NotifyConnectionDiagnosticsIssue(const std::string& issue);

  // Notifies this object that a portal detection trial has finished with probe
  // results from both the HTTP probe and the HTTPS probe.
  virtual void NotifyPortalDetectionMultiProbeResult(
      const PortalDetector::Result& http_result,
      const PortalDetector::Result& https_result);

  // Notifies this object that of the HS20 support of an access that has
  // been connected to.
  void NotifyHS20Support(bool hs20_supported, int hs20_version_number);

  // Calculate Regulatory domain value given two letter country code.
  // Return value corresponds to Network.Shill.WiFi.RegulatoryDomain histogram
  // buckets. The full enum can be found in
  // /chromium/src/tools/metrics/histograms/enums.xml.
  static int GetRegulatoryDomainValue(std::string country_code);

 private:
  friend class MetricsTest;
  FRIEND_TEST(MetricsTest, FrequencyToChannel);
  FRIEND_TEST(MetricsTest, ResetConnectTimer);
  FRIEND_TEST(MetricsTest, ServiceFailure);
  FRIEND_TEST(MetricsTest, TimeOnlineTimeToDrop);
  FRIEND_TEST(MetricsTest, TimeToConfig);
  FRIEND_TEST(MetricsTest, TimeToOnline);
  FRIEND_TEST(MetricsTest, TimeToPortal);
  FRIEND_TEST(MetricsTest, TimeToScanIgnore);
  FRIEND_TEST(MetricsTest, WiFiServicePostReady);
  FRIEND_TEST(MetricsTest, NotifySuspendWithWakeOnWiFiEnabledDone);
  FRIEND_TEST(MetricsTest, NotifyWakeOnWiFiThrottled);
  FRIEND_TEST(MetricsTest, NotifySuspendActionsCompleted_Success);
  FRIEND_TEST(MetricsTest, NotifySuspendActionsCompleted_Failure);
  FRIEND_TEST(MetricsTest, NotifyDarkResumeActionsCompleted_Success);
  FRIEND_TEST(MetricsTest, NotifyDarkResumeActionsCompleted_Failure);
  FRIEND_TEST(MetricsTest, NotifySuspendActionsStarted);
  FRIEND_TEST(MetricsTest, NotifyDarkResumeActionsStarted);
  FRIEND_TEST(MetricsTest, NotifyDarkResumeInitiateScan);
  FRIEND_TEST(MetricsTest, NotifyDarkResumeScanResultsReceived);
  FRIEND_TEST(MetricsTest, NotifyDarkResumeScanRetry);
  FRIEND_TEST(MetricsTest, NotifyBeforeSuspendActions_InDarkResume);
  FRIEND_TEST(MetricsTest, NotifyBeforeSuspendActions_NotInDarkResume);
  FRIEND_TEST(WiFiMainTest, GetGeolocationObjects);

  using TimerReporters =
      std::vector<std::unique_ptr<chromeos_metrics::TimerReporter>>;
  using TimerReportersList = std::list<chromeos_metrics::TimerReporter*>;
  using TimerReportersByState =
      std::map<Service::ConnectState, TimerReportersList>;
  struct ServiceMetrics {
    // All TimerReporter objects are stored in |timers| which owns the objects.
    // |start_on_state| and |stop_on_state| contain pointers to the
    // TimerReporter objects and control when to start and stop the timers.
    TimerReporters timers;
    TimerReportersByState start_on_state;
    TimerReportersByState stop_on_state;
  };
  using ServiceMetricsLookupMap =
      std::map<const Service*, std::unique_ptr<ServiceMetrics>>;

  struct DeviceMetrics {
    DeviceMetrics() : auto_connect_tries(0) {}
    Technology technology;
    std::unique_ptr<chromeos_metrics::TimerReporter> initialization_timer;
    std::unique_ptr<chromeos_metrics::TimerReporter> enable_timer;
    std::unique_ptr<chromeos_metrics::TimerReporter> disable_timer;
    std::unique_ptr<chromeos_metrics::TimerReporter> scan_timer;
    std::unique_ptr<chromeos_metrics::TimerReporter> connect_timer;
    std::unique_ptr<chromeos_metrics::TimerReporter> scan_connect_timer;
    std::unique_ptr<chromeos_metrics::TimerReporter> auto_connect_timer;
    int auto_connect_tries;
  };
  using DeviceMetricsLookupMap =
      std::map<const int, std::unique_ptr<DeviceMetrics>>;

  static const uint16_t kWiFiBandwidth5MHz;
  static const uint16_t kWiFiBandwidth20MHz;
  static const uint16_t kWiFiFrequency2412;
  static const uint16_t kWiFiFrequency2472;
  static const uint16_t kWiFiFrequency2484;
  static const uint16_t kWiFiFrequency5170;
  static const uint16_t kWiFiFrequency5180;
  static const uint16_t kWiFiFrequency5230;
  static const uint16_t kWiFiFrequency5240;
  static const uint16_t kWiFiFrequency5320;
  static const uint16_t kWiFiFrequency5500;
  static const uint16_t kWiFiFrequency5700;
  static const uint16_t kWiFiFrequency5745;
  static const uint16_t kWiFiFrequency5825;

  void InitializeCommonServiceMetrics(const Service& service);
  void UpdateServiceStateTransitionMetrics(ServiceMetrics* service_metrics,
                                           Service::ConnectState new_state);
  void SendServiceFailure(const Service& service);

  DeviceMetrics* GetDeviceMetrics(int interface_index) const;
  void AutoConnectMetricsReset(DeviceMetrics* device_metrics);

  // Notifies this object about the removal/resetting of a device with given
  // technology type.
  void NotifyDeviceRemovedEvent(Technology technology_id);

  // Returns |true| if and only if a device that supports |technology_id| is
  // registered.
  bool IsTechnologyPresent(Technology technology_id) const;

  // For unit test purposes.
  void set_library(MetricsLibraryInterface* library);
  void set_time_online_timer(chromeos_metrics::Timer* timer) {
    time_online_timer_.reset(timer);  // Passes ownership
  }
  void set_time_to_drop_timer(chromeos_metrics::Timer* timer) {
    time_to_drop_timer_.reset(timer);  // Passes ownership
  }
  void set_time_resume_to_ready_timer(chromeos_metrics::Timer* timer) {
    time_resume_to_ready_timer_.reset(timer);  // Passes ownership
  }
  void set_time_termination_actions_timer(chromeos_metrics::Timer* timer) {
    time_termination_actions_timer.reset(timer);  // Passes ownership
  }
  void set_time_suspend_actions_timer(chromeos_metrics::Timer* timer) {
    time_suspend_actions_timer.reset(timer);  // Passes ownership
  }
  void set_time_dark_resume_actions_timer(chromeos_metrics::Timer* timer) {
    time_dark_resume_actions_timer.reset(timer);  // Passes ownership
  }
  void set_time_to_scan_timer(int interface_index,
                              chromeos_metrics::TimerReporter* timer) {
    DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
    device_metrics->scan_timer.reset(timer);  // Passes ownership
  }
  void set_time_to_connect_timer(int interface_index,
                                 chromeos_metrics::TimerReporter* timer) {
    DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
    device_metrics->connect_timer.reset(timer);  // Passes ownership
  }
  void set_time_to_scan_connect_timer(int interface_index,
                                      chromeos_metrics::TimerReporter* timer) {
    DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
    device_metrics->scan_connect_timer.reset(timer);  // Passes ownership
  }

  // |library_| points to |metrics_library_| when shill runs normally.
  // However, in order to allow for unit testing, we point |library_| to a
  // MetricsLibraryMock object instead.
  MetricsLibrary metrics_library_;
  MetricsLibraryInterface* library_;
  ServiceMetricsLookupMap services_metrics_;
  Technology last_default_technology_;
  Technology last_physical_technology_;
  bool was_last_online_;
  std::unique_ptr<chromeos_metrics::Timer> time_online_timer_;
  std::unique_ptr<chromeos_metrics::Timer> time_to_drop_timer_;
  std::unique_ptr<chromeos_metrics::Timer> time_resume_to_ready_timer_;
  std::unique_ptr<chromeos_metrics::Timer> time_termination_actions_timer;
  std::unique_ptr<chromeos_metrics::Timer> time_suspend_actions_timer;
  std::unique_ptr<chromeos_metrics::Timer> time_dark_resume_actions_timer;
  bool collect_bootstats_;
  DeviceMetricsLookupMap devices_metrics_;
  int num_scan_results_expected_in_dark_resume_;
  bool wake_on_wifi_throttled_;
  bool wake_reason_received_;
  int dark_resume_scan_retries_;
  std::unique_ptr<chromeos_metrics::CumulativeMetrics> daily_metrics_;
  std::unique_ptr<chromeos_metrics::CumulativeMetrics> monthly_metrics_;
};

}  // namespace shill

#endif  // SHILL_METRICS_H_
